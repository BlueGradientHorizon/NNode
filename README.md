# NNode

NNode is a lightweight web dashboard for ESP32 devices, styled after the
OpenWrt LuCI interface: plain tables and forms, no JavaScript frameworks,
no cloud, everything served by the chip itself.

The whole single-page app is minified into one HTML file, gzipped, and
baked into the firmware image, so the panel works with zero external
assets and fits easily next to your application.

## Features

- Status overview: chip model, NNode and ESP-IDF versions, uptime, heap,
  flash partitions, network state
- Live graphs of CPU load, heap usage and network traffic
- FreeRTOS task list with per-task CPU share (only NNode tasks can be stopped)
- System log viewer with severity filter
- WiFi client: network scan, connect and disconnect, up to 8 saved networks
  with per-network BSSID lock and auth floor
- Access point with `auto` and `always` modes, optional IPv4 NAT relay that
  shares the station uplink, and DHCP-served DNS
- Diagnostics with live output: ping, traceroute and nslookup
- File manager for the LittleFS `/data` partition: upload, download, delete
- ELF program loader: run, stop and unload modules stored in `/data`
- One-file backup and restore (CBOR) plus factory reset
- Hostname, timezone, NTP/SNTP, 12/24-hour clock and RU/EN language settings
- Hash routing (`#/section/page`), served over plain HTTP

## Quick start

You need ESP-IDF v6.1 with the `esp32c3` target, plus two Python packages
for the frontend build:

```sh
pip install rjsmin rcssmin
python3 tools/web_inline.py
./build.sh esp32c3 build
./build.sh esp32c3 flash   # needs the board on /dev/ttyACM0, see PORT in build.sh
```

`build.sh` takes the board as its first argument (`./build.sh [target]
[action]`, defaults: `esp32c3 build`). Each target builds in its own
`build-<target>/` directory with its own `boards/<target>/sdkconfig`, so
switching boards never forces a full rebuild of the other one.

Do not run `erase_flash` on a provisioned board. The `storage` partition
that holds `/data` lives at a fixed offset and is meant to survive
reflashing; a normal `idf.py flash` already preserves it.

On the first boot with empty settings the board starts an open access
point called `NNode-XXXX` (stock address `http://192.168.4.1`). Join it,
open the panel, and connect the board to your WiFi from the Wireless page.
The default hostname mirrors the AP name (`nnode-XXXX`); both share one
random suffix that is generated once and kept in NVS.

## Boards

Only `esp32c3` is supported for now. Everything board-specific lives in
`boards/<target>/` (`partitions.csv`, `sdkconfig.defaults`, and the
generated `sdkconfig`); shared Kconfig defaults stay in the root
`sdkconfig.defaults`. To add a board, copy `boards/esp32c3` to
`boards/<target>`, adjust flash size and partitions, then run
`./build.sh <target> build` - the first build runs `set-target`
automatically and writes a fresh `boards/<target>/sdkconfig` from
the defaults.

## How it works

### Frontend

The UI lives in `src_web/` (`index.html`, `style.css`, `i18n.js`, `app.js`)
and is plain dependency-free JavaScript. `tools/web_inline.py` inlines and
minifies
the CSS/JS into a single bundle, checks that every route from
`app.js` resolves to a `<section>` in the markup, writes
`src_web/inlined/index.html` for desktop layout checks, and drops
`index.html.gz` straight into `main/page/`. The `.gz` copy is embedded into the firmware
(`EMBED_FILES` in `main/CMakeLists.txt`) and served as-is with
`Content-Encoding: gzip`, so the MCU pays zero runtime cost for
decompression.

To hack on the UI, edit `src_web/`, rerun `python3 tools/web_inline.py`,
and open `src_web/inlined/index.html` in a desktop browser. Layout and translations render
fine; API calls fail without a board attached.

### Backend

The firmware runs `esp_http_server` with a small JSON REST API:

- system: `GET /api/summary`, `GET /api/sys`, `GET /api/tasks`,
  `POST /api/kill`, `GET /api/graphs`, `GET /api/log`,
  `POST /api/log/clear`, `POST /api/reboot`
- time and identity: `GET/POST /api/time`, `GET/POST /api/hostname`,
  `GET/POST /api/tz`, `GET/POST /api/ntp`, `POST /api/ntp/sync`
- WiFi: `GET /api/net`, `GET /api/scan`, `GET /api/nets`,
  `POST /api/nets/add`, `POST /api/nets/update`, `POST /api/nets/forget`,
  `POST /api/sta/connect`, `POST /api/sta/disconnect`, `GET/POST /api/ap`,
  `GET /api/ap/stations`, `POST /api/ap/deauth`, `POST /api/renew`
- diagnostics: `POST /api/ping`, `POST /api/traceroute`,
  `POST /api/nslookup` (chunked live output)
- files and programs: `GET /api/files`, `GET /api/files/download`,
  `POST /api/files/upload`, `POST /api/files/delete`,
  `POST /api/elf/run`, `POST /api/elf/stop`, `POST /api/elf/unload`
- backup: `GET /api/backup`, `POST /api/backup/restore`,
  `POST /api/factory-reset`

Settings live in the `nnode` NVS namespace (saved networks, AP config,
hostname, NTP, timezone); user files live in LittleFS on `/data`.
A station watchdog keeps retrying known networks and falls back to the
access point after 10 seconds without a connection.

### Limits

- At most 8 saved WiFi networks; SSID is 1-32 chars, passphrase is 0-63
  chars (PSK or open networks only, no Enterprise or WEP); passphrases are
  stored in NVS as plain text.
- Filenames in `/data` may not contain `/` or `..` and are capped at
  64 chars; only `.elf` files can be executed, and up to 8 modules stay
  loaded at once.
- Only tasks named `nnode_*` can be stopped from the Processes page.

### Flash layout (4 MB, offsets are fixed - never move `storage`)

```csv
nvs,      data, nvs,      0x9000,   0x6000,
phy_init, data, phy,      0xf000,   0x1000,
factory,  app,  factory,  0x10000,  0x140000,
ota_0,    app,  ota_0,    0x150000, 0x140000,
storage,  data, littlefs, 0x290000, 0x170000,
```

## Repository layout

```text
NNode/
  src_web/        web UI sources: index.html, style.css, i18n.js, app.js;
                  generated preview in src_web/inlined/ (not committed)
  tools/          build helpers: web_inline.py (minify, inline, gzip),
                  gen_modsym.py (ELF module symbol table)
  main/           firmware: REST API, WiFi, NVS settings, LittleFS, ELF runner
  examples/       loadable ELF module sources (blink for esp32c3);
                  built via examples/build.sh <target> build <app> into
                  build-<target>/modules/, nothing generated inside
  main/page/      generated index.html.gz, embedded into the firmware
  boards/         per-board files: partitions.csv, sdkconfig.defaults,
                  sdkconfig (generated from the defaults, not committed)
  sdkconfig.defaults  Kconfig defaults shared by all boards
  build.sh        build/flash helper: ./build.sh <target> <action>
```

`build-*/`, `managed_components/`, `src_web/inlined/` and
`main/page/index.html*` are generated - do not commit them. Keep `sdkconfig` out of version control as
well: it can hold real WiFi credentials.
