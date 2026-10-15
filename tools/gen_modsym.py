#!/usr/bin/env python3
"""Build NNode/main/modsym.c from the legacy elf_loader symbol table.

Reads extern decls + ESP_ELFSYM_EXPORT entries from
NNode/_components/elf_loader/src/esp_all_symbol.c and emits a
self-contained resolver table for the NNode app (approach B:
custom elf_set_symbol_resolver with fallback to elf_find_sym_default).
No regex-free requirement here, plain build helper.
"""
import os
import re
import sys

ROOT = os.path.normpath(os.path.join(
    os.path.dirname(os.path.abspath(__file__)), ".."))
SRC = os.path.join(ROOT, "_components", "elf_loader", "src", "esp_all_symbol.c")
OUT = os.path.join(ROOT, "main", "modsym.c")

if not os.path.isfile(SRC):
    print("legacy table not found: %s" % SRC, file=sys.stderr)
    print("(kept outside the repo while the fork migration is in progress)",
          file=sys.stderr)
    sys.exit(1)
with open(SRC, "r", encoding="utf-8") as f:
    text = f.read()

externs = re.findall(r"^extern int (\w+);", text, flags=re.M)
exports = re.findall(r"ESP_ELFSYM_EXPORT\((\w+)\)", text)

print("externs=%d exports=%d" % (len(externs), len(exports)))
if set(externs) != set(exports):
    print("MISMATCH extern vs export sets:",
          set(externs) ^ set(exports), file=sys.stderr)
    sys.exit(1)

seen = set()
names = []
for n in exports:
    if n not in seen:
        seen.add(n)
        names.append(n)
print("unique=%d" % len(names))

# Verified against nm over all C3 archives (13737 unique symbols) and
# libfreertos.a (229 defined symbols). Families below, in table order.


def family(n):
    if n.startswith("gpio_"):
        return "gpio"
    if n.startswith("rtc_gpio_"):
        return "rtc"
    if n.startswith("dedic_gpio_"):
        return "dedic"
    return "freertos"


GROUPS = ("gpio", "rtc", "dedic", "freertos")
GROUP_TITLE = {
    "gpio": "gpio_ driver group",
    "rtc": "rtc_gpio_ group",
    "dedic": "dedic_gpio_ (dedicated GPIO) group",
    "freertos": "FreeRTOS kernel group",
}
# Deliberately omitted symbols per family, with reasons.
# The "v6.0-removed" set keeps regeneration from the old (v5.x) vendor
# table link-clean on ESP-IDF v6: those symbols no longer exist there.
OMITTED = {
    "gpio": ([
        "gpio_hal_intr_disable",
        "gpio_hal_intr_enable_on_core",
        "gpio_hal_iomux_in",
        "gpio_hal_iomux_out",
        "gpio_hal_isolate_in_sleep",
        "gpio_deep_sleep_wakeup_disable",  # v6.0-removed, use gpio_wakeup_*_on_hp_periph_powerdown_sleep
        "gpio_deep_sleep_wakeup_enable",  # v6.0-removed, use gpio_wakeup_*_on_hp_periph_powerdown_sleep
        "gpio_iomux_in",  # v6.0-removed, use gpio_iomux_input
        "gpio_iomux_out",  # v6.0-removed, use gpio_iomux_output
    ], "HAL internals, not part of the driver API normal modules use"),
    "rtc": ([], None),
    "dedic": ([], None),
    "freertos": ([
        "FreeRTOS_openocd_params",
        "uxTopUsedPriority",
        "vApplicationStackOverflowHook",
        "ulTaskNotifyTake",  # v6.0-removed compat fn (now a macro, no symbol)
        "vTaskDelayUntil",  # v6.0-removed compat fn (use xTaskDelayUntil)
        "xQueueGenericReceive",  # v6.0-removed compat fn (use xQueueReceive/Peek)
        "xTaskNotifyWait",  # v6.0-removed compat fn (now a macro, no symbol)
    ], "debugger-only struct, kernel-internal counter and an "
        "application-provided hook (must be defined by the app, "
        "never imported from the firmware) respectively"),
}

ordered = []
for g in GROUPS:
    ordered.extend(n for n in names if family(n) == g)
assert len(ordered) == len(names), "family() must cover every symbol"
names = ordered

# Symbols that the old vendor table lacks but the firmware must export
# (ESP-IDF v6.0 replacements for the removed gpio deep-sleep wakeup APIs).
EXTRA = {
    "gpio": [
        "gpio_wakeup_disable_on_hp_periph_powerdown_sleep",
        "gpio_wakeup_enable_on_hp_periph_powerdown_sleep",
    ],
    "rtc": [],
    "dedic": [],
    "freertos": [],
}

OMIT = set(n for v in OMITTED.values() for n in v[0])
names = [n for n in names if n not in OMIT]
for g in GROUPS:
    names.extend(n for n in EXTRA[g] if n not in names)

lines = []
lines.append("/* NNode ELF module symbol table (custom resolver for elf_loader).")
lines.append(" *")
lines.append(" * Generated from the legacy vendor table by tools/gen_modsym.py.")
lines.append(" * These are the extra firmware symbols (GPIO driver + FreeRTOS)")
lines.append(" * that ELF modules may call, beyond the loader defaults.")
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
lines.append("")
lines.append("/* Single resolver chain: arena wrappers first (they shadow the same-named")
lines.append(" * raw firmware symbols in the table below), then the modsym table, then")
lines.append(" * the loader defaults. */")
lines.append("static uintptr_t nnode_resolver(const char *name)")
lines.append("{")
lines.append("    uintptr_t addr = nnode_arena_resolve(name);")
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
