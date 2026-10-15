#!/bin/bash
# Build (and optionally flash) the nnode firmware for a given board target.
#
# Run ON THE VM (ssh ubuntu-vm), never on Windows:
#   cd /media/sf_esp32c3/NNode && ./build.sh [target] [action]
#
#   target  board in boards/<target>/ (default: esp32c3)
#   action  build|flash|monitor|clean (default: build)
#
# What it does:
#   1. src_web/* --web_inline.py--> src_web/inlined/index.html
#      + main/page/index.html.gz (the whole SPA in one file,
#      page copy baked into firmware via EMBED_FILES)
#   2. idf.py build|flash in build-<target>/ with
#      boards/<target>/sdkconfig (ESP-IDF v5.5.5)
#
# Never runs erase_flash: the storage partition (/data) must survive reflashes.
set -e
cd "$(dirname "$0")"

TARGET="${1:-esp32c3}"
ACTION="${2:-build}"
BOARD_DIR="boards/$TARGET"
BUILD_DIR="build-$TARGET"
SDKCONFIG="$BOARD_DIR/sdkconfig"
DEFAULTS="sdkconfig.defaults;$BOARD_DIR/sdkconfig.defaults"

PORT="${PORT:-/dev/serial/by-id/usb-Espressif_USB_JTAG_serial_debug_unit_D8:3B:DA:34:B0:84-if00}"

if [ ! -d "$BOARD_DIR" ]; then
  echo "unknown target '$TARGET' (no $BOARD_DIR directory)" >&2
  exit 1
fi

# shellcheck disable=SC1091
if [ -n "${IDF_PATH:-}" ] && [ -f "$IDF_PATH/export.sh" ]; then
  . "$IDF_PATH/export.sh" > /dev/null
elif [ -f ~/esp-idf/export.sh ]; then
  . ~/esp-idf/export.sh > /dev/null
fi

idf() {
  idf.py -B "$BUILD_DIR" -D "SDKCONFIG=$SDKCONFIG" -D "SDKCONFIG_DEFAULTS=$DEFAULTS" "$@"
}

case "$ACTION" in
  build|flash)
    python3 tools/web_inline.py
    if [ ! -f "$SDKCONFIG" ]; then
      idf set-target "$TARGET"
    fi
    idf build
    if [ "$ACTION" = flash ]; then
      idf -p "$PORT" flash
    fi
    ;;
  monitor)
    idf -p "$PORT" monitor
    ;;
  clean)
    rm -rf "$BUILD_DIR"
    ;;
  *)
    echo "usage: $0 [target] [build|flash|monitor|clean]" >&2
    exit 1
    ;;
esac
