# BOARD_DIR comes from examples/build.py (-D BOARD_DIR=<boards/<target>>).
# Fallback keeps plain idf.py builds working without build.py.
if(NOT DEFINED BOARD_DIR)
    set(BOARD_DIR "../../../boards/esp32c3")
endif()
idf_component_register(SRCS "blink.c" "../../../sdk/nnode_entry.c"
                    INCLUDE_DIRS "." "../../../sdk" "${BOARD_DIR}")
