#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "logbuf.h"

#define LOG_LINES 128
#define LOG_LEN 128

static char g_lines[LOG_LINES][LOG_LEN];
static uint8_t g_lvl[LOG_LINES];
static int g_head, g_count;
static portMUX_TYPE s_mu = portMUX_INITIALIZER_UNLOCKED;
static vprintf_like_t g_old;

static int lvl_of(char c)
{
    switch (c) {
    case 'V': return 0;
    case 'D': return 1;
    case 'I': return 2;
    case 'W': return 3;
    case 'E': return 4;
    default: return 2;
    }
}

static int hook(const char *fmt, va_list ap)
{
    char tmp[LOG_LEN];
    va_list cpy;
    va_copy(cpy, ap);
    vsnprintf(tmp, sizeof(tmp), fmt, cpy);
    va_end(cpy);
    taskENTER_CRITICAL(&s_mu);
    strncpy(g_lines[g_head], tmp, LOG_LEN - 1);
    g_lines[g_head][LOG_LEN - 1] = 0;
    g_lvl[g_head] = (uint8_t)lvl_of(tmp[0]);
    g_head = (g_head + 1) % LOG_LINES;
    if (g_count < LOG_LINES) {
        g_count++;
    }
    taskEXIT_CRITICAL(&s_mu);
    return g_old(fmt, ap);
}

void nnode_log_start(void)
{
    g_old = esp_log_set_vprintf(hook);
}

void nnode_log_clear(void)
{
    taskENTER_CRITICAL(&s_mu);
    g_head = 0;
    g_count = 0;
    taskEXIT_CRITICAL(&s_mu);
}

static int esc_copy(char *dst, size_t cap, const char *src)
{
    size_t o = 0;
    for (; *src && o + 1 < cap; src++) {
        if (*src == '"' || *src == '\\') {
            if (o + 2 >= cap) {
                break;
            }
            dst[o++] = '\\';
            dst[o++] = *src;
        } else if ((unsigned char)*src < 0x20) {
            if (o + 6 >= cap) {
                break;
            }
            o += snprintf(dst + o, cap - o, "\\u%04x", *src);
        } else {
            dst[o++] = *src;
        }
    }
    dst[o] = 0;
    return (int)o;
}

int nnode_log_json(char *buf, size_t len, int min_level, int max_n)
{
    char line[LOG_LEN];
    char esc[LOG_LEN * 2];
    size_t o = 0;
    int i, shown = 0, first = 1;
    if (!len) {
        return -1;
    }
    if (max_n <= 0) {
        max_n = LOG_LINES;
    }
    o += snprintf(buf + o, len - o, "{\"lines\":[");
    vTaskSuspendAll();
    for (i = 0; i < g_count && shown < max_n; i++) {
        int k = (g_head - g_count + i + LOG_LINES * 2) % LOG_LINES;
        if (g_lvl[k] < min_level) {
            continue;
        }
        memcpy(line, g_lines[k], LOG_LEN);
        esc_copy(esc, sizeof(esc), line);
        o += snprintf(buf + o, len - o, "%s\"%s\"", first ? "" : ",", esc);
        first = 0;
        shown++;
    }
    xTaskResumeAll();
    if (o < len) {
        o += snprintf(buf + o, len - o, "]}");
    }
    return (int)o;
}
