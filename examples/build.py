#!/usr/bin/env python3
"""Build (and optionally upload) a loadable ELF module from examples/<app>/.

Uses only shared root dirs: boards/<target>/ for Kconfig defaults,
build-<target>/modules/<app>/ for output and generated config, and
managed_components/ for the loader component (reused, never downloaded
again). Nothing is generated inside examples/ except the committable
dependencies.lock (same policy as the root one).

Usage: build.py <target> <action> <app> [ip]

  target  board in ../boards/<target>/ (e.g. esp32c3)
  action  build|upload|upload-run|clean
  app     module in examples/<app>/ (e.g. blink)
  ip      board address, required for upload and upload-run (e.g. 192.168.1.241)

Modules are never flashed over serial: "upload" sends the .app.elf to the
board over HTTP without starting it, "upload-run" additionally starts it
via /api/elf/run.
"""
import argparse
import json
import os
import shutil
import subprocess
import sys
import urllib.error
import urllib.request

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")


def load_idf_env():
    """Import the IDF export.sh environment (IDF_PATH or ~/esp-idf)."""
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


def check_defaults(src):
    """Fail fast on a bad config.defaults (full check in tools/)."""
    defaults = os.path.join(src, "config.defaults")
    if not os.path.isfile(defaults):
        print("no config.defaults: module has no settings")
        return None
    val = os.path.join(ROOT, "tools", "nnode_cfg_validate.py")
    r = subprocess.run([sys.executable, val, defaults])
    if r.returncode != 0:
        sys.exit("config.defaults invalid, fix it first")
    return defaults


def embed_defaults(defaults, elf, target):
    """Inject config.defaults verbatim as the .nnode_default section."""
    if not defaults:
        return
    objcopy = None
    # Board metadata wins when present, probe is the fallback.
    board_json = os.path.join(ROOT, "boards", target, "board.json")
    try:
        with open(board_json, encoding="utf-8") as f:
            prefix = json.load(f).get("toolchain_prefix")
        if prefix:
            objcopy = shutil.which(prefix + "-objcopy")
    except (OSError, ValueError):
        objcopy = None
    objcopy = objcopy or shutil.which("riscv32-esp-elf-objcopy") or \
        shutil.which("xtensa-esp-elf-objcopy")
    if not objcopy:
        sys.exit("objcopy not on PATH (IDF env missing?)")
    # Incremental builds relink only on change, so the section may
    # already be there from the previous run: drop it first.
    subprocess.run([objcopy, "--remove-section", ".nnode_default", elf],
                   capture_output=True)
    run([objcopy, "--add-section", ".nnode_default=%s" % defaults,
         "--set-section-flags", ".nnode_default=readonly,data", elf])
    print("embedded %s as .nnode_default" % defaults)


def api(ip, method, path, data=None):
    """POST/GET the board API. Returns the response body as text."""
    url = f"http://{ip}{path}"
    req = urllib.request.Request(url, data=data, method=method)
    if data is not None:
        req.add_header("Content-Type", "application/octet-stream")
    try:
        with urllib.request.urlopen(req, timeout=30) as resp:
            return resp.read().decode("utf-8", "replace")
    except urllib.error.HTTPError as e:
        # The board reports errors as JSON with a non-200 status.
        return e.read().decode("utf-8", "replace")
    except urllib.error.URLError as e:
        sys.exit(f"board {ip} unreachable: {e}")


def require_ok(body, what):
    if '"ok":true' not in body:
        sys.exit(f"{what} failed: {body}")


def main():
    ap = argparse.ArgumentParser(
        description="Build/upload a loadable NNode ELF module.")
    ap.add_argument("target", help="board in boards/<target>/ (e.g. esp32c3)")
    ap.add_argument(
        "action",
        choices=["build", "upload", "upload-run", "clean"],
        help="what to do")
    ap.add_argument("app", help="module in examples/<app>/ (e.g. blink)")
    ap.add_argument("ip", nargs="?", default=None,
                    help="board address, required for upload and upload-run")
    args = ap.parse_args()

    if args.action in ("upload", "upload-run") and not args.ip:
        ap.error("upload needs <ip> (e.g. 192.168.1.241)")
    if args.action not in ("upload", "upload-run") and args.ip:
        ap.error("<ip> only makes sense for upload")

    os.chdir(ROOT)
    load_idf_env()

    target, action, app = args.target, args.action, args.app
    src = os.path.join("examples", app)
    out = os.path.join(f"build-{target}", "modules", app)
    board_dir = os.path.join("boards", target)

    if not os.path.isdir(board_dir):
        sys.exit(f"unknown target '{target}' (no {board_dir} directory)")
    if not os.path.isdir(src):
        sys.exit(f"unknown app '{app}' (no {src} directory)")

    if action == "clean":
        shutil.rmtree(out, ignore_errors=True)
        return

    defaults = check_defaults(src)

    # Reuse the loader component from the root managed_components.
    extra_loader = " ".join([
        os.path.join(ROOT, "managed_components", "elf_loader"),
        os.path.join(ROOT, "managed_components", "espressif__cmake_utilities"),
    ])

    def idf_extra(*a):
        run(["idf.py", "-C", src, "-B", out,
             "-D", f"SDKCONFIG={os.path.join(ROOT, out, 'sdkconfig')}",
             "-D", f"EXTRA_COMPONENT_DIRS={extra_loader}",
             "-D", f"BOARD_DIR={os.path.join(ROOT, board_dir)}", *a])

    def idf_mod(*a):
        run(["idf.py", "-C", src, "-B", out,
             "-D", f"SDKCONFIG={os.path.join(ROOT, out, 'sdkconfig')}",
             "-D", f"SDKCONFIG_DEFAULTS={module_defaults}",
             "-D", f"EXTRA_COMPONENT_DIRS={extra_loader}",
             "-D", f"BOARD_DIR={os.path.join(ROOT, board_dir)}", *a])

    if not os.path.isfile(os.path.join(out, "sdkconfig")):
        # Fresh configure in two steps: set-target refuses to fullclean a
        # directory it did not create, so target selection runs first on
        # an empty dir, then the shared defaults are applied on the build.
        shutil.rmtree(out, ignore_errors=True)
        idf_extra("set-target", target)
        os.remove(os.path.join(out, "sdkconfig"))

    # Shared Kconfig base: root defaults + board defaults. The board
    # partition filename is project-relative, but our project lives in
    # examples/<app>/, so pin the absolute shared path in a generated
    # override (inside the build dir, never in examples/).
    module_defaults = (
        f"{ROOT}/sdkconfig.defaults;{ROOT}/{board_dir}/sdkconfig.defaults"
    )
    partitions = os.path.join(board_dir, "partitions.csv")
    if os.path.isfile(partitions):
        os.makedirs(out, exist_ok=True)
        with open(os.path.join(out, "sdkconfig.defaults"), "w",
                  encoding="utf-8") as f:
            f.write("# Generated by examples/build.py: "
                    "absolute path to the shared board file.\n"
                    f'CONFIG_PARTITION_TABLE_CUSTOM_FILENAME='
                    f'"{ROOT}/{partitions}"\n')
        module_defaults += f";{ROOT}/{out}/sdkconfig.defaults"

    idf_mod("build")
    elf = os.path.join(out, f"{app}.app.elf")
    embed_defaults(defaults, elf, target)
    print(f"{os.path.getsize(elf)} bytes: {elf}")

    if action in ("upload", "upload-run"):
        with open(elf, "rb") as f:
            body = api(args.ip, "POST",
                       f"/api/files/upload?name=programs/{app}.elf", f.read())
        print("upload:", body)
        require_ok(body, "upload")
        if action == "upload-run":
            # Replace a running copy: stop/unload errors are fine when idle.
            body = api(args.ip, "POST", f"/api/elf/stop?name={app}.elf")
            print("stop:", body)
            body = api(args.ip, "POST", f"/api/elf/unload?name={app}.elf")
            print("unload:", body)
            body = api(args.ip, "POST", f"/api/elf/run?name={app}.elf")
            print("run:", body)
            require_ok(body, "run")


if __name__ == "__main__":
    main()
