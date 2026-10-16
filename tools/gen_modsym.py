#!/usr/bin/env python3
"""Build NNode/main/modsym.c from the curated symbol baseline.

Inputs (both must exist, no legacy paths):
- managed_components/elf_loader/src/esp_elf_symbol.c: upstream loader
  defaults (ESP_ELFSYM_EXPORT in g_esp_libc/espidf tables). Our table
  must stay disjoint from it: names already covered upstream need no
  entry here (overlap is a warning, the entry still wins - same address).
- main/modsym.c (current file): the baseline name list. The legacy
  vendor table this list was derived from is gone, so the generated file
  itself is the source of truth for the gpio/rtc/dedic/freertos groups.

On top of the baseline the script appends EXTRA groups (symbols the
baseline lacks but the firmware must export) and emits the resolver tail.
Resolver chain (see modsym.c): arena wrappers first, then the lifecycle
contract (nnode_register/nnode_stop_requested via nnode_lifecycle_resolve
in main/files.c), then this table, then the loader defaults.

Do NOT hand-edit modsym.c: rerun this script instead.
"""
import os
import re
import sys

ROOT = os.path.normpath(os.path.join(
    os.path.dirname(os.path.abspath(__file__)), ".."))
LOADER_SRC = os.path.join(ROOT, "managed_components", "elf_loader",
                          "src", "esp_elf_symbol.c")
OUT = os.path.join(ROOT, "main", "modsym.c")

if not os.path.isfile(LOADER_SRC):
    print("loader defaults not found: %s" % LOADER_SRC, file=sys.stderr)
    sys.exit(1)
if not os.path.isfile(OUT):
    print("baseline not found: %s" % OUT, file=sys.stderr)
    sys.exit(1)
with open(LOADER_SRC, "r", encoding="utf-8") as f:
    loader_text = f.read()
with open(OUT, "r", encoding="utf-8") as f:
    base_text = f.read()

upstream = set(re.findall(r"ESP_ELFSYM_EXPORT\((\w+)\)", loader_text))
baseline = re.findall(r'^\s*\{\s*"(\w+)", \(uintptr_t\)&\1 \},',
                      base_text, flags=re.M)
print("upstream=%d baseline=%d" % (len(upstream), len(baseline)))
if len(baseline) != len(set(baseline)):
    print("MISMATCH duplicate names in baseline", file=sys.stderr)
    sys.exit(1)

# Verified against nm over all C3 archives (13737 unique symbols) and
# libfreertos.a (229 defined symbols). Families below, in table order.

LIBC = {"fopen", "fclose", "fread", "fseek", "ftell", "rename", "unlink"}


def family(n):
    if n in LIBC:
        return "libc"
    if n.startswith("gpio_"):
        return "gpio"
    if n.startswith("rtc_gpio_"):
        return "rtc"
    if n.startswith("dedic_gpio_"):
        return "dedic"
    return "freertos"


GROUPS = ("gpio", "rtc", "dedic", "freertos", "libc")
GROUP_TITLE = {
    "gpio": "gpio_ driver group",
    "rtc": "rtc_gpio_ group",
    "dedic": "dedic_gpio_ (dedicated GPIO) group",
    "freertos": "FreeRTOS kernel group",
    "libc": "libc file ops (missing from the loader defaults)",
}
# Deliberately omitted symbols per family, with reasons.
# The "v6.0-removed" set keeps the old (v5.x) vendor table link-clean
# on ESP-IDF v6: those symbols no longer exist there.
OMITTED = {
    "gpio": ([
        "gpio_hal_intr_disable",
        "gpio_hal_intr_enable_on_core",
        "gpio_hal_iomux_in",
        "gpio_hal_iomux_out",
        "gpio_hal_isolate_in_sleep",
    ], "HAL internals, not part of the driver API normal modules use"),
    "rtc": ([], None),
    "dedic": ([], None),
    "freertos": ([
        "FreeRTOS_openocd_params",
        "uxTopUsedPriority",
        "vApplicationStackOverflowHook",
    ], "debugger-only struct, kernel-internal counter and an "
        "application-provided hook (must be defined by the app, "
        "never imported from the firmware) respectively"),
    "libc": ([
        "fprintf",
        "fwrite",
        "printf",
    ], "already exported by the loader defaults, no entry needed"),
}

# Extra comment lines kept under a group footer (history worth keeping).
NOTE = {
    "gpio": [
        "ESP-IDF v6.0: gpio_deep_sleep_wakeup_enable/disable were removed,",
        "replaced by gpio_wakeup_enable/disable_on_hp_periph_powerdown_sleep;",
        "gpio_iomux_in/out were removed, use gpio_iomux_input/output.",
    ],
    "freertos": [
        "Dropped in ESP-IDF v6.0 (were already macros/removed upstream, no",
        "linkable symbol left): ulTaskNotifyTake, vTaskDelayUntil,",
        "xQueueGenericReceive, xTaskNotifyWait. Use ulTaskNotifyTake/xTaskDelayUntil",
        "macros and xQueueReceive/xQueuePeek/xQueueSemaphoreTake instead.",
    ],
}
# Symbols that the baseline lacks but the firmware must export
# (ESP-IDF v6.0 replacements plus libc file ops missing upstream).
EXTRA = {
    "gpio": [
        "gpio_wakeup_disable_on_hp_periph_powerdown_sleep",
        "gpio_wakeup_enable_on_hp_periph_powerdown_sleep",
    ],
    "rtc": [],
    "dedic": [],
    "freertos": [],
    "libc": sorted(LIBC),
}

names = [n for n in baseline if family(n) in GROUPS]
assert len(names) == len(baseline), "family() must cover every symbol"
for g in GROUPS:
    names.extend(n for n in EXTRA[g] if n not in names)

overlap = [n for n in names if n in upstream]
if overlap:
    print("WARNING already upstream (redundant entries): %s" % overlap)

ordered = []
for g in GROUPS:
    ordered.extend(n for n in names if family(n) == g)
assert len(ordered) == len(names), "family() must cover every symbol"
names = ordered
print("unique=%d" % len(names))

lines = []
lines.append("/* NNode ELF module symbol table (custom resolver for elf_loader).")
lines.append(" *")
lines.append(" * Generated by tools/gen_modsym.py - do not hand-edit.")
lines.append(" * These are the extra firmware symbols (GPIO driver, FreeRTOS,")
lines.append(" * libc file ops) that ELF modules may call, beyond the loader")
lines.append(" * defaults.")
lines.append(" *")
lines.append(" * The `extern int` declarations only take addresses (same trick")
lines.append(" * as upstream esp_all_symbol.c) and pin the functions against")
lines.append(" * linker garbage-collection.")
lines.append(" */")
lines.append("#include <stddef.h>")
lines.append("#include <stdint.h>")
lines.append("")
lines.append("/* Local strcmp to avoid string.h (newlib declares __getreent")
lines.append(" * as a function there, clashing with the extern int below). */")
lines.append("static int nnode_streq(const char *a, const char *b)")
lines.append("{")
lines.append("    while (*a && *a == *b) {")
lines.append("        a++;")
lines.append("        b++;")
lines.append("    }")
lines.append("    return *a == *b;")
lines.append("}")
lines.append("")
lines.append('#pragma GCC diagnostic push')
lines.append('#pragma GCC diagnostic ignored "-Wbuiltin-declaration-mismatch"')
for g in GROUPS:
    lines.append("/* %s. */" % GROUP_TITLE[g])
    for n in (x for x in names if family(x) == g):
        lines.append("extern int %s;" % n)
lines.append('#pragma GCC diagnostic pop')
lines.append("")
lines.append("typedef struct { const char *name; uintptr_t addr; } nnode_sym_t;")
lines.append("")
lines.append("static const nnode_sym_t kNNodeSyms[] = {")
for g in GROUPS:
    group = [n for n in names if family(n) == g]
    for n in group:
        lines.append('    { "%s", (uintptr_t)&%s },' % (n, n))
    omitted, reason = OMITTED[g]
    if omitted:
        lines.append("    /* --- end of %s (%d exported)." % (GROUP_TITLE[g], len(group)))
        for note in NOTE.get(g, []):
            lines.append("     * %s" % note)
        lines.append("     * Deliberately not exported (%s):" % reason)
        lines.append("     * %s. */" % ", ".join(omitted))
    else:
        lines.append("    /* --- end of %s: complete, all %d exported. */"
                     % (GROUP_TITLE[g], len(group)))
lines.append("    { NULL, (uintptr_t)0 },")
lines.append("};")
lines.append("")
lines.append("uintptr_t nnode_sym_resolve(const char *name)")
lines.append("{")
lines.append("    const nnode_sym_t *s;")
lines.append("    if (!name) {")
lines.append("        return 0;")
lines.append("    }")
lines.append("    for (s = kNNodeSyms; s->name; s++) {")
lines.append("        if (nnode_streq(s->name, name)) {")
lines.append("            return s->addr;")
lines.append("        }")
lines.append("    }")
lines.append("    return 0;")
lines.append("}")
lines.append("")
lines.append("/* Loader-private API (elf_loader include/private/elf_symbol.h,")
lines.append(" * not exposed via public INCLUDE_DIRS; symbols are global). */")
lines.append("typedef uintptr_t (*nnode_symbol_resolver_fn)(const char *sym_name);")
lines.append("extern void elf_set_symbol_resolver(nnode_symbol_resolver_fn resolver);")
lines.append("extern uintptr_t elf_find_sym_default(const char *sym_name);")
lines.append("extern uintptr_t nnode_arena_resolve(const char *sym_name);")
lines.append("extern uintptr_t nnode_lifecycle_resolve(const char *sym_name);")
lines.append("")
lines.append("/* Single resolver chain: arena wrappers first (they shadow the same-named")
lines.append(" * raw firmware symbols in the table below), then the lifecycle contract")
lines.append(" * (nnode_register/nnode_stop_requested), then the modsym table, then")
lines.append(" * the loader defaults. */")
lines.append("static uintptr_t nnode_resolver(const char *name)")
lines.append("{")
lines.append("    uintptr_t addr = nnode_arena_resolve(name);")
lines.append("    if (addr) {")
lines.append("        return addr;")
lines.append("    }")
lines.append("    addr = nnode_lifecycle_resolve(name);")
lines.append("    if (addr) {")
lines.append("        return addr;")
lines.append("    }")
lines.append("    addr = nnode_sym_resolve(name);")
lines.append("    if (addr) {")
lines.append("        return addr;")
lines.append("    }")
lines.append("    return elf_find_sym_default(name);")
lines.append("}")
lines.append("")
lines.append("void nnode_sym_resolver_install(void)")
lines.append("{")
lines.append("    elf_set_symbol_resolver(nnode_resolver);")
lines.append("}")
lines.append("")

with open(OUT, "w", encoding="utf-8", newline="") as f:
    f.write("\n".join(lines))
print("wrote %s" % OUT)
