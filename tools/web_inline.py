#!/usr/bin/env python3
"""Bundle the NNode web UI into a single self-contained HTML file.

Pipeline:
  ../src_web/index.html + ../src_web/*.css + ../src_web/*.js
    -> inline local <link rel="stylesheet"> and <script src> references
       (contents minified with rcssmin / rjsmin)
    -> validate that every route from the SECTIONS table in app.js
       resolves to a <section> id in the markup
    -> drop comments and whitespace-only text nodes
    -> ../src_web/inlined/index.html (handy for layout checks
       in a desktop browser)
    -> ../main/page/index.html.gz (gzip -9, deterministic;
       the copy baked into the firmware)

Only the .gz file is baked into the firmware (see EMBED_FILES in
main/CMakeLists.txt) and served with Content-Encoding: gzip.

Parsing is done with html.parser plus a small hand-written scanner for the
route table. No regular expressions are used on purpose: the previous
regex-based builder broke silently on multiline tags and on ">" characters
inside quoted attribute values.

Dependencies: pip install rjsmin rcssmin
"""
import gzip
import os
import sys
from html.parser import HTMLParser

HERE = os.path.dirname(os.path.abspath(__file__))
WEB_SRC = os.path.normpath(os.path.join(HERE, "..", "src_web"))
HTML_OUT_DIR = os.path.join(WEB_SRC, "inlined")
PAGE_DIR = os.path.normpath(os.path.join(HERE, "..", "main", "page"))

VOID_ELEMENTS = frozenset([
    "area", "base", "br", "col", "embed", "hr", "img", "input",
    "link", "meta", "param", "source", "track", "wbr",
])
# Elements whose text must be copied verbatim, never whitespace-folded.
RAW_TEXT_ELEMENTS = frozenset(["script", "style", "pre", "textarea"])


def fail(msg):
    sys.exit("web_inline.py: error: %s" % msg)


def read_src(name):
    path = os.path.join(WEB_SRC, name)
    if not os.path.isfile(path):
        fail("missing source file: %s" % path)
    with open(path, "r", encoding="utf-8") as f:
        return f.read()


def minify_css(text):
    try:
        import rcssmin
    except ImportError:
        fail("need rcssmin: pip install rcssmin")
    return rcssmin.cssmin(text)


def minify_js(text):
    try:
        import rjsmin
    except ImportError:
        fail("need rjsmin: pip install rjsmin")
    return rjsmin.jsmin(text)


def is_local_ref(value):
    """True for relative in-project references (not URLs, anchors, data:)."""
    if not value:
        return False
    low = value.lower()
    if low.startswith(("http://", "https://", "//", "data:", "#")):
        return False
    return True


def escape_text(text):
    return text.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")


def escape_attr(value):
    return value.replace("&", "&amp;").replace('"', "&quot;")


class Bundler(HTMLParser):
    """Single pass over index.html: inline assets, collect section ids."""

    def __init__(self, loader):
        super().__init__(convert_charrefs=True)
        self.loader = loader
        self.out = []
        self.section_ids = set()
        self.leftover_refs = []
        self._raw_depth = 0
        self._skip_script = False

    def _emit_tag(self, tag, attrs):
        parts = [tag]
        for name, value in attrs:
            if value is None:
                parts.append(name)
            else:
                parts.append('%s="%s"' % (name, escape_attr(value)))
        self.out.append("<" + " ".join(parts) + ">")

    def handle_decl(self, decl):
        self.out.append("<!" + decl + ">")

    def handle_comment(self, data):
        pass  # comments are build noise, drop them

    def handle_starttag(self, tag, attrs):
        current = {name: (value if value is not None else "") for name, value in attrs}
        if tag == "link" and "stylesheet" in current.get("rel", "").split():
            href = current.get("href", "")
            if is_local_ref(href):
                css = minify_css(self.loader(href))
                if "</style" in css.lower():
                    fail("detected </style> inside minified %s" % href)
                media = current.get("media", "")
                extra = ' media="%s"' % escape_attr(media) if media else ""
                self.out.append("<style%s>%s</style>" % (extra, css))
                return
        if tag == "script" and is_local_ref(current.get("src", "")):
            if "async" in current or "defer" in current:
                pass  # exotic case, copy through and report below
            else:
                src = current["src"]
                js = minify_js(self.loader(src))
                if "</script" in js.lower():
                    fail("detected </script> inside minified %s" % src)
                self.out.append("<script>%s</script>" % js)
                self._skip_script = True  # swallow the original empty body
                return
        for key in ("href", "src"):
            if key in current and is_local_ref(current[key]):
                self.leftover_refs.append("%s %s=%s" % (tag, key, current[key]))
        if tag == "section" and current.get("id", "").startswith("page-"):
            self.section_ids.add(current["id"])
        self._emit_tag(tag, attrs)
        if tag in RAW_TEXT_ELEMENTS:
            self._raw_depth += 1

    def handle_startendtag(self, tag, attrs):
        # Self-closed form (<link ... />): same handling, void elements
        # never get an end tag.
        self.handle_starttag(tag, attrs)

    def handle_data(self, data):
        if self._skip_script:
            return
        if self._raw_depth > 0:
            self.out.append(data)
            return
        if data.strip() == "":
            return  # pure indentation between tags, drop it
        self.out.append(escape_text(data))

    def handle_endtag(self, tag):
        if tag == "script" and self._skip_script:
            self._skip_script = False
            return
        if tag in VOID_ELEMENTS:
            return  # stray end tag for a void element, ignore it
        if tag in RAW_TEXT_ELEMENTS and self._raw_depth > 0:
            self._raw_depth -= 1
        self.out.append("</" + tag + ">")


# ---------------------------------------------------------------------------
# Minimal scanner for the SECTIONS route table in app.js.
# Understands just enough JavaScript to pull out objects shaped like
#   { id: "status", pages: ["overview", ...] }
# and skips everything else. Raises ValueError on malformed input.
# ---------------------------------------------------------------------------

def _skip_ws_comments(text, i):
    n = len(text)
    while i < n:
        c = text[i]
        if c in " \t\r\n\v\f":
            i += 1
        elif c == "/" and i + 1 < n and text[i + 1] == "/":
            j = text.find("\n", i + 2)
            i = n if j < 0 else j + 1
        elif c == "/" and i + 1 < n and text[i + 1] == "*":
            j = text.find("*/", i + 2)
            if j < 0:
                return n
            i = j + 2
        else:
            break
    return i


def _parse_string(text, i):
    quote = text[i]
    i += 1
    buf = []
    n = len(text)
    while i < n:
        c = text[i]
        if c == "\\" and i + 1 < n:
            buf.append(text[i + 1])
            i += 2
            continue
        if c == quote:
            return "".join(buf), i + 1
        buf.append(c)
        i += 1
    raise ValueError("unterminated string literal")


def _parse_ident(text, i):
    j = i
    while j < len(text) and (text[j].isalnum() or text[j] in "_$"):
        j += 1
    if j == i:
        raise ValueError("expected identifier")
    return text[i:j], j


def _skip_value(text, i):
    """Skip one JavaScript value starting at i, return the index after it."""
    i = _skip_ws_comments(text, i)
    if i >= len(text):
        raise ValueError("unexpected end of input")
    c = text[i]
    if c in "\"'":
        _, i = _parse_string(text, i)
        return i
    if c in "[{":
        opener = c
        closer = "]" if c == "[" else "}"
        depth = 1
        i += 1
        while i < len(text) and depth > 0:
            ch = text[i]
            if ch in "\"'":
                _, i = _parse_string(text, i)
                continue
            if ch == "/" and i + 1 < len(text) and text[i + 1] in "/*":
                i = _skip_ws_comments(text, i)
                continue
            if ch == opener:
                depth += 1
            elif ch == closer:
                depth -= 1
            i += 1
        if depth != 0:
            raise ValueError("unbalanced brackets")
        return i
    while i < len(text) and text[i] not in ",}]":
        if text[i] in "\"'":
            _, i = _parse_string(text, i)
            continue
        i += 1
    return i


def _try_parse_route_object(text, i):
    """Parse one {...} at i. Return ((id, [pages]) or None, index after })."""
    j = _skip_ws_comments(text, i + 1)
    rid = None
    pages = None
    while True:
        j = _skip_ws_comments(text, j)
        if j < len(text) and text[j] == "}":
            return (None, j + 1) if rid is None or pages is None else ((rid, pages), j + 1)
        if j < len(text) and text[j] in "\"'":
            key, j = _parse_string(text, j)
        else:
            key, j = _parse_ident(text, j)
        j = _skip_ws_comments(text, j)
        if j >= len(text) or text[j] != ":":
            raise ValueError("not a route object")
        j = _skip_ws_comments(text, j + 1)
        if key == "id":
            if j >= len(text) or text[j] not in "\"'":
                raise ValueError("route id is not a string")
            rid, j = _parse_string(text, j)
        elif key == "pages":
            if j >= len(text) or text[j] != "[":
                raise ValueError("route pages is not an array")
            j += 1
            items = []
            while True:
                j = _skip_ws_comments(text, j)
                if j < len(text) and text[j] == "]":
                    j += 1
                    break
                if j >= len(text) or text[j] not in "\"'":
                    raise ValueError("route page is not a string")
                value, j = _parse_string(text, j)
                items.append(value)
                j = _skip_ws_comments(text, j)
                if j < len(text) and text[j] == ",":
                    j += 1
                elif j < len(text) and text[j] == "]":
                    j += 1
                    break
                else:
                    raise ValueError("malformed pages array")
            pages = items
        else:
            j = _skip_value(text, j)
        j = _skip_ws_comments(text, j)
        if j < len(text) and text[j] == ",":
            j += 1
        elif j < len(text) and text[j] == "}":
            if rid is None or pages is None:
                return None, j + 1
            return (rid, pages), j + 1
        else:
            raise ValueError("not a route object")


def _skip_template(text, i):
    """Skip a `...` template literal starting at i, return index after it."""
    assert text[i] == "`"
    i += 1
    n = len(text)
    while i < n:
        c = text[i]
        if c == "\\" and i + 1 < n:
            i += 2
            continue
        if c == "`":
            return i + 1
        if c == "$" and i + 1 < n and text[i + 1] == "{":
            i = _skip_value(text, i + 1)
            continue
        i += 1
    raise ValueError("unterminated template literal")


def find_routes(js):
    """Return [(section_id, [pages])] for every route object in the script."""
    routes = []
    i = 0
    n = len(js)
    while i < n:
        i = _skip_ws_comments(js, i)
        if i >= n:
            break
        c = js[i]
        if c in "\"'":
            try:
                _, i = _parse_string(js, i)
            except ValueError:
                i += 1
            continue
        if c == "`":
            try:
                i = _skip_template(js, i)
            except ValueError:
                i += 1
            continue
        if c != "{":
            i += 1
            continue
        try:
            found, j = _try_parse_route_object(js, i)
        except (ValueError, IndexError):
            i += 1  # not a route-shaped object, keep scanning
            continue
        if found is not None:
            routes.append(found)
        i = j
    return routes


def main():
    html = read_src("index.html")
    bundler = Bundler(read_src)
    bundler.feed(html)
    bundler.close()
    bundled = "".join(bundler.out)

    if bundler.leftover_refs:
        print("WARNING: uninlined local refs left: %s" % bundler.leftover_refs)

    # Every #/section/page route must resolve to a <section> id:
    # multi-page sections use page-<sec>-<page>, single-page use page-<sec>.
    routes = find_routes(read_src("app.js"))
    if not routes:
        fail("no routes found in app.js (SECTIONS table missing?)")
    bad = []
    for sec, pages in routes:
        if len(pages) > 1:
            wants = ["page-%s-%s" % (sec, page) for page in pages]
        else:
            wants = ["page-%s" % sec]
        for want in wants:
            if want not in bundler.section_ids:
                bad.append(want)
    if bad:
        fail("route/section mismatch: %s" % "; ".join(bad))

    data = bundled.encode("utf-8")
    gz = gzip.compress(data, compresslevel=9, mtime=0)
    os.makedirs(HTML_OUT_DIR, exist_ok=True)
    out = os.path.join(HTML_OUT_DIR, "index.html")
    with open(out, "w", encoding="utf-8", newline="") as f:
        f.write(bundled)
    os.makedirs(PAGE_DIR, exist_ok=True)
    page_gz = os.path.join(PAGE_DIR, "index.html.gz")
    with open(page_gz, "wb") as f:
        f.write(gz)
    print("html %6d -> gzip %6d (%4.1f%%)" % (len(data), len(gz), 100.0 * len(gz) / len(data)))
    print("wrote %s" % out)
    print("wrote %s" % page_gz)
    return 0


if __name__ == "__main__":
    sys.exit(main())
