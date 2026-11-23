#!/usr/bin/env python3

"""
ProZest OS - Exception Assembly Verification

Inspect the compiled object file using objdump.

This is a static test. It does not execute
exception handlers or privileged instructions.
"""

import re
import subprocess
import sys
from pathlib import Path

OBJECT = Path("build/exception_stubs.o")

ERROR_CODE_VECTORS = {
    8, 10, 11, 12, 13,
    14, 17, 21, 29, 30
}


def has(pattern, text):
    return re.search(pattern, text) is not None


def main():
    print("ProZest OS - Exception Stub Tests")
    print("================================")

    if not OBJECT.is_file():
        print("[FAIL] Assembly object not found")
        return 1

    result = subprocess.run(
        ["objdump", "-dr", str(OBJECT)],
        capture_output=True,
        text=True,
        check=True
    )

    disassembly = result.stdout

    # Find each compiled exception entry point.
    symbol_pattern = (
        r"(?m)^[0-9a-f]+ "
        r"<exception_stub_(\d+)>:[ \t]*$"
    )

    matches = list(
        re.finditer(symbol_pattern, disassembly)
    )

    stubs = {}

    for index, match in enumerate(matches):
        vector = int(match.group(1))

        end = (
            matches[index + 1].start()
            if index + 1 < len(matches)
            else len(disassembly)
        )

        stubs[vector] = disassembly[
            match.end():end
        ]

    failures = 0

    if len(matches) != 32 or set(stubs) != set(range(32)):
        print("[FAIL] Missing or duplicate vectors")
        print("Found:", sorted(stubs))
        return 1

    for vector in range(32):
        code = stubs[vector]

        expected_error = (
            vector in ERROR_CODE_VECTORS
        )

        # Is the correct vector passed in RDI?
        vector_ok = has(
            rf"\bmov\s+\$0x{vector:x},%rdi",
            code
        )

        # Error-code and no-error-code entry patterns.
        reads_cpu_error = has(
            r"\bmov\s+\(%rsp\),%rsi",
            code
        )

        clears_error = has(
            r"\bxor\s+%rsi,%rsi",
            code
        )

        if expected_error:
            error_ok = (
                reads_cpu_error and
                not clears_error
            )
        else:
            error_ok = (
                clears_error and
                not reads_cpu_error
            )

        # Verify the essential entry and fallback logic.
        safety_ok = all([
            has(r"\bcli\b", code),
            has(r"\bcld\b", code),
            has(
                r"\band\s+\$0xfffffffffffffff0,%rsp",
                code
            ),
            has(r"\bcall\b", code),
            has(r"\bhlt\b", code),
            "exception_dispatch" in code
        ])

        passed = (
            vector_ok and
            error_ok and
            safety_ok
        )

        kind = (
            "CPU error code"
            if expected_error
            else "No error code"
        )

        print(
            f"[{'PASS' if passed else 'FAIL'}] "
            f"Vector {vector:02d} - {kind}"
        )

        if not passed:
            failures += 1

            if not vector_ok:
                print("  Incorrect vector argument")

            if not error_ok:
                print("  Incorrect error-code handling")

            if not safety_ok:
                print("  Missing entry or fallback instruction")

    print("--------------------------------")
    print(f"Verified vectors: {len(stubs)}")
    print(f"Failures: {failures}")

    if failures == 0:
        print("[PASS] All 32 exception stubs")
        return 0

    return 1


if __name__ == "__main__":
    sys.exit(main())
