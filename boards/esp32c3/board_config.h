#pragma once

/* Board constants for ESP32-C3 Super Mini (single source of truth).
 * Header-only, no IDF includes, so both firmware (main/bsp_esp32c3.c)
 * and loadable module examples (examples/blink) can share it.
 * Keep in sync with boards/esp32c3/board.json.
 */

#define BOARD_TARGET "esp32c3"
#define BOARD_MACHINE "esp32c3"
#define BOARD_MODEL "ESP32-C3"
#define BOARD_CPU_ARCH "RISC-V RV32IMC"
#define BOARD_HAS_TEMP_SENSOR 1

/* Onboard LED, active low: 0 lights it. */
#define BOARD_LED_GPIO_DEFAULT 8
#define BOARD_LED_ACTIVE_LOW 1

/* User GPIOs on this board: 11..17 belong to SPI flash. */
static inline int board_gpio_valid(int g)
{
    return (g >= 0 && g <= 10) || (g >= 18 && g <= 21);
}
