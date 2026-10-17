#pragma once

/* Board support package interface.
 * Project code must query board specifics through these helpers
 * instead of hardcoding GPIOs, model strings or sensor presence.
 * Implementation lives in bsp_<target>.c (currently bsp_esp32c3.c).
 * New board: add its bsp_<target>.c and select it in CMakeLists.txt.
 */
#ifdef __cplusplus
extern "C" {
#endif

/* Machine id for /api/summary (matches CONFIG_IDF_TARGET). */
const char *bsp_machine(void);
/* Human readable chip model and CPU arch for /api/sys. */
const char *bsp_model(void);
const char *bsp_arch(void);
/* 1 when the on-chip temperature sensor exists, 0 otherwise. */
int bsp_has_temp_sensor(void);
/* Best-effort die temperature in Celsius, 0 on failure/absence. */
float bsp_temp_celsius(void);
/* Default onboard LED GPIO from the board definition. */
int bsp_led_gpio_default(void);
/* 1 when the GPIO is usable by modules on this board. */
int bsp_gpio_valid(int gpio);

#ifdef __cplusplus
}
#endif
