#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_partition.h"
#include "esp_ota_ops.h"
#include "esp_app_desc.h"
#include "ota.h"
#include "files.h"

static const char *TAG = "nnode_ota";

/* OTA app subtypes in boot order. Labels stay standard (ota_0/ota_1):
 * the panel renders the slot name from the subtype already. */
static const esp_partition_subtype_t kOtaSubs[] = {
    ESP_PARTITION_SUBTYPE_APP_OTA_0,
    ESP_PARTITION_SUBTYPE_APP_OTA_1,
};

static const char *state_name(const esp_partition_t *p)
{
    esp_ota_img_states_t st;
    esp_app_desc_t d;
    int has = esp_ota_get_partition_description(p, &d) == ESP_OK;
    if (esp_ota_get_state_partition(p, &st) != ESP_OK) {
        return has ? "valid" : "empty";
    }
    switch (st) {
    case ESP_OTA_IMG_NEW:
        return "new";
    case ESP_OTA_IMG_PENDING_VERIFY:
        return "pending";
    case ESP_OTA_IMG_VALID:
        return "valid";
    case ESP_OTA_IMG_INVALID:
        return "invalid";
    case ESP_OTA_IMG_ABORTED:
        return "aborted";
    default:
        /* Fresh serial flash leaves otadata erased (UNDEFINED) while
         * the slot holds a good image: report it as usable. */
        return has ? "valid" : "empty";
    }
}

int nnode_ota_status_json(char *buf, size_t len)
{
    const esp_partition_t *run = esp_ota_get_running_partition();
    const esp_partition_t *boot = esp_ota_get_boot_partition();
    int n, i, first = 1;
    n = snprintf(buf, len, "{\"running\":\"%s\",\"boot\":\"%s\",\"slots\":[",
        run ? run->label : "?", boot ? boot->label : "?");
    for (i = 0; i < 2 && n < (int)len; i++) {
        const esp_partition_t *p =
            esp_partition_find_first(ESP_PARTITION_TYPE_APP, kOtaSubs[i], NULL);
        esp_app_desc_t d;
        int has;
        if (!p) {
            continue;
        }
        has = esp_ota_get_partition_description(p, &d) == ESP_OK;
        n += snprintf(buf + n, len - n,
            "%s{\"label\":\"%s\",\"version\":\"%s\",\"state\":\"%s\"%s%s}",
            first ? "" : ",",
            p->label, has ? d.version : "", state_name(p),
            (run && strcmp(run->label, p->label) == 0) ? ",\"running\":1" : "",
            (boot && strcmp(boot->label, p->label) == 0) ? ",\"boot\":1" : "");
        first = 0;
    }
    if (n < (int)len) {
        n += snprintf(buf + n, len - n, "]}");
    }
    return n;
}

int nnode_ota_upload(nnode_ota_read_fn rd, void *ctx, int total,
    char *err, size_t errlen)
{
    const esp_partition_t *part;
    esp_ota_handle_t h = 0;
    char chunk[4096];
    int left = total;
    int first = 1;
    if (nnode_elf_any_loaded()) {
        snprintf(err, errlen, "tasks_running");
        return -1;
    }
    /* With two slots IDF returns the inactive one: roles swap on
     * every update, the previous image stays as the fallback. */
    part = esp_ota_get_next_update_partition(NULL);
    if (!part) {
        snprintf(err, errlen, "ota_target");
        return -1;
    }
    ESP_LOGI(TAG, "flash %d bytes to %s", total, part->label);
    if (esp_ota_begin(part, (size_t)total, &h) != ESP_OK) {
        snprintf(err, errlen, "ota_begin");
        return -1;
    }
    while (left > 0) {
        int want = left > (int)sizeof(chunk) ? (int)sizeof(chunk) : left;
        int got = rd(ctx, chunk, (size_t)want);
        if (got <= 0) {
            esp_ota_abort(h);
            snprintf(err, errlen, "ota_write");
            return -1;
        }
        if (first) {
            first = 0;
            /* App images start with the 0xE9 magic; reject stray
             * uploads (backup files, ELF modules) right away. */
            if ((unsigned char)chunk[0] != 0xE9) {
                esp_ota_abort(h);
                snprintf(err, errlen, "bad_image");
                return -1;
            }
        }
        if (esp_ota_write(h, chunk, (size_t)got) != ESP_OK) {
            esp_ota_abort(h);
            snprintf(err, errlen, "ota_write");
            return -1;
        }
        left -= got;
    }
    /* End validates the image; only then flip the boot flag. */
    if (esp_ota_end(h) != ESP_OK) {
        snprintf(err, errlen, "ota_end");
        return -1;
    }
    if (esp_ota_set_boot_partition(part) != ESP_OK) {
        snprintf(err, errlen, "ota_boot");
        return -1;
    }
    ESP_LOGI(TAG, "slot %s staged, rebooting", part->label);
    return 0;
}

int nnode_ota_rollback(const char *label, char *err, size_t errlen)
{
    const esp_partition_t *run = esp_ota_get_running_partition();
    int i;
    if (nnode_elf_any_loaded()) {
        snprintf(err, errlen, "tasks_running");
        return -1;
    }
    for (i = 0; i < 2; i++) {
        const esp_partition_t *p =
            esp_partition_find_first(ESP_PARTITION_TYPE_APP, kOtaSubs[i], NULL);
        esp_app_desc_t d;
        esp_ota_img_states_t st;
        if (!p) {
            continue;
        }
        if (run && strcmp(run->label, p->label) == 0) {
            continue;
        }
        if (label && label[0] && strcmp(label, p->label) != 0) {
            continue;
        }
        /* Target must hold a readable image that is not marked bad. */
        if (esp_ota_get_partition_description(p, &d) != ESP_OK) {
            continue;
        }
        if (esp_ota_get_state_partition(p, &st) == ESP_OK &&
            (st == ESP_OTA_IMG_INVALID || st == ESP_OTA_IMG_ABORTED)) {
            continue;
        }
        if (esp_ota_set_boot_partition(p) != ESP_OK) {
            snprintf(err, errlen, "ota_boot");
            return -1;
        }
        ESP_LOGI(TAG, "rollback to %s", p->label);
        return 0;
    }
    snprintf(err, errlen, "not_found");
    return -1;
}

void nnode_ota_mark_valid(void)
{
#ifdef CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE
    const esp_partition_t *run = esp_ota_get_running_partition();
    esp_ota_img_states_t st;
    if (run && esp_ota_get_state_partition(run, &st) == ESP_OK &&
        st == ESP_OTA_IMG_PENDING_VERIFY) {
        if (esp_ota_mark_app_valid_cancel_rollback() == ESP_OK) {
            ESP_LOGI(TAG, "slot %s confirmed", run->label);
        }
    }
#endif
}
