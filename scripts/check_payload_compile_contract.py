#!/usr/bin/env python3
"""Compile TinyLink payload contract probes with a native C++11 compiler."""

import os
import re
import shlex
import shutil
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
EXPECTED_DIAGNOSTIC = "TinyLink: Payload must be trivially copyable."


def compiler_command():
    """Choose CXX when configured, otherwise the first available native compiler."""
    configured = os.environ.get("CXX")
    if configured:
        command = shlex.split(configured, posix=(os.name != "nt"))
    else:
        compiler = next(
            (shutil.which(name) for name in ("c++", "g++", "clang++")
             if shutil.which(name)),
            None,
        )
        command = [compiler] if compiler else []

    if not command or not shutil.which(command[0]):
        raise RuntimeError("Set CXX to an available GNU or Clang C++ compiler.")
    return command


def compile_probe(compiler, source):
    """Compile one contract case without linking or producing an executable."""
    return subprocess.run(
        compiler + [
            "-std=c++11",
            "-fsyntax-only",
            "-Isrc",
            "-Itest/compile_contract",
            str(source),
        ],
        cwd=str(ROOT),
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        universal_newlines=True,
    )


def main():
    """Require the valid case to compile and the invalid case to fail as intended."""
    try:
        compiler = compiler_command()
        # The positive probe covers both a trivial payload and the 64-byte limit.
        valid = compile_probe(compiler, ROOT / "test/compile_contract/valid_payload.cpp")
        if valid.returncode != 0:
            sys.stderr.write("Valid payload compile probe failed:\n" + valid.stdout)
            return 1

        # Reject any failure that does not match the one intended static assertion.
        invalid = compile_probe(
            compiler, ROOT / "test/compile_contract/nontrivial_payload.cpp"
        )
        error_lines = [
            line for line in invalid.stdout.splitlines()
            if re.search(r"\berror:", line)
        ]
        if (invalid.returncode == 0 or len(error_lines) != 1
                or not re.search(r"static assertion failed|static_assert failed",
                                 error_lines[0])
                or EXPECTED_DIAGNOSTIC not in invalid.stdout):
            sys.stderr.write(
                "Non-trivially-copyable payload did not fail for the expected "
                "static assertion:\n" + invalid.stdout
            )
            return 1

        print("Payload compile contracts passed (valid accepted; non-trivial rejected).")
        return 0
    except (OSError, RuntimeError) as error:
        sys.stderr.write(str(error) + "\n")
        return 1


if __name__ == "__main__":
    sys.exit(main())
