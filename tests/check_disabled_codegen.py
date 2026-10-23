#!/usr/bin/env python3
"""Compile disabled_codegen.cpp at -O0 and check the disabled marker vanishes."""

import pathlib
import re
import subprocess
import sys


def function_body(asm: str, name: str) -> str:
    # `.size` is indented. Stop at that directive so the next function is not included.
    match = re.search(rf"^{re.escape(name)}:.*?(?=^\s*\.size\b|\Z)", asm, re.M | re.S)
    if not match:
        sys.stderr.write(f"missing function {name}\n")
        sys.stderr.write(asm[:2000])
        sys.exit(1)
    return match.group(0)


def main() -> None:
    if len(sys.argv) != 5:
        sys.exit("usage: check_disabled_codegen.py COMPILER SOURCE INCLUDE_DIR ASM_OUT")
    compiler, source, include, asm_out = sys.argv[1:5]
    result = subprocess.run(
        [
            compiler,
            "-std=c++17",
            "-O0",
            "-fno-stack-protector",
            "-S",
            "-o",
            asm_out,
            source,
            "-I",
            include,
        ],
        check=False,
    )
    if result.returncode != 0:
        sys.exit(result.returncode)

    asm = pathlib.Path(asm_out).read_text()
    disabled = function_body(asm, "instrument_disabled")
    enabled = function_body(asm, "instrument_enabled")

    if "rdtscp" in disabled or re.search(r"\bcall\b", disabled):
        sys.stderr.write(disabled)
        sys.exit("disabled marker was not compiled away at -O0")
    if "rdtscp" not in enabled:
        sys.stderr.write(enabled)
        sys.exit("enabled marker did not emit rdtscp; codegen check is vacuous")
    print("disabled codegen ok")


if __name__ == "__main__":
    main()
