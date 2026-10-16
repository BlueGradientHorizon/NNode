#include "nvs_flash.h"
#include "esp_log.h"
#include "cfg.h"
#include "wifi.h"
#include "arena.h"
#include "graphs.h"
#include "logbuf.h"
#include "syscfg.h"
#include "fs.h"
#include "files.h"
#include "http.h"
#include "modsym.h"

static const char *TAG = "nnode";

void app_main(void)
{
    /* NVS init serves IDF internals only (the WiFi driver aborts
     * without it, even with WIFI_STORAGE_RAM). NNode settings live
     * in /data/config files and never touch NVS. */
    esp_err_t r = nvs_flash_init();
    if (r == ESP_ERR_NVS_NO_FREE_PAGES || r == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        r = nvs_flash_init();
    }
    if (r != ESP_OK) {
        ESP_LOGE(TAG, "nvs init failed: %s", esp_err_to_name(r));
    }
    nnode_log_start();
    nnode_arena_init();
    nnode_sym_resolver_install();
    nnode_fs_mount();
    nnode_cfg_init();
    nnode_programs_init();
    nnode_cfg_seed_defaults();
    nnode_wifi_start();
    nnode_sys_apply();
    nnode_graphs_start();
    nnode_http_start();
    ESP_LOGI(TAG, "ready");
}
