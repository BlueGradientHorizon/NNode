#include "driver/temperature_sensor.h"
#include "bsp.h"
#include "board_config.h"

/* ESP32-C3 board support. Constants come from board_config.h
 * (same directory) which mirrors board.json. No other board
 * is mentioned here: a new target gets its own boards/<target>/bsp.c.
 */

const char *bsp_machine(void)
{
    return CONFIG_IDF_TARGET;
}

const char *bsp_model(void)
{
    return BOARD_MODEL;
}

const char *bsp_arch(void)
{
    return BOARD_CPU_ARCH;
}

int bsp_has_temp_sensor(void)
{
    return BOARD_HAS_TEMP_SENSOR;
}

static temperature_sensor_handle_t s_ts;
static int s_ts_state; /* 0=untried 1=ok -1=dead */

float bsp_temp_celsius(void)
{
    if (s_ts_state == 0) {
        s_ts_state = -1;
        temperature_sensor_config_t c = TEMPERATURE_SENSOR_CONFIG_DEFAULT(-10, 80);
        if (temperature_sensor_install(&c, &s_ts) == ESP_OK &&
            temperature_sensor_enable(s_ts) == ESP_OK) {
            s_ts_state = 1;
        }
    }
    if (s_ts_state == 1) {
        float t = 0;
        if (temperature_sensor_get_celsius(s_ts, &t) == ESP_OK) {
            return t;
        }
    }
    return 0;
}

int bsp_led_gpio_default(void)
{
    return BOARD_LED_GPIO_DEFAULT;
}

int bsp_gpio_valid(int gpio)
{
    return board_gpio_valid(gpio);
}
