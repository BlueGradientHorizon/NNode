#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include "esp_log.h"
#include "esp_elf.h"
#include "esp_heap_caps.h"
#include "private/elf_types.h"
#include "arena.h"
#include "files.h"

static const char *TAG = "nnode_files";

int nnode_file_valid(const char *name)
{
    if (!name || !name[0] || strlen(name) > 64) {
        return 0;
    }
    if (strchr(name, '/') || strstr(name, "..")) {
        return 0;
    }
    return 1;
}

/* ELF detection by magic, not by extension */
static int has_elf_magic(const char *path)
{
    FILE *f;
    unsigned char m[4];
    size_t n = 0;
    f = fopen(path, "rb");
    if (!f) {
        return 0;
    }
    n = fread(m, 1, 4, f);
    fclose(f);
    return n == 4 && m[0] == 0x7f && m[1] == 'E' && m[2] == 'L' && m[3] == 'F';
}

int nnode_files_json(char *buf, size_t len)
{
    DIR *d;
    struct dirent *e;
    size_t o = 0;
    int first = 1;
    d = opendir("/data");
    if (!d) {
        return snprintf(buf, len, "{\"error\":\"fs\"}");
    }
    o += snprintf(buf + o, len - o, "[");
    while ((e = readdir(d)) && o < len) {
        struct stat st;
        char path[80];
        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) {
            continue;
        }
        snprintf(path, sizeof(path), "/data/%.64s", e->d_name);
        memset(&st, 0, sizeof(st));
        stat(path, &st);
        o += snprintf(buf + o, len - o, "%s{\"name\":\"%s\",\"size\":%u,\"loaded\":%d,\"is_elf\":%d}",
            first ? "" : ",", e->d_name, (unsigned)st.st_size,
            nnode_elf_loaded(e->d_name) ? 1 : 0, has_elf_magic(path) ? 1 : 0);
        first = 0;
    }
    closedir(d);
    if (o < len) {
        o += snprintf(buf + o, len - o, "]");
    }
    return (int)o;
}

int nnode_file_delete(const char *name)
{
    char path[80];
    if (!nnode_file_valid(name)) {
        return -1;
    }
    snprintf(path, sizeof(path), "/data/%s", name);
    if (unlink(path) != 0) {
        return -2;
    }
    ESP_LOGI(TAG, "deleted %s", name);
    return 0;
}

/* Loaded modules registry: a module stays mapped until explicitly
 * unloaded, because its tasks may outlive app_main (unloading under
 * a running task executes freed IRAM -> Illegal instruction).
 * Each slot owns a dynamic accounting arena (see arena.c): code lives in
 * IRAM (MALLOC_CAP_EXEC via the loader), data/heap in DRAM; mallocs and
 * tasks created by the module are attributed to the slot and reclaimed
 * on unload. Nothing is reserved at boot. */
#define NNODE_MAX_LOADED 8
#if NNODE_MAX_LOADED != NNODE_ARENA_SLOTS
#error "slot count must match arena slots"
#endif
typedef struct {
    int used;
    char name[65];
    elf_file_t file;
    esp_elf_t elf;
    size_t text_size; /* PT_LOAD with PF_X: IRAM image */
    size_t data_size; /* other PT_LOADs: DRAM image */
} loaded_mod_t;
static loaded_mod_t s_mods[NNODE_MAX_LOADED];

/* C3 uses the non-mirror loader path (single psegment, sec[] stays empty),
 * so measure the images from the program headers of the resident payload:
 * executable segments go to IRAM, the rest to DRAM. */
static void measure_images(const uint8_t *payload, size_t *text, size_t *data)
{
    const elf32_hdr_t *ehdr;
    const elf32_phdr_t *phdr;
    int i;
    *text = 0;
    *data = 0;
    if (!payload) {
        return;
    }
    ehdr = (const elf32_hdr_t *)payload;
    if (ehdr->ident[0] != 0x7f || ehdr->ident[1] != 'E' ||
        ehdr->ident[2] != 'L' || ehdr->ident[3] != 'F') {
        return;
    }
    if (ehdr->phoff == 0 || ehdr->phnum == 0 || ehdr->phnum > 16) {
        return;
    }
    phdr = (const elf32_phdr_t *)(payload + ehdr->phoff);
    for (i = 0; i < ehdr->phnum; i++) {
        if (phdr[i].type != PT_LOAD || phdr[i].memsz == 0) {
            continue;
        }
        if (phdr[i].flags & 1) {
            *text += phdr[i].memsz;
        } else {
            *data += phdr[i].memsz;
        }
    }
}

int nnode_elf_loaded(const char *name)
{
    int i;
    if (!name) {
        return 0;
    }
    for (i = 0; i < NNODE_MAX_LOADED; i++) {
        if (s_mods[i].used && strcmp(s_mods[i].name, name) == 0) {
            return 1;
        }
    }
    return 0;
}

int nnode_elf_any_loaded(void)
{
    int i;
    for (i = 0; i < NNODE_MAX_LOADED; i++) {
        if (s_mods[i].used) {
            return 1;
        }
    }
    return 0;
}

int nnode_elf_run(const char *name, char *errbuf, size_t errlen)
{
    elf_file_t f;
    esp_elf_t elf;
    int r, i, slot;
    char path[80];
    if (!nnode_file_valid(name)) {
        snprintf(errbuf, errlen, "bad_name");
        return -1;
    }
    snprintf(path, sizeof(path), "/data/%s", name);
    if (!has_elf_magic(path)) {
        snprintf(errbuf, errlen, "not_elf");
        return -1;
    }
    if (nnode_elf_loaded(name)) {
        snprintf(errbuf, errlen, "already_loaded");
        return -1;
    }
    size_t exec_free, mem_free;
    int prev_active;
    nnode_arena_init();
    slot = -1;
    for (i = 0; i < NNODE_MAX_LOADED; i++) {
        if (!s_mods[i].used) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        snprintf(errbuf, errlen, "no_slots");
        return -1;
    }
    exec_free = heap_caps_get_largest_free_block(MALLOC_CAP_EXEC);
    mem_free = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
    if (exec_free < 8192 || mem_free < 24576) {
        snprintf(errbuf, errlen, "no_mem");
        return -1;
    }
    nnode_arena_alloc(slot);
    memset(&f, 0, sizeof(f));
    if (esp_elf_open(&f, name) != 0) {
        snprintf(errbuf, errlen, "elf_open");
        nnode_arena_free_slot(slot);
        return -1;
    }
    memset(&elf, 0, sizeof(elf));
    if (esp_elf_init(&elf) != 0) {
        snprintf(errbuf, errlen, "elf_init");
        esp_elf_close(&f);
        nnode_arena_free_slot(slot);
        return -1;
    }
    r = esp_elf_relocate(&elf, f.payload);
    if (r != 0) {
        ESP_LOGE(TAG, "relocate %s failed: %d", name, r);
        snprintf(errbuf, errlen, "elf_relocate");
        esp_elf_deinit(&elf);
        esp_elf_close(&f);
        nnode_arena_free_slot(slot);
        return -1;
    }
    ESP_LOGI(TAG, "running %s", name);
    prev_active = nnode_arena_set_active(slot);
    esp_elf_request(&elf, 0, 0, NULL);
    nnode_arena_set_active(prev_active);
    ESP_LOGI(TAG, "%s stays loaded (unload explicitly)", name);
    memset(&s_mods[slot], 0, sizeof(s_mods[slot]));
    s_mods[slot].used = 1;
    strncpy(s_mods[slot].name, name, sizeof(s_mods[slot].name) - 1);
    s_mods[slot].file = f;
    s_mods[slot].elf = elf;
    measure_images(f.payload, &s_mods[slot].text_size, &s_mods[slot].data_size);
    ESP_LOGI(TAG, "%s: text %u data %u file %u", name,
        (unsigned)s_mods[slot].text_size, (unsigned)s_mods[slot].data_size,
        (unsigned)f.size);
    return 0;
}


int nnode_elf_stop(const char *name)
{
    int i;
    if (!nnode_file_valid(name)) {
        return -1;
    }
    for (i = 0; i < NNODE_MAX_LOADED; i++) {
        if (s_mods[i].used && strcmp(s_mods[i].name, name) == 0) {
            nnode_arena_kill_tasks(i);
            nnode_arena_free_slot(i);
            esp_elf_deinit(&s_mods[i].elf);
            esp_elf_close(&s_mods[i].file);
            memset(&s_mods[i], 0, sizeof(s_mods[i]));
            ESP_LOGI(TAG, "stopped %s", name);
            return 0;
        }
    }
    return -2;
}

int nnode_elf_unload(const char *name)
{
    int i;
    if (!nnode_file_valid(name)) {
        return -1;
    }
    for (i = 0; i < NNODE_MAX_LOADED; i++) {
        if (s_mods[i].used && strcmp(s_mods[i].name, name) == 0) {
            if (nnode_arena_slot_has_live_tasks(i)) {
                ESP_LOGW(TAG, "unload %s refused: kill tasks first", name);
                return -3;
            }
            nnode_arena_free_slot(i);
            esp_elf_deinit(&s_mods[i].elf);
            esp_elf_close(&s_mods[i].file);
            memset(&s_mods[i], 0, sizeof(s_mods[i]));
            ESP_LOGI(TAG, "unloaded %s", name);
            return 0;
        }
    }
    return -2;
}
