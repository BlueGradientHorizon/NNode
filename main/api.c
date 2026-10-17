#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include "esp_chip_info.h"
#include "esp_system.h"
#include "esp_idf_version.h"
#include "esp_app_desc.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_partition.h"
#include "esp_mac.h"
#include "esp_flash.h"
#include "wifi.h"
#include "fs.h"
#include "syscfg.h"
#include "api.h"
#include "bsp.h"

int nnode_api_summary(char *buf, size_t len)
{
    char ip[16];
    char idf[16];
    uint64_t fst, fsu;
    uint32_t hfree, htotal;
    multi_heap_info_t mi;
    nnode_net_ip(ip, sizeof(ip));
    nnode_fs_usage(&fst, &fsu);
    /* esp_get_idf_version() returns a git describe/hash, show the numeric one */
    if (ESP_IDF_VERSION_PATCH == 0) {
        snprintf(idf, sizeof(idf), "%d.%d",
            ESP_IDF_VERSION_MAJOR, ESP_IDF_VERSION_MINOR);
    } else {
        snprintf(idf, sizeof(idf), "%d.%d.%d",
            ESP_IDF_VERSION_MAJOR, ESP_IDF_VERSION_MINOR, ESP_IDF_VERSION_PATCH);
    }
    memset(&mi, 0, sizeof(mi));
    heap_caps_get_info(&mi, MALLOC_CAP_INTERNAL);
    hfree = (uint32_t)mi.total_free_bytes;
    htotal = (uint32_t)(mi.total_allocated_bytes + mi.total_free_bytes);
    return snprintf(buf, len,
        "{\"sysname\":\"ESP-IDF\",\"release\":\"%s\",\"machine\":\"%s\","
        "\"uptime\":%lld,\"ip\":\"%s\","
        "\"mem_used\":%u,\"mem_free\":%u,"
        "\"largest\":%u,\"min\":%u,\"ablocks\":%u,\"fblocks\":%u,"
        "\"fs_blocks\":%llu,\"fs_bfree\":%llu,\"fs_bsize\":1}",
        idf, bsp_machine(),
        (long long)(esp_timer_get_time() / 1000000),
        ip, (unsigned)(htotal - hfree), (unsigned)hfree,
        (unsigned)mi.largest_free_block, (unsigned)mi.minimum_free_bytes,
        (unsigned)mi.allocated_blocks, (unsigned)mi.free_blocks,
        (unsigned long long)fst, (unsigned long long)(fst - fsu));
}

static float nnode_temp(void)
{
    return bsp_temp_celsius();
}

int nnode_api_sys(char *buf, size_t len)
{
    char ip[16];
    char host[40];
    char mac[18];
    char idf[16];
    esp_chip_info_t ci;
    const esp_app_desc_t *app;
    const char *reset;
    uint32_t flash_size = 0;
    uint8_t m[6];
    size_t n;
    esp_partition_iterator_t it;
    int first = 1;
    struct timeval tv;
    uint64_t fst = 0, fsu = 0;
    const char *model = bsp_model();
    const char *arch = bsp_arch();
    nnode_net_ip(ip, sizeof(ip));
    esp_chip_info(&ci);
    app = esp_app_get_description();
    gettimeofday(&tv, NULL);
    nnode_fs_usage(&fst, &fsu);
    nnode_hostname_get(host, sizeof(host));
    if (ESP_IDF_VERSION_PATCH == 0) {
        snprintf(idf, sizeof(idf), "%d.%d",
            ESP_IDF_VERSION_MAJOR, ESP_IDF_VERSION_MINOR);
    } else {
        snprintf(idf, sizeof(idf), "%d.%d.%d",
            ESP_IDF_VERSION_MAJOR, ESP_IDF_VERSION_MINOR, ESP_IDF_VERSION_PATCH);
    }
    if (esp_read_mac(m, ESP_MAC_BASE) == ESP_OK) {
        snprintf(mac, sizeof(mac), "%02x:%02x:%02x:%02x:%02x:%02x",
            m[0], m[1], m[2], m[3], m[4], m[5]);
    } else {
        snprintf(mac, sizeof(mac), "?");
    }
    if (esp_flash_get_size(NULL, &flash_size) != ESP_OK) {
        flash_size = 0;
    }
    switch (esp_reset_reason()) {
    case ESP_RST_POWERON: reset = "power-on"; break;
    case ESP_RST_EXT: reset = "external"; break;
    case ESP_RST_SW: reset = "software"; break;
    case ESP_RST_PANIC: reset = "panic"; break;
    case ESP_RST_INT_WDT: reset = "int-wdt"; break;
    case ESP_RST_TASK_WDT: reset = "task-wdt"; break;
    case ESP_RST_WDT: reset = "wdt"; break;
    case ESP_RST_DEEPSLEEP: reset = "deep-sleep"; break;
    case ESP_RST_BROWNOUT: reset = "brownout"; break;
    case ESP_RST_SDIO: reset = "sdio"; break;
    case ESP_RST_USB: reset = "usb"; break;
    case ESP_RST_JTAG: reset = "jtag"; break;
    case ESP_RST_EFUSE: reset = "efuse"; break;
    case ESP_RST_PWR_GLITCH: reset = "pwr-glitch"; break;
    case ESP_RST_CPU_LOCKUP: reset = "cpu-lockup"; break;
    default: reset = "unknown"; break;
    }
    n = snprintf(buf, len,
        "{\"fw\":\"%s\",\"idf\":\"%s\",\"uptime\":%lld,\"time\":%lld,\"ip\":\"%s\",\"rssi\":%d,"
        "\"cpu_mhz\":%u,\"temp_c\":%.1f,\"heap_free\":%u,\"heap_min\":%u,"
        "\"chip_rev\":%d,\"cores\":%d,\"fs_total\":%llu,\"fs_used\":%llu,"
        "\"hostname\":\"%s\",\"model\":\"%s\",\"arch\":\"%s\",\"target\":\"espressif/%s\","
        "\"flash_mb\":%u,\"mac\":\"%s\",\"reset\":\"%s\",\"partitions\":[",
        app->version, idf,
        (long long)(esp_timer_get_time() / 1000000), (long long)tv.tv_sec, ip, nnode_net_rssi(),
        (unsigned)CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ, nnode_temp(),
        (unsigned)esp_get_free_heap_size(), (unsigned)esp_get_minimum_free_heap_size(),
        (int)ci.revision, (int)ci.cores,
        (unsigned long long)fst, (unsigned long long)fsu,
        host, model, arch, CONFIG_IDF_TARGET,
        (unsigned)(flash_size / 1048576), mac, reset);
    it = esp_partition_find(ESP_PARTITION_TYPE_ANY, ESP_PARTITION_SUBTYPE_ANY, NULL);
    for (; it && n < len; it = esp_partition_next(it)) {
        const esp_partition_t *p = esp_partition_get(it);
        n += snprintf(buf + n, len - n,
            "%s{\"label\":\"%s\",\"type\":%d,\"subtype\":%d,\"offset\":%u,\"size\":%u}",
            first ? "" : ",", p->label, (int)p->type, (int)p->subtype,
            (unsigned)p->address, (unsigned)p->size);
        first = 0;
    }
    esp_partition_iterator_release(it);
    if (n < len) {
        n += snprintf(buf + n, len - n, "]}");
    }
    return (int)n;
}

int nnode_api_time_set(int64_t ts)
{
    struct timeval tv;
    tv.tv_sec = (time_t)ts;
    tv.tv_usec = 0;
    return settimeofday(&tv, NULL) == 0 ? 0 : -1;
}
