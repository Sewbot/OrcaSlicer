#!/usr/bin/env python3
"""
Convenience script: runs the codegen pipeline.

1. Compile .proto -> binary descriptor set (protoc)
2. Generate C++ from descriptors (config_codegen.py)
3. Validate output against original

Usage:
    python tools/run_codegen.py                 # full pipeline
    python tools/run_codegen.py --validate-only # just validate
"""

import argparse
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
PROTO_DIR = ROOT / "src" / "PrintConfigs"
CODEGEN_OUT = ROOT / "src" / "slic3r" / "GUI" / "generated"
DESC_FILE = ROOT / "config.desc"
LAYOUT_YAML = PROTO_DIR / "layout.yaml"


def _ensure_pyyaml():
    """Install pyyaml if not present — needed for tab layout generation."""
    try:
        import yaml  # noqa: F401
        return True
    except ImportError:
        print("  Installing pyyaml (required for tab layout generation)...")
        result = subprocess.run(
            [sys.executable, "-m", "pip", "install", "pyyaml", "-q"],
            capture_output=True)
        if result.returncode != 0:
            print("  ERROR: failed to install pyyaml")
            return False
        return True


def run(cmd, **kwargs):
    print(f"  $ {' '.join(str(c) for c in cmd)}")
    result = subprocess.run(cmd, **kwargs)
    if result.returncode != 0:
        print(f"  FAILED (exit code {result.returncode})")
        return False
    return True


def _protoc_cmd():
    """Return the protoc command list. Prefers standalone protoc, falls back to grpc_tools."""
    if shutil.which("protoc"):
        return ["protoc"]
    try:
        import grpc_tools.protoc  # noqa: F401
        return [sys.executable, "-m", "grpc_tools.protoc"]
    except ImportError:
        pass
    print("  ERROR: protoc not found. Install protoc or run: pip install grpcio-tools")
    return None


def step_compile():
    print("\n=== Step 1: Compile .proto -> descriptor set ===")
    proto_files = [f for f in PROTO_DIR.glob("*.proto") if not f.name.endswith("_gen.proto") and f.name != "config_metadata.proto"]
    if not proto_files:
        print("  ERROR: No .proto files found")
        return False

    protoc = _protoc_cmd()
    if protoc is None:
        return False

    return run(protoc + [
        f"--proto_path={PROTO_DIR}",
        f"--descriptor_set_out={DESC_FILE}",
        "--include_imports",
    ] + [str(f) for f in proto_files])


def step_generate():
    print("\n=== Step 2: Generate C++ from descriptors + layout.yaml ===")
    _ensure_pyyaml()  # tab layout generation requires pyyaml
    return run([sys.executable, str(ROOT / "tools" / "config_codegen.py"),
                str(DESC_FILE), str(CODEGEN_OUT)])


def step_validate():
    print("\n=== Step 3: Validate ===")
    return run([sys.executable, str(ROOT / "tools" / "validate_codegen.py")])


def main():
    parser = argparse.ArgumentParser(description="Run OrcaSlicer config codegen pipeline")
    parser.add_argument("--validate-only", action="store_true",
                        help="Only run validation")
    parser.add_argument("--no-validate", action="store_true",
                        help="Skip validation step (used by cmake build)")
    args = parser.parse_args()

    if args.validate_only:
        sys.exit(0 if step_validate() else 1)

    for name, fn in [("Compile", step_compile), ("Generate", step_generate)]:
        if not fn():
            print(f"\n*** Pipeline FAILED at: {name} ***")
            sys.exit(1)

    if not args.no_validate:
        if not step_validate():
            print("\n*** Validate FAILED (run with --no-validate to skip) ***")
            sys.exit(1)

    print("\n=== Pipeline completed successfully ===")


if __name__ == "__main__":
    main()
