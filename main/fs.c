#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_partition.h"
#include "esp_littlefs.h"
#include "fs.h"

static const char *TAG = "nnode_fs";

static void nnode_fs_register(void)
{
    const esp_partition_t *part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
        ESP_PARTITION_SUBTYPE_DATA_LITTLEFS, "storage");
    if (!part) {
        ESP_LOGE(TAG, "storage partition not found");
        return;
    }
    esp_vfs_littlefs_conf_t c = {
        .base_path = "/data",
        .partition_label = NULL,
        .partition = part,
        .format_if_mount_failed = true,
        .dont_mount = false,
    };
    esp_err_t r = esp_vfs_littlefs_register(&c);
    if (r != ESP_OK) {
        ESP_LOGE(TAG, "mount failed: %s", esp_err_to_name(r));
    }
}

void nnode_fs_mount(void)
{
    size_t t = 0, u = 0;
    nnode_fs_register();
    esp_err_t r2 = esp_littlefs_info("storage", &t, &u);
    ESP_LOGI(TAG, "mounted /data total=%u used=%u info=%d", (unsigned)t, (unsigned)u, (int)r2);
    if (r2 == ESP_OK && t == 0) {
        /* Mount reported success on a filesystem with zero capacity
         * (seen on blank flash: mount neither fails nor formats).
         * Zero capacity means nothing is readable, so an explicit
         * format cannot destroy user data here. */
        ESP_LOGW(TAG, "zero capacity, explicit format...");
        esp_vfs_littlefs_unregister("storage");
        esp_err_t fr = esp_littlefs_format("storage");
        ESP_LOGI(TAG, "explicit format=%d", (int)fr);
        nnode_fs_register();
        t = u = 0;
        r2 = esp_littlefs_info("storage", &t, &u);
        ESP_LOGI(TAG, "remounted /data total=%u used=%u info=%d", (unsigned)t, (unsigned)u, (int)r2);
    }
    FILE *f = fopen("/data/.selftest", "w");
    if (!f) {
        ESP_LOGE(TAG, "selftest write open failed");
        return;
    }
    int w = fputs("nnode-ok", f);
    int ce = fclose(f);
    ESP_LOGI(TAG, "selftest write w=%d fclose=%d", w, ce);
    char rb[16];
    memset(rb, 0, sizeof(rb));
    int rr = -1;
    f = fopen("/data/.selftest", "r");
    if (f) {
        rr = fread(rb, 1, sizeof(rb) - 1, f);
        fclose(f);
    }
    ESP_LOGI(TAG, "selftest read rr=%d data=%s", rr, rb);
}

void nnode_fs_usage(uint64_t *total, uint64_t *used)
{
    size_t t = 0, u = 0;
    if (esp_littlefs_info("storage", &t, &u) == ESP_OK) {
        *total = t;
        *used = u;
    } else {
        *total = 0;
        *used = 0;
    }
}
