#!/usr/bin/env python3
"""
Local firmware flasher server for MURAL.

Serves flasher.html and the PlatformIO-built binaries over HTTP so the
Web Serial flasher page can flash the ESP32 without any cloud dependency.

Usage:
    python3 flasher/serve.py            # serve only (binaries must be pre-built)
    python3 flasher/serve.py --build    # build firmware first, then serve
    python3 flasher/serve.py --port 9090

Requirements:
    - PlatformIO CLI  (pio)  in PATH  — install via VS Code PlatformIO extension
    - Node.js  in PATH                — needed by the TypeScript pre-build step
"""

import argparse
import glob
import http.server
import json
import os
import subprocess
import sys
import webbrowser
from pathlib import Path
from socketserver import ThreadingMixIn
from threading import Timer

# ---------------------------------------------------------------------------
# Paths
# ---------------------------------------------------------------------------

# Project root is one level above this script (MURAL/)
PROJECT_ROOT = Path(__file__).resolve().parent.parent
BUILD_DIR    = PROJECT_ROOT / ".pio" / "build" / "esp32dev"
FLASHER_HTML = Path(__file__).resolve().parent / "flasher.html"

DEFAULT_PORT = 8080

# Flash layout (must match partitions.csv + ESP32 bootloader convention)
FLASH_FILES = [
    {"name": "bootloader.bin", "address": 0x1000},
    {"name": "partitions.bin", "address": 0x8000},
    {"name": "boot_app0.bin",  "address": 0xE000},
    {"name": "firmware.bin",   "address": 0x10000},
    {"name": "littlefs.bin",   "address": 0x13C000},
]

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def find_boot_app0() -> Path | None:
    """Locate boot_app0.bin inside PlatformIO's cached ESP32 framework."""
    patterns = [
        "~/.platformio/packages/framework-arduinoespressif32*/tools/partitions/boot_app0.bin",
        "~/.platformio/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin",
    ]
    for pat in patterns:
        matches = sorted(glob.glob(os.path.expanduser(pat)))
        if matches:
            return Path(matches[-1])   # pick latest version
    return None


def get_binary_paths() -> dict[str, Path | None]:
    boot_app0 = find_boot_app0()
    return {
        "bootloader.bin": BUILD_DIR / "bootloader.bin",
        "partitions.bin": BUILD_DIR / "partitions.bin",
        "boot_app0.bin":  boot_app0,
        "firmware.bin":   BUILD_DIR / "firmware.bin",
        "littlefs.bin":   BUILD_DIR / "littlefs.bin",
    }


def build_firmware(log=print) -> bool:
    """Run PlatformIO build for firmware + filesystem. Returns True on success."""
    log("── Building firmware (pio run) ──")
    r = subprocess.run(["pio", "run"], cwd=PROJECT_ROOT)
    if r.returncode != 0:
        log("ERROR: Firmware build failed.")
        return False

    log("── Building filesystem (pio run --target buildfs) ──")
    r = subprocess.run(["pio", "run", "--target", "buildfs"], cwd=PROJECT_ROOT)
    if r.returncode != 0:
        log("ERROR: Filesystem build failed.")
        return False

    log("── Build complete ──")
    return True


# ---------------------------------------------------------------------------
# HTTP handler
# ---------------------------------------------------------------------------

class FlasherHandler(http.server.BaseHTTPRequestHandler):

    # Shared binary path map, set before starting the server
    binary_paths: dict[str, Path | None] = {}

    # ── routing ─────────────────────────────────────────────────────────────

    def do_GET(self):
        if self.path in ("/", "/flasher.html"):
            self._serve_file(FLASHER_HTML, "text/html; charset=utf-8")

        elif self.path == "/status":
            payload = {}
            for entry in FLASH_FILES:
                name = entry["name"]
                p = self.binary_paths.get(name)
                payload[name] = {
                    "found": bool(p and Path(p).exists()),
                    "path":  str(p) if p else None,
                }
            self._send_json(payload)

        elif self.path.startswith("/firmware/"):
            fname = self.path[len("/firmware/"):]
            p = self.binary_paths.get(fname)
            if p and Path(p).exists():
                self._serve_file(Path(p), "application/octet-stream")
            else:
                self.send_error(404, f"Binary not found: {fname}")

        else:
            self.send_error(404)

    def do_POST(self):
        if self.path == "/build":
            success = build_firmware()
            # Refresh binary paths after build
            self.__class__.binary_paths = get_binary_paths()
            self._send_json({"ok": success,
                             "message": "Build complete" if success else "Build failed — check terminal"})
        else:
            self.send_error(404)

    def do_OPTIONS(self):
        self.send_response(200)
        self._cors()
        self.end_headers()

    # ── helpers ──────────────────────────────────────────────────────────────

    def _serve_file(self, path: Path, content_type: str):
        try:
            data = path.read_bytes()
            self.send_response(200)
            self.send_header("Content-Type", content_type)
            self.send_header("Content-Length", str(len(data)))
            self._cors()
            self.end_headers()
            self.wfile.write(data)
        except FileNotFoundError:
            self.send_error(404, f"File not found: {path}")

    def _send_json(self, obj):
        data = json.dumps(obj).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(data)))
        self._cors()
        self.end_headers()
        self.wfile.write(data)

    def _cors(self):
        self.send_header("Access-Control-Allow-Origin",  "*")
        self.send_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS")
        self.send_header("Access-Control-Allow-Headers", "*")

    def log_message(self, fmt, *args):
        # Only log errors, suppress normal 200s to keep the terminal clean
        if args and str(args[1]) not in ("200", "204"):
            super().log_message(fmt, *args)


class ThreadingHTTPServer(ThreadingMixIn, http.server.HTTPServer):
    """Multi-threaded HTTP server — keeps serving while a build runs."""
    daemon_threads = True


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(
        description="MURAL local firmware flasher server",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__,
    )
    parser.add_argument("--build", action="store_true",
                        help="Build firmware + filesystem before starting the server")
    parser.add_argument("--port", type=int, default=DEFAULT_PORT,
                        help=f"Port to listen on (default: {DEFAULT_PORT})")
    args = parser.parse_args()

    # Optional pre-build
    if args.build:
        ok = build_firmware()
        if not ok:
            sys.exit(1)

    # Resolve binary paths
    binary_paths = get_binary_paths()
    FlasherHandler.binary_paths = binary_paths

    # Print binary status
    print()
    all_ready = True
    for entry in FLASH_FILES:
        name = entry["name"]
        p = binary_paths.get(name)
        if p and Path(p).exists():
            print(f"  ✓  {name}")
        else:
            print(f"  ✗  {name}  ← not found")
            all_ready = False

    if not all_ready:
        print()
        if any(not (binary_paths.get(e["name"]) and Path(binary_paths[e["name"]]).exists())
               for e in FLASH_FILES if e["name"] != "boot_app0.bin"):
            print("  Tip: run with --build to compile first, or run 'pio run' manually.")
        if not binary_paths.get("boot_app0.bin"):
            print("  Tip: boot_app0.bin is missing — run 'pio run' once to download the")
            print("       ESP32 framework; it will be cached in ~/.platformio/packages/.")

    url = f"http://localhost:{args.port}"
    print(f"\n  Flasher ready → {url}")
    print("  Press Ctrl+C to stop.\n")

    Timer(1.2, lambda: webbrowser.open(url)).start()

    with ThreadingHTTPServer(("", args.port), FlasherHandler) as httpd:
        try:
            httpd.serve_forever()
        except KeyboardInterrupt:
            print("\nStopped.")


if __name__ == "__main__":
    main()
