/* NNode ELF module example: blinker.
 * Board constants (default LED GPIO, LED polarity, valid GPIO ranges)
 * come from board_config.h, supplied via BOARD_DIR by ../build.py,
 * so the example follows whichever board it is built for.
 *
 * Build: ../build.py <target> build blink  ->
 *   build-<target>/modules/blink/blink.app.elf
 * Upload:  POST /api/files/upload?name=blink.elf  (raw .app.elf bytes)
 * Run:     POST /api/elf/run?name=blink.elf
 * Stop:    POST /api/elf/stop?name=blink.elf
 * Unload:  POST /api/elf/unload?name=blink.elf
 *
 * Contract: define nnode_main (like the old app_main) and nnode_term
 * (SIGTERM handler). The entry shim in sdk/nnode_entry.c provides the
 * ELF entry point, registers both pointers and forwards to nnode_main.
 * Missing definitions fail the module link.
 *
 * Settings: config.defaults next to the sources (validated and embedded
 * into the .elf as .nnode_default by ../build.py). The panel renders it
 * as a settings page and stores values in /data/config/blink. Read them
 * here via nnode_config_get* (restart the module to apply changes).
 *
 * nnode_main runs in an httpd worker task, so it must NOT block: it spawns
 * a worker task (tracked per-module by the firmware, see main/arena.c)
 * and returns immediately. nnode_term runs on Stop with the slot active:
 * keep it short and non-blocking, only signal tasks and release hardware.
 *
 * Only use symbols the firmware exports: plain libc (printf, ...),
 * gpio_* and FreeRTOS from the modsym table (main/modsym.c), plus
 * nnode_register/nnode_stop_requested from sdk/nnode_sdk.h. No ESP_LOGx:
 * esp_log_write is not exported. Note: printf goes to UART only, it does
 * not land in /api/log (that buffer captures esp_log output).
 *
 * LED: the default GPIO and its active level come from board_config.h
 * (BOARD_LED_GPIO_DEFAULT, BOARD_LED_ACTIVE_LOW). Plain toggling only
 * blinks boards with a plain LED; on RGB revisions the log counter
 * proves execution instead.
 */
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "nnode_sdk.h"
#include "board_config.h"

static volatile unsigned g_count;
static volatile int g_gpio = BOARD_LED_GPIO_DEFAULT;
static volatile int g_on_ms = 500;
static volatile int g_off_ms = 500;
static volatile int g_invert;
static volatile int g_mode; /* 0 blink, 1 always on, 2 always off */
static volatile int g_on_stop; /* 0 enable, 1 disable, 2 nothing */

static void led_on(void)
{
    gpio_set_level(g_gpio, BOARD_LED_ACTIVE_LOW ? 0 : 1);
}

static void led_off(void)
{
    gpio_set_level(g_gpio, BOARD_LED_ACTIVE_LOW ? 1 : 0);
}

static void blink_task(void *arg)
{
    const int on = BOARD_LED_ACTIVE_LOW ? 0 : 1;
    const int off = BOARD_LED_ACTIVE_LOW ? 1 : 0;
    int level = on;
    (void)arg;
    if (g_mode == 1) {
        led_on();
    } else if (g_mode == 2) {
        led_off();
    }
    while (!nnode_stop_requested()) {
        int out;
        if (g_mode != 0) {
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }
        out = level;
        level = (level == on) ? off : on;
        if (g_invert) {
            out = !out;
        }
        gpio_set_level(g_gpio, out);
        printf("blink %u: %s\n", g_count++,
            out == on ? "on" : "off");
        vTaskDelay(pdMS_TO_TICKS(
            out == on ? g_on_ms : g_off_ms));
    }
    printf("blink: worker exiting\n");
    vTaskDelete(NULL);
}

int nnode_main(int argc, char *argv[])
{
    long v;
    char mode[8];
    char stop[16];
    (void)argc;
    (void)argv;
    g_mode = 0;
    if (nnode_config_get("mode", mode, sizeof(mode)) == 0) {
        if (strcmp(mode, "on") == 0) {
            g_mode = 1;
        } else if (strcmp(mode, "off") == 0) {
            g_mode = 2;
        }
    }
    v = nnode_config_get_int("gpio", BOARD_LED_GPIO_DEFAULT);
    if (v >= 0 && board_gpio_valid((int)v)) {
        g_gpio = (int)v;
    }
    g_on_stop = 1;
    if (nnode_config_get("on_stop", stop, sizeof(stop)) == 0) {
        if (strcmp(stop, "enable_led") == 0) {
            g_on_stop = 0;
        } else if (strcmp(stop, "do_nothing") == 0) {
            g_on_stop = 2;
        }
    }
    if (g_mode == 0) {
        /* Timings and polarity matter for blinking only: on/off
         * modes drive the pin straight, invert stays unread. */
        v = nnode_config_get_int("on_ms", 500);
        if (v >= 10 && v <= 60000) {
            g_on_ms = (int)v;
        }
        v = nnode_config_get_int("off_ms", 500);
        if (v >= 10 && v <= 60000) {
            g_off_ms = (int)v;
        }
        g_invert = nnode_config_get_bool("invert", 0) ? 1 : 0;
    } else {
        g_invert = 0;
    }
    gpio_reset_pin(g_gpio);
    gpio_set_direction(g_gpio, GPIO_MODE_OUTPUT);
    led_off();
    if (xTaskCreatePinnedToCore(blink_task, "blink", 2048, NULL, 5,
            NULL, tskNO_AFFINITY) != pdPASS) {
        printf("blink: task create failed\n");
        return -1;
    }
    printf("blink: started on GPIO%d\n", g_gpio);
    return 0;
}

void nnode_term(void)
{
    /* Stop was pressed: firmware already raised the per-instance flag,
     * workers polling nnode_stop_requested() exit on their own. */
    printf("blink: stopping\n");
    if (g_on_stop == 0) {
        led_on();
    } else if (g_on_stop == 1) {
        led_off();
    }
}
