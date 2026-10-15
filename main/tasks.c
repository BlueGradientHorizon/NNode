#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "arena.h"
#include "tasks.h"

typedef struct { UBaseType_t num; uint32_t run; } prev_t;
static prev_t *s_prev;
static size_t s_prev_n;
static uint32_t s_prev_total;
static int s_have_prev;

static char state_char(eTaskState s)
{
    switch (s) {
    case eRunning:
    case eReady:
        return 'R';
    case eBlocked:
        return 'B';
    case eSuspended:
        return 'S';
    case eDeleted:
        return 'D';
    default:
        return '?';
    }
}

int nnode_tasks_json(char *buf, size_t len)
{
    UBaseType_t n = uxTaskGetNumberOfTasks();
    TaskStatus_t *a = malloc(n * sizeof(*a));
    uint32_t total = 0;
    size_t o;
    UBaseType_t i;
    size_t k;
    if (!a || !len) {
        free(a);
        return -1;
    }
    n = uxTaskGetSystemState(a, n, &total);
    o = 0;
    o += snprintf(buf + o, len - o, "[");
    for (i = 0; i < n && o < len; i++) {
        float pct = -1;
        uint32_t hwm = 0;
        if (s_have_prev && total > s_prev_total) {
            for (k = 0; k < s_prev_n; k++) {
                if (s_prev[k].num == a[i].xTaskNumber) {
                    uint32_t dr = a[i].ulRunTimeCounter - s_prev[k].run;
                    pct = 100.0f * (float)dr / (float)(total - s_prev_total);
                    break;
                }
            }
        }
        if (a[i].xHandle) {
            hwm = (uint32_t)uxTaskGetStackHighWaterMark(a[i].xHandle) * sizeof(StackType_t);
        }
        o += snprintf(buf + o, len - o,
            "%s{\"name\":\"%s\",\"state\":\"%c\",\"prio\":%u,\"stack_hwm\":%u,\"cpu\":%.1f,\"core\":0}",
            i ? "," : "", a[i].pcTaskName, state_char(a[i].eCurrentState),
            (unsigned)a[i].uxCurrentPriority, (unsigned)hwm,
            pct);
    }
    if (o < len) {
        o += snprintf(buf + o, len - o, "]");
    }
    free(s_prev);
    s_prev = malloc(n * sizeof(*s_prev));
    s_prev_n = s_prev ? n : 0;
    if (s_prev) {
        for (i = 0; i < n; i++) {
            s_prev[i].num = a[i].xTaskNumber;
            s_prev[i].run = a[i].ulRunTimeCounter;
        }
    }
    s_prev_total = total;
    s_have_prev = 1;
    free(a);
    return (int)o;
}

static int kill_allowed(const char *name)
{
    if (!name || strncmp(name, "nnode_", 6) != 0) {
        return 0;
    }
    if (strcmp(name, "main") == 0) {
        return 0;
    }
    return 1;
}

int nnode_task_kill(const char *name)
{
    TaskHandle_t h;
    if (!kill_allowed(name)) {
        return -2;
    }
    h = xTaskGetHandle(name);
    if (!h) {
        return -1;
    }
    if (xTaskGetCurrentTaskHandle() == h) {
        return -2;
    }
    vTaskDelete(h);
    nnode_arena_forget_task(h);
    return 0;
}
