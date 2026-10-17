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
- File manager for the LittleFS `/data` partition: subdirectories,
  upload, download, delete
- ELF program loader: run several copies of modules stored in
  `/data/programs`, stop (SIGTERM) or kill (SIGKILL) each instance
- One-file backup and restore (CBOR) plus factory reset
- Hostname, timezone, NTP/SNTP, 12/24-hour clock and RU/EN language settings
- Hash routing (`#/section/page`), served over plain HTTP

## Quick start

You need ESP-IDF v6.1 with the `esp32c3` target, plus two Python packages
for the frontend build:

```sh
pip install rjsmin rcssmin
python3 tools/web_inline.py
./build.py esp32c3 build
./build.py esp32c3 flash   # needs the board on /dev/ttyACM0, see PORT in build.py
```

`build.py` takes the board and the action as explicit arguments
(`./build.py <target> <action>`). Each target builds in its own
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
`./build.py <target> build` - the first build runs `set-target`
automatically and writes a fresh `boards/<target>/sdkconfig` from
the defaults.

## How it works

### Frontend

The UI lives in `src_web/` (`index.html` shell with `<!-- @include -->`
partials, `style.css`, `i18n.js`, shared `api.js`/`router.js`/`ui.js`/`format.js`/
`init.js` in the root plus `<category>/<page>/<page>.js|html` per section,
listed in `manifest.json`)
and is plain dependency-free JavaScript. `tools/web_inline.py` inlines and
minifies
the CSS/JS into a single bundle, checks that every route from
`router.js` resolves to a `<section>` in the markup, writes
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
   `POST /api/elf/run`, `POST /api/elf/stop`, `POST /api/elf/kill`,
   `POST /api/elf/unload`, `GET /api/elf/instances`,
   `GET /api/elf/autostart`, `POST /api/elf/autostart/add`,
   `POST /api/elf/autostart/remove`, `POST /api/elf/autostart/move`,
   `GET /api/elf/config`, `POST /api/elf/config/save`
- backup: `GET /api/backup`, `POST /api/backup/restore`,
  `POST /api/factory-reset`, `GET /api/config/export`

Settings live in text files under `/data/configs` (one file per package:
`device`, `time`, `sta`, `ap`); user files live in LittleFS on `/data`.
`nvs_flash_init` stays only because the IDF WiFi driver requires it -
no NNode setting is stored in NVS. A station watchdog keeps retrying
known networks and falls back to the
access point after 10 seconds without a connection.

Each settings package is a flat text file with `package` / `version` /
`<key> <value>` lines, e.g. `/data/configs/time`:

```
package time
version 1
tz 'UTC0'
zonename ''
enabled '1'
server1 'pool.ntp.org'
server2 ''
server3 ''
```

Field types (bool, int, port, ip4addr, netmask, cidr4, macaddr,
hostname, range, wpakey, string, enum, list) are declared in the
schema: compiled-in defaults for firmware packages
(`main/fw_defaults.c`), an embedded `.nnode_default` section for
loadable modules. Values are validated against the schema on save,
restore and first sight.

`device` holds the stable id and hostname, `sta` the network count
plus the last-ok marker, `nets` the 8 saved network slots, `ap` the
access point, `autostart` the boot list. All files are seeded with
defaults on the first boot; later every change rewrites its file
immediately (atomically, tmp plus rename).
`GET /api/config/export` returns all packages concatenated, and the
CBOR backup embeds each package text plus the plain `/data` files.

### ELF modules

Modules in `/data/programs` are relocatable `.elf` files built from
`examples/<app>` via `examples/build.py <target> build <app>`. Every
module must define `nnode_main` (like `app_main`: spawn tasks, return
immediately, never block) and `nnode_term` (SIGTERM hook: short,
non-blocking, save state). `sdk/nnode_entry.c` provides the ELF entry
point and registers both; a module without them fails its link, and the
board rejects legacy images with `bad_abi`. Contract and imports live
in `sdk/nnode_sdk.h`, the reference is `examples/blink` (GPIO8 LED,
active-low on the C3 Super Mini, worker polls `nnode_stop_requested()`).

One file may run several times: each `POST /api/elf/run` loads a fresh
copy into one of 8 slots and returns its instance `id`.
`POST /api/elf/stop` (by `id` or legacy `name`) raises the per-instance
stop flag, calls `nnode_term`, waits up to 2 s for tasks to exit on
their own, then kills leftovers and frees the slot.
`POST /api/elf/kill` skips the hook and the wait. `GET /api/elf/instances`
lists live instances; the panel shows one row per file (Run only) plus
one row per instance (`-`, id, live state, Stop/Kill with a kill
confirmation).

Besides libc, modules may call the `gpio_*`/FreeRTOS symbols from
`main/modsym.c` plus file ops (`fopen`, `fclose`, `fread`, `fwrite`,
`fprintf`, `fseek`, `ftell`, `rename`, `unlink`) on LittleFS `/data`.
`malloc`/`free` and tasks are accounted per instance (see `main/arena.c`)
and reclaimed on stop/kill; anything else (open files, queues) must be
released by the module itself. Module `printf` goes to UART only, it
never lands in `/api/log` (that buffer captures `esp_log` output).

### Module settings

A module ships `config.defaults` next to its sources: `package` plus
`version`, `#@tab` / `#@field` meta lines and one `<key> <value>` line
per field (the default). `examples/build.py` validates it with
`tools/nnode_cfg_validate.py` and embeds it verbatim as the
`.nnode_default` ELF section. The firmware finds the section by its ELF
headers (module code never runs for this) and creates
`/data/configs/<pkg>` on first sight: listing the Programs subsection
or running the module, whichever comes first. On upgrade new keys are
added with defaults and orphan keys are dropped, user values are kept.
The Settings button stays inactive without valid defaults. The package
name must not be firmware-reserved (device, time, sta, ap, autostart).
Tabs are optional (none, or at least two); a field may carry
`show_if="mode==blink"` to hide it in the panel until the condition on
earlier bool/enum/int fields holds (UI-only, the module reads every
key). Types: bool, int, port, ip4addr, netmask, cidr4, macaddr,
hostname, range, wpakey, string, enum, list (one line, comma-separated).
The panel renders tabs and fields generically (`GET /api/elf/config`,
`POST /api/elf/config/save`, values validated server-side); the module
reads its own values via `nnode_config_get*` (`sdk/nnode_sdk.h`),
restart to apply. See `examples/blink/config.defaults`.

### Limits

- At most 8 saved WiFi networks; SSID is 1-32 chars, passphrase is 0-63
   chars (PSK or open networks only, no Enterprise or WEP); passphrases are
   stored in `/data/configs` as plain text.
- Path segments in `/data` may not be `.` or contain `/` or `..` and are
   capped at 64 chars (8 levels, 128 chars total); only `.elf` files in
   `/data/programs` can be executed, and up to 8 instances stay loaded
   at once (one file may run several times, each copy takes a slot).
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
  src_web/        web UI sources: index.html shell, style.css, i18n.js,
                  shared api/router/ui/format/init.js in the root,
                  <category>/<page>/ partials and logic, manifest.json;
                  generated preview in src_web/inlined/ (not committed)
  tools/          build helpers: web_inline.py (minify, inline, gzip),
                  gen_modsym.py (ELF module symbol table)
  sdk/            module contract: nnode_sdk.h (imports + nnode_main/nnode_term),
                  nnode_entry.c (entry shim linked into each module)
   main/           firmware: REST API, WiFi, file-backed settings, LittleFS, ELF runner
  examples/       loadable ELF module sources (blink for esp32c3);
                  built via examples/build.py <target> <action> <app> into
                  build-<target>/modules/, nothing generated inside
  main/page/      generated index.html.gz, embedded into the firmware
  boards/         per-board files: partitions.csv, sdkconfig.defaults,
                  sdkconfig (generated from the defaults, not committed)
  sdkconfig.defaults  Kconfig defaults shared by all boards
  build.py        build/flash helper: ./build.py <target> <action>
```

`build-*/`, `managed_components/`, `src_web/inlined/` and
`main/page/index.html*` are generated - do not commit them. Keep `sdkconfig` out of version control as
well: it can hold real WiFi credentials.
