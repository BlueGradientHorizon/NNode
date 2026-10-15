#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "private/elf_symbol.h"
#include "arena.h"

static const char *TAG = "nnode_arena";

void *nnode_malloc(size_t n);
void *nnode_calloc(size_t nmemb, size_t size);
void *nnode_realloc(void *ptr, size_t n);
void nnode_free(void *ptr);

typedef struct hnode {
    void *ptr;
    size_t size;
    struct hnode *next;
} hnode_t;

typedef struct {
    int used;
    hnode_t *heap;
    size_t heap_bytes;
} arena_slot_t;

#define NNODE_ARENA_MAX_TASKS 32
typedef struct {
    TaskHandle_t h;
    int slot;
    size_t stack_bytes; /* usStackDepth at create time, 0 if unknown */
} task_bind_t;

static arena_slot_t s_slots[NNODE_ARENA_SLOTS];
static task_bind_t s_tasks[NNODE_ARENA_MAX_TASKS];
static volatile int s_active = -1;
static portMUX_TYPE s_mu = portMUX_INITIALIZER_UNLOCKED;

static int slot_live(int s)
{
    return s >= 0 && s < NNODE_ARENA_SLOTS && s_slots[s].used;
}

int nnode_arena_owner_current(void)
{
    TaskHandle_t self = xTaskGetCurrentTaskHandle();
    int i;
    for (i = 0; i < NNODE_ARENA_MAX_TASKS; i++) {
        if (s_tasks[i].h == self && slot_live(s_tasks[i].slot)) {
            return s_tasks[i].slot;
        }
    }
    if (s_active >= 0 && slot_live(s_active)) {
        return s_active;
    }
    return -1;
}

void nnode_arena_bind_task(int slot, TaskHandle_t h, size_t stack_bytes)
{
    int i;
    if (!slot_live(slot) || !h) {
        return;
    }
    taskENTER_CRITICAL(&s_mu);
    for (i = 0; i < NNODE_ARENA_MAX_TASKS; i++) {
        if (s_tasks[i].h == h) {
            s_tasks[i].slot = slot;
            if (stack_bytes) {
                s_tasks[i].stack_bytes = stack_bytes;
            }
            taskEXIT_CRITICAL(&s_mu);
            return;
        }
    }
    for (i = 0; i < NNODE_ARENA_MAX_TASKS; i++) {
        if (!s_tasks[i].h) {
            s_tasks[i].h = h;
            s_tasks[i].slot = slot;
            s_tasks[i].stack_bytes = stack_bytes;
            taskEXIT_CRITICAL(&s_mu);
            return;
        }
    }
    taskEXIT_CRITICAL(&s_mu);
    ESP_LOGW(TAG, "task map full, slot %d loses a task", slot);
}

void nnode_arena_note_task(int slot, TaskHandle_t h)
{
    nnode_arena_bind_task(slot, h, 0);
}

size_t nnode_arena_slot_stacks(int slot)
{
    int i;
    size_t n = 0;
    if (slot < 0 || slot >= NNODE_ARENA_SLOTS) {
        return 0;
    }
    for (i = 0; i < NNODE_ARENA_MAX_TASKS; i++) {
        if (s_tasks[i].h && s_tasks[i].slot == slot) {
            n += s_tasks[i].stack_bytes;
        }
    }
    return n;
}

void nnode_arena_forget_task(TaskHandle_t h)
{
    int i;
    if (!h) {
        return;
    }
    taskENTER_CRITICAL(&s_mu);
    for (i = 0; i < NNODE_ARENA_MAX_TASKS; i++) {
        if (s_tasks[i].h == h) {
            s_tasks[i].h = NULL;
            s_tasks[i].slot = -1;
        }
    }
    taskEXIT_CRITICAL(&s_mu);
}

int nnode_arena_slot_tasks(int slot)
{
    int i, n = 0;
    if (slot < 0 || slot >= NNODE_ARENA_SLOTS) {
        return 0;
    }
    for (i = 0; i < NNODE_ARENA_MAX_TASKS; i++) {
        if (s_tasks[i].h && s_tasks[i].slot == slot) {
            n++;
        }
    }
    return n;
}

int nnode_arena_slot_has_live_tasks(int slot)
{
    return nnode_arena_slot_tasks(slot) > 0;
}

size_t nnode_arena_heap_bytes(int slot)
{
    if (slot < 0 || slot >= NNODE_ARENA_SLOTS) {
        return 0;
    }
    return s_slots[slot].heap_bytes;
}

int nnode_arena_alloc(int slot)
{
    int i;
    if (slot < 0 || slot >= NNODE_ARENA_SLOTS) {
        return -1;
    }
    taskENTER_CRITICAL(&s_mu);
    s_slots[slot].used = 1;
    s_slots[slot].heap = NULL;
    s_slots[slot].heap_bytes = 0;
    for (i = 0; i < NNODE_ARENA_MAX_TASKS; i++) {
        if (s_tasks[i].slot == slot) {
            s_tasks[i].h = NULL;
            s_tasks[i].slot = -1;
        }
    }
    taskEXIT_CRITICAL(&s_mu);
    return 0;
}

int nnode_arena_free_slot(int slot)
{
    hnode_t *n, *next;
    int i, count = 0;
    size_t bytes = 0;
    if (slot < 0 || slot >= NNODE_ARENA_SLOTS) {
        return -1;
    }
    taskENTER_CRITICAL(&s_mu);
    n = s_slots[slot].heap;
    s_slots[slot].heap = NULL;
    bytes = s_slots[slot].heap_bytes;
    s_slots[slot].heap_bytes = 0;
    s_slots[slot].used = 0;
    for (i = 0; i < NNODE_ARENA_MAX_TASKS; i++) {
        if (s_tasks[i].slot == slot) {
            s_tasks[i].h = NULL;
            s_tasks[i].slot = -1;
        }
    }
    taskEXIT_CRITICAL(&s_mu);
    while (n) {
        next = n->next;
        heap_caps_free(n->ptr);
        heap_caps_free(n);
        count++;
        n = next;
    }
    if (count) {
        ESP_LOGI(TAG, "slot %d: reclaimed %d blocks (%u bytes)", slot, count, (unsigned)bytes);
    }
    return 0;
}

int nnode_arena_set_active(int slot)
{
    int prev = s_active;
    s_active = slot;
    return prev;
}

static hnode_t *find_node(void *ptr, int *slot_out)
{
    int s;
    for (s = 0; s < NNODE_ARENA_SLOTS; s++) {
        hnode_t *n;
        if (!s_slots[s].used) {
            continue;
        }
        for (n = s_slots[s].heap; n; n = n->next) {
            if (n->ptr == ptr) {
                if (slot_out) {
                    *slot_out = s;
                }
                return n;
            }
        }
    }
    return NULL;
}

/* Wrappers below are exported to ELF modules under the plain libc/FreeRTOS
 * names by nnode_arena_resolve (first link of the resolver chain in
 * modsym.c). Firmware code itself keeps calling the real
 * functions (wrappers have the nnode_ prefix), so no recursion. */

void *nnode_malloc(size_t n)
{
    int owner = nnode_arena_owner_current();
    void *p;
    hnode_t *node;
    if (n == 0) {
        n = 1;
    }
    if (owner < 0) {
        return heap_caps_malloc(n, MALLOC_CAP_DEFAULT);
    }
    taskENTER_CRITICAL(&s_mu);
    if (s_slots[owner].heap_bytes + n > NNODE_ARENA_HEAP_QUOTA) {
        taskEXIT_CRITICAL(&s_mu);
        return NULL;
    }
    taskEXIT_CRITICAL(&s_mu);
    p = heap_caps_malloc(n, MALLOC_CAP_DEFAULT);
    if (!p) {
        return NULL;
    }
    node = heap_caps_malloc(sizeof(*node), MALLOC_CAP_DEFAULT);
    if (!node) {
        heap_caps_free(p);
        return NULL;
    }
    node->ptr = p;
    node->size = n;
    taskENTER_CRITICAL(&s_mu);
    node->next = s_slots[owner].heap;
    s_slots[owner].heap = node;
    s_slots[owner].heap_bytes += n;
    taskEXIT_CRITICAL(&s_mu);
    return p;
}

void *nnode_calloc(size_t nmemb, size_t size)
{
    size_t n;
    void *p;
    if (nmemb && size > SIZE_MAX / nmemb) {
        return NULL;
    }
    n = nmemb * size;
    p = nnode_malloc(n ? n : 1);
    if (p) {
        memset(p, 0, n);
    }
    return p;
}

void *nnode_realloc(void *ptr, size_t n)
{
    int s = -1;
    hnode_t *node;
    void *p2;
    if (!ptr) {
        return nnode_malloc(n);
    }
    if (n == 0) {
        nnode_free(ptr);
        return NULL;
    }
    taskENTER_CRITICAL(&s_mu);
    node = find_node(ptr, &s);
    if (!node) {
        taskEXIT_CRITICAL(&s_mu);
        return heap_caps_realloc(ptr, n, MALLOC_CAP_DEFAULT);
    }
    if (s_slots[s].heap_bytes - node->size + n > NNODE_ARENA_HEAP_QUOTA) {
        taskEXIT_CRITICAL(&s_mu);
        return NULL;
    }
    taskEXIT_CRITICAL(&s_mu);
    p2 = heap_caps_realloc(ptr, n, MALLOC_CAP_DEFAULT);
    if (!p2) {
        return NULL;
    }
    taskENTER_CRITICAL(&s_mu);
    s_slots[s].heap_bytes += n - node->size;
    node->ptr = p2;
    node->size = n;
    taskEXIT_CRITICAL(&s_mu);
    return p2;
}

void nnode_free(void *ptr)
{
    int s = -1;
    hnode_t *prev = NULL, *n;
    if (!ptr) {
        return;
    }
    taskENTER_CRITICAL(&s_mu);
    for (s = 0; s < NNODE_ARENA_SLOTS; s++) {
        if (!s_slots[s].used) {
            continue;
        }
        for (n = s_slots[s].heap; n; prev = n, n = n->next) {
            if (n->ptr == ptr) {
                if (prev) {
                    prev->next = n->next;
                } else {
                    s_slots[s].heap = n->next;
                }
                s_slots[s].heap_bytes -= n->size;
                taskEXIT_CRITICAL(&s_mu);
                heap_caps_free(ptr);
                heap_caps_free(n);
                return;
            }
        }
        prev = NULL;
    }
    taskEXIT_CRITICAL(&s_mu);
    heap_caps_free(ptr);
}

/* Task trampolines: register the new task to the owner slot on entry, so
 * later mallocs from that thread resolve even after s_active is reset. */

typedef struct {
    int slot;
    TaskFunction_t fn;
    void *arg;
    size_t stack_bytes;
} task_wrap_t;

static void nnode_task_tramp(void *a)
{
    task_wrap_t *w = a;
    TaskFunction_t f = w->fn;
    void *arg = w->arg;
    int slot = w->slot;
    size_t sb = w->stack_bytes;
    heap_caps_free(w);
    nnode_arena_bind_task(slot, xTaskGetCurrentTaskHandle(), sb);
    f(arg);
    nnode_arena_forget_task(xTaskGetCurrentTaskHandle());
    vTaskDelete(NULL);
}

typedef struct {
    int slot;
    void *(*fn)(void *);
    void *arg;
    size_t stack_bytes;
} pthread_wrap_t;

static void *nnode_pthread_tramp(void *a)
{
    pthread_wrap_t *w = a;
    void *(*f)(void *) = w->fn;
    void *arg = w->arg;
    int slot = w->slot;
    size_t sb = w->stack_bytes;
    heap_caps_free(w);
    nnode_arena_bind_task(slot, xTaskGetCurrentTaskHandle(), sb);
    {
        void *ret = f(arg);
        nnode_arena_forget_task(xTaskGetCurrentTaskHandle());
        return ret;
    }
}

BaseType_t nnode_xTaskCreatePinnedToCore(TaskFunction_t fn, const char *name,
    uint32_t stack, void *arg, UBaseType_t prio, TaskHandle_t *out, BaseType_t core)
{
    int owner = nnode_arena_owner_current();
    task_wrap_t *w;
    TaskHandle_t tmp = NULL;
    BaseType_t r;
    size_t sb;
    if (owner < 0 || !fn) {
        return xTaskCreatePinnedToCore(fn, name, stack, arg, prio, out, core);
    }
    sb = (size_t)stack * sizeof(StackType_t);
    w = heap_caps_malloc(sizeof(*w), MALLOC_CAP_DEFAULT);
    if (!w) {
        return xTaskCreatePinnedToCore(fn, name, stack, arg, prio, out, core);
    }
    w->slot = owner;
    w->fn = fn;
    w->arg = arg;
    w->stack_bytes = sb;
    r = xTaskCreatePinnedToCore(nnode_task_tramp, name, stack, w, prio, out ? out : &tmp, core);
    if (r != pdPASS) {
        heap_caps_free(w);
        return r;
    }
    /* NB: trampoline may already have run and freed w (preemption),
     * so use the saved sb, not w->stack_bytes. */
    nnode_arena_bind_task(owner, out ? *out : tmp, sb);
    return r;
}

TaskHandle_t nnode_xTaskCreateStaticPinnedToCore(TaskFunction_t fn, const char *name,
    uint32_t stack, void *arg, UBaseType_t prio, StackType_t *stackbuf,
    StaticTask_t *tcb, BaseType_t core)
{
    int owner = nnode_arena_owner_current();
    task_wrap_t *w;
    TaskHandle_t h;
    size_t sb;
    if (owner < 0 || !fn) {
        return xTaskCreateStaticPinnedToCore(fn, name, stack, arg, prio, stackbuf, tcb, core);
    }
    sb = (size_t)stack * sizeof(StackType_t);
    w = heap_caps_malloc(sizeof(*w), MALLOC_CAP_DEFAULT);
    if (!w) {
        return xTaskCreateStaticPinnedToCore(fn, name, stack, arg, prio, stackbuf, tcb, core);
    }
    w->slot = owner;
    w->fn = fn;
    w->arg = arg;
    w->stack_bytes = sb;
    h = xTaskCreateStaticPinnedToCore(nnode_task_tramp, name, stack, w, prio, stackbuf, tcb, core);
    if (h) {
        nnode_arena_bind_task(owner, h, sb);
    } else {
        heap_caps_free(w);
    }
    return h;
}

int nnode_pthread_create(pthread_t *t, const pthread_attr_t *attr,
    void *(*fn)(void *), void *arg)
{
    int owner = nnode_arena_owner_current();
    pthread_wrap_t *w;
    size_t sb = 0;
    if (owner < 0 || !fn) {
        return pthread_create(t, attr, fn, arg);
    }
    if (attr) {
        pthread_attr_getstacksize(attr, &sb);
    }
    w = heap_caps_malloc(sizeof(*w), MALLOC_CAP_DEFAULT);
    if (!w) {
        return pthread_create(t, attr, fn, arg);
    }
    w->slot = owner;
    w->fn = fn;
    w->arg = arg;
    w->stack_bytes = sb;
    return pthread_create(t, attr, nnode_pthread_tramp, w);
}

void nnode_vTaskDelete(TaskHandle_t h)
{
    TaskHandle_t t = h ? h : xTaskGetCurrentTaskHandle();
    vTaskDelete(h);
    nnode_arena_forget_task(t);
}

/* Kill every task attributed to the slot (by handle, no name whitelist:
 * module tasks may be named arbitrarily). Called from the httpd task,
 * never from inside the victim tasks. Stacks are reclaimed by the idle
 * task; the slot's heap is freed separately by nnode_arena_free_slot. */
int nnode_arena_kill_tasks(int slot)
{
    TaskHandle_t victims[NNODE_ARENA_MAX_TASKS];
    int n = 0, i;
    if (slot < 0 || slot >= NNODE_ARENA_SLOTS) {
        return 0;
    }
    taskENTER_CRITICAL(&s_mu);
    for (i = 0; i < NNODE_ARENA_MAX_TASKS && n < NNODE_ARENA_MAX_TASKS; i++) {
        if (s_tasks[i].h && s_tasks[i].slot == slot) {
            victims[n++] = s_tasks[i].h;
        }
    }
    taskEXIT_CRITICAL(&s_mu);
    for (i = 0; i < n; i++) {
        vTaskDelete(victims[i]);
        nnode_arena_forget_task(victims[i]);
    }
    if (n) {
        ESP_LOGI(TAG, "slot %d: killed %d tasks", slot, n);
    }
    return n;
}

/* Resolve the 8 wrapped names to the nnode_ wrappers above. Called FIRST
 * by the single symbol resolver (see modsym.c): the wrappers must shadow
 * the same-named raw firmware symbols in the modsym table, otherwise module
 * tasks and mallocs escape per-slot tracking - stop/unload then leak, and
 * a surviving task keeps running freed IRAM until the next run reuses
 * the heap (Instruction access fault, reboot). Returns 0 for anything
 * else - the chain continues in modsym.c. */
uintptr_t nnode_arena_resolve(const char *name)
{
    if (!name) {
        return 0;
    }
    if (!strcmp(name, "malloc")) {
        return (uintptr_t)nnode_malloc;
    }
    if (!strcmp(name, "calloc")) {
        return (uintptr_t)nnode_calloc;
    }
    if (!strcmp(name, "realloc")) {
        return (uintptr_t)nnode_realloc;
    }
    if (!strcmp(name, "free")) {
        return (uintptr_t)nnode_free;
    }
    if (!strcmp(name, "xTaskCreatePinnedToCore")) {
        return (uintptr_t)nnode_xTaskCreatePinnedToCore;
    }
    if (!strcmp(name, "xTaskCreateStaticPinnedToCore")) {
        return (uintptr_t)nnode_xTaskCreateStaticPinnedToCore;
    }
    if (!strcmp(name, "pthread_create")) {
        return (uintptr_t)nnode_pthread_create;
    }
    if (!strcmp(name, "vTaskDelete")) {
        return (uintptr_t)nnode_vTaskDelete;
    }
    return 0;
}

void nnode_arena_init(void)
{
    static int done = 0;
    if (!done) {
        done = 1;
        /* No elf_set_symbol_resolver here: modsym.c installs the single
         * chained resolver (arena -> modsym table -> loader defaults). */
        ESP_LOGI(TAG, "arena ready (wrappers resolve via the modsym chain)");
    }
}
