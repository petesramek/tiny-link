#!/usr/bin/env python3
"""Compile-contract checks for TinyLink payload types (Unity cannot express
compile failures). Usage: check.py [compiler]  (default: g++)."""
import os
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
HERE = os.path.dirname(os.path.abspath(__file__))
CXX = sys.argv[1] if len(sys.argv) > 1 else "g++"

CASES = [
    ("payload_valid.cpp", None),
    ("payload_not_trivially_copyable.cpp", "must be trivially copyable"),
    ("payload_too_large.cpp", "exceeds 64 bytes"),
]


def compile_case(name):
    cmd = [CXX, "-std=gnu++11", "-fsyntax-only", "-DTINYLINK_NATIVE_TEST",
           "-I", os.path.join(ROOT, "src"), "-I", os.path.join(ROOT, "test"),
           os.path.join(HERE, name)]
    return subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                          universal_newlines=True)


failed = False
for name, expected in CASES:
    result = compile_case(name)
    if expected is None:
        ok = result.returncode == 0
    else:
        ok = result.returncode != 0 and expected in result.stdout
    print("%s %s" % ("PASS" if ok else "FAIL", name))
    if not ok:
        failed = True
        print(result.stdout)
sys.exit(1 if failed else 0)
