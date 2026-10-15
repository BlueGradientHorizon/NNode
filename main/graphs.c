#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "lwip/stats.h"
#include "graphs.h"

static const char *TAG = "nnode_graphs";

/* Ring history, oldest drained first by the JSON dumper. cpu is
 * hundredths of percent, rx/tx are packets per second. */
static uint16_t g_cpu[GRAPH_N];
static uint32_t g_heap[GRAPH_N];
static uint32_t g_rx[GRAPH_N];
static uint32_t g_tx[GRAPH_N];
static int g_pos, g_fill;
static portMUX_TYPE s_mu = portMUX_INITIALIZER_UNLOCKED;

static void push(uint16_t cpu, uint32_t heap, uint32_t rx, uint32_t tx)
{
    taskENTER_CRITICAL(&s_mu);
    g_cpu[g_pos] = cpu;
    g_heap[g_pos] = heap;
    g_rx[g_pos] = rx;
    g_tx[g_pos] = tx;
    g_pos = (g_pos + 1) % GRAPH_N;
    if (g_fill < GRAPH_N) {
        g_fill++;
    }
    taskEXIT_CRITICAL(&s_mu);
}

static int is_idle_task(const char *name)
{
    return name && strncmp(name, "IDLE", 4) == 0;
}

static void sampler(void *arg)
{
    uint32_t prev_total = 0, prev_idle = 0;
    uint32_t prev_rx = 0, prev_tx = 0;
    TickType_t prev_t = 0;
    int first = 1;
    (void)arg;
    for (;;) {
        UBaseType_t n, i;
        TaskStatus_t *a;
        uint32_t total = 0, idle = 0;
        uint32_t rx, tx, heap;
        TickType_t now;
        uint32_t dt_ms;
        vTaskDelay(pdMS_TO_TICKS(GRAPH_STEP_MS));
        n = uxTaskGetNumberOfTasks();
        a = malloc(n * sizeof(*a));
        if (!a) {
            continue;
        }
        n = uxTaskGetSystemState(a, n, NULL);
        for (i = 0; i < n; i++) {
            total += a[i].ulRunTimeCounter;
            if (is_idle_task(a[i].pcTaskName)) {
                idle += a[i].ulRunTimeCounter;
            }
        }
        free(a);
        heap = esp_get_free_heap_size();
#if LWIP_STATS
        /* NB: link counters stay zero on the WiFi path (frames bypass
         * ethernet_input); IP counters do increment. */
        rx = lwip_stats.ip.recv;
        tx = lwip_stats.ip.xmit;
#else
        rx = 0;
        tx = 0;
#endif
        now = xTaskGetTickCount();
        dt_ms = (uint32_t)((now - prev_t) * 1000 / configTICK_RATE_HZ);
        if (first || dt_ms == 0) {
            first = 0;
        } else {
            uint16_t cpu = 0;
            uint32_t rxr = 0, txr = 0;
            if (total > prev_total) {
                uint32_t busy = (total - prev_total) - (idle - prev_idle);
                cpu = (uint16_t)(10000u * busy / (total - prev_total));
            }
            rxr = (rx - prev_rx) * 1000u / dt_ms;
            txr = (tx - prev_tx) * 1000u / dt_ms;
            push(cpu, heap, rxr, txr);
        }
        prev_total = total;
        prev_idle = idle;
        prev_rx = rx;
        prev_tx = tx;
        prev_t = now;
    }
}

void nnode_graphs_start(void)
{
    xTaskCreate(sampler, "nnode_mon", 2560, NULL, 2, NULL);
    ESP_LOGI(TAG, "sampler started (%d pts x %d ms)", GRAPH_N, GRAPH_STEP_MS);
}

int nnode_graphs_json(char *buf, size_t len)
{
    uint16_t cpu[GRAPH_N];
    uint32_t heap[GRAPH_N], rx[GRAPH_N], tx[GRAPH_N];
    int fill, i;
    size_t o = 0;
    taskENTER_CRITICAL(&s_mu);
    fill = g_fill;
    for (i = 0; i < fill; i++) {
        int k = (g_pos - fill + i + GRAPH_N * 2) % GRAPH_N;
        cpu[i] = g_cpu[k];
        heap[i] = g_heap[k];
        rx[i] = g_rx[k];
        tx[i] = g_tx[k];
    }
    taskEXIT_CRITICAL(&s_mu);
    if (!len) {
        return -1;
    }
    o += snprintf(buf + o, len - o,
        "{\"n\":%d,\"step\":%d,\"cpu\":[", fill, GRAPH_STEP_MS / 1000);
    for (i = 0; i < fill && o < len; i++) {
        o += snprintf(buf + o, len - o, "%s%u", i ? "," : "", (unsigned)cpu[i]);
    }
    o += snprintf(buf + o, len - o, "],\"heap\":[");
    for (i = 0; i < fill && o < len; i++) {
        o += snprintf(buf + o, len - o, "%s%u", i ? "," : "", (unsigned)heap[i]);
    }
    o += snprintf(buf + o, len - o, "],\"rx\":[");
    for (i = 0; i < fill && o < len; i++) {
        o += snprintf(buf + o, len - o, "%s%u", i ? "," : "", (unsigned)rx[i]);
    }
    o += snprintf(buf + o, len - o, "],\"tx\":[");
    for (i = 0; i < fill && o < len; i++) {
        o += snprintf(buf + o, len - o, "%s%u", i ? "," : "", (unsigned)tx[i]);
    }
    if (o < len) {
        o += snprintf(buf + o, len - o, "]}");
    }
    return (int)o;
}
