#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include "esp_system.h"
#include "http.h"
#include "esp_timer.h"
#include "api.h"
#include "graphs.h"
#include "logbuf.h"
#include "syscfg.h"
#include "backup.h"
#include "tasks.h"
#include "net.h"
#include "wifi.h"
#include "nets.h"
#include "files.h"

extern const uint8_t _binary_index_html_gz_start[] asm("_binary_index_html_gz_start");
extern const uint8_t _binary_index_html_gz_end[] asm("_binary_index_html_gz_end");

static const char *TAG = "nnode_http";

/* Error code dictionary for {"error":"CODE"} responses.
 * Success stays {"ok":true}. CODE is a stable lowercase snake_case id,
 * never human text and never a printf format: the frontend maps it to
 * i18n key err_<CODE> and falls back to a generic error for unknown codes.
 * Rule: a new code must be listed here and translated in src_web/i18n.js
 * (ru and en) in the same change.
 * Generic: method (405 wrong method), body (400 empty body),
 * ts (400 bad timestamp), name (400 bad name param),
 * not_found (404 no such item), not_loaded (404 module not loaded),
 * forbidden (403 not allowed), host (400 missing host),
 * ssid (400 missing SSID), mac (400 missing MAC),
 * size (411 no Content-Length), busy (409 restore already running),
 * error (fallback, unspecified failure).
 * Settings (400 bad value): ap, hostname, ntp, servers, tz, nets.
 * WiFi: need_pass (400 password required), connect (400 connect failed),
 * exists (409 SSID already saved).
 * Backup restore (400): parse, version, config, wipe, files, space.
 * Modules: bad_name, not_elf, already_loaded, no_slots, no_mem,
 * elf_open, elf_init, elf_relocate, tasks_running (409 live tasks),
 * dup (409 already in autostart).
 * Files: open (500 cannot create), write (500 write failed).
 * Reset: tasks_running (409 unload modules first), reset (500 failed). */
static esp_err_t send_err(httpd_req_t *r, const char *code)
{
    char eb[128];
    int n;
    if (!code || !code[0]) {
        code = "error";
    }
    n = snprintf(eb, sizeof(eb), "{\"error\":\"%s\"}", code);
    return httpd_resp_send(r, eb, n < 0 ? 0 : n);
}

static esp_err_t h_tasks(httpd_req_t *r);
static esp_err_t h_net(httpd_req_t *r);
static esp_err_t h_scan(httpd_req_t *r);
static esp_err_t h_ping(httpd_req_t *r);
static esp_err_t h_trace(httpd_req_t *r);
static esp_err_t h_nslookup(httpd_req_t *r);
static esp_err_t h_renew(httpd_req_t *r);
static esp_err_t h_reboot(httpd_req_t *r);
static esp_err_t h_nets(httpd_req_t *r);
static esp_err_t h_ap(httpd_req_t *r);
static esp_err_t h_stations(httpd_req_t *r);
static esp_err_t h_deauth(httpd_req_t *r);
static esp_err_t h_files(httpd_req_t *r);
static esp_err_t h_download(httpd_req_t *r);
static esp_err_t h_upload(httpd_req_t *r);
static esp_err_t h_fdelete(httpd_req_t *r);
static esp_err_t h_mkdir(httpd_req_t *r);
static esp_err_t h_elfrun(httpd_req_t *r);
static esp_err_t h_elfunload(httpd_req_t *r);
static esp_err_t h_elfstop(httpd_req_t *r);
static esp_err_t h_elfkill(httpd_req_t *r);
static esp_err_t h_elfinstances(httpd_req_t *r);
static esp_err_t h_autostart(httpd_req_t *r);
static esp_err_t h_autostart_add(httpd_req_t *r);
static esp_err_t h_autostart_remove(httpd_req_t *r);
static esp_err_t h_autostart_move(httpd_req_t *r);
static esp_err_t h_graphs(httpd_req_t *r);
static esp_err_t h_log(httpd_req_t *r);
static esp_err_t h_logclear(httpd_req_t *r);
static esp_err_t h_ntp(httpd_req_t *r);
static esp_err_t h_ntpsync(httpd_req_t *r);
static esp_err_t h_hostname(httpd_req_t *r);
static esp_err_t h_tz(httpd_req_t *r);
static esp_err_t h_backup(httpd_req_t *r);
static esp_err_t h_cfgexport(httpd_req_t *r);
static esp_err_t h_restore(httpd_req_t *r);
static esp_err_t h_reset(httpd_req_t *r);
static esp_err_t h_stacon(httpd_req_t *r);
static esp_err_t h_stadisc(httpd_req_t *r);
static esp_err_t h_forget(httpd_req_t *r);
static esp_err_t h_netsupdate(httpd_req_t *r);
static esp_err_t h_netsadd(httpd_req_t *r);
static void qry_decode(char *s);
static esp_err_t h_time(httpd_req_t *r);
static esp_err_t h_kill(httpd_req_t *r);

/* The page is embedded gzip-compressed (see tools/web_inline.py). Zero MCU cost,
 * one blob in flash. gzip is understood by every client; brotli was
 * dropped: Firefox never offers br over plain HTTP, so it would sit
 * in flash unused most of the time. */
static esp_err_t h_root(httpd_req_t *r)
{
    httpd_resp_set_type(r, "text/html");
    httpd_resp_set_hdr(r, "Cache-Control", "no-store");
    httpd_resp_set_hdr(r, "Content-Encoding", "gzip");
    return httpd_resp_send(r, (const char *)_binary_index_html_gz_start,
        _binary_index_html_gz_end - _binary_index_html_gz_start);
}

static esp_err_t h_summary(httpd_req_t *r)
{
    char *b = malloc(1024);
    int n;
    esp_err_t e;
    if (!b) {
        return ESP_ERR_NO_MEM;
    }
    n = nnode_api_summary(b, 1024);
    httpd_resp_set_type(r, "application/json");
    httpd_resp_set_hdr(r, "Cache-Control", "no-store");
    e = httpd_resp_send(r, b, n < 0 ? 0 : n);
    free(b);
    return e;
}

static esp_err_t h_sys(httpd_req_t *r)
{
    char *b = malloc(4096);
    int n;
    esp_err_t e;
    if (!b) {
        return ESP_ERR_NO_MEM;
    }
    n = nnode_api_sys(b, 4096);
    httpd_resp_set_type(r, "application/json");
    httpd_resp_set_hdr(r, "Cache-Control", "no-store");
    e = httpd_resp_send(r, b, n < 0 ? 0 : n);
    free(b);
    return e;
}

/* URI handler budget: httpd preallocates max_uri_handlers slots and
 * answers 404 to anything past it, so the table size is asserted at
 * compile time below. Bump together with the table. */
#define NNODE_HTTP_MAX_URIS 64

void nnode_http_start(void)
{
    httpd_handle_t h = NULL;
    httpd_config_t c = HTTPD_DEFAULT_CONFIG();
    size_t i;
    c.max_uri_handlers = NNODE_HTTP_MAX_URIS;
    /* 8 KB: console logging (ESP_LOG over USB-JTAG) plus TLS-less chunked
     * IO no longer fit 6 KB once the backup handlers landed. */
    c.stack_size = 8192;
    /* Single registration table: one row per endpoint, in serving order.
     * New endpoints go here, never via ad-hoc register calls. */
    static const httpd_uri_t kUris[] = {
        { .uri = "/", .method = HTTP_GET, .handler = h_root },
        { .uri = "/api/summary", .method = HTTP_GET, .handler = h_summary },
        { .uri = "/api/sys", .method = HTTP_GET, .handler = h_sys },
        { .uri = "/api/tasks", .method = HTTP_GET, .handler = h_tasks },
        { .uri = "/api/time", .method = HTTP_POST, .handler = h_time },
        { .uri = "/api/kill", .method = HTTP_POST, .handler = h_kill },
        { .uri = "/api/net", .method = HTTP_GET, .handler = h_net },
        { .uri = "/api/scan", .method = HTTP_GET, .handler = h_scan },
        { .uri = "/api/ping", .method = HTTP_POST, .handler = h_ping },
        { .uri = "/api/traceroute", .method = HTTP_POST, .handler = h_trace },
        { .uri = "/api/nslookup", .method = HTTP_POST, .handler = h_nslookup },
        { .uri = "/api/renew", .method = HTTP_POST, .handler = h_renew },
        { .uri = "/api/reboot", .method = HTTP_POST, .handler = h_reboot },
        { .uri = "/api/nets", .method = HTTP_GET, .handler = h_nets },
        { .uri = "/api/ap", .method = HTTP_GET, .handler = h_ap },
        { .uri = "/api/ap", .method = HTTP_POST, .handler = h_ap },
        { .uri = "/api/sta/connect", .method = HTTP_POST, .handler = h_stacon },
        { .uri = "/api/sta/disconnect", .method = HTTP_POST, .handler = h_stadisc },
        { .uri = "/api/nets/forget", .method = HTTP_POST, .handler = h_forget },
        { .uri = "/api/nets/update", .method = HTTP_POST, .handler = h_netsupdate },
        { .uri = "/api/nets/add", .method = HTTP_POST, .handler = h_netsadd },
        { .uri = "/api/ap/stations", .method = HTTP_GET, .handler = h_stations },
        { .uri = "/api/ap/deauth", .method = HTTP_POST, .handler = h_deauth },
        { .uri = "/api/files", .method = HTTP_GET, .handler = h_files },
        { .uri = "/api/files/download", .method = HTTP_GET, .handler = h_download },
        { .uri = "/api/files/upload", .method = HTTP_POST, .handler = h_upload },
        { .uri = "/api/files/delete", .method = HTTP_POST, .handler = h_fdelete },
        { .uri = "/api/files/mkdir", .method = HTTP_POST, .handler = h_mkdir },
        { .uri = "/api/elf/run", .method = HTTP_POST, .handler = h_elfrun },
        { .uri = "/api/elf/unload", .method = HTTP_POST, .handler = h_elfunload },
        { .uri = "/api/elf/stop", .method = HTTP_POST, .handler = h_elfstop },
        { .uri = "/api/elf/kill", .method = HTTP_POST, .handler = h_elfkill },
        { .uri = "/api/elf/instances", .method = HTTP_GET, .handler = h_elfinstances },
        { .uri = "/api/elf/autostart", .method = HTTP_GET, .handler = h_autostart },
        { .uri = "/api/elf/autostart/add", .method = HTTP_POST, .handler = h_autostart_add },
        { .uri = "/api/elf/autostart/remove", .method = HTTP_POST, .handler = h_autostart_remove },
        { .uri = "/api/elf/autostart/move", .method = HTTP_POST, .handler = h_autostart_move },
        { .uri = "/api/graphs", .method = HTTP_GET, .handler = h_graphs },
        { .uri = "/api/log", .method = HTTP_GET, .handler = h_log },
        { .uri = "/api/log/clear", .method = HTTP_POST, .handler = h_logclear },
        { .uri = "/api/ntp", .method = HTTP_GET, .handler = h_ntp },
        { .uri = "/api/ntp", .method = HTTP_POST, .handler = h_ntp },
        { .uri = "/api/ntp/sync", .method = HTTP_POST, .handler = h_ntpsync },
        { .uri = "/api/hostname", .method = HTTP_GET, .handler = h_hostname },
        { .uri = "/api/hostname", .method = HTTP_POST, .handler = h_hostname },
        { .uri = "/api/tz", .method = HTTP_GET, .handler = h_tz },
        { .uri = "/api/tz", .method = HTTP_POST, .handler = h_tz },
        { .uri = "/api/backup", .method = HTTP_GET, .handler = h_backup },
        { .uri = "/api/config/export", .method = HTTP_GET, .handler = h_cfgexport },
        { .uri = "/api/backup/restore", .method = HTTP_POST, .handler = h_restore },
        { .uri = "/api/factory-reset", .method = HTTP_POST, .handler = h_reset },
    };
    _Static_assert(sizeof(kUris) / sizeof(kUris[0]) <= NNODE_HTTP_MAX_URIS,
        "uri table exceeds max_uri_handlers: bump NNODE_HTTP_MAX_URIS");
    if (httpd_start(&h, &c) != ESP_OK) {
        ESP_LOGE(TAG, "httpd start failed");
        return;
    }
    for (i = 0; i < sizeof(kUris) / sizeof(kUris[0]); i++) {
        if (httpd_register_uri_handler(h, &kUris[i]) != ESP_OK) {
            ESP_LOGE(TAG, "uri register failed: %s", kUris[i].uri);
        }
    }
    ESP_LOGI(TAG, "httpd on :80");
}

static esp_err_t h_tasks(httpd_req_t *r)
{
    char *b = malloc(8192);
    int n;
    esp_err_t e;
    if (!b) {
        return ESP_ERR_NO_MEM;
    }
    n = nnode_tasks_json(b, 8192);
    httpd_resp_set_type(r, "application/json");
    httpd_resp_set_hdr(r, "Cache-Control", "no-store");
    e = httpd_resp_send(r, b, n < 0 ? 0 : n);
    free(b);
    return e;
}

static esp_err_t h_time(httpd_req_t *r)
{
    char b[64];
    int n;
    char *p;
    long long ts;
    if (r->method != HTTP_POST) {
        httpd_resp_set_status(r, "405 Method Not Allowed");
        return send_err(r, "method");
    }
    n = httpd_req_recv(r, b, sizeof(b) - 1);
    if (n <= 0) {
        httpd_resp_set_status(r, "400 Bad Request");
        return send_err(r, "body");
    }
    b[n] = 0;
    p = strstr(b, "ts");
    ts = (p && strchr(p, ':')) ? atoll(strchr(p, ':') + 1) : 0;
    if (ts <= 0 || nnode_api_time_set(ts) != 0) {
        httpd_resp_set_status(r, "400 Bad Request");
        return send_err(r, "ts");
    }
    httpd_resp_set_type(r, "application/json");
    httpd_resp_set_hdr(r, "Cache-Control", "no-store");
    return httpd_resp_sendstr(r, "{\"ok\":true}");
}

static esp_err_t h_kill(httpd_req_t *r)
{
    char q[96];
    char name[40];
    int rc;
    if (r->method != HTTP_POST) {
        httpd_resp_set_status(r, "405 Method Not Allowed");
        return send_err(r, "method");
    }
    if (httpd_req_get_url_query_str(r, q, sizeof(q)) != ESP_OK ||
        httpd_query_key_value(q, "name", name, sizeof(name)) != ESP_OK) {
        httpd_resp_set_status(r, "400 Bad Request");
        return send_err(r, "name");
    }
    qry_decode(name);
    rc = nnode_task_kill(name);
    httpd_resp_set_type(r, "application/json");
    httpd_resp_set_hdr(r, "Cache-Control", "no-store");
    if (rc == 0) {
        return httpd_resp_sendstr(r, "{\"ok\":true}");
    }
    if (rc == -1) {
        httpd_resp_set_status(r, "404 Not Found");
        return send_err(r, "not_found");
    }
    httpd_resp_set_status(r, "403 Forbidden");
    return send_err(r, "forbidden");
}

static esp_err_t h_net(httpd_req_t *r)
{
    char *b = malloc(4096);
    int n;
    esp_err_t e;
    if (!b) {
        return ESP_ERR_NO_MEM;
    }
    n = nnode_net_json(b, 4096);
    httpd_resp_set_type(r, "application/json");
    httpd_resp_set_hdr(r, "Cache-Control", "no-store");
    e = httpd_resp_send(r, b, n < 0 ? 0 : n);
    free(b);
    return e;
}

static esp_err_t h_scan(httpd_req_t *r)
{
    char *b = malloc(4096);
    int n;
    esp_err_t e;
    if (!b) {
        return ESP_ERR_NO_MEM;
    }
    n = nnode_net_scan(b, 4096);
    httpd_resp_set_type(r, "application/json");
    httpd_resp_set_hdr(r, "Cache-Control", "no-store");
    e = httpd_resp_send(r, b, n < 0 ? 0 : n);
    free(b);
    return e;
}

typedef struct {
    httpd_req_t *req;
    int failed;
} chunk_ctx_t;

static int emit_chunk(void *ctx, const char *line)
{
    chunk_ctx_t *c = ctx;
    if (c->failed) {
        return 1;
    }
    if (httpd_resp_send_chunk(c->req, line, strlen(line)) != ESP_OK) {
        c->failed = 1;
        return 1;
    }
    return 0;
}

static esp_err_t h_ping(httpd_req_t *r)
{
    char q[128];
    char host[64];
    char cnt[8];
    char ivl[8];
    int n, ms;
    chunk_ctx_t cx;
    if (r->method != HTTP_POST) {
        httpd_resp_set_status(r, "405 Method Not Allowed");
        return send_err(r, "method");
    }
    if (httpd_req_get_url_query_str(r, q, sizeof(q)) != ESP_OK ||
        httpd_query_key_value(q, "host", host, sizeof(host)) != ESP_OK) {
        httpd_resp_set_status(r, "400 Bad Request");
        return send_err(r, "host");
    }
    qry_decode(host);
    n = 4;
    if (httpd_query_key_value(q, "count", cnt, sizeof(cnt)) == ESP_OK) {
        n = atoi(cnt);
    }
    ms = 1000;
    if (httpd_query_key_value(q, "interval", ivl, sizeof(ivl)) == ESP_OK) {
        ms = atoi(ivl);
    }
    httpd_resp_set_type(r, "text/plain");
    httpd_resp_set_hdr(r, "Cache-Control", "no-store");
    cx.req = r;
    cx.failed = 0;
    nnode_net_ping_lines(host, n, ms, emit_chunk, &cx);
    httpd_resp_send_chunk(r, NULL, 0);
    return ESP_OK;
}

static esp_err_t h_trace(httpd_req_t *r)
{
    char q[128];
    char host[64];
    chunk_ctx_t cx;
    if (r->method != HTTP_POST) {
        httpd_resp_set_status(r, "405 Method Not Allowed");
        return send_err(r, "method");
    }
    if (httpd_req_get_url_query_str(r, q, sizeof(q)) != ESP_OK ||
        httpd_query_key_value(q, "host", host, sizeof(host)) != ESP_OK) {
        httpd_resp_set_status(r, "400 Bad Request");
        return send_err(r, "host");
    }
    qry_decode(host);
    httpd_resp_set_type(r, "text/plain");
    httpd_resp_set_hdr(r, "Cache-Control", "no-store");
    cx.req = r;
    cx.failed = 0;
    nnode_net_traceroute(host, emit_chunk, &cx);
    httpd_resp_send_chunk(r, NULL, 0);
    return ESP_OK;
}

static esp_err_t h_nslookup(httpd_req_t *r)
{
    char q[128];
    char host[64];
    char *b;
    int n;
    if (r->method != HTTP_POST) {
        httpd_resp_set_status(r, "405 Method Not Allowed");
        return send_err(r, "method");
    }
    if (httpd_req_get_url_query_str(r, q, sizeof(q)) != ESP_OK ||
        httpd_query_key_value(q, "host", host, sizeof(host)) != ESP_OK) {
        httpd_resp_set_status(r, "400 Bad Request");
        return send_err(r, "host");
    }
    qry_decode(host);
    b = malloc(512);
    if (!b) {
        return ESP_ERR_NO_MEM;
    }
    n = nnode_net_nslookup(host, b, 512);
    httpd_resp_set_type(r, "text/plain");
    httpd_resp_set_hdr(r, "Cache-Control", "no-store");
    if (n < 0) {
        free(b);
        httpd_resp_set_status(r, "500 Internal Error");
        return httpd_resp_send(r, "nslookup failed\n", 17);
    }
    httpd_resp_send(r, b, (size_t)n);
    free(b);
    return ESP_OK;
}

static esp_err_t h_renew(httpd_req_t *r)
{
    if (r->method != HTTP_POST) {
        httpd_resp_set_status(r, "405 Method Not Allowed");
        return send_err(r, "method");
    }
    httpd_resp_set_type(r, "application/json");
    httpd_resp_set_hdr(r, "Cache-Control", "no-store");
    httpd_resp_sendstr(r, "{\"ok\":true}");
    nnode_net_renew();
    return ESP_OK;
}

static esp_err_t h_reboot(httpd_req_t *r)
{
    if (r->method != HTTP_POST) {
        httpd_resp_set_status(r, "405 Method Not Allowed");
        return send_err(r, "method");
    }
    httpd_resp_set_type(r, "application/json");
    httpd_resp_set_hdr(r, "Cache-Control", "no-store");
    httpd_resp_sendstr(r, "{\"ok\":true}");
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
    return ESP_OK;
}

static esp_err_t h_nets(httpd_req_t *r)
{
    char *b = malloc(1024);
    int n;
    esp_err_t e;
    if (!b) {
        return ESP_ERR_NO_MEM;
    }
    n = nnode_nets_json(b, 1024);
    httpd_resp_set_type(r, "application/json");
    e = httpd_resp_send(r, b, n < 0 ? 0 : n);
    free(b);
    return e;
}

static esp_err_t h_ap(httpd_req_t *r)
{
    if (r->method == HTTP_GET) {
        char b[256];
        int n = nnode_ap_json(b, sizeof(b));
        httpd_resp_set_type(r, "application/json");
        return httpd_resp_send(r, b, n < 0 ? 0 : n);
    }
    if (r->method == HTTP_POST) {
        char b[192];
        nnode_ap_cfg_t apc;
        int n = httpd_req_recv(r, b, sizeof(b) - 1);
        httpd_resp_set_type(r, "application/json");
        if (n <= 0) {
            httpd_resp_set_status(r, "400 Bad Request");
            return send_err(r, "body");
        }
        b[n] = 0;
        if (nnode_ap_parse_json(b, &apc) != 0) {
            httpd_resp_set_status(r, "400 Bad Request");
            return send_err(r, "ap");
        }
        httpd_resp_sendstr(r, "{\"ok\":true}");
        nnode_ap_set(&apc);
        return ESP_OK;
    }
    httpd_resp_set_status(r, "405 Method Not Allowed");
    return send_err(r, "method");
}

static esp_err_t h_stacon(httpd_req_t *r)
{
    char q[160];
    char ssid[40];
    char pass[70];
    int rc;
    if (r->method != HTTP_POST) {
        httpd_resp_set_status(r, "405 Method Not Allowed");
        return send_err(r, "method");
    }
    if (httpd_req_get_url_query_str(r, q, sizeof(q)) != ESP_OK ||
        httpd_query_key_value(q, "ssid", ssid, sizeof(ssid)) != ESP_OK) {
        httpd_resp_set_status(r, "400 Bad Request");
        return send_err(r, "ssid");
    }
    qry_decode(ssid);
    pass[0] = 0;
    if (httpd_query_key_value(q, "pass", pass, sizeof(pass)) == ESP_OK) {
        qry_decode(pass);
    }
    rc = nnode_sta_connect(ssid, pass[0] ? pass : NULL);
    httpd_resp_set_type(r, "application/json");
    if (rc == 0) {
        return httpd_resp_sendstr(r, "{\"ok\":true}");
    }
    if (rc == -2) {
        httpd_resp_set_status(r, "400 Bad Request");
        return send_err(r, "need_pass");
    }
    httpd_resp_set_status(r, "400 Bad Request");
    return send_err(r, "connect");
}

static esp_err_t h_stadisc(httpd_req_t *r)
{
    if (r->method != HTTP_POST) {
        httpd_resp_set_status(r, "405 Method Not Allowed");
        return send_err(r, "method");
    }
    nnode_sta_disconnect();
    httpd_resp_set_type(r, "application/json");
    return httpd_resp_sendstr(r, "{\"ok\":true}");
}

static esp_err_t h_forget(httpd_req_t *r)
{
    char q[96];
    char ssid[40];
    if (r->method != HTTP_POST) {
        httpd_resp_set_status(r, "405 Method Not Allowed");
        return send_err(r, "method");
    }
    if (httpd_req_get_url_query_str(r, q, sizeof(q)) != ESP_OK ||
        httpd_query_key_value(q, "ssid", ssid, sizeof(ssid)) != ESP_OK) {
        httpd_resp_set_status(r, "400 Bad Request");
        return send_err(r, "ssid");
    }
    qry_decode(ssid);
    httpd_resp_set_type(r, "application/json");
    if (nnode_nets_forget(ssid) == 0) {
        /* Task: deleting the live network must also drop the link,
         * else the UI keeps showing a stale connected row that can
         * never be deleted again (not_found on retry). */
        char cur[33], dummy[64];
        nnode_sta_cur(cur, sizeof(cur), dummy, sizeof(dummy));
        if (strcmp(cur, ssid) == 0) {
            nnode_sta_disconnect();
        }
        return httpd_resp_sendstr(r, "{\"ok\":true}");
    }
    httpd_resp_set_status(r, "404 Not Found");
    return send_err(r, "not_found");
}

static esp_err_t h_netsupdate(httpd_req_t *r)
{
    char q[320];
    char ssid[40];
    char newb[40], passb[70], bssidb[32], authb[16];
    const char *new_ssid = NULL, *pass = NULL, *bssid = NULL, *auth = NULL;
    nnode_net_cred_t dummy;
    int rc;
    if (r->method != HTTP_POST) {
        httpd_resp_set_status(r, "405 Method Not Allowed");
        return send_err(r, "method");
    }
    if (httpd_req_get_url_query_str(r, q, sizeof(q)) != ESP_OK ||
        httpd_query_key_value(q, "ssid", ssid, sizeof(ssid)) != ESP_OK) {
        httpd_resp_set_status(r, "400 Bad Request");
        return send_err(r, "ssid");
    }
    qry_decode(ssid);
    if (nnode_nets_get(ssid, &dummy) != 0) {
        httpd_resp_set_type(r, "application/json");
        httpd_resp_set_status(r, "404 Not Found");
        return send_err(r, "not_found");
    }
    if (httpd_query_key_value(q, "new_ssid", newb, sizeof(newb)) == ESP_OK) {
        qry_decode(newb);
        new_ssid = newb;
    }
    /* pass/bssid/auth present (even empty) mean set; absent mean keep */
    if (httpd_query_key_value(q, "pass", passb, sizeof(passb)) == ESP_OK) {
        qry_decode(passb);
        pass = passb;
    }
    if (httpd_query_key_value(q, "bssid", bssidb, sizeof(bssidb)) == ESP_OK) {
        qry_decode(bssidb);
        bssid = bssidb;
    }
    if (httpd_query_key_value(q, "auth", authb, sizeof(authb)) == ESP_OK) {
        qry_decode(authb);
        auth = authb;
    }
    rc = nnode_nets_update(ssid, new_ssid, pass, pass != NULL,
        bssid, bssid != NULL, auth, auth != NULL);
    httpd_resp_set_type(r, "application/json");
    if (rc == 0) {
        return httpd_resp_sendstr(r, "{\"ok\":true}");
    }
    if (rc == -2) {
        httpd_resp_set_status(r, "409 Conflict");
        return send_err(r, "exists");
    }
    httpd_resp_set_status(r, "400 Bad Request");
    return send_err(r, "nets");
}

static esp_err_t h_netsadd(httpd_req_t *r)
{
    char q[256];
    char ssid[40];
    char pass[70];
    char bssid[32], auth[16];
    const char *bp = NULL, *ap = NULL;
    if (r->method != HTTP_POST) {
        httpd_resp_set_status(r, "405 Method Not Allowed");
        return send_err(r, "method");
    }
    if (httpd_req_get_url_query_str(r, q, sizeof(q)) != ESP_OK ||
        httpd_query_key_value(q, "ssid", ssid, sizeof(ssid)) != ESP_OK ||
        !ssid[0]) {
        httpd_resp_set_status(r, "400 Bad Request");
        return send_err(r, "ssid");
    }
    qry_decode(ssid);
    /* pass absent means open network; present (even empty) sets it */
    pass[0] = 0;
    if (httpd_query_key_value(q, "pass", pass, sizeof(pass)) == ESP_OK) {
        qry_decode(pass);
    }
    if (httpd_query_key_value(q, "bssid", bssid, sizeof(bssid)) == ESP_OK) {
        qry_decode(bssid);
        bp = bssid;
    }
    if (httpd_query_key_value(q, "auth", auth, sizeof(auth)) == ESP_OK) {
        qry_decode(auth);
        ap = auth;
    }
    httpd_resp_set_type(r, "application/json");
    httpd_resp_set_hdr(r, "Cache-Control", "no-store");
    if (nnode_nets_add(ssid, pass, bp, ap) < 0) {
        httpd_resp_set_status(r, "400 Bad Request");
        return send_err(r, "nets");
    }
    return httpd_resp_sendstr(r, "{\"ok\":true}");
}

static esp_err_t h_stations(httpd_req_t *r)
{
    char b[512];
    int n = nnode_ap_stations(b, sizeof(b));
    httpd_resp_set_type(r, "application/json");
    httpd_resp_set_hdr(r, "Cache-Control", "no-store");
    return httpd_resp_send(r, b, n < 0 ? 0 : n);
}

/* httpd_query_key_value does not percent-decode, while the UI always sends
 * encodeURIComponent values (needed for &/=/spaces and non-ASCII, e.g. a
 * colon sent as %3A would fail strict parsers below). So every string value
 * taken from the query is decoded in place right after parsing; without this
 * a Cyrillic SSID is stored still encoded and later stops matching. */
static void qry_decode(char *s)
{
    char *wr = s;
    while (*s) {
        if (*s == '%' && s[1] && s[2]) {
            char h[3] = { s[1], s[2], 0 };
            *wr++ = (char)strtol(h, NULL, 16);
            s += 3;
        } else {
            *wr++ = *s++;
        }
    }
    *wr = 0;
}

static esp_err_t h_deauth(httpd_req_t *r)
{
    char q[96];
    char mac[64];
    if (r->method != HTTP_POST) {
        httpd_resp_set_status(r, "405 Method Not Allowed");
        return send_err(r, "method");
    }
    if (httpd_req_get_url_query_str(r, q, sizeof(q)) != ESP_OK ||
        httpd_query_key_value(q, "mac", mac, sizeof(mac)) != ESP_OK) {
        httpd_resp_set_status(r, "400 Bad Request");
        return send_err(r, "mac");
    }
    qry_decode(mac);
    httpd_resp_set_type(r, "application/json");
    httpd_resp_set_hdr(r, "Cache-Control", "no-store");
    if (nnode_ap_deauth(mac) == 0) {
        return httpd_resp_sendstr(r, "{\"ok\":true}");
    }
    httpd_resp_set_status(r, "404 Not Found");
    return send_err(r, "not_found");
}

static esp_err_t h_graphs(httpd_req_t *r)
{
    char *b = malloc(4096);
    int n;
    esp_err_t e;
    if (!b) {
        return ESP_ERR_NO_MEM;
    }
    n = nnode_graphs_json(b, 4096);
    httpd_resp_set_type(r, "application/json");
    httpd_resp_set_hdr(r, "Cache-Control", "no-store");
    e = httpd_resp_send(r, b, n < 0 ? 0 : n);
    free(b);
    return e;
}

static int log_level_of(char c)
{
    switch (c) {
    case 'V': return 0;
    case 'D': return 1;
    case 'I': return 2;
    case 'W': return 3;
    case 'E': return 4;
    default: return 0;
    }
}

static esp_err_t h_log(httpd_req_t *r)
{
    char q[64];
    char v[8];
    char *b;
    int n;
    esp_err_t e;
    int lvl = 0;
    b = malloc(8192);
    if (!b) {
        return ESP_ERR_NO_MEM;
    }
    if (httpd_req_get_url_query_str(r, q, sizeof(q)) == ESP_OK &&
        httpd_query_key_value(q, "level", v, sizeof(v)) == ESP_OK && v[0]) {
        lvl = log_level_of(v[0]);
    }
    n = nnode_log_json(b, 8192, lvl, 128);
    httpd_resp_set_type(r, "application/json");
    httpd_resp_set_hdr(r, "Cache-Control", "no-store");
    e = httpd_resp_send(r, b, n < 0 ? 0 : n);
    free(b);
    return e;
}

static esp_err_t h_logclear(httpd_req_t *r)
{
    if (r->method != HTTP_POST) {
        httpd_resp_set_status(r, "405 Method Not Allowed");
        return send_err(r, "method");
    }
    nnode_log_clear();
    httpd_resp_set_type(r, "application/json");
    httpd_resp_set_hdr(r, "Cache-Control", "no-store");
    return httpd_resp_sendstr(r, "{\"ok\":true}");
}

static esp_err_t h_ntp(httpd_req_t *r)
{
    if (r->method == HTTP_GET) {
        char b[512];
        char srvs[NNODE_NTP_MAX][NNODE_NTP_SRV_LEN];
        int en = 0, n, i, o;
        n = nnode_ntp_get(&en, srvs, NNODE_NTP_MAX);
        o = snprintf(b, sizeof(b), "{\"enabled\":%d,\"synced\":%d,\"servers\":[",
            en, nnode_ntp_synced());
        for (i = 0; i < n && o < (int)sizeof(b); i++) {
            o += snprintf(b + o, sizeof(b) - o, "%s\"%s\"",
                i ? "," : "", srvs[i]);
        }
        if (o < (int)sizeof(b)) {
            o += snprintf(b + o, sizeof(b) - o, "]}");
        }
        httpd_resp_set_type(r, "application/json");
        httpd_resp_set_hdr(r, "Cache-Control", "no-store");
        return httpd_resp_send(r, b, o < 0 ? 0 : o);
    }
    if (r->method == HTTP_POST) {
        char b[256];
        int n = httpd_req_recv(r, b, sizeof(b) - 1);
        int en = 0;
        char srvs[256];
        const char *p;
        size_t k = 0;
        if (n <= 0) {
            httpd_resp_set_status(r, "400 Bad Request");
            return send_err(r, "body");
        }
        b[n] = 0;
        en = strstr(b, "\"enabled\":1") != NULL;
        srvs[0] = 0;
        p = strstr(b, "\"servers\":\"");
        if (p) {
            p += 11;
            while (*p && *p != '"' && k + 1 < sizeof(srvs)) {
                srvs[k++] = *p++;
            }
            srvs[k] = 0;
            if (*p != '"') {
                httpd_resp_set_status(r, "400 Bad Request");
                return send_err(r, "servers");
            }
        }
        httpd_resp_set_type(r, "application/json");
        if (nnode_ntp_save(en, srvs) != 0) {
            httpd_resp_set_status(r, "400 Bad Request");
            return send_err(r, "ntp");
        }
        return httpd_resp_sendstr(r, "{\"ok\":true}");
    }
    httpd_resp_set_status(r, "405 Method Not Allowed");
    return send_err(r, "method");
}

static esp_err_t h_ntpsync(httpd_req_t *r)
{
    if (r->method != HTTP_POST) {
        httpd_resp_set_status(r, "405 Method Not Allowed");
        return send_err(r, "method");
    }
    nnode_ntp_sync_now();
    httpd_resp_set_type(r, "application/json");
    httpd_resp_set_hdr(r, "Cache-Control", "no-store");
    return httpd_resp_sendstr(r, "{\"ok\":true}");
}

static esp_err_t h_hostname(httpd_req_t *r)
{
    if (r->method == HTTP_GET) {
        char b[64], host[40];
        int n;
        nnode_hostname_get(host, sizeof(host));
        n = snprintf(b, sizeof(b), "{\"hostname\":\"%s\"}", host);
        httpd_resp_set_type(r, "application/json");
        httpd_resp_set_hdr(r, "Cache-Control", "no-store");
        return httpd_resp_send(r, b, n < 0 ? 0 : n);
    }
    if (r->method == HTTP_POST) {
        char b[128];
        char host[40];
        const char *p;
        size_t k = 0;
        int n = httpd_req_recv(r, b, sizeof(b) - 1);
        if (n <= 0) {
            httpd_resp_set_status(r, "400 Bad Request");
            return send_err(r, "body");
        }
        b[n] = 0;
        p = strstr(b, "\"hostname\":\"");
        if (!p) {
            httpd_resp_set_status(r, "400 Bad Request");
            return send_err(r, "hostname");
        }
        p += 12;
        while (*p && *p != '"' && k + 1 < sizeof(host)) {
            host[k++] = *p++;
        }
        host[k] = 0;
        if (*p != '"') {
            httpd_resp_set_status(r, "400 Bad Request");
            return send_err(r, "hostname");
        }
        httpd_resp_set_type(r, "application/json");
        if (nnode_hostname_save(host) != 0) {
            httpd_resp_set_status(r, "400 Bad Request");
            return send_err(r, "hostname");
        }
        httpd_resp_sendstr(r, "{\"ok\":true}");
        /* Renew drops the station IP for a moment; defer past the
         * response so the client gets its ok first. */
        {
            esp_timer_handle_t t = NULL;
            const esp_timer_create_args_t a = {
                .callback = nnode_hostname_apply_deferred,
                .name = "hostapply",
            };
            if (esp_timer_create(&a, &t) == ESP_OK) {
                esp_timer_start_once(t, 1000000);
            } else {
                nnode_hostname_apply();
            }
        }
        return ESP_OK;
    }
    httpd_resp_set_status(r, "405 Method Not Allowed");
    return send_err(r, "method");
}

static esp_err_t h_tz(httpd_req_t *r)
{
    if (r->method == HTTP_GET) {
        char b[192], tz[NNODE_TZ_MAX + 1], zn[NNODE_TZ_MAX + 1];
        int n;
        nnode_tz_get(tz, sizeof(tz));
        nnode_zonename_get(zn, sizeof(zn));
        n = snprintf(b, sizeof(b), "{\"tz\":\"%s\",\"zonename\":\"%s\"}", tz, zn);
        httpd_resp_set_type(r, "application/json");
        httpd_resp_set_hdr(r, "Cache-Control", "no-store");
        return httpd_resp_send(r, b, n < 0 ? 0 : n);
    }
    if (r->method == HTTP_POST) {
        char b[320];
        char tz[NNODE_TZ_MAX + 1];
        char zn[NNODE_TZ_MAX + 1];
        const char *p;
        size_t k = 0;
        int n = httpd_req_recv(r, b, sizeof(b) - 1);
        if (n <= 0) {
            httpd_resp_set_status(r, "400 Bad Request");
            return send_err(r, "body");
        }
        b[n] = 0;
        p = strstr(b, "\"tz\":\"");
        if (!p) {
            httpd_resp_set_status(r, "400 Bad Request");
            return send_err(r, "tz");
        }
        p += 6;
        while (*p && *p != '"' && k + 1 < sizeof(tz)) {
            tz[k++] = *p++;
        }
        tz[k] = 0;
        if (*p != '"') {
            httpd_resp_set_status(r, "400 Bad Request");
            return send_err(r, "tz");
        }
        zn[0] = 0;
        p = strstr(b, "\"zonename\":\"");
        if (p) {
            p += 12;
            k = 0;
            while (*p && *p != '"' && k + 1 < sizeof(zn)) {
                zn[k++] = *p++;
            }
            zn[k] = 0;
            if (*p != '"') {
                httpd_resp_set_status(r, "400 Bad Request");
                return send_err(r, "tz");
            }
        }
        httpd_resp_set_type(r, "application/json");
        if (nnode_tz_save(tz) != 0 ||
            (zn[0] && nnode_zonename_save(zn) != 0)) {
            httpd_resp_set_status(r, "400 Bad Request");
            return send_err(r, "tz");
        }
        nnode_tz_apply();
        return httpd_resp_sendstr(r, "{\"ok\":true}");
    }
    httpd_resp_set_status(r, "405 Method Not Allowed");
    return send_err(r, "method");
}

typedef struct {
    httpd_req_t *req;
    int failed;
} bk_ctx_t;

static int bk_emit(void *ctx, const char *data, size_t len)
{
    bk_ctx_t *c = ctx;
    if (c->failed) {
        return 1;
    }
    if (!len) {
        return 0;
    }
    if (httpd_resp_send_chunk(c->req, data, len) != ESP_OK) {
        c->failed = 1;
        return 1;
    }
    return 0;
}

static esp_err_t h_backup(httpd_req_t *r)
{
    bk_ctx_t cx;
    cx.req = r;
    cx.failed = 0;
    httpd_resp_set_type(r, "application/cbor");
    httpd_resp_set_hdr(r, "Content-Disposition",
        "attachment; filename=\"nnode-backup.cbor\"");
    httpd_resp_set_hdr(r, "Cache-Control", "no-store");
    if (nnode_backup_emit(bk_emit, &cx) != 0 || cx.failed) {
        return ESP_FAIL;
    }
    httpd_resp_send_chunk(r, NULL, 0);
    return ESP_OK;
}

static esp_err_t h_cfgexport(httpd_req_t *r)
{
    bk_ctx_t cx;
    cx.req = r;
    cx.failed = 0;
    httpd_resp_set_type(r, "text/plain");
    httpd_resp_set_hdr(r, "Cache-Control", "no-store");
    if (nnode_config_export(bk_emit, &cx) != 0 || cx.failed) {
        return ESP_FAIL;
    }
    httpd_resp_send_chunk(r, NULL, 0);
    return ESP_OK;
}

typedef struct {
    httpd_req_t *req;
    int left;
} rr_ctx_t;

static int rr_read(void *ctx, char *buf, size_t len)
{
    rr_ctx_t *c = ctx;
    int want, n;
    if (c->left <= 0 || !len) {
        return -1;
    }
    want = (int)len > c->left ? c->left : (int)len;
    n = httpd_req_recv(c->req, buf, (size_t)want);
    if (n <= 0) {
        return -1;
    }
    c->left -= n;
    return n;
}

/* Restore runs in its own task: the parser + NVS/LittleFS call chains
 * do not fit the 6 KB httpd worker stack (proven by a stack fault).
 * The handler only waits for the result. */
typedef struct {
    httpd_req_t *req;
    int content_len;
    TaskHandle_t caller;
    char err[32];
    int rc;
} restore_job_t;

static int s_restoring;
static portMUX_TYPE s_restore_mu = portMUX_INITIALIZER_UNLOCKED;

static void restore_task(void *a)
{
    restore_job_t *j = (restore_job_t *)a;
    rr_ctx_t cx;
    cx.req = j->req;
    cx.left = j->content_len;
    j->rc = nnode_backup_restore_stream(rr_read, &cx, j->content_len,
        j->err, sizeof(j->err));
    xTaskNotifyGive(j->caller);
    vTaskDelete(NULL);
}

static esp_err_t h_restore(httpd_req_t *r)
{
    restore_job_t j;
    int busy, noreboot = 0;
    char q[64], v[8];
    if (r->method != HTTP_POST) {
        httpd_resp_set_status(r, "405 Method Not Allowed");
        return send_err(r, "method");
    }
    /* Chunked uploads carry no Content-Length; browsers send it for
     * Blob POST, which is all the UI uses. No RAM-size cap: the parser
     * streams, the limit is free space in /data. */
    if (r->content_len <= 0) {
        httpd_resp_set_status(r, "411 Length Required");
        return send_err(r, "size");
    }
    /* Debug aid: skip the reboot to inspect the restored state first. */
    if (httpd_req_get_url_query_str(r, q, sizeof(q)) == ESP_OK &&
        httpd_query_key_value(q, "noreboot", v, sizeof(v)) == ESP_OK &&
        v[0] == '1') {
        noreboot = 1;
    }
    taskENTER_CRITICAL(&s_restore_mu);
    busy = s_restoring;
    if (!busy) {
        s_restoring = 1;
    }
    taskEXIT_CRITICAL(&s_restore_mu);
    if (busy) {
        httpd_resp_set_status(r, "409 Conflict");
        return send_err(r, "busy");
    }
    j.req = r;
    j.content_len = r->content_len;
    j.caller = xTaskGetCurrentTaskHandle();
    j.err[0] = 0;
    j.rc = -1;
    if (xTaskCreate(restore_task, "nnode_rst", 12288, &j, 5, NULL) != pdPASS) {
        taskENTER_CRITICAL(&s_restore_mu);
        s_restoring = 0;
        taskEXIT_CRITICAL(&s_restore_mu);
        return ESP_ERR_NO_MEM;
    }
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    taskENTER_CRITICAL(&s_restore_mu);
    s_restoring = 0;
    taskEXIT_CRITICAL(&s_restore_mu);
    httpd_resp_set_type(r, "application/json");
    if (j.rc != 0) {
        httpd_resp_set_status(r, "400 Bad Request");
        return send_err(r, j.err[0] ? j.err : "parse");
    }
    httpd_resp_sendstr(r, "{\"ok\":true}");
    if (noreboot) {
        return ESP_OK;
    }
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
    return ESP_OK;
}

static esp_err_t h_reset(httpd_req_t *r)
{
    int rc;
    if (r->method != HTTP_POST) {
        httpd_resp_set_status(r, "405 Method Not Allowed");
        return send_err(r, "method");
    }
    rc = nnode_factory_reset();
    httpd_resp_set_type(r, "application/json");
    httpd_resp_set_hdr(r, "Cache-Control", "no-store");
    if (rc == -2) {
        httpd_resp_set_status(r, "409 Conflict");
        return send_err(r, "tasks_running");
    }
    if (rc != 0) {
        httpd_resp_set_status(r, "500 Internal Error");
        return send_err(r, "reset");
    }
    httpd_resp_sendstr(r, "{\"ok\":true}");
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
    return ESP_OK;
}

/* Single-segment name (modules, kept strict). */
static int query_name(httpd_req_t *r, char *name, size_t cap)
{
    char q[128];
    if (httpd_req_get_url_query_str(r, q, sizeof(q)) != ESP_OK ||
        httpd_query_key_value(q, "name", name, cap) != ESP_OK) {
        return -1;
    }
    qry_decode(name);
    return nnode_file_valid(name) ? 0 : -1;
}

/* Optional instance id: >= 0 when present and numeric, else -1. */
static int query_id(httpd_req_t *r)
{
    char q[128], v[16];
    if (httpd_req_get_url_query_str(r, q, sizeof(q)) != ESP_OK ||
        httpd_query_key_value(q, "id", v, sizeof(v)) != ESP_OK) {
        return -1;
    }
    return atoi(v);
}

/* Multi-segment path inside /data (file manager). */
static int query_path(httpd_req_t *r, char *name, size_t cap)
{
    char q[256];
    if (httpd_req_get_url_query_str(r, q, sizeof(q)) != ESP_OK ||
        httpd_query_key_value(q, "name", name, cap) != ESP_OK) {
        return -1;
    }
    qry_decode(name);
    return nnode_path_valid(name) ? 0 : -1;
}

static esp_err_t h_files(httpd_req_t *r)
{
    char *b = malloc(4096);
    char q[256], dir[136];
    int n;
    esp_err_t e;
    if (!b) {
        return ESP_ERR_NO_MEM;
    }
    dir[0] = 0;
    if (httpd_req_get_url_query_str(r, q, sizeof(q)) == ESP_OK &&
        httpd_query_key_value(q, "dir", dir, sizeof(dir)) == ESP_OK) {
        qry_decode(dir);
        if (!nnode_path_valid(dir)) {
            free(b);
            httpd_resp_set_status(r, "400 Bad Request");
            return send_err(r, "name");
        }
    }
    n = nnode_files_json(b, 4096, dir);
    httpd_resp_set_type(r, "application/json");
    httpd_resp_set_hdr(r, "Cache-Control", "no-store");
    e = httpd_resp_send(r, b, n < 0 ? 0 : n);
    free(b);
    return e;
}

static esp_err_t h_download(httpd_req_t *r)
{
    char name[140];
    char path[160];
    FILE *f;
    long size;
    char *chunk;
    struct stat st;
    if (query_path(r, name, sizeof(name)) != 0) {
        httpd_resp_set_status(r, "400 Bad Request");
        return send_err(r, "name");
    }
    snprintf(path, sizeof(path), "/data/%s", name);
    memset(&st, 0, sizeof(st));
    if (stat(path, &st) != 0 || S_ISDIR(st.st_mode)) {
        httpd_resp_set_status(r, "404 Not Found");
        return send_err(r, "not_found");
    }
    {
        /* Bare name for the save dialog; header-unsafe chars are
         * replaced so a crafted name cannot split the header. */
        const char *bn = strrchr(name, '/');
        char disp[80], hdr[110];
        size_t i, o = 0;
        bn = bn ? bn + 1 : name;
        for (i = 0; bn[i] && o + 1 < sizeof(disp); i++) {
            char c = bn[i];
            disp[o++] = (c == '"' || c == ';' || c == '\r' || c == '\n') ? '_' : c;
        }
        disp[o] = 0;
        snprintf(hdr, sizeof(hdr), "attachment; filename=\"%s\"", disp);
        httpd_resp_set_hdr(r, "Content-Disposition", hdr);
    }
    f = fopen(path, "rb");
    if (!f) {
        httpd_resp_set_status(r, "404 Not Found");
        return send_err(r, "not_found");
    }
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    chunk = malloc(4096);
    if (!chunk) {
        fclose(f);
        return ESP_ERR_NO_MEM;
    }
    httpd_resp_set_type(r, "application/octet-stream");
    httpd_resp_set_hdr(r, "Cache-Control", "no-store");
    while (size > 0) {
        size_t want = (size_t)size > 4096 ? 4096 : (size_t)size;
        size_t got = fread(chunk, 1, want, f);
        if (!got) {
            break;
        }
        if (httpd_resp_send_chunk(r, chunk, got) != ESP_OK) {
            break;
        }
        size -= (long)got;
    }
    free(chunk);
    fclose(f);
    httpd_resp_send_chunk(r, NULL, 0);
    return ESP_OK;
}

static esp_err_t h_upload(httpd_req_t *r)
{
    char name[140];
    char path[160];
    FILE *f;
    long total = 0;
    char *chunk;
    int left = r->content_len;
    if (r->method != HTTP_POST) {
        httpd_resp_set_status(r, "405 Method Not Allowed");
        return send_err(r, "method");
    }
    if (query_path(r, name, sizeof(name)) != 0) {
        httpd_resp_set_status(r, "400 Bad Request");
        return send_err(r, "name");
    }
    snprintf(path, sizeof(path), "/data/%s", name);
    f = fopen(path, "wb");
    if (!f) {
        httpd_resp_set_status(r, "500 Internal Error");
        return send_err(r, "open");
    }
    chunk = malloc(4096);
    if (!chunk) {
        fclose(f);
        return ESP_ERR_NO_MEM;
    }
    while (left > 0) {
        int got = httpd_req_recv(r, chunk, left > 4096 ? 4096 : left);
        if (got <= 0) {
            break;
        }
        if (fwrite(chunk, 1, (size_t)got, f) != (size_t)got) {
            total = -1;
            break;
        }
        total += got;
        left -= got;
    }
    free(chunk);
    fclose(f);
    httpd_resp_set_type(r, "application/json");
    httpd_resp_set_hdr(r, "Cache-Control", "no-store");
    if (total < 0 || left != 0) {
        unlink(path);
        httpd_resp_set_status(r, "500 Internal Error");
        return send_err(r, "write");
    }
    {
        char ok[64];
        int n = snprintf(ok, sizeof(ok), "{\"ok\":true,\"size\":%ld}", total);
        return httpd_resp_send(r, ok, n);
    }
}

static esp_err_t h_fdelete(httpd_req_t *r)
{
    char name[140];
    int rc;
    if (r->method != HTTP_POST) {
        httpd_resp_set_status(r, "405 Method Not Allowed");
        return send_err(r, "method");
    }
    if (query_path(r, name, sizeof(name)) != 0) {
        httpd_resp_set_status(r, "400 Bad Request");
        return send_err(r, "name");
    }
    rc = nnode_file_delete(name);
    httpd_resp_set_type(r, "application/json");
    if (rc == 0) {
        return httpd_resp_sendstr(r, "{\"ok\":true}");
    }
    httpd_resp_set_status(r, "404 Not Found");
    return send_err(r, "not_found");
}

static esp_err_t h_mkdir(httpd_req_t *r)
{
    char name[140];
    if (r->method != HTTP_POST) {
        httpd_resp_set_status(r, "405 Method Not Allowed");
        return send_err(r, "method");
    }
    if (query_path(r, name, sizeof(name)) != 0) {
        httpd_resp_set_status(r, "400 Bad Request");
        return send_err(r, "name");
    }
    httpd_resp_set_type(r, "application/json");
    httpd_resp_set_hdr(r, "Cache-Control", "no-store");
    if (nnode_dir_create(name) == 0) {
        return httpd_resp_sendstr(r, "{\"ok\":true}");
    }
    httpd_resp_set_status(r, "500 Internal Error");
    return send_err(r, "open");
}

static esp_err_t h_elfrun(httpd_req_t *r)
{
    char name[72];
    char err[96];
    char ok[64];
    int id = 0;
    if (r->method != HTTP_POST) {
        httpd_resp_set_status(r, "405 Method Not Allowed");
        return send_err(r, "method");
    }
    if (query_name(r, name, sizeof(name)) != 0) {
        httpd_resp_set_status(r, "400 Bad Request");
        return send_err(r, "name");
    }
    httpd_resp_set_type(r, "application/json");
    if (nnode_elf_run(name, &id, err, sizeof(err)) == 0) {
        snprintf(ok, sizeof(ok), "{\"ok\":true,\"id\":%d}", id);
        return httpd_resp_sendstr(r, ok);
    }
    httpd_resp_set_status(r, "500 Internal Error");
    return send_err(r, err);
}

static esp_err_t h_elfunload(httpd_req_t *r)
{
    char name[72];
    int rc;
    if (r->method != HTTP_POST) {
        httpd_resp_set_status(r, "405 Method Not Allowed");
        return send_err(r, "method");
    }
    if (query_name(r, name, sizeof(name)) != 0) {
        httpd_resp_set_status(r, "400 Bad Request");
        return send_err(r, "name");
    }
    rc = nnode_elf_unload(name);
    httpd_resp_set_type(r, "application/json");
    if (rc == 0) {
        return httpd_resp_sendstr(r, "{\"ok\":true}");
    }
    if (rc == -3) {
        httpd_resp_set_status(r, "409 Conflict");
        return send_err(r, "tasks_running");
    }
    httpd_resp_set_status(r, "404 Not Found");
    return send_err(r, "not_loaded");
}

static esp_err_t h_elfstop(httpd_req_t *r)
{
    char name[72];
    int rc, id;
    if (r->method != HTTP_POST) {
        httpd_resp_set_status(r, "405 Method Not Allowed");
        return send_err(r, "method");
    }
    id = query_id(r);
    if (id < 0 && query_name(r, name, sizeof(name)) != 0) {
        httpd_resp_set_status(r, "400 Bad Request");
        return send_err(r, "name");
    }
    rc = nnode_elf_stop(id, id < 0 ? name : NULL);
    httpd_resp_set_type(r, "application/json");
    if (rc == 0) {
        return httpd_resp_sendstr(r, "{\"ok\":true}");
    }
    httpd_resp_set_status(r, "404 Not Found");
    return send_err(r, "not_loaded");
}

static esp_err_t h_elfkill(httpd_req_t *r)
{
    char name[72];
    int rc, id;
    if (r->method != HTTP_POST) {
        httpd_resp_set_status(r, "405 Method Not Allowed");
        return send_err(r, "method");
    }
    id = query_id(r);
    if (id < 0 && query_name(r, name, sizeof(name)) != 0) {
        httpd_resp_set_status(r, "400 Bad Request");
        return send_err(r, "name");
    }
    rc = nnode_elf_kill(id, id < 0 ? name : NULL);
    httpd_resp_set_type(r, "application/json");
    if (rc == 0) {
        return httpd_resp_sendstr(r, "{\"ok\":true}");
    }
    httpd_resp_set_status(r, "404 Not Found");
    return send_err(r, "not_loaded");
}

static esp_err_t h_elfinstances(httpd_req_t *r)
{
    char *b = malloc(1024);
    int n;
    if (!b) {
        return ESP_ERR_NO_MEM;
    }
    n = nnode_elf_instances_json(b, 1024);
    httpd_resp_set_type(r, "application/json");
    httpd_resp_set_hdr(r, "Cache-Control", "no-store");
    httpd_resp_send(r, b, n < 0 ? 0 : n);
    free(b);
    return ESP_OK;
}

static esp_err_t h_autostart(httpd_req_t *r)
{
    char *b = malloc(1024);
    int n;
    if (!b) {
        return ESP_ERR_NO_MEM;
    }
    n = nnode_autostart_json(b, 1024);
    httpd_resp_set_type(r, "application/json");
    httpd_resp_set_hdr(r, "Cache-Control", "no-store");
    httpd_resp_send(r, b, n < 0 ? 0 : n);
    free(b);
    return ESP_OK;
}

static esp_err_t h_autostart_add(httpd_req_t *r)
{
    char name[72];
    int rc;
    if (r->method != HTTP_POST) {
        httpd_resp_set_status(r, "405 Method Not Allowed");
        return send_err(r, "method");
    }
    if (query_name(r, name, sizeof(name)) != 0) {
        httpd_resp_set_status(r, "400 Bad Request");
        return send_err(r, "name");
    }
    rc = nnode_autostart_add(name);
    httpd_resp_set_type(r, "application/json");
    if (rc == 0) {
        return httpd_resp_sendstr(r, "{\"ok\":true}");
    }
    if (rc == -3) {
        httpd_resp_set_status(r, "409 Conflict");
        return send_err(r, "dup");
    }
    if (rc == -4) {
        httpd_resp_set_status(r, "409 Conflict");
        return send_err(r, "no_slots");
    }
    httpd_resp_set_status(r, "404 Not Found");
    return send_err(r, "not_found");
}

static esp_err_t h_autostart_remove(httpd_req_t *r)
{
    char name[72];
    int rc;
    if (r->method != HTTP_POST) {
        httpd_resp_set_status(r, "405 Method Not Allowed");
        return send_err(r, "method");
    }
    if (query_name(r, name, sizeof(name)) != 0) {
        httpd_resp_set_status(r, "400 Bad Request");
        return send_err(r, "name");
    }
    rc = nnode_autostart_remove(name);
    httpd_resp_set_type(r, "application/json");
    if (rc == 0) {
        return httpd_resp_sendstr(r, "{\"ok\":true}");
    }
    httpd_resp_set_status(r, "404 Not Found");
    return send_err(r, "not_found");
}

static esp_err_t h_autostart_move(httpd_req_t *r)
{
    char name[72];
    char q[160], dir[8];
    int rc, step;
    if (r->method != HTTP_POST) {
        httpd_resp_set_status(r, "405 Method Not Allowed");
        return send_err(r, "method");
    }
    if (query_name(r, name, sizeof(name)) != 0) {
        httpd_resp_set_status(r, "400 Bad Request");
        return send_err(r, "name");
    }
    if (httpd_req_get_url_query_str(r, q, sizeof(q)) != ESP_OK ||
        httpd_query_key_value(q, "dir", dir, sizeof(dir)) != ESP_OK) {
        httpd_resp_set_status(r, "400 Bad Request");
        return send_err(r, "name");
    }
    if (strcmp(dir, "up") == 0) {
        step = -1;
    } else if (strcmp(dir, "down") == 0) {
        step = 1;
    } else {
        httpd_resp_set_status(r, "400 Bad Request");
        return send_err(r, "name");
    }
    rc = nnode_autostart_move(name, step);
    httpd_resp_set_type(r, "application/json");
    if (rc == 0) {
        return httpd_resp_sendstr(r, "{\"ok\":true}");
    }
    httpd_resp_set_status(r, "404 Not Found");
    return send_err(r, "not_found");
}
