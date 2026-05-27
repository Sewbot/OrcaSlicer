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
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
PROTO_DIR = ROOT / "src" / "PrintConfigs"
PROTO_GEN_DIR = PROTO_DIR / "generated"
CODEGEN_OUT = ROOT / "codegen" / "generated"
DESC_FILE = ROOT / "config.desc"


def run(cmd, **kwargs):
    print(f"  $ {' '.join(str(c) for c in cmd)}")
    result = subprocess.run(cmd, **kwargs)
    if result.returncode != 0:
        print(f"  FAILED (exit code {result.returncode})")
        return False
    return True


def step_compile():
    print("\n=== Step 1: Compile .proto -> descriptor set ===")
    proto_files = [f for f in PROTO_GEN_DIR.glob("*.proto") if not f.name.endswith("_gen.proto")]
    if not proto_files:
        print("  ERROR: No .proto files found")
        return False

    return run([
        "protoc",
        f"--proto_path={PROTO_DIR}",
        f"--proto_path={PROTO_GEN_DIR}",
        f"--descriptor_set_out={DESC_FILE}",
        "--include_imports",
    ] + [str(f) for f in proto_files])


def step_generate():
    print("\n=== Step 2: Generate C++ from descriptors ===")
    return run([sys.executable, str(ROOT / "tools" / "config_codegen.py"),
                str(DESC_FILE), str(CODEGEN_OUT)])


def step_validate():
    print("\n=== Step 3: Validate ===")
    return run([sys.executable, str(ROOT / "tools" / "validate_codegen.py")])


def main():
    parser = argparse.ArgumentParser(description="Run OrcaSlicer config codegen pipeline")
    parser.add_argument("--validate-only", action="store_true",
                        help="Only run validation")
    args = parser.parse_args()

    if args.validate_only:
        sys.exit(0 if step_validate() else 1)

    for name, fn in [("Compile", step_compile), ("Generate", step_generate), ("Validate", step_validate)]:
        if not fn():
            print(f"\n*** Pipeline FAILED at: {name} ***")
            sys.exit(1)

    print("\n=== Pipeline completed successfully ===")


if __name__ == "__main__":
    main()
