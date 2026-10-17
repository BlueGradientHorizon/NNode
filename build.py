#!/usr/bin/env python3
"""Build (and optionally flash) the nnode firmware for a given board target.

Usage: build.py <target> <action>

  target  board in boards/<target>/ (e.g. esp32c3)
  action  build|flash|monitor|clean

What it does:
  1. src_web/* --web_inline.py--> src_web/inlined/index.html
     + main/page/index.html.gz (the whole SPA in one file,
     page copy baked into firmware via EMBED_FILES)
  2. idf.py build|flash in build-<target>/ with
     boards/<target>/sdkconfig (ESP-IDF v6.1)

Never runs erase_flash: the storage partition (/data) must survive reflashes.
"""
import argparse
import json
import os
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.abspath(__file__))


def load_idf_env():
    """Import the IDF export.sh environment (IDF_PATH or ~/esp-idf).

    Returns the export.sh path used, or None when no IDF install was
    found (idf.py is then expected on PATH already).
    """
    candidates = []
    if os.environ.get("IDF_PATH"):
        candidates.append(os.path.join(os.environ["IDF_PATH"], "export.sh"))
    candidates.append(os.path.expanduser("~/esp-idf/export.sh"))
    for sh in candidates:
        if os.path.isfile(sh):
            cmd = (
                f'. "{sh}" >/dev/null 2>&1 && '
                'exec python3 -c "import json,os;print(json.dumps(dict(os.environ)))"'
            )
            try:
                out = subprocess.run(
                    ["bash", "-c", cmd],
                    capture_output=True, text=True, check=True,
                )
            except (OSError, subprocess.CalledProcessError):
                return None
            os.environ.update(json.loads(out.stdout))
            return sh
    return None


def run(cmd):
    subprocess.run(cmd, check=True)


def git_sha():
    """Short HEAD hash, or None when git cannot tell.

    CI checkouts (shallow, container, dubious ownership) often fail
    here even though the commit is known to the runner.
    """
    try:
        out = subprocess.run(
            ["git", "rev-parse", "--short", "HEAD"],
            capture_output=True, text=True, check=True,
        ).stdout.strip()
    except (OSError, subprocess.CalledProcessError):
        return None
    return out or None


def ci_sha():
    """Commit hash from the CI environment, for checkouts git cannot read."""
    sha = os.environ.get("GITHUB_SHA", "").strip()
    return sha[:7] if sha else None


def git_dirty():
    try:
        out = subprocess.run(
            ["git", "status", "--porcelain"],
            capture_output=True, text=True, check=True,
        ).stdout.strip()
    except (OSError, subprocess.CalledProcessError):
        return False
    return bool(out)


def project_version():
    """Compose '<version> <short-hash>[-dirty]' for the app descriptor.

    The base version comes from the VERSION file (first token). The hash
    is fresh HEAD on every build, with -dirty when the tree has changes.
    Falls back to the CI commit, then to the bare version, when git is
    unavailable.
    """
    try:
        with open("VERSION", encoding="utf-8") as f:
            ver = f.read().strip().split()[0]
    except (OSError, IndexError):
        ver = "0.0.0"
    sha = git_sha() or ci_sha()
    if not sha:
        return ver
    return f"{ver} {sha}{'-dirty' if git_dirty() else ''}"


def main():
    ap = argparse.ArgumentParser(description="Build/flash the nnode firmware.")
    ap.add_argument("target", help="board in boards/<target>/ (e.g. esp32c3)")
    ap.add_argument(
        "action", choices=["build", "flash", "monitor", "clean"],
        help="what to do",
    )
    args = ap.parse_args()

    os.chdir(ROOT)
    load_idf_env()

    board_dir = os.path.join("boards", args.target)
    build_dir = f"build-{args.target}"
    sdkconfig = os.path.join(board_dir, "sdkconfig")
    defaults = f"sdkconfig.defaults;{board_dir}/sdkconfig.defaults"
    port = os.environ.get("PORT", "/dev/ttyACM0")

    if not os.path.isdir(board_dir):
        sys.exit(f"unknown target '{args.target}' (no {board_dir} directory)")

    # Fresh on every build: -DPROJECT_VER wins over IDF's own sources
    # (version.txt, project() VERSION, git describe), so the app
    # descriptor always carries the current commit.
    version = project_version()
    print(f"nnode version: {version}", flush=True)

    def idf(*a):
        run(["idf.py", "-B", build_dir,
             "-D", f"SDKCONFIG={sdkconfig}",
             "-D", f"SDKCONFIG_DEFAULTS={defaults}",
             "-D", f"PROJECT_VER={version}", *a])

    if args.action in ("build", "flash"):
        run(["python3", "tools/web_inline.py"])
        if not os.path.isfile(sdkconfig):
            idf("set-target", args.target)
        idf("build")
        if args.action == "flash":
            idf("-p", port, "flash")
    elif args.action == "monitor":
        idf("-p", port, "monitor")
    elif args.action == "clean":
        shutil.rmtree(build_dir, ignore_errors=True)


if __name__ == "__main__":
    main()
