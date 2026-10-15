#include <stdio.h>
#include <string.h>
#include <netdb.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <sys/select.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "lwip/inet.h"
#include "esp_netif_net_stack.h"
#include "lwip/netif.h"
#include "lwip/pbuf.h"
#include "ping/ping_sock.h"
#include <stdatomic.h>
#include "wifi.h"
#include "net.h"

static const char *auth_name(wifi_auth_mode_t m)
{
    switch (m) {
    case WIFI_AUTH_OPEN: return "open";
    case WIFI_AUTH_WEP: return "wep";
    case WIFI_AUTH_WPA_PSK: return "wpa";
    case WIFI_AUTH_WPA2_PSK: return "wpa2";
    case WIFI_AUTH_WPA_WPA2_PSK: return "wpa/wpa2";
    case WIFI_AUTH_ENTERPRISE: return "enterprise";
    case WIFI_AUTH_WPA3_PSK: return "wpa3";
    case WIFI_AUTH_WPA2_WPA3_PSK: return "wpa2/wpa3";
    case WIFI_AUTH_WAPI_PSK: return "wapi";
    default: return "?";
    }
}

static const char *cipher_name(wifi_cipher_type_t c)
{
    switch (c) {
    case WIFI_CIPHER_TYPE_CCMP: return "CCMP";
    case WIFI_CIPHER_TYPE_TKIP: return "TKIP";
    case WIFI_CIPHER_TYPE_TKIP_CCMP: return "TKIP/CCMP";
    case WIFI_CIPHER_TYPE_AES_CMAC128: return "AES-CMAC";
    case WIFI_CIPHER_TYPE_SMS4: return "SMS4";
    case WIFI_CIPHER_TYPE_WEP40:
    case WIFI_CIPHER_TYPE_WEP104: return "WEP";
    default: return NULL;
    }
}

static void sta_encr(const wifi_ap_record_t *ap, char *out, size_t cap)
{
    const char *base = NULL, *ciph;
    if (!ap || !out || !cap) {
        return;
    }
    switch (ap->authmode) {
    case WIFI_AUTH_OPEN: snprintf(out, cap, "None"); return;
    case WIFI_AUTH_WEP: base = "WEP"; break;
    case WIFI_AUTH_WPA_PSK: base = "WPA PSK"; break;
    case WIFI_AUTH_WPA2_PSK: base = "WPA2 PSK"; break;
    case WIFI_AUTH_WPA_WPA2_PSK: base = "WPA/WPA2 PSK"; break;
    case WIFI_AUTH_ENTERPRISE: base = "Enterprise"; break;
    case WIFI_AUTH_WPA3_PSK: base = "WPA3 SAE"; break;
    case WIFI_AUTH_WPA2_WPA3_PSK: base = "WPA2/WPA3 PSK"; break;
    case WIFI_AUTH_WAPI_PSK: base = "WAPI PSK"; break;
    default: base = "?"; break;
    }
    ciph = cipher_name(ap->pairwise_cipher);
    if (!ciph) {
        ciph = cipher_name(ap->group_cipher);
    }
    if (ciph) {
        snprintf(out, cap, "%s (%s)", base, ciph);
    } else {
        snprintf(out, cap, "%s", base);
    }
}
static void mac_str(const uint8_t *m, char *out)
{
    snprintf(out, 18, "%02x:%02x:%02x:%02x:%02x:%02x",
        m[0], m[1], m[2], m[3], m[4], m[5]);
}

static int prefix_len(uint32_t mask_be)
{
    uint32_t mask = ntohl(mask_be);
    int n = 0;
    while (mask & 0x80000000) {
        n++;
        mask <<= 1;
    }
    return n;
}

/* Honest per-iface packet accounting: wrap the lwIP netif
 * input/linkoutput of each interface and count real passing packets.
 * Global lwip_stats cannot do that (they are totals), and neither lwIP
 * nor the WiFi driver provides other per-iface counters. Counters are
 * cumulative since boot, like Pkts. in OpenWrt. */
typedef err_t (*tap_in_fn)(struct pbuf *p, struct netif *nf);
typedef err_t (*tap_out_fn)(struct netif *nf, struct pbuf *p);

typedef struct {
    struct netif *nf;
    tap_in_fn orig_in;
    tap_out_fn orig_out;
    atomic_uint_fast32_t rx;
    atomic_uint_fast32_t tx;
} iface_tap_t;

static iface_tap_t s_taps[2];

static err_t tap_input(struct pbuf *p, struct netif *nf)
{
    int i;
    for (i = 0; i < 2; i++) {
        if (s_taps[i].nf == nf && s_taps[i].orig_in) {
            atomic_fetch_add(&s_taps[i].rx, 1);
            return s_taps[i].orig_in(p, nf);
        }
    }
    return ERR_VAL;
}

static err_t tap_linkoutput(struct netif *nf, struct pbuf *p)
{
    int i;
    for (i = 0; i < 2; i++) {
        if (s_taps[i].nf == nf && s_taps[i].orig_out) {
            atomic_fetch_add(&s_taps[i].tx, 1);
            return s_taps[i].orig_out(nf, p);
        }
    }
    return ERR_VAL;
}

void nnode_net_taps_install(void)
{
    esp_netif_t *ifs[2] = { nnode_net_sta(), nnode_net_ap() };
    int i;
    for (i = 0; i < 2; i++) {
        struct netif *l;
        if (!ifs[i]) {
            continue;
        }
        l = esp_netif_get_netif_impl(ifs[i]);
        /* pointers live as long as esp_netif (created once),
         * a WiFi restart does not change them, no need to re-wrap */
        if (!l || !l->input || !l->linkoutput || l->input == tap_input) {
            continue;
        }
        s_taps[i].nf = l;
        s_taps[i].orig_in = l->input;
        s_taps[i].orig_out = l->linkoutput;
        l->input = tap_input;
        l->linkoutput = tap_linkoutput;
    }
}

static int tap_counts(struct netif *lwip, uint32_t *rx, uint32_t *tx)
{
    int i;
    *rx = 0;
    *tx = 0;
    if (!lwip) {
        return 0;
    }
    for (i = 0; i < 2; i++) {
        if (s_taps[i].nf == lwip && s_taps[i].orig_in && s_taps[i].orig_out) {
            *rx = (uint32_t)atomic_load(&s_taps[i].rx);
            *tx = (uint32_t)atomic_load(&s_taps[i].tx);
            return 1;
        }
    }
    return 0;
}

int nnode_net_json(char *buf, size_t len)
{
    char ip[16], mask[16], gw[16], bssid[18], mac[18], encr[40], cidr[20], dns[16];
    wifi_ap_record_t ap;
    esp_netif_ip_info_t ni;
    esp_netif_t *sta;
    esp_netif_t *it;
    int rssi = 0, ch = 0, have_ap = 0;
    size_t o;
    int first;
    memset(&ap, 0, sizeof(ap));
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
        rssi = ap.rssi;
        ch = ap.primary;
        have_ap = 1;
    }
    mac_str(ap.bssid, bssid);
    sta_encr(have_ap ? &ap : NULL, encr, sizeof(encr));
    if (!have_ap) {
        snprintf(encr, sizeof(encr), "-");
    }
    sta = nnode_net_sta();
    memset(&ni, 0, sizeof(ni));
    if (sta) {
        esp_netif_get_ip_info(sta, &ni);
    }
    snprintf(ip, sizeof(ip), IPSTR, IP2STR(&ni.ip));
    snprintf(mask, sizeof(mask), IPSTR, IP2STR(&ni.netmask));
    snprintf(gw, sizeof(gw), IPSTR, IP2STR(&ni.gw));
    {
        /* prefix_len() yields 0..32 by construction; clamp so the
         * compiler can prove "%s/%d" (max 15+1+2 chars) fits cidr[20]. */
        int plen = prefix_len(ni.netmask.addr);
        if (plen < 0) {
            plen = 0;
        }
        if (plen > 32) {
            plen = 32;
        }
        snprintf(cidr, sizeof(cidr), "%s/%d", ip, plen);
    }
    snprintf(dns, sizeof(dns), "-");
    if (sta) {
        esp_netif_dns_info_t di;
        memset(&di, 0, sizeof(di));
        if (esp_netif_get_dns_info(sta, ESP_NETIF_DNS_MAIN, &di) == ESP_OK &&
            di.ip.u_addr.ip4.addr) {
            snprintf(dns, sizeof(dns), IPSTR, IP2STR(&di.ip.u_addr.ip4));
        }
    }
    /* The driver may reassign input/linkoutput at WiFi start,
     * so reinstall here: idempotent, fixes any order. */
    nnode_net_taps_install();
    o = 0;
    {
        char cur_ssid[33], cur_pass[64];
        nnode_sta_cur(cur_ssid, sizeof(cur_ssid), cur_pass, sizeof(cur_pass));
        o += snprintf(buf + o, len - o,
            "{\"ssid\":\"%s\",\"bssid\":\"%s\",\"channel\":%d,\"rssi\":%d,\"ap_up\":%d,"
            "\"ip\":\"%s\",\"mask\":\"%s\",\"gw\":\"%s\",\"password\":\"%s\","
            "\"cidr\":\"%s\",\"dns\":\"%s\",\"encr\":\"%s\","
            "\"sta_uptime\":%d,\"ap_uptime\":%d,\"ifaces\":[",
            cur_ssid, bssid, ch, rssi, nnode_ap_up(), ip, mask, gw, cur_pass,
            cidr, dns, encr,
            nnode_sta_uptime(), nnode_ap_uptime());
    }
    first = 1;
    for (it = esp_netif_next_unsafe(NULL); it && o < len; it = esp_netif_next_unsafe(it)) {
        esp_netif_ip_info_t ii;
        uint8_t m[6];
        uint32_t mtu = 0, rx = 0, tx = 0;
        int tapped = 0;
        memset(&ii, 0, sizeof(ii));
        esp_netif_get_ip_info(it, &ii);
        esp_netif_get_mac(it, m);
        mac_str(m, mac);
        {
            struct netif *lwip = esp_netif_get_netif_impl(it);
            mtu = lwip ? lwip->mtu : 0;
            tapped = tap_counts(lwip, &rx, &tx);
        }
        o += snprintf(buf + o, len - o,
            "%s{\"name\":\"%s\",\"ip\":\"" IPSTR "\",\"up\":%d,\"mac\":\"%s\",\"mtu\":%u,\"rx\":%u,\"tx\":%u,\"tap\":%d}",
            first ? "" : ",", esp_netif_get_desc(it),
            IP2STR(&ii.ip), esp_netif_is_netif_up(it) ? 1 : 0, mac,
            (unsigned)mtu, (unsigned)rx, (unsigned)tx, tapped);
        first = 0;
    }
    if (o < len) {
        o += snprintf(buf + o, len - o, "],\"routes\":[");
    }
    first = 1;
    for (it = esp_netif_next_unsafe(NULL); it && o < len; it = esp_netif_next_unsafe(it)) {
        esp_netif_ip_info_t ii;
        memset(&ii, 0, sizeof(ii));
        esp_netif_get_ip_info(it, &ii);
        if (ii.ip.addr == 0) {
            continue;
        }
        o += snprintf(buf + o, len - o,
            "%s{\"dst\":\"" IPSTR "/%d\",\"gw\":\"-\",\"iface\":\"%s\"}",
            first ? "" : ",",
            IP2STR((esp_ip4_addr_t *)&(uint32_t){ ii.ip.addr & ii.netmask.addr }),
            prefix_len(ii.netmask.addr), esp_netif_get_desc(it));
        first = 0;
    }
    if (ni.gw.addr && o < len) {
        o += snprintf(buf + o, len - o,
            "%s{\"dst\":\"default\",\"gw\":\"%s\",\"iface\":\"sta\"}",
            first ? "" : ",", gw);
    }
    if (o < len) {
        o += snprintf(buf + o, len - o, "]}");
    }
    return (int)o;
}

int nnode_net_scan(char *buf, size_t len)
{
    uint16_t num = 20;
    wifi_ap_record_t recs[20];
    size_t o;
    uint16_t i;
    if (esp_wifi_scan_start(NULL, true) != ESP_OK) {
        return snprintf(buf, len, "{\"error\":\"scan_start\"}");
    }
    if (esp_wifi_scan_get_ap_num(&num) != ESP_OK) {
        return snprintf(buf, len, "{\"error\":\"scan_num\"}");
    }
    if (num > 20) {
        num = 20;
    }
    if (esp_wifi_scan_get_ap_records(&num, recs) != ESP_OK) {
        return snprintf(buf, len, "{\"error\":\"scan_get\"}");
    }
    o = 0;
    o += snprintf(buf + o, len - o, "[");
    for (i = 0; i < num && o < len; i++) {
        char bs[18];
        mac_str(recs[i].bssid, bs);
        o += snprintf(buf + o, len - o,
            "%s{\"ssid\":\"%s\",\"bssid\":\"%s\",\"channel\":%d,\"rssi\":%d,\"auth\":\"%s\",\"width\":%d,\"second\":%d}",
            i ? "," : "", recs[i].ssid, bs, recs[i].primary,
            recs[i].rssi, auth_name(recs[i].authmode),
            recs[i].bandwidth == WIFI_BW40 ? 40 : 20,
            (int)recs[i].second);
    }
    if (o < len) {
        o += snprintf(buf + o, len - o, "]");
    }
    return (int)o;
}

typedef struct {
    int type; /* 0=reply, 1=end */
    uint32_t seq;
    uint32_t ms;
    int ttl;
} ping_ev_t;

typedef struct {
    QueueHandle_t q;
} ping_state_t;

static void ping_ok(esp_ping_handle_t h, void *args)
{
    ping_state_t *st = args;
    ping_ev_t ev;
    uint32_t v = 0, sz = sizeof(v);
    ev.type = 0;
    ev.seq = 0;
    ev.ms = 0;
    ev.ttl = 0;
    if (esp_ping_get_profile(h, ESP_PING_PROF_SEQNO, &v, sz) == ESP_OK) {
        ev.seq = v;
    }
    if (esp_ping_get_profile(h, ESP_PING_PROF_TIMEGAP, &v, sz) == ESP_OK) {
        ev.ms = v;
    }
    if (esp_ping_get_profile(h, ESP_PING_PROF_TTL, &v, sz) == ESP_OK) {
        ev.ttl = (int)v;
    }
    xQueueSend(st->q, &ev, 0);
}

static void ping_to(esp_ping_handle_t h, void *args)
{
    (void)h;
    (void)args;
}

static void ping_end(esp_ping_handle_t h, void *args)
{
    ping_state_t *st = args;
    ping_ev_t ev;
    (void)h;
    ev.type = 1;
    ev.seq = 0;
    ev.ms = 0;
    ev.ttl = 0;
    xQueueSend(st->q, &ev, 0);
}

int nnode_net_ping_lines(const char *host, int count, int interval_ms,
    nnode_ping_line_fn emit, void *ctx)
{
    ping_state_t st;
    esp_ping_config_t cfg = ESP_PING_DEFAULT_CONFIG();
    esp_ping_callbacks_t cbs;
    esp_ping_handle_t h = NULL;
    ip_addr_t addr;
    char ipstr[16];
    char line[128];
    int sent = 0, recv = 0;
    uint32_t total_ms = 0, min_ms = 0, max_ms = 0;
    int done = 0;
    if (!host || !host[0] || !emit) {
        return -1;
    }
    if (count < 1) {
        count = 1;
    }
    if (count > 10) {
        count = 10;
    }
    if (interval_ms < 200) {
        interval_ms = 200;
    }
    if (interval_ms > 10000) {
        interval_ms = 10000;
    }
    if (!ipaddr_aton(host, &addr)) {
        struct addrinfo hint, *res = NULL;
        memset(&hint, 0, sizeof(hint));
        hint.ai_family = AF_INET;
        if (getaddrinfo(host, NULL, &hint, &res) != 0 || !res) {
            emit(ctx, "ping: cannot resolve host\n");
            return -1;
        }
        /* IPv4: ip_addr_t holds the 4 address bytes up front */
        memcpy(&addr, &((struct sockaddr_in *)res->ai_addr)->sin_addr, 4);
        freeaddrinfo(res);
    }
    snprintf(ipstr, sizeof(ipstr), "%s", ipaddr_ntoa(&addr));
    memset(&st, 0, sizeof(st));
    st.q = xQueueCreate(16, sizeof(ping_ev_t));
    if (!st.q) {
        return -1;
    }
    cfg.count = (uint32_t)count;
    cfg.interval_ms = (uint32_t)interval_ms;
    cfg.timeout_ms = 1000;
    cfg.data_size = 56;
    cfg.target_addr = addr;
    memset(&cbs, 0, sizeof(cbs));
    cbs.cb_args = &st;
    cbs.on_ping_success = ping_ok;
    cbs.on_ping_timeout = ping_to;
    cbs.on_ping_end = ping_end;
    if (esp_ping_new_session(&cfg, &cbs, &h) != ESP_OK || !h) {
        vQueueDelete(st.q);
        emit(ctx, "ping: session failed\n");
        return -1;
    }
    snprintf(line, sizeof(line), "PING %s (%s): 56 data bytes\n", host, ipstr);
    if (emit(ctx, line)) {
        goto out;
    }
    esp_ping_start(h);
    while (!done) {
        ping_ev_t ev;
        if (xQueueReceive(st.q, &ev, pdMS_TO_TICKS(15000)) != pdTRUE) {
            emit(ctx, "ping: stalled\n");
            break;
        }
        if (ev.type == 1) {
            uint32_t s = 0, sz = sizeof(s);
            esp_ping_get_profile(h, ESP_PING_PROF_REQUEST, &s, sz);
            sent = (int)s;
            done = 1;
        } else {
            recv++;
            total_ms += ev.ms;
            if (recv == 1 || ev.ms < min_ms) {
                min_ms = ev.ms;
            }
            if (ev.ms > max_ms) {
                max_ms = ev.ms;
            }
            snprintf(line, sizeof(line), "64 bytes from %s: seq=%u ttl=%d time=%u ms\n",
                ipstr, (unsigned)ev.seq, ev.ttl, (unsigned)ev.ms);
            if (emit(ctx, line)) {
                break;
            }
        }
    }
out:
    esp_ping_stop(h);
    esp_ping_delete_session(h);
    vQueueDelete(st.q);
    if (done) {
        snprintf(line, sizeof(line), "\n--- %s ping statistics ---\n", host);
        if (emit(ctx, line)) {
            return 0;
        }
        snprintf(line, sizeof(line), "%d packets transmitted, %d received, %.1f%% packet loss\n",
            sent, recv, sent ? 100.0f * (float)(sent - recv) / (float)sent : 0);
        if (emit(ctx, line)) {
            return 0;
        }
        snprintf(line, sizeof(line), "round-trip min/avg/max = %u/%.1f/%u ms\n",
            (unsigned)min_ms, recv ? (float)total_ms / (float)recv : 0, (unsigned)max_ms);
        emit(ctx, line);
    }
    return 0;
}

#define TRACE_HOPS 20
#define TRACE_TIMEOUT_MS 1200
#define TRACE_PORT 33434
#define TRACE_DATA_LEN 18 /* 20 IP + 8 UDP + 18 = 46 bytes, as in the reference */

/* Classic traceroute: UDP to a closed port with increasing TTL,
 * replies caught with an ICMP raw socket (11 = TTL exceeded, 3 = reached).
 * The enclosed packet is matched (our UDP to our port), foreign ones ignored. */
int nnode_net_traceroute(const char *host, nnode_ping_line_fn emit, void *ctx)
{
    ip_addr_t addr;
    char ipstr[16];
    char line[192];
    int rsock = -1, ssock = -1;
    int ttl;
    if (!host || !host[0] || !emit) {
        return -1;
    }
    if (!ipaddr_aton(host, &addr)) {
        struct addrinfo hint, *res = NULL;
        memset(&hint, 0, sizeof(hint));
        hint.ai_family = AF_INET;
        if (getaddrinfo(host, NULL, &hint, &res) != 0 || !res) {
            emit(ctx, "traceroute: cannot resolve host\n");
            return -1;
        }
        memcpy(&addr, &((struct sockaddr_in *)res->ai_addr)->sin_addr, 4);
        freeaddrinfo(res);
    }
    snprintf(ipstr, sizeof(ipstr), "%s", ipaddr_ntoa(&addr));
    rsock = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
    ssock = socket(AF_INET, SOCK_DGRAM, 0);
    if (rsock < 0 || ssock < 0) {
        emit(ctx, "traceroute: socket failed\n");
        if (rsock >= 0) {
            close(rsock);
        }
        if (ssock >= 0) {
            close(ssock);
        }
        return -1;
    }
    snprintf(line, sizeof(line), "traceroute to %s (%s), %d hops max, %d byte packets\n",
        host, ipstr, TRACE_HOPS, 20 + 8 + TRACE_DATA_LEN);
    if (emit(ctx, line)) {
        goto out;
    }
    for (ttl = 1; ttl <= TRACE_HOPS; ttl++) {
        struct sockaddr_in dst;
        uint16_t dport = (uint16_t)(TRACE_PORT + ttl);
        uint8_t payload[TRACE_DATA_LEN];
        int64_t t0;
        size_t o = 0;
        int got = 0, reached = 0;
        memset(payload, 0, sizeof(payload));
        memset(&dst, 0, sizeof(dst));
        dst.sin_family = AF_INET;
        dst.sin_port = htons(dport);
        memcpy(&dst.sin_addr, &addr, 4);
        setsockopt(ssock, IPPROTO_IP, IP_TTL, &ttl, sizeof(ttl));
        o += snprintf(line + o, sizeof(line) - o, "%2d  ", ttl);
        t0 = esp_timer_get_time();
        if (sendto(ssock, payload, sizeof(payload), 0,
                (struct sockaddr *)&dst, sizeof(dst)) < 0) {
            o += snprintf(line + o, sizeof(line) - o, "*");
        } else {
            while (!got &&
                (esp_timer_get_time() - t0) / 1000 < TRACE_TIMEOUT_MS) {
                fd_set rfds;
                struct timeval tv;
                uint8_t buf[256];
                struct sockaddr_in src;
                socklen_t slen = sizeof(src);
                int n, ihl, type, budget;
                FD_ZERO(&rfds);
                FD_SET(rsock, &rfds);
                budget = TRACE_TIMEOUT_MS -
                    (int)((esp_timer_get_time() - t0) / 1000);
                tv.tv_sec = 0;
                tv.tv_usec = budget * 1000;
                if (select(rsock + 1, &rfds, NULL, NULL, &tv) <= 0) {
                    break;
                }
                n = recvfrom(rsock, buf, sizeof(buf), 0,
                    (struct sockaddr *)&src, &slen);
                if (n < 20) {
                    continue;
                }
                ihl = (buf[0] & 0x0f) * 4;
                if (ihl < 20 || n < ihl + 8) {
                    continue;
                }
                type = buf[ihl];
                if (type != 11 && type != 3) {
                    continue;
                }
                {
                    int off = ihl + 8;
                    uint8_t ihl2;
                    uint16_t inner_dport;
                    if (n < off + 20) {
                        continue;
                    }
                    if (buf[off + 9] != 17) { /* not UDP */
                        continue;
                    }
                    ihl2 = (buf[off] & 0x0f) * 4;
                    if (ihl2 < 20 || n < off + ihl2 + 4) {
                        continue;
                    }
                    /* an IPv4 address is the first 4 bytes of ip_addr_t */
                    if (memcmp(buf + off + 16, &addr, 4) != 0) {
                        continue;
                    }
                    inner_dport = ((uint16_t)buf[off + ihl2 + 2] << 8) |
                        buf[off + ihl2 + 3];
                    if (inner_dport != dport) {
                        continue;
                    }
                }
                {
                    char hstr[16];
                    int64_t dt = esp_timer_get_time() - t0;
                    snprintf(hstr, sizeof(hstr), "%s",
                        ipaddr_ntoa((const ip_addr_t *)&src.sin_addr));
                    o += snprintf(line + o, sizeof(line) - o, "%s %.3f ms",
                        hstr, dt / 1000.0);
                }
                got = 1;
                if (type == 3) {
                    reached = 1;
                }
            }
            if (!got) {
                o += snprintf(line + o, sizeof(line) - o, "*");
            }
        }
        o += snprintf(line + o, sizeof(line) - o, "\n");
        if (emit(ctx, line)) {
            goto out;
        }
        if (reached) {
            break;
        }
    }
out:
    close(rsock);
    close(ssock);
    return 0;
}

int nnode_net_nslookup(const char *host, char *buf, size_t len)
{
    struct addrinfo hint, *res = NULL, *p;
    size_t o = 0;
    int n = 0;
    if (!host || !host[0] || !buf || !len) {
        return -1;
    }
    memset(&hint, 0, sizeof(hint));
    hint.ai_family = AF_INET;
    if (getaddrinfo(host, NULL, &hint, &res) != 0 || !res) {
        return snprintf(buf, len, "nslookup: cannot resolve %s\n", host);
    }
    o += snprintf(buf + o, len - o, "%s:\n", host);
    for (p = res; p && o < len; p = p->ai_next) {
        char ip[16];
        if (p->ai_family != AF_INET) {
            continue;
        }
        snprintf(ip, sizeof(ip), "%s",
            ipaddr_ntoa((const ip_addr_t *)
                &((struct sockaddr_in *)p->ai_addr)->sin_addr));
        o += snprintf(buf + o, len - o, "  %s\n", ip);
        n++;
    }
    freeaddrinfo(res);
    if (!n && o < len) {
        o += snprintf(buf + o, len - o, "  no IPv4 records\n");
    }
    return (int)o;
}

int nnode_net_renew(void)
{
    esp_netif_t *sta = nnode_net_sta();
    if (!sta) {
        return -1;
    }
    esp_netif_dhcpc_stop(sta);
    return esp_netif_dhcpc_start(sta) == ESP_OK ? 0 : -1;
}

int nnode_ap_stations(char *buf, size_t len)
{
    wifi_sta_list_t list;
    esp_netif_t *ap;
    size_t o = 0;
    int i;
    memset(&list, 0, sizeof(list));
    if (esp_wifi_ap_get_sta_list(&list) != ESP_OK) {
        return snprintf(buf, len, "{\"error\":\"sta_list\"}");
    }
    ap = nnode_net_ap();
    o += snprintf(buf + o, len - o, "[");
    for (i = 0; i < list.num && o < len; i++) {
        char m[18];
        char ip[16];
        esp_netif_pair_mac_ip_t pair;
        mac_str(list.sta[i].mac, m);
        snprintf(ip, sizeof(ip), "-");
        if (ap) {
            memcpy(pair.mac, list.sta[i].mac, 6);
            pair.ip.addr = 0;
            if (esp_netif_arp_get_client_by_mac(ap, &pair) == ESP_OK && pair.ip.addr) {
                snprintf(ip, sizeof(ip), IPSTR, IP2STR(&pair.ip));
            }
        }
        o += snprintf(buf + o, len - o, "%s{\"mac\":\"%s\",\"ip\":\"%s\",\"rssi\":%d}",
            i ? "," : "", m, ip, list.sta[i].rssi);
    }
    if (o < len) {
        o += snprintf(buf + o, len - o, "]");
    }
    return (int)o;
}

int nnode_ap_deauth(const char *mac)
{
    uint8_t m[6];
    unsigned int v[6];
    int i;
    uint16_t aid = 0;
    if (!mac || sscanf(mac, "%2x:%2x:%2x:%2x:%2x:%2x",
            &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]) != 6) {
        return -1;
    }
    for (i = 0; i < 6; i++) {
        m[i] = (uint8_t)v[i];
    }
    if (esp_wifi_ap_get_sta_aid(m, &aid) != ESP_OK || !aid) {
        return -1;
    }
    return esp_wifi_deauth_sta(aid) == ESP_OK ? 0 : -1;
}

static int jval(const char *body, const char *key, char *out, size_t cap)
{
    char pat[40];
    const char *p;
    size_t o = 0;
    snprintf(pat, sizeof(pat), "\"%s\":\"", key);
    p = strstr(body, pat);
    if (!p) {
        return -1;
    }
    p += strlen(pat);
    while (*p && o + 1 < cap) {
        if (*p == '\\' && p[1]) {
            p++;
            out[o++] = *p++;
        } else if (*p == '"') {
            out[o] = 0;
            return 0;
        } else {
            out[o++] = *p++;
        }
    }
    out[o] = 0;
    return *p == '"' ? 0 : -1;
}

int nnode_nets_json(char *buf, size_t len)
{
    nnode_net_cred_t nets[NNODE_MAX_NETS];
    char cur[33], dummy[64];
    int n, i;
    size_t o = 0;
    n = nnode_nets_load(nets, NNODE_MAX_NETS);
    nnode_sta_cur(cur, sizeof(cur), dummy, sizeof(dummy));
    o += snprintf(buf + o, len - o, "{\"nets\":[");
    for (i = 0; i < n && o < len; i++) {
        o += snprintf(buf + o, len - o, "%s{\"ssid\":\"%s\",\"bssid\":\"%s\",\"auth\":\"%s\"}",
            i ? "," : "", nets[i].ssid, nets[i].bssid,
            nnode_auth_valid(nets[i].auth) ? nets[i].auth : "auto");
    }
    if (o < len) {
        o += snprintf(buf + o, len - o, "],\"connected\":\"%s\"}", cur);
    }
    return (int)o;
}

int nnode_ap_json(char *buf, size_t len)
{
    nnode_ap_cfg_t ap;
    nnode_ap_get(&ap);
    return snprintf(buf, len, "{\"ssid\":\"%s\",\"auth\":\"%s\",\"pass\":\"%s\",\"mode\":\"%s\",\"nat\":%d}",
        ap.ssid, ap.auth, ap.pass, ap.mode, ap.nat ? 1 : 0);
}

int nnode_ap_set_json(const char *body)
{
    nnode_ap_cfg_t ap;
    if (nnode_ap_parse_json(body, &ap) != 0) {
        return -1;
    }
    return nnode_ap_set(&ap);
}

int nnode_ap_parse_json(const char *body, nnode_ap_cfg_t *out)
{
    const char *p;
    if (!body || !out) {
        return -1;
    }
    nnode_ap_get(out);
    if (jval(body, "ssid", out->ssid, sizeof(out->ssid)) != 0 ||
        jval(body, "auth", out->auth, sizeof(out->auth)) != 0) {
        return -1;
    }
    jval(body, "pass", out->pass, sizeof(out->pass));
    if (jval(body, "mode", out->mode, sizeof(out->mode)) != 0) {
        strncpy(out->mode, "auto", sizeof(out->mode) - 1);
    }
    p = strstr(body, "\"nat\":");
    if (p) {
        out->nat = atoi(p + 6) ? 1 : 0;
    }
    return 0;
}
