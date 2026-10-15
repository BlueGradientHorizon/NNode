/* NNode ELF module example: blinker for ESP32-C3.
 *
 * Build: ../build.sh esp32c3 build blink  ->
 *   build-<target>/modules/blink/blink.app.elf
 * Upload:  POST /api/files/upload?name=blink.elf  (raw .app.elf bytes)
 * Run:     POST /api/elf/run?name=blink.elf
 * Stop:    POST /api/elf/stop?name=blink.elf
 * Unload:  POST /api/elf/unload?name=blink.elf
 *
 * The entry runs in an httpd worker task, so it must NOT block: it spawns
 * a worker task (tracked per-module by the firmware, see main/arena.c)
 * and returns immediately.
 *
 * Only use symbols the firmware exports: plain libc (printf, ...),
 * gpio_* and FreeRTOS from the modsym table (main/modsym.c). No ESP_LOGx:
 * esp_log_write is not exported, but printf lines still land in /api/log.
 *
 * LED: GPIO8 is the onboard LED on the ESP32-C3 Super Mini (plain LED on
 * older revisions, WS2812 data-in on RGB ones - plain toggling only blinks
 * the former; the log counter proves execution either way).
 */
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"

#define LED_GPIO 8
#define BLINK_PERIOD_MS 500

static void blink_task(void *arg)
{
    int level = 0;
    unsigned n = 0;
    (void)arg;
    for (;;) {
        gpio_set_level(LED_GPIO, level);
        printf("blink %u: led=%d\n", n++, level);
        level = !level;
        vTaskDelay(pdMS_TO_TICKS(BLINK_PERIOD_MS));
    }
}

int app_main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;
    gpio_reset_pin(LED_GPIO);
    gpio_set_direction(LED_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(LED_GPIO, 0);
    if (xTaskCreatePinnedToCore(blink_task, "blink", 2048, NULL, 5,
            NULL, tskNO_AFFINITY) != pdPASS) {
        printf("blink: task create failed\n");
        return -1;
    }
    printf("blink: started on GPIO%d\n", LED_GPIO);
    return 0;
}
