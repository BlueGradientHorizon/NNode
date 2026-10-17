#pragma once

/* ESP common layer inventory (no code, only layer markers).
 * PORTABLE (no IDF includes): cbor.c, cfg_types.c, cfg_schema.c,
 *   fw_defaults.c, nets.c logic, backup format code.
 * ESP_COMMON (Espressif only, portable across ESP32 targets):
 *   wifi.c, net.c (esp_wifi, esp_netif, esp_event, lwIP NAPT, esp_ping),
 *   fs.c, syscfg.c NTP/hostname parts (LittleFS, esp_sntp, esp_netif),
 *   http.c (esp_http_server, EMBED_FILES page blob, esp_restart, esp_timer),
 *   arena.c, tasks.c, graphs.c, logbuf.c (FreeRTOS, heap_caps, esp_log),
 *   files.c ELF runner (esp_elf, heap_caps EXEC/8BIT, /data/programs),
 *   modsym.c (gpio/FreeRTOS/libc symbol table, loader private API),
 *   backup.c watchdog part (esp_task_wdt), nnode_main.c (app_main, NVS init).
 * BOARD (per target in boards/<target>/ + main/bsp_<target>.c):
 *   machine/model/arch strings, LED GPIO, valid GPIO ranges,
 *   temperature sensor presence, partitions.csv, sdkconfig.defaults,
 *   board.json toolchain prefix and flash size.
 * New ESP target: copy boards/esp32c3 to boards/<target>, update
 * board.json/board_config.h/partitions/sdkconfig, add main/bsp_<target>.c,
 * regenerate modsym per target archives. No portable file changes needed.
 */
