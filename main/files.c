#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include "esp_log.h"
#include "esp_elf.h"
#include "esp_heap_caps.h"
#include "private/elf_types.h"
#include "arena.h"
#include "cfg.h"
#include "files.h"
#include "cfg_types.h"
#include "cfg_schema.h"
#include "fw_defaults.h"

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

/* One path segment: same rules as a file name, plus no bare dots. */
static int seg_valid(const char *s)
{
    if (!nnode_file_valid(s)) {
        return 0;
    }
    if (strcmp(s, ".") == 0) {
        return 0;
    }
    return 1;
}

/* Relative path inside /data: up to 8 segments, 128 chars total. */
int nnode_path_valid(const char *p)
{
    int depth = 0;
    const char *s;
    size_t total;
    if (!p || (total = strlen(p)) == 0 || total > 128) {
        return 0;
    }
    if (p[0] == '/' || p[total - 1] == '/') {
        return 0;
    }
    s = p;
    while (*s) {
        const char *e = strchr(s, '/');
        size_t n = e ? (size_t)(e - s) : strlen(s);
        char seg[72];
        if (n == 0 || n > 64 || ++depth > 8) {
            return 0;
        }
        memcpy(seg, s, n);
        seg[n] = 0;
        if (!seg_valid(seg)) {
            return 0;
        }
        s = e ? e + 1 : s + n;
    }
    return 1;
}

int nnode_dir_create(const char *path)
{
    char full[160];
    if (!nnode_path_valid(path)) {
        return -1;
    }
    snprintf(full, sizeof(full), "/data/%s", path);
    if (mkdir(full, 0755) != 0) {
        return -1;
    }
    ESP_LOGI(TAG, "mkdir %s", path);
    return 0;
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

void nnode_programs_init(void)
{
    DIR *d;
    struct dirent *e;
    struct stat st;
    memset(&st, 0, sizeof(st));
    if (stat("/data/" NNOODE_PROGRAMS_DIR, &st) != 0) {
        if (mkdir("/data/" NNOODE_PROGRAMS_DIR, 0755) != 0) {
            ESP_LOGE(TAG, "programs mkdir failed");
            return;
        }
    }
    /* One-time move: root ELF files from before the programs dir
     * existed go to their new home. Nothing is loaded yet, so a
     * rename is safe. */
    d = opendir("/data");
    if (!d) {
        return;
    }
    while ((e = readdir(d)) != NULL) {
        char src[80], dst[160];
        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) {
            continue;
        }
        snprintf(src, sizeof(src), "/data/%.64s", e->d_name);
        memset(&st, 0, sizeof(st));
        if (stat(src, &st) != 0 || S_ISDIR(st.st_mode)) {
            continue;
        }
        if (!has_elf_magic(src)) {
            continue;
        }
        snprintf(dst, sizeof(dst), "/data/" NNOODE_PROGRAMS_DIR "/%.64s",
            e->d_name);
        memset(&st, 0, sizeof(st));
        if (stat(dst, &st) == 0) {
            continue;
        }
        if (rename(src, dst) == 0) {
            ESP_LOGI(TAG, "moved %s to programs", e->d_name);
        }
    }
    closedir(d);
}

int nnode_files_json(char *buf, size_t len, const char *dir)
{
    DIR *d;
    struct dirent *e;
    size_t o = 0;
    int first = 1;
    char base[160];
    int at_root, in_mods;
    if (!buf || !len) {
        return 0;
    }
    if (!dir) {
        dir = "";
    }
    at_root = dir[0] == 0;
    if (!at_root && !nnode_path_valid(dir)) {
        return snprintf(buf, len, "{\"error\":\"fs\"}");
    }
    /* Module slots keep bare names, so the loaded flag is meaningful
     * for the root and the programs dir only. Settings probe (defaults
     * section scan, read-only) runs for programs only; values files
     * are created on run and on Settings open, never here. */
    in_mods = at_root || strcmp(dir, NNOODE_PROGRAMS_DIR) == 0;
    if (at_root) {
        snprintf(base, sizeof(base), "/data");
    } else {
        snprintf(base, sizeof(base), "/data/%s", dir);
    }
    d = opendir(base);
    if (!d) {
        return snprintf(buf, len, "{\"error\":\"fs\"}");
    }
    o += snprintf(buf + o, len - o, "[");
    while ((e = readdir(d)) && o < len) {
        struct stat st;
        char path[256];
        int is_dir;
        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) {
            continue;
        }
        snprintf(path, sizeof(path), "%s/%.64s", base, e->d_name);
        memset(&st, 0, sizeof(st));
        if (stat(path, &st) != 0) {
            continue;
        }
        is_dir = S_ISDIR(st.st_mode) ? 1 : 0;
        if (is_dir) {
            o += snprintf(buf + o, len - o, "%s{\"name\":\"%s\",\"size\":0,"
                "\"loaded\":0,\"is_elf\":0,\"dir\":1}",
                first ? "" : ",", e->d_name);
        } else {
            int is_elf = has_elf_magic(path);
            int has_schema = 0;
            if (!at_root && strcmp(dir, NNOODE_PROGRAMS_DIR) == 0 && is_elf) {
                has_schema = nnode_schema_has(e->d_name);
            }
            o += snprintf(buf + o, len - o, "%s{\"name\":\"%s\",\"size\":%u,"
                "\"loaded\":%d,\"is_elf\":%d,\"has_schema\":%d,\"dir\":0}",
                first ? "" : ",", e->d_name, (unsigned)st.st_size,
                in_mods && nnode_elf_loaded(e->d_name) ? 1 : 0,
                is_elf ? 1 : 0, has_schema);
        }
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
    char path[160];
    struct stat st;
    if (!nnode_path_valid(name)) {
        return -1;
    }
    snprintf(path, sizeof(path), "/data/%s", name);
    memset(&st, 0, sizeof(st));
    if (stat(path, &st) != 0) {
        return -2;
    }
    if (S_ISDIR(st.st_mode)) {
        /* Only empty dirs go; the error maps to not_found. */
        if (rmdir(path) != 0) {
            return -2;
        }
    } else if (unlink(path) != 0) {
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
    int id; /* instance id, monotonic, never reused */
    char name[65];
    elf_file_t file;
    esp_elf_t elf;
    size_t text_size; /* PT_LOAD with PF_X: IRAM image */
    size_t data_size; /* other PT_LOADs: DRAM image */
    int (*main_fn)(int argc, char *argv[]);
    void (*term_fn)(void);
    int stop_flag; /* set by stop, polled via nnode_stop_requested */
    char cfg_pkg[24]; /* settings package, pinned at run */
} loaded_mod_t;
static loaded_mod_t s_mods[NNODE_MAX_LOADED];
static int s_next_id = 1;

/* Grace for tasks to exit on their own after SIGTERM before SIGKILL. */
#define NNODE_STOP_GRACE_MS 2000

/* Locate a slot by instance id, else the first name match. */
static int find_slot(int id, const char *name)
{
    int i;
    if (id >= 0) {
        for (i = 0; i < NNODE_MAX_LOADED; i++) {
            if (s_mods[i].used && s_mods[i].id == id) {
                return i;
            }
        }
        return -1;
    }
    if (!name) {
        return -1;
    }
    for (i = 0; i < NNODE_MAX_LOADED; i++) {
        if (s_mods[i].used && strcmp(s_mods[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

/* Release a slot: free arena, unmap the image. Callers kill tasks first. */
static void drop_slot(int i)
{
    ESP_LOGI(TAG, "dropped %s#%d", s_mods[i].name, s_mods[i].id);
    nnode_arena_free_slot(i);
    esp_elf_deinit(&s_mods[i].elf);
    esp_elf_close(&s_mods[i].file);
    memset(&s_mods[i], 0, sizeof(s_mods[i]));
}

/* Lifecycle contract: the entry shim (sdk/nnode_entry.c) calls this from
 * app_main while the slot's arena is active, so the owner resolves to the
 * slot being started. No reverse symbol lookup is needed. */
void nnode_register(void *main_fn, void *term_fn)
{
    int slot = nnode_arena_owner_current();
    if (slot < 0 || slot >= NNODE_MAX_LOADED) {
        return;
    }
    if (!s_mods[slot].used) {
        return;
    }
    s_mods[slot].main_fn = (int (*)(int, char **))main_fn;
    s_mods[slot].term_fn = (void (*)(void))term_fn;
}

int nnode_stop_requested(void)
{
    int slot = nnode_arena_owner_current();
    if (slot < 0 || slot >= NNODE_MAX_LOADED) {
        return 0;
    }
    if (!s_mods[slot].used) {
        return 0;
    }
    return s_mods[slot].stop_flag;
}

uintptr_t nnode_lifecycle_resolve(const char *name)
{
    if (!name) {
        return 0;
    }
    if (strcmp(name, "nnode_register") == 0) {
        return (uintptr_t)nnode_register;
    }
    if (strcmp(name, "nnode_stop_requested") == 0) {
        return (uintptr_t)nnode_stop_requested;
    }
    if (strcmp(name, "nnode_config_get") == 0) {
        return (uintptr_t)nnode_config_get;
    }
    if (strcmp(name, "nnode_config_get_int") == 0) {
        return (uintptr_t)nnode_config_get_int;
    }
    if (strcmp(name, "nnode_config_get_bool") == 0) {
        return (uintptr_t)nnode_config_get_bool;
    }
    if (strcmp(name, "nnode_config_get_port") == 0) {
        return (uintptr_t)nnode_config_get_port;
    }
    if (strcmp(name, "nnode_config_get_ip4") == 0) {
        return (uintptr_t)nnode_config_get_ip4;
    }
    if (strcmp(name, "nnode_config_get_mac") == 0) {
        return (uintptr_t)nnode_config_get_mac;
    }
    if (strcmp(name, "nnode_config_get_list") == 0) {
        return (uintptr_t)nnode_config_get_list;
    }
    return 0;
}

/* Per-module config: values live in /data/configs/<pkg>, created and
 * merged by nnode_schema_ensure before the instance starts. The slot package
 * is pinned at run time, so getters never scan ELF bytes. */

/* Package of the calling instance. -1 when the caller is not a module
 * task or the module has no settings. */
static int current_pkg(char *out, size_t len)
{
    int slot = nnode_arena_owner_current();
    if (slot < 0 || slot >= NNODE_MAX_LOADED || !s_mods[slot].used ||
        !s_mods[slot].cfg_pkg[0]) {
        return -1;
    }
    snprintf(out, len, "%s", s_mods[slot].cfg_pkg);
    return 0;
}

int nnode_config_get(const char *key, char *out, size_t len)
{
    char pkg[24];
    if (!key || !out || len < 2) {
        return -1;
    }
    if (current_pkg(pkg, sizeof(pkg)) != 0) {
        return -1;
    }
    return nnode_schema_lookup(pkg, key, out, len);
}

long nnode_config_get_int(const char *key, long dflt)
{
    char b[32];
    long v;
    if (!key || nnode_config_get(key, b, sizeof(b)) != 0 ||
        nnode_ct_int(b, &v) != 0) {
        return dflt;
    }
    return v;
}

int nnode_config_get_bool(const char *key, int dflt)
{
    char b[8];
    if (!key || nnode_config_get(key, b, sizeof(b)) != 0) {
        return dflt;
    }
    if (strcmp(b, "1") == 0) {
        return 1;
    }
    if (strcmp(b, "0") == 0) {
        return 0;
    }
    return dflt;
}

long nnode_config_get_port(const char *key, long dflt)
{
    char b[32];
    long v;
    if (!key || nnode_config_get(key, b, sizeof(b)) != 0 ||
        nnode_ct_port(b, &v) != 0) {
        return dflt;
    }
    return v;
}

int nnode_config_get_ip4(const char *key, char *out, size_t len)
{
    char b[32];
    size_t n;
    if (!key || !out || len < 2) {
        return -1;
    }
    if (nnode_config_get(key, b, sizeof(b)) != 0 ||
        nnode_ct_ip4(b, NULL) != 0) {
        return -1;
    }
    n = strlen(b) + 1;
    if (n > len) {
        return -1;
    }
    memcpy(out, b, n);
    return 0;
}

int nnode_config_get_mac(const char *key, char *out, size_t len)
{
    char b[32];
    size_t n;
    if (!key || !out || len < 2) {
        return -1;
    }
    if (nnode_config_get(key, b, sizeof(b)) != 0 ||
        nnode_ct_mac(b, NULL) != 0) {
        return -1;
    }
    n = strlen(b) + 1;
    if (n > len) {
        return -1;
    }
    memcpy(out, b, n);
    return 0;
}

/* Flat output: item i goes to out + i * cap. Empty value is 0 items. */
int nnode_config_get_list(const char *key, char *out, size_t cap, int max)
{
    char b[128];
    const char *p;
    int n = 0;
    if (!key || !out || cap < 2 || max < 1) {
        return -1;
    }
    if (nnode_config_get(key, b, sizeof(b)) != 0) {
        return -1;
    }
    if (b[0] == 0) {
        return 0;
    }
    p = b;
    while (*p) {
        const char *c = strchr(p, ',');
        size_t len = c ? (size_t)(c - p) : strlen(p);
        if (len == 0 || len + 1 > cap || n >= max) {
            return -1;
        }
        memcpy(out + (size_t)n * cap, p, len);
        out[(size_t)n * cap + len] = 0;
        n++;
        p = c ? c + 1 : p + len;
    }
    return n;
}

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

int nnode_elf_run(const char *name, int *id_out, char *errbuf, size_t errlen)
{
    elf_file_t f;
    esp_elf_t elf;
    int r, i, slot;
    char path[80];
    if (!nnode_file_valid(name)) {
        snprintf(errbuf, errlen, "bad_name");
        return -1;
    }
    snprintf(path, sizeof(path), "/data/" NNOODE_PROGRAMS_DIR "/%s", name);
    if (!has_elf_magic(path)) {
        snprintf(errbuf, errlen, "not_elf");
        return -1;
    }
    /* Settings first: a freshly uploaded module may never have been
     * listed, so its values file may not exist yet. Ensure it here so
     * the instance reads defaults on its very first run. */
    char run_pkg[24] = { 0 };
    nnode_schema_ensure(name, run_pkg, sizeof(run_pkg));
    /* Same file may run several times: each instance gets its own
     * slot with a fresh image (code and data duplicated). */
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
    /* Reserve the slot before the entry runs: nnode_register writes
     * the main/term pointers here while the slot is active. */
    memset(&s_mods[slot], 0, sizeof(s_mods[slot]));
    s_mods[slot].used = 1;
    s_mods[slot].id = s_next_id++;
    strncpy(s_mods[slot].name, name, sizeof(s_mods[slot].name) - 1);
    snprintf(s_mods[slot].cfg_pkg, sizeof(s_mods[slot].cfg_pkg), "%s", run_pkg);
    memset(&f, 0, sizeof(f));
    {
        /* The loader builds FS_PATH/name, so the subdir goes here. */
        char rel[80];
        snprintf(rel, sizeof(rel), NNOODE_PROGRAMS_DIR "/%s", name);
        if (esp_elf_open(&f, rel) != 0) {
            snprintf(errbuf, errlen, "elf_open");
            memset(&s_mods[slot], 0, sizeof(s_mods[slot]));
            nnode_arena_free_slot(slot);
            return -1;
        }
    }
    memset(&elf, 0, sizeof(elf));
    if (esp_elf_init(&elf) != 0) {
        snprintf(errbuf, errlen, "elf_init");
        esp_elf_close(&f);
        memset(&s_mods[slot], 0, sizeof(s_mods[slot]));
        nnode_arena_free_slot(slot);
        return -1;
    }
    r = esp_elf_relocate(&elf, f.payload);
    if (r != 0) {
        ESP_LOGE(TAG, "relocate %s failed: %d", name, r);
        snprintf(errbuf, errlen, "elf_relocate");
        esp_elf_deinit(&elf);
        esp_elf_close(&f);
        memset(&s_mods[slot], 0, sizeof(s_mods[slot]));
        nnode_arena_free_slot(slot);
        return -1;
    }
    ESP_LOGI(TAG, "running %s", name);
    /* Image handles go in before the entry runs: module code (nnode_main
     * and friends) may already query its schema-backed config, which
     * reads the resident file payload of this slot. */
    s_mods[slot].file = f;
    s_mods[slot].elf = elf;
    prev_active = nnode_arena_set_active(slot);
    esp_elf_request(&elf, 0, 0, NULL);
    nnode_arena_set_active(prev_active);
    if (!s_mods[slot].main_fn || !s_mods[slot].term_fn) {
        ESP_LOGE(TAG, "%s missing nnode_main/nnode_term (link sdk/nnode_entry.c)", name);
        snprintf(errbuf, errlen, "bad_abi");
        esp_elf_deinit(&elf);
        esp_elf_close(&f);
        memset(&s_mods[slot], 0, sizeof(s_mods[slot]));
        nnode_arena_free_slot(slot);
        return -1;
    }
    ESP_LOGI(TAG, "%s stays loaded (unload explicitly)", name);
    measure_images(f.payload, &s_mods[slot].text_size, &s_mods[slot].data_size);
    ESP_LOGI(TAG, "%s#%d: text %u data %u file %u", name, s_mods[slot].id,
        (unsigned)s_mods[slot].text_size, (unsigned)s_mods[slot].data_size,
        (unsigned)f.size);
    if (id_out) {
        *id_out = s_mods[slot].id;
    }
    return 0;
}


int nnode_elf_stop(int id, const char *name)
{
    int i, waited = 0;
    if (id < 0 && !nnode_file_valid(name)) {
        return -1;
    }
    i = find_slot(id, name);
    if (i < 0) {
        return -2;
    }
    /* SIGTERM: let the module save state and signal its tasks, then
     * give them a grace period to exit on their own. Runs in the caller
     * (httpd worker) with the slot active so mallocs attribute
     * correctly. Must stay short; kill below is the SIGKILL path that
     * never calls back into the module. */
    s_mods[i].stop_flag = 1;
    if (s_mods[i].term_fn) {
        int prev = nnode_arena_set_active(i);
        s_mods[i].term_fn();
        nnode_arena_set_active(prev);
    }
    while (nnode_arena_slot_has_live_tasks(i) && waited < NNODE_STOP_GRACE_MS) {
        vTaskDelay(pdMS_TO_TICKS(50));
        waited += 50;
    }
    if (nnode_arena_slot_has_live_tasks(i)) {
        ESP_LOGW(TAG, "stop %s#%d: %d tasks ignored SIGTERM, killing",
            s_mods[i].name, s_mods[i].id, nnode_arena_slot_tasks(i));
    }
    nnode_arena_kill_tasks(i);
    ESP_LOGI(TAG, "stopped %s#%d", s_mods[i].name, s_mods[i].id);
    drop_slot(i);
    return 0;
}

int nnode_elf_kill(int id, const char *name)
{
    int i;
    if (id < 0 && !nnode_file_valid(name)) {
        return -1;
    }
    i = find_slot(id, name);
    if (i < 0) {
        return -2;
    }
    /* SIGKILL: no hook, no grace. For state-saving stops use nnode_elf_stop. */
    nnode_arena_kill_tasks(i);
    ESP_LOGI(TAG, "killed %s#%d", s_mods[i].name, s_mods[i].id);
    drop_slot(i);
    return 0;
}

int nnode_elf_instances_json(char *buf, size_t len)
{
    size_t o = 0;
    int i, first = 1;
    if (!buf || !len) {
        return 0;
    }
    o += snprintf(buf + o, len - o, "{\"instances\":[");
    for (i = 0; i < NNODE_MAX_LOADED && o < len; i++) {
        int tasks;
        if (!s_mods[i].used) {
            continue;
        }
        tasks = nnode_arena_slot_tasks(i);
        o += snprintf(buf + o, len - o,
            "%s{\"id\":%d,\"name\":\"%s\",\"state\":\"%s\",\"tasks\":%d}",
            first ? "" : ",", s_mods[i].id, s_mods[i].name,
            tasks > 0 ? "running" : "exited", tasks);
        first = 0;
    }
    if (o < len) {
        o += snprintf(buf + o, len - o, "]}");
    }
    return (int)o;
}

int nnode_elf_unload(const char *name)
{
    int i, found = 0;
    if (!nnode_file_valid(name)) {
        return -1;
    }
    for (i = 0; i < NNODE_MAX_LOADED; i++) {
        if (s_mods[i].used && strcmp(s_mods[i].name, name) == 0) {
            found = 1;
            if (!nnode_arena_slot_has_live_tasks(i)) {
                ESP_LOGI(TAG, "unloaded %s#%d", name, s_mods[i].id);
                drop_slot(i);
                return 0;
            }
        }
    }
    if (found) {
        ESP_LOGW(TAG, "unload %s refused: kill tasks first", name);
        return -3;
    }
    return -2;
}

/* Autostart list: ordered file names in fixed entry1..8 slots.
 * Order is boot order (first entry starts first). */
#define NNODE_AUTOSTART_MAX NNODE_MAX_LOADED

/* Fixed 65-byte slot copy (memmove: dst and src may overlap). */
static void autostart_copy(char dst[65], const char src[65])
{
    memmove(dst, src, 65);
}

int nnode_autostart_load(char names[][65], int max)
{
    char key[12], b[128];
    int i, w = 0;
    if (!names || max <= 0) {
        return 0;
    }
    memset(names, 0, (size_t)max * 65);
    if (max > NNODE_AUTOSTART_MAX) {
        max = NNODE_AUTOSTART_MAX;
    }
    for (i = 0; i < NNODE_AUTOSTART_MAX; i++) {
        int j, dup = 0;
        snprintf(key, sizeof(key), "entry%d", i + 1);
        if (nnode_schema_lookup(NNOODE_CFG_PKG_AUTOSTART, key, b, sizeof(b)) != 0 ||
            !b[0] || !nnode_file_valid(b)) {
            continue;
        }
        for (j = 0; j < w; j++) {
            if (strcmp(names[j], b) == 0) {
                dup = 1;
                break;
            }
        }
        if (dup || w >= max) {
            continue;
        }
        /* file_valid above caps the name at 64 chars: plain copy. */
        memcpy(names[w], b, strlen(b) + 1);
        w++;
    }
    return w;
}

/* Validate an autostart package text (restore path). */
int nnode_autostart_validate(const char *text)
{
    const char *def = nnode_fw_defaults(NNOODE_CFG_PKG_AUTOSTART);
    /* Heap: restore runs next to 8 KB staged texts. */
    char (*keys)[NNODE_SCHEMA_ID_MAX + 1] = malloc(sizeof(*keys) * 8);
    char (*vals)[128] = malloc(sizeof(*vals) * 8);
    int n = 0, i, rc = -1;
    char key[12];
    if (!keys || !vals) {
        goto out;
    }
    if (!text || nnode_schema_validate(def, text) != 0 ||
        nnode_schema_kv(NNOODE_CFG_PKG_AUTOSTART, text,
            keys, vals, &n) != 0) {
        goto out;
    }
    for (i = 1; i <= NNODE_AUTOSTART_MAX; i++) {
        int j;
        const char *v = NULL;
        snprintf(key, sizeof(key), "entry%d", i);
        for (j = 0; j < n; j++) {
            if (strcmp(keys[j], key) == 0) {
                v = vals[j];
                break;
            }
        }
        if (!v || (v[0] && !nnode_file_valid(v))) {
            goto out;
        }
    }
    rc = 0;
out:
    free(keys);
    free(vals);
    return rc;
}

static int autostart_store(char names[][65], int n)
{
    char keys[NNODE_AUTOSTART_MAX][NNODE_SCHEMA_ID_MAX + 1];
    char vals[NNODE_AUTOSTART_MAX][128];
    char key[12];
    int i;
    if (n < 0 || n > NNODE_AUTOSTART_MAX) {
        return -1;
    }
    for (i = 0; i < NNODE_AUTOSTART_MAX; i++) {
        snprintf(key, sizeof(key), "entry%d", i + 1);
        memcpy(keys[i], key, strlen(key) + 1);
        if (i < n) {
            size_t len;
            if (!nnode_file_valid(names[i])) {
                return -1;
            }
            len = strlen(names[i]);
            if (len >= sizeof(vals[i])) {
                return -1;
            }
            memcpy(vals[i], names[i], len + 1);
        } else {
            vals[i][0] = 0;
        }
    }
    return nnode_schema_store(
        nnode_fw_defaults(NNOODE_CFG_PKG_AUTOSTART), keys, vals,
        NNODE_AUTOSTART_MAX);
}

int nnode_autostart_json(char *buf, size_t len)
{
    char names[NNODE_AUTOSTART_MAX][65];
    int n, i;
    size_t o = 0;
    if (!buf || !len) {
        return 0;
    }
    n = nnode_autostart_load(names, NNODE_AUTOSTART_MAX);
    o += snprintf(buf + o, len - o, "{\"progs\":[");
    for (i = 0; i < n && o < len; i++) {
        o += snprintf(buf + o, len - o, "%s\"%s\"", i ? "," : "", names[i]);
    }
    if (o < len) {
        o += snprintf(buf + o, len - o, "]}");
    }
    return (int)o;
}

int nnode_autostart_add(const char *name)
{
    char names[NNODE_AUTOSTART_MAX][65];
    char path[80];
    int n, i;
    if (!nnode_file_valid(name)) {
        return -1;
    }
    snprintf(path, sizeof(path), "/data/" NNOODE_PROGRAMS_DIR "/%s", name);
    if (!has_elf_magic(path)) {
        return -2;
    }
    n = nnode_autostart_load(names, NNODE_AUTOSTART_MAX);
    for (i = 0; i < n; i++) {
        if (strcmp(names[i], name) == 0) {
            return -3;
        }
    }
    if (n >= NNODE_AUTOSTART_MAX) {
        return -4;
    }
    snprintf(names[n], sizeof(names[n]), "%s", name);
    return autostart_store(names, n + 1);
}

int nnode_autostart_remove(const char *name)
{
    char names[NNODE_AUTOSTART_MAX][65];
    int n, i, w = 0;
    if (!nnode_file_valid(name)) {
        return -1;
    }
    n = nnode_autostart_load(names, NNODE_AUTOSTART_MAX);
    for (i = 0; i < n; i++) {
        if (strcmp(names[i], name) == 0) {
            continue;
        }
        if (w != i) {
            autostart_copy(names[w], names[i]);
        }
        w++;
    }
    if (w == n) {
        return -2;
    }
    return autostart_store(names, w);
}

int nnode_autostart_move(const char *name, int dir)
{
    char names[NNODE_AUTOSTART_MAX][65];
    char tmp[65];
    int n, i, at = -1, to;
    if (!nnode_file_valid(name) || (dir != -1 && dir != 1)) {
        return -1;
    }
    n = nnode_autostart_load(names, NNODE_AUTOSTART_MAX);
    for (i = 0; i < n; i++) {
        if (strcmp(names[i], name) == 0) {
            at = i;
            break;
        }
    }
    if (at < 0) {
        return -2;
    }
    to = at + dir;
    if (to < 0 || to >= n) {
        return 0;
    }
    snprintf(tmp, sizeof(tmp), "%s", names[at]);
    autostart_copy(names[at], names[to]);
    autostart_copy(names[to], tmp);
    return autostart_store(names, n);
}

/* Boot: start the autostart list in stored order. Failures (deleted
 * file, no free slot) are logged and skipped, never fatal. Desired
 * state, not supervision: a crashed module is not restarted. */
void nnode_autostart_run(void)
{
    char names[NNODE_AUTOSTART_MAX][65];
    int n, i;
    n = nnode_autostart_load(names, NNODE_AUTOSTART_MAX);
    for (i = 0; i < n; i++) {
        char err[96];
        int id = 0;
        if (nnode_elf_run(names[i], &id, err, sizeof(err)) != 0) {
            ESP_LOGW(TAG, "autostart %s skipped: %s", names[i], err);
        } else {
            ESP_LOGI(TAG, "autostart %s#%d", names[i], id);
        }
    }
}
