idf_component_register(SRCS "nnode_main.c" "wifi.c" "fs.c" "http.c" "api.c" "tasks.c" "net.c" "nets.c" "files.c" "arena.c" "graphs.c" "logbuf.c" "syscfg.c" "backup.c" "cbor.c" "modsym.c" "cfg.c" "cfg_types.c" "cfg_schema.c" "fw_defaults.c" "../boards/esp32c3/bsp.c"
                       INCLUDE_DIRS "."
                         EMBED_FILES "page/index.html.gz")
