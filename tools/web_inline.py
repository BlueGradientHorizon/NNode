#!/usr/bin/env python3
"""Bundle the NNode web UI into a single self-contained HTML file.

Pipeline (manifest mode):
  src_web/index.html (shell with <!-- @include <page> --> directives)
    + src_web/<sec>/<page>/<page>.html partials
    + src_web/*.js and src_web/<sec>/**/*.js in manifest order
  -> expand includes
  -> inline local <link rel="stylesheet"> and <script src> references
     (contents minified with rcssmin / rjsmin, JS combined into one
     <script> wrapped in a single IIFE so fragments share closure scope)
  -> validate that every route from the SECTIONS table in routes_from
     resolves to a <section> id in the markup
  -> drop comments and whitespace-only text nodes
  -> ../src_web/inlined/index.html (handy for layout checks
     in a desktop browser)
  -> ../main/page/index.html.gz (gzip -9, deterministic;
     the copy baked into the firmware)

Legacy mode (no src_web/manifest.json): old flat layout with
  index.html + style.css + i18n.js + app.js, one <script> per file,
  routes scanned from app.js. Kept so the rewrite can land incrementally.

Only the .gz file is baked into the firmware (see EMBED_FILES in
main/CMakeLists.txt) and served with Content-Encoding: gzip.

Parsing is done with html.parser plus a small route scanner in
web_routes.py. No regular expressions are used for HTML on purpose:
the previous regex-based builder broke silently on multiline tags and
on ">" characters inside quoted attribute values. Includes use plain
string search for the same reason.

Dependencies: pip install rjsmin rcssmin
"""
import argparse
import gzip
import json
import os
import sys
from html.parser import HTMLParser

try:
    from web_routes import find_routes
except ImportError:  # allow `python3 tools/web_inline.py` from any cwd
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    from web_routes import find_routes

HERE = os.path.dirname(os.path.abspath(__file__))
WEB_SRC = os.path.normpath(os.path.join(HERE, "..", "src_web"))
HTML_OUT_DIR = os.path.join(WEB_SRC, "inlined")
PAGE_DIR = os.path.normpath(os.path.join(HERE, "..", "main", "page"))
MANIFEST = os.path.join(WEB_SRC, "manifest.json")

VOID_ELEMENTS = frozenset([
    "area", "base", "br", "col", "embed", "hr", "img", "input",
    "link", "meta", "param", "source", "track", "wbr",
])
# Elements whose text must be copied verbatim, never whitespace-folded.
RAW_TEXT_ELEMENTS = frozenset(["script", "style", "pre", "textarea"])

INCLUDE_OPEN = "<!-- @include"
INCLUDE_CLOSE = "-->"


def fail(msg):
    sys.exit("web_inline.py: error: %s" % msg)


def read_src(name):
    path = os.path.join(WEB_SRC, name)
    if not os.path.isfile(path):
        fail("missing source file: %s" % path)
    with open(path, "r", encoding="utf-8") as f:
        return f.read()


def minify_css(text, dev=False):
    if dev:
        return text
    try:
        import rcssmin
    except ImportError:
        fail("need rcssmin: pip install rcssmin")
    return rcssmin.cssmin(text)


def minify_js(text, dev=False):
    if dev:
        return text
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


def expand_includes(html, seen=None):
    """Replace <!-- @include <relpath> --> with the file content.

    Paths are relative to src_web. Includes nest; cycles fail.
    Plain string search only, no regex.
    """
    if seen is None:
        seen = []
    out = []
    pos = 0
    while True:
        beg = html.find(INCLUDE_OPEN, pos)
        if beg < 0:
            out.append(html[pos:])
            break
        end = html.find(INCLUDE_CLOSE, beg)
        if end < 0:
            fail("unterminated @include directive")
        directive = html[beg:end + len(INCLUDE_CLOSE)]
        inner = directive[len(INCLUDE_OPEN):-len(INCLUDE_CLOSE)].strip()
        if not inner:
            fail("empty @include directive")
        if inner in seen:
            fail("include cycle: %s" % " -> ".join(seen + [inner]))
        inc_path = os.path.join(WEB_SRC, inner)
        if not os.path.isfile(inc_path):
            fail("include not found: %s" % inc_path)
        with open(inc_path, "r", encoding="utf-8") as f:
            content = f.read()
        content = expand_includes(content, seen + [inner])
        out.append(html[pos:beg])
        out.append(content)
        pos = end + len(INCLUDE_CLOSE)
    return "".join(out)


def list_includes(shell_html):
    """Return [@include paths] in order of appearance in the shell."""
    found = []
    pos = 0
    while True:
        beg = shell_html.find(INCLUDE_OPEN, pos)
        if beg < 0:
            return found
        end = shell_html.find(INCLUDE_CLOSE, beg)
        if end < 0:
            fail("unterminated @include directive")
        inner = shell_html[beg + len(INCLUDE_OPEN):end].strip()
        found.append(inner)
        pos = end + len(INCLUDE_CLOSE)


class Bundler(HTMLParser):
    """Single pass over HTML: inline assets, collect section ids.

    With combine_js=True all local scripts are merged into one <script>
    at the position of the first script tag, wrapped in a single IIFE
    by the caller (see combined_script). Legacy mode emits one
    <script> per file.
    """

    def __init__(self, loader, combine_js=False, dev=False, sizes=None):
        super().__init__(convert_charrefs=True)
        self.loader = loader
        self.combine_js = combine_js
        self.dev = dev
        self.sizes = sizes if sizes is not None else {}
        self.out = []
        self.section_ids = set()
        self.leftover_refs = []
        self._raw_depth = 0
        self._skip_script = False
        self.js_parts = []  # [(src, code)] in encounter order
        self.js_slot = None  # index in self.out for the combined script

    def combined_script(self):
        body = "\n".join(code for _, code in self.js_parts)
        return "<script>(function () {\n%s\n})();</script>" % body

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
                raw = self.loader(href)
                css = minify_css(raw, self.dev)
                if "</style" in css.lower():
                    fail("detected </style> inside minified %s" % href)
                self.sizes[href] = (len(raw.encode("utf-8")),
                                    len(css.encode("utf-8")))
                media = current.get("media", "")
                extra = ' media="%s"' % escape_attr(media) if media else ""
                self.out.append("<style%s>%s</style>" % (extra, css))
                return
        if tag == "script" and is_local_ref(current.get("src", "")):
            if "async" in current or "defer" in current:
                pass  # exotic case, copy through and report below
            else:
                src = current["src"]
                raw = self.loader(src)
                js = minify_js(raw, self.dev)
                if "</script" in js.lower():
                    fail("detected </script> inside minified %s" % src)
                self.sizes[src] = (len(raw.encode("utf-8")),
                                   len(js.encode("utf-8")))
                if self.combine_js:
                    self.js_parts.append((src, js))
                    if self.js_slot is None:
                        self.js_slot = len(self.out)
                        self.out.append(None)  # placeholder, filled after parse
                    self._skip_script = True  # swallow the original empty body
                    return
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


def load_manifest():
    if not os.path.isfile(MANIFEST):
        return None
    with open(MANIFEST, "r", encoding="utf-8") as f:
        try:
            data = json.load(f)
        except ValueError as e:
            fail("bad manifest.json: %s" % e)
    for key in ("shell", "css", "js", "pages"):
        if key not in data:
            fail("manifest.json missing key: %s" % key)
    return data


def shell_script_srcs(shell_html):
    """Local script srcs in shell order (string search, no regex)."""
    srcs = []
    pos = 0
    while True:
        i = shell_html.find("<script", pos)
        if i < 0:
            return srcs
        j = shell_html.find(">", i)
        if j < 0:
            fail("unterminated <script> tag in shell")
        tag = shell_html[i:j + 1]
        # find src="..." with either quote style
        for q in ('"', "'"):
            key = "src=" + q
            k = tag.find(key)
            if k >= 0:
                m = tag.find(q, k + len(key))
                if m >= 0:
                    srcs.append(tag[k + len(key):m])
                break
        pos = j + 1


def check_manifest(manifest, shell_html):
    want_js = manifest["js"]
    got_js = [s for s in shell_script_srcs(shell_html) if is_local_ref(s)]
    if got_js != want_js:
        fail("shell scripts != manifest js:\n shell: %s\n manifest: %s"
             % (got_js, want_js))
    want_pages = manifest["pages"]
    got_pages = list_includes(shell_html)
    if got_pages != want_pages:
        fail("shell includes != manifest pages:\n shell: %s\n manifest: %s"
             % (got_pages, want_pages))
    routes_from = manifest.get("routes_from", "router.js")
    if routes_from not in want_js:
        fail("routes_from %s not in manifest js" % routes_from)
    return routes_from


def main(argv=None):
    ap = argparse.ArgumentParser(description="Bundle the NNode web UI.")
    ap.add_argument("--dev", action="store_true",
                    help="skip minification (larger output, easier to debug)")
    ap.add_argument("--check", action="store_true",
                    help="validate only, write nothing")
    ap.add_argument("--verbose", action="store_true",
                    help="print per-file sizes")
    args = ap.parse_args(argv)

    manifest = load_manifest()
    if manifest is None:
        shell_name = "index.html"
        routes_name = "app.js"
        combine = False
        routes_from = routes_name
    else:
        shell_name = manifest["shell"]
        with open(os.path.join(WEB_SRC, shell_name), "r",
                  encoding="utf-8") as f:
            shell_raw = f.read()
        routes_from = check_manifest(manifest, shell_raw)
        combine = True

    shell_html = read_src(shell_name)
    html = expand_includes(shell_html)
    sizes = {}
    bundler = Bundler(read_src, combine_js=combine, dev=args.dev,
                       sizes=sizes)
    bundler.feed(html)
    bundler.close()
    if combine and bundler.js_parts:
        bundler.out[bundler.js_slot] = bundler.combined_script()
    bundled = "".join(x for x in bundler.out if x is not None)

    if bundler.leftover_refs:
        print("WARNING: uninlined local refs left: %s" % bundler.leftover_refs)

    # Every #/section/page route must resolve to a <section> id:
    # multi-page sections use page-<sec>-<page>, single-page use page-<sec>.
    try:
        routes_js = read_src(routes_from)
    except SystemExit:
        raise
    routes = find_routes(routes_js)
    if not routes and manifest is not None:
        # Fallback while migrating: scan the whole bundle.
        routes = find_routes("\n".join(code for _, code in bundler.js_parts))
    if not routes:
        fail("no routes found in %s (SECTIONS table missing?)" % routes_from)
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
    if args.verbose or manifest is not None:
        for name in sorted(sizes):
            raw, out = sizes[name]
            pct = (100.0 * out / raw) if raw else 0.0
            print(" %-40s %6d -> %6d (%4.1f%%)" % (name, raw, out, pct))
    print("html %6d -> gzip %6d (%4.1f%%)" % (len(data), len(gz),
                                              100.0 * len(gz) / len(data)))
    if args.check:
        print("check ok: %d routes, %d sections"
              % (len(routes), len(bundler.section_ids)))
        return 0
    os.makedirs(HTML_OUT_DIR, exist_ok=True)
    out = os.path.join(HTML_OUT_DIR, "index.html")
    with open(out, "w", encoding="utf-8", newline="") as f:
        f.write(bundled)
    os.makedirs(PAGE_DIR, exist_ok=True)
    page_gz = os.path.join(PAGE_DIR, "index.html.gz")
    with open(page_gz, "wb") as f:
        f.write(gz)
    print("wrote %s" % out)
    print("wrote %s" % page_gz)
    return 0


if __name__ == "__main__":
    sys.exit(main())
