#!/usr/bin/env python3
"""Validate NNode module config.defaults.

Usage: nnode_cfg_validate.py <config.defaults> [more files...]
Exits 0 when all files are valid, 1 with a message on the first error.
Used by examples/build.py before embedding the defaults into the ELF.

Grammar (keep in sync with main/cfg_schema.c):
  blank lines and '#' comments are ignored ('#@tab' and '#@field' are meta)
  package <pkg>            first data line, [a-z0-9_]{1,23}, not firmware-reserved
  version <N>              second data line, decimal 1..999999
  #@tab <id> <Title...>   0 or >=2 tabs; id [a-z0-9_]{1,32}, title 1..64 chars
  #@field <key> <type> [attrs]   one per field, declaration order = UI order
  <key> <value>            one value line per field (the default)

Field attrs (space separated k=v, quote with "..." when it holds spaces):
  label="..."   required, 1..64 chars
  tab=<id>      required when tabs exist, forbidden when they do not
  int:          min=<n> max=<n> (optional bounds)
  string:       maxlen=<n> (optional, default 64, 1..127)
  enum:         vals=a,b,c (required, 1..16 ids)
  list:         sub=<string|hostname|ip4addr|macaddr|port|int> (required),
                maxitems=<n> (optional, default 16, 1..32),
                maxlen=<n> (optional, for string sub)
  range:        min=<n> max=<n> (optional global bounds)
  show_if="EXPR"  optional visibility cond, no spaces inside; EXPR is
                OR of AND of comparisons: key==lit, key!=lit (bool/enum),
                plus <,<=,>,>= for int. Referenced key must be an earlier
                field of type bool, enum or int.
"""
import sys

TYPES = {"bool", "int", "list", "ip4addr", "netmask", "cidr4", "macaddr",
         "port", "hostname", "range", "wpakey", "string", "enum"}
COND_TYPES = {"bool", "enum", "int"}
LIST_SUBS = {"string", "hostname", "ip4addr", "macaddr", "port", "int"}
RESERVED = {"device", "time", "sta", "ap", "autostart"}


class CfgError(Exception):
    pass


def id_valid(s, maxlen=32):
    if not s or len(s) > maxlen:
        return False
    return all(("a" <= c <= "z") or ("0" <= c <= "9") or c == "_" for c in s)


ALLOW_RESERVED = [False]


def pkg_valid(s):
    return bool(s) and len(s) < 24 and id_valid(s, 23) and \
        (ALLOW_RESERVED[0] or s not in RESERVED)


def parse_int(s):
    s = s.strip()
    if not s:
        raise CfgError("bad integer '%s'" % s)
    neg = s.startswith("-") or s.startswith("+")
    body = s[1:] if neg else s
    if not body or not body.isdigit():
        raise CfgError("bad integer '%s'" % s)
    return int(s)


def split_attrs(s, fname, lineno):
    """Split 'k=v k2=\"v 2\"' into a dict, respecting double quotes."""
    attrs = {}
    i, n = 0, len(s)
    while i < n:
        while i < n and s[i] in " \t":
            i += 1
        if i >= n:
            break
        j = i
        while j < n and s[j] not in " \t=":
            j += 1
        key = s[i:j]
        if j >= n or s[j] != "=":
            raise CfgError("%s:%d: attr '%s' misses =" % (fname, lineno, key))
        j += 1
        if j < n and s[j] == '"':
            j += 1
            val = ""
            closed = False
            while j < n:
                if s[j] == "\\" and j + 1 < n:
                    val += s[j + 1]
                    j += 2
                elif s[j] == '"':
                    closed = True
                    j += 1
                    break
                else:
                    val += s[j]
                    j += 1
            if not closed:
                raise CfgError("%s:%d: unterminated quote" % (fname, lineno))
        else:
            k = j
            while j < n and s[j] not in " \t":
                j += 1
            val = s[k:j]
        if not key or len(key) > 15 or key in attrs:
            raise CfgError("%s:%d: bad/duplicate attr '%s'" % (fname, lineno, key))
        if len(val) > 127:
            raise CfgError("%s:%d: attr '%s' too long" % (fname, lineno, key))
        attrs[key] = val
        i = j
    if len(attrs) > 12:
        raise CfgError("%s:%d: too many attrs" % (fname, lineno))
    return attrs


def v_bool(v):
    if v not in ("0", "1"):
        raise CfgError("bool wants 0/1, got '%s'" % v)


def v_int(v, lo=None, hi=None):
    n = parse_int(v)
    if lo is not None and n < lo:
        raise CfgError("int %s below min %d" % (v, lo))
    if hi is not None and n > hi:
        raise CfgError("int %s above max %d" % (v, hi))
    return n


def v_port(v):
    return v_int(v, 1, 65535)


def v_ip4(v):
    p = v.split(".")
    if len(p) != 4:
        raise CfgError("bad ip4addr '%s'" % v)
    for g in p:
        if not g or not g.isdigit() or len(g) > 3:
            raise CfgError("bad ip4addr '%s'" % v)
        if len(g) > 1 and g.startswith("0"):
            raise CfgError("bad ip4addr '%s' (leading zero)" % v)
        if int(g) > 255:
            raise CfgError("bad ip4addr '%s'" % v)


def v_netmask(v):
    p = v.split(".")
    if len(p) != 4:
        raise CfgError("bad netmask '%s'" % v)
    bits = ""
    for g in p:
        if not g.isdigit() or int(g) > 255:
            raise CfgError("bad netmask '%s'" % v)
        bits += format(int(g), "08b")
    if "01" in bits:
        raise CfgError("bad netmask '%s' (not contiguous)" % v)
    if set(bits) == {"0"}:
        raise CfgError("bad netmask '%s' (all zero)" % v)


def v_cidr4(v):
    if "/" not in v:
        raise CfgError("bad cidr4 '%s' (want A.B.C.D/N)" % v)
    addr, _, prefix = v.partition("/")
    v_ip4(addr)
    if not prefix.isdigit() or int(prefix) > 32:
        raise CfgError("bad cidr4 '%s' (prefix 0..32)" % v)


def v_mac(v):
    p = v.split(":")
    if len(p) != 6:
        raise CfgError("bad macaddr '%s'" % v)
    for g in p:
        if len(g) != 2 or any(c not in "0123456789abcdefABCDEF" for c in g):
            raise CfgError("bad macaddr '%s'" % v)


def v_hostname(v, maxlen=64):
    if not v or len(v) > maxlen or v.startswith("-") or v.endswith("-"):
        raise CfgError("bad hostname '%s'" % v)
    for c in v:
        if not (c.isalnum() or c in "-."):
            raise CfgError("bad hostname '%s'" % v)


def v_range(v, lo=None, hi=None):
    a, sep, b = v.partition("-") if not v.startswith("-") else (None, None, None)
    if v.startswith("-"):
        # negative LO: find the separator after the first char
        idx = v.find("-", 1)
        if idx < 0:
            raise CfgError("bad range '%s' (want LO-HI)" % v)
        a, b = v[:idx], v[idx + 1:]
    elif sep != "-":
        raise CfgError("bad range '%s' (want LO-HI)" % v)
    nlo = parse_int(a)
    nhi = parse_int(b)
    if nlo > nhi:
        raise CfgError("bad range '%s' (LO>HI)" % v)
    if lo is not None and nlo < lo:
        raise CfgError("range '%s' below min %d" % (v, lo))
    if hi is not None and nhi > hi:
        raise CfgError("range '%s' above max %d" % (v, hi))


def v_wpakey(v):
    if len(v) == 64 and all(c in "0123456789abcdefABCDEF" for c in v):
        return
    if 8 <= len(v) <= 63 and all(32 <= ord(c) <= 126 for c in v):
        return
    raise CfgError("bad wpakey (8..63 printable ASCII or 64 hex)")


def v_string(v, maxlen=64):
    if len(v) > maxlen:
        raise CfgError("string too long (%d>%d)" % (len(v), maxlen))


def unquote_value(v, fname, lineno):
    v = v.strip()
    if not v:
        raise CfgError("%s:%d: empty value" % (fname, lineno))
    if v.startswith("'"):
        if len(v) < 2 or not v.endswith("'"):
            raise CfgError("%s:%d: unterminated quote" % (fname, lineno))
        inner = v[1:-1]
        out = ""
        i = 0
        while i < len(inner):
            c = inner[i]
            if c == "\\" and i + 1 < len(inner):
                nxt = inner[i + 1]
                out += {"n": "\n", "r": "\r", "t": "\t"}.get(nxt, nxt)
                i += 2
            elif c == "'":
                raise CfgError("%s:%d: raw quote inside quotes" % (fname, lineno))
            else:
                out += c
                i += 1
        return out
    if any(c in " \t'\"\\" for c in v):
        raise CfgError("%s:%d: bare value holds spaces/quotes, quote it" % (fname, lineno))
    return v


def check_value(ftype, raw, field, fname, lineno):
    try:
        if ftype == "bool":
            v_bool(raw)
        elif ftype == "int":
            v_int(raw, field.get("lo"), field.get("hi"))
        elif ftype == "port":
            v_port(raw)
        elif ftype == "ip4addr":
            v_ip4(raw)
        elif ftype == "netmask":
            v_netmask(raw)
        elif ftype == "cidr4":
            v_cidr4(raw)
        elif ftype == "macaddr":
            v_mac(raw)
        elif ftype == "hostname":
            v_hostname(raw)
        elif ftype == "range":
            v_range(raw, field.get("lo"), field.get("hi"))
        elif ftype == "wpakey":
            v_wpakey(raw)
        elif ftype == "string":
            v_string(raw, field.get("maxlen", 64))
        elif ftype == "enum":
            if raw not in field["vals"]:
                raise CfgError("enum wants one of %s, got '%s'"
                               % (",".join(field["vals"]), raw))
        elif ftype == "list":
            if raw == "":
                items = []
            else:
                items = raw.split(",")
            if len(items) > field.get("maxitems", 16):
                raise CfgError("list holds %d items, max %d"
                               % (len(items), field.get("maxitems", 16)))
            for it in items:
                if it == "" or any(c in " \t'\"\\" for c in it):
                    raise CfgError("bad list item '%s'" % it)
                sub = field["sub"]
                if sub == "string":
                    v_string(it, field.get("maxlen", 64))
                elif sub == "hostname":
                    v_hostname(it)
                elif sub == "ip4addr":
                    v_ip4(it)
                elif sub == "macaddr":
                    v_mac(it)
                elif sub == "port":
                    v_port(it)
                elif sub == "int":
                    v_int(it)
    except CfgError as e:
        raise CfgError("%s:%d: field '%s': %s" % (fname, lineno, field["key"], e))


def check_cond(expr, fields, order, fname, lineno):
    if not expr or " " in expr or "\t" in expr or len(expr) > 127:
        raise CfgError("%s:%d: bad show_if" % (fname, lineno))
    for or_part in expr.split("||"):
        if not or_part:
            raise CfgError("%s:%d: empty || branch" % (fname, lineno))
        for term in or_part.split("&&"):
            if not term:
                raise CfgError("%s:%d: empty && term" % (fname, lineno))
            op = None
            for cand in ("==", "!=", "<=", ">=", "<", ">"):
                if cand in term:
                    op = cand
                    break
            if not op:
                raise CfgError("%s:%d: bad term '%s'" % (fname, lineno, term))
            key, _, lit = term.partition(op)
            if not key or not lit:
                raise CfgError("%s:%d: bad term '%s'" % (fname, lineno, term))
            if key not in fields:
                raise CfgError("%s:%d: show_if refs unknown '%s'" % (fname, lineno, key))
            if order.index(key) >= order.index(cur_key[0]):
                raise CfgError("%s:%d: show_if must ref an earlier field ('%s')"
                               % (fname, lineno, key))
            rt = fields[key]["type"]
            if rt not in COND_TYPES:
                raise CfgError("%s:%d: show_if on '%s' of type %s (bool/enum/int only)"
                               % (fname, lineno, key, rt))
            if op not in ("==", "!=") and rt != "int":
                raise CfgError("%s:%d: op '%s' needs int, '%s' is %s"
                               % (fname, lineno, op, key, rt))
            ref = fields[key]
            try:
                if rt == "bool":
                    v_bool(lit)
                elif rt == "enum":
                    if lit not in ref["vals"]:
                        raise CfgError("unknown enum value '%s'" % lit)
                else:
                    parse_int(lit)
            except CfgError as e:
                raise CfgError("%s:%d: bad literal '%s': %s" % (fname, lineno, lit, e))


cur_key = [None]


def validate_file(path):
    with open(path, "r", encoding="utf-8") as f:
        lines = f.read().split("\n")
    pkg = None
    version = None
    tabs = {}
    tab_order = []
    fields = {}
    order = []
    values = {}
    cur_key[0] = None
    seen_data = 0
    for idx, raw in enumerate(lines):
        lineno = idx + 1
        if len(raw) > 255:
            raise CfgError("%s:%d: line too long" % (path, lineno))
        s = raw.strip()
        if not s or (s.startswith("#") and not s.startswith("#@")):
            continue
        if s.startswith("#@tab"):
            rest = s[len("#@tab"):].strip()
            if " " not in rest and "\t" not in rest:
                raise CfgError("%s:%d: #@tab wants '<id> <Title>'" % (path, lineno))
            tid, _, title = rest.partition(" ")
            tid = tid.strip()
            title = title.strip()
            if not id_valid(tid):
                raise CfgError("%s:%d: bad tab id '%s'" % (path, lineno, tid))
            if not title or len(title) > 64 or tid in tabs:
                raise CfgError("%s:%d: bad/duplicate tab '%s'" % (path, lineno, tid))
            tabs[tid] = title
            tab_order.append(tid)
            continue
        if s.startswith("#@field"):
            rest = s[len("#@field"):].strip()
            # key and type are the first two words, the rest is attrs
            parts = rest.split(None, 2)
            if len(parts) < 2:
                raise CfgError("%s:%d: #@field wants '<key> <type> [attrs]'" % (path, lineno))
            key, ftype = parts[0], parts[1]
            attr_s = parts[2] if len(parts) > 2 else ""
            if not id_valid(key):
                raise CfgError("%s:%d: bad field key '%s'" % (path, lineno, key))
            if key in fields:
                raise CfgError("%s:%d: duplicate field '%s'" % (path, lineno, key))
            if ftype not in TYPES:
                raise CfgError("%s:%d: bad type '%s'" % (path, lineno, ftype))
            attrs = split_attrs(attr_s, path, lineno)
            label = attrs.pop("label", "")
            if not label or len(label) > 64:
                raise CfgError("%s:%d: field '%s' needs label 1..64" % (path, lineno, key))
            tab = attrs.pop("tab", "")
            show_if = attrs.pop("show_if", "")
            f = {"key": key, "type": ftype, "label": label, "tab": tab,
                 "show_if": show_if, "lineno": lineno}
            if ftype == "int" or ftype == "range":
                if "min" in attrs:
                    f["lo"] = parse_int(attrs.pop("min"))
                if "max" in attrs:
                    f["hi"] = parse_int(attrs.pop("max"))
                if "lo" in f and "hi" in f and f["lo"] > f["hi"]:
                    raise CfgError("%s:%d: min>max" % (path, lineno))
            if ftype == "string":
                f["maxlen"] = 64
                if "maxlen" in attrs:
                    f["maxlen"] = parse_int(attrs.pop("maxlen"))
                    if not 1 <= f["maxlen"] <= 127:
                        raise CfgError("%s:%d: maxlen 1..127" % (path, lineno))
            if ftype == "enum":
                if "vals" not in attrs:
                    raise CfgError("%s:%d: enum needs vals=a,b,c" % (path, lineno))
                vals = attrs.pop("vals").split(",")
                if not 1 <= len(vals) <= 8 or any(not id_valid(v) for v in vals):
                    raise CfgError("%s:%d: bad enum vals" % (path, lineno))
                if len(set(vals)) != len(vals):
                    raise CfgError("%s:%d: duplicate enum vals" % (path, lineno))
                f["vals"] = vals
            if ftype == "list":
                if "sub" not in attrs or attrs["sub"] not in LIST_SUBS:
                    raise CfgError("%s:%d: list needs sub=%s" % (path, lineno, "|".join(sorted(LIST_SUBS))))
                f["sub"] = attrs.pop("sub")
                f["maxitems"] = 16
                if "maxitems" in attrs:
                    f["maxitems"] = parse_int(attrs.pop("maxitems"))
                    if not 1 <= f["maxitems"] <= 32:
                        raise CfgError("%s:%d: maxitems 1..32" % (path, lineno))
                f["maxlen"] = 64
                if "maxlen" in attrs:
                    f["maxlen"] = parse_int(attrs.pop("maxlen"))
                    if not 1 <= f["maxlen"] <= 127:
                        raise CfgError("%s:%d: maxlen 1..127" % (path, lineno))
            if attrs:
                raise CfgError("%s:%d: unknown attrs %s" % (path, lineno, ",".join(sorted(attrs))))
            fields[key] = f
            order.append(key)
            continue
        if s.startswith("#@"):
            raise CfgError("%s:%d: unknown meta '%s'" % (path, lineno, s[:16]))
        # data lines: package, version, values
        parts = s.split(None, 1)
        head = parts[0]
        if seen_data == 0:
            if head != "package" or len(parts) != 2 or not pkg_valid(parts[1].strip()):
                raise CfgError("%s:%d: first line must be 'package <name>'" % (path, lineno))
            pkg = parts[1].strip()
            seen_data = 1
            continue
        if seen_data == 1:
            if head != "version" or len(parts) != 2:
                raise CfgError("%s:%d: second line must be 'version <N>'" % (path, lineno))
            version = parse_int(parts[1].strip())
            if not 1 <= version <= 999999:
                raise CfgError("%s:%d: version 1..999999" % (path, lineno))
            seen_data = 2
            continue
        # value line
        if head in ("package", "version"):
            raise CfgError("%s:%d: '%s' only at the top" % (path, lineno, head))
        if not id_valid(head):
            raise CfgError("%s:%d: bad key '%s'" % (path, lineno, head))
        if head not in fields:
            raise CfgError("%s:%d: value for unknown field '%s'" % (path, lineno, head))
        if head in values:
            raise CfgError("%s:%d: duplicate value '%s'" % (path, lineno, head))
        if len(parts) != 2:
            raise CfgError("%s:%d: value line wants '<key> <value>'" % (path, lineno))
        raw_val = unquote_value(parts[1], path, lineno)
        if len(raw_val) >= 128:
            raise CfgError("%s:%d: value too long" % (path, lineno))
        values[head] = (raw_val, lineno)
    if pkg is None or version is None:
        raise CfgError("%s: missing package/version" % path)
    if not fields:
        raise CfgError("%s: no fields" % path)
    if len(tabs) == 1:
        raise CfgError("%s: one tab makes no sense, add a second or drop tabs" % path)
    for key in order:
        f = fields[key]
        if tabs and (not f["tab"] or f["tab"] not in tabs):
            raise CfgError("%s:%d: field '%s' needs a known tab" % (path, f["lineno"], key))
        if not tabs and f["tab"]:
            raise CfgError("%s:%d: field '%s' sets tab but no tabs exist" % (path, f["lineno"], key))
    if tabs:
        used = {f["tab"] for f in fields.values()}
        for tid in tab_order:
            if tid not in used:
                raise CfgError("%s: tab '%s' holds no fields" % (path, tid))
    for key in order:
        if key not in values:
            f = fields[key]
            raise CfgError("%s:%d: field '%s' misses its value line" % (path, f["lineno"], key))
    for key, (raw_val, vl) in values.items():
        check_value(fields[key]["type"], raw_val, fields[key], path, vl)
    for key in order:
        cur_key[0] = key
        expr = fields[key]["show_if"]
        if expr:
            check_cond(expr, fields, order, path, fields[key]["lineno"])
    return True


def main():
    args = [a for a in sys.argv[1:] if a != "--allow-reserved"]
    if "--allow-reserved" in sys.argv[1:]:
        ALLOW_RESERVED[0] = True
    if not args:
        print("usage: nnode_cfg_validate.py [--allow-reserved] <config.defaults> [...]",
              file=sys.stderr)
        return 2
    rc = 0
    for path in args:
        try:
            validate_file(path)
            print("ok: %s" % path)
        except CfgError as e:
            print("error: %s" % e, file=sys.stderr)
            rc = 1
        except OSError as e:
            print("error: cannot read %s: %s" % (path, e), file=sys.stderr)
            rc = 1
    return rc


if __name__ == "__main__":
    sys.exit(main())
