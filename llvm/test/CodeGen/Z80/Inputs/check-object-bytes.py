"""Check emitted section bytes against explicit instruction-encoding examples."""
import pathlib
import re
import sys

source, binary = map(pathlib.Path, sys.argv[1:])
expected = bytes.fromhex(" ".join(re.findall(r"BYTES: ([0-9a-f ]+)", source.read_text())))
actual = binary.read_bytes()
if actual != expected:
    for offset, (want, got) in enumerate(zip(expected, actual)):
        if want != got:
            raise SystemExit(f"byte {offset:#x}: expected {want:02x}, got {got:02x}")
    raise SystemExit(f"expected {len(expected)} bytes, got {len(actual)}")
