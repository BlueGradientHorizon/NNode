/* NNode ELF module example: blinker for ESP32-C3.
 *
 * Build: ../build.py esp32c3 build blink  ->
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
 * LED: GPIO8 is the onboard LED on the ESP32-C3 Super Mini, active-low
 * (0 lights it). Plain toggling only blinks boards with a plain LED;
 * RGB revisions wire GPIO8 to WS2812 data-in, there the log counter
 * proves execution instead.
 */
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "nnode_sdk.h"

#define LED_GPIO 8
#define BLINK_PERIOD_MS 500

/* Onboard LED is active-low: 0 is lit, 1 is dark. */
#define LED_ON 0
#define LED_OFF 1

static volatile unsigned g_count;

static void blink_task(void *arg)
{
    int level = LED_ON;
    (void)arg;
    while (!nnode_stop_requested()) {
        gpio_set_level(LED_GPIO, level);
        printf("blink %u: %s\n", g_count++, level == LED_ON ? "on" : "off");
        level = (level == LED_ON) ? LED_OFF : LED_ON;
        vTaskDelay(pdMS_TO_TICKS(BLINK_PERIOD_MS));
    }
    printf("blink: worker exiting\n");
    vTaskDelete(NULL);
}

int nnode_main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;
    gpio_reset_pin(LED_GPIO);
    gpio_set_direction(LED_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(LED_GPIO, LED_OFF);
    if (xTaskCreatePinnedToCore(blink_task, "blink", 2048, NULL, 5,
            NULL, tskNO_AFFINITY) != pdPASS) {
        printf("blink: task create failed\n");
        return -1;
    }
    printf("blink: started on GPIO%d\n", LED_GPIO);
    return 0;
}

void nnode_term(void)
{
    /* Stop was pressed: firmware already raised the per-instance flag,
     * workers polling nnode_stop_requested() exit on their own. */
    printf("blink: stopping, LED off\n");
    gpio_set_level(LED_GPIO, LED_OFF);
}
