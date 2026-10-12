"""Expand the unreachable body without maintaining hundreds of duplicate lines."""

import pathlib
import sys

source = pathlib.Path(sys.argv[1]).read_text()
body = "\n".join(
    "      simulation.ref.store %j to %out : i32, !simulation.ref<i32>"
    for _ in range(500)
)
print(source.replace("      // EXPAND-ZERO-TRIP-BODY", body), end="")
