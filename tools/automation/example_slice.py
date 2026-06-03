"""End-to-end smoke test: launch OrcaSlicer with the automation server, load a
model, slice it, wait for completion, and save a window PNG.

Run:
    python example_slice.py --orca /path/to/OrcaSlicer --model /path/to/cube.stl

On Linux CI, wrap with a virtual display, e.g.:
    xvfb-run -a python example_slice.py --orca ./OrcaSlicer --model cube.stl
"""
from __future__ import annotations
import argparse
import subprocess
import sys
import time

from orca_automation import OrcaClient, OrcaError


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--orca", required=True, help="path to the OrcaSlicer executable")
    ap.add_argument("--model", required=True, help="path to an STL/3MF to load")
    ap.add_argument("--port", type=int, default=13619)
    args = ap.parse_args()

    proc = subprocess.Popen([
        args.orca,
        "--automation-server",
        f"--automation-server-port={args.port}",
    ])
    try:
        orca = OrcaClient(port=args.port)

        # Wait for the server to come up.
        for _ in range(60):
            try:
                print("connected:", orca.version())
                break
            except OSError:
                time.sleep(0.5)
        else:
            print("ERROR: automation server did not start", file=sys.stderr)
            return 1

        # Switch to the Prepare (3D editor) view first, then load the model into the
        # already-running instance. file.open is synchronous, so project_loaded is
        # already true on return; the wait below is a belt-and-suspenders guard.
        orca.select_view("prepare")
        orca.open([args.model])
        deadline = time.time() + 30
        while time.time() < deadline:
            if orca.app_state().get("project_loaded"):
                break
            time.sleep(0.5)

        # Click Slice and wait for the Export button to become enabled
        # (slicing complete) — wait_for replaces fragile fixed sleeps.
        orca.click({"id": "btn_slice"})
        orca.wait_for({"id": "btn_export"}, state="enabled", timeout_ms=180000,
                      poll_ms=500)

        # The window screenshot is captured from the on-screen composited
        # framebuffer, so it already includes the 3D viewport (model in the
        # editor, or toolpaths in Preview after slicing).
        with open("window.png", "wb") as f:
            f.write(orca.screenshot())
        print("wrote window.png")
        return 0
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=10)
        except subprocess.TimeoutExpired:
            proc.kill()


if __name__ == "__main__":
    raise SystemExit(main())
