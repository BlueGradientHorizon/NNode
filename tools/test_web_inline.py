#!/usr/bin/env python3
"""Smoke tests for tools/web_inline.py (run from NNode/: python3 tools/test_web_inline.py)."""
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, ".."))
WEB_SRC = os.path.join(ROOT, "src_web")

sys.path.insert(0, HERE)
from web_routes import find_routes
from web_inline import Bundler, expand_includes


def section_ids():
    with open(os.path.join(WEB_SRC, "index.html"), encoding="utf-8") as f:
        shell = f.read()
    html = expand_includes(shell)
    got = set()

    class Collector(__import__("html.parser", fromlist=["HTMLParser"]).HTMLParser):
        def handle_starttag(self, tag, attrs):
            cur = dict(attrs)
            if tag == "section" and cur.get("id", "").startswith("page-"):
                got.add(cur["id"])

    c = Collector(convert_charrefs=True)
    c.feed(html)
    c.close()
    return got


def routes():
    with open(os.path.join(WEB_SRC, "router.js"), encoding="utf-8") as f:
        return find_routes(f.read())


def wants(sec, pages):
    if len(pages) > 1:
        return ["page-%s-%s" % (sec, p) for p in pages]
    return ["page-%s" % sec]


def test_ok_build():
    r = subprocess.run([sys.executable, os.path.join(HERE, "web_inline.py"),
                        "--check"], capture_output=True, text=True, cwd=ROOT)
    assert r.returncode == 0, r.stderr + r.stdout
    assert "check ok" in r.stdout, r.stdout


def test_route_section_match():
    secs = section_ids()
    assert len(secs) == 15, secs
    bad = [w for sec, pages in routes() for w in wants(sec, pages)
           if w not in secs]
    assert not bad, bad
    # negative: a fabricated route must be reported missing
    bad_routes = [("status", ["overview", "nope"])]
    missing = [w for sec, pages in bad_routes for w in wants(sec, pages)
               if w not in secs]
    assert missing == ["page-status-nope"], missing


def test_script_guard():
    def loader(name):
        assert name == "evil.js"
        return 'var x = "</ScRiPt>";'
    b = Bundler(loader, combine_js=True)
    try:
        b.feed('<html><body><script src="evil.js"></script></body></html>')
        b.close()
    except SystemExit as e:
        assert "evil.js" in str(e), str(e)
        return
    raise AssertionError("Bundler accepted </script> inside JS")


if __name__ == "__main__":
    test_ok_build()
    test_route_section_match()
    test_script_guard()
    print("web_inline tests: 3 passed")
