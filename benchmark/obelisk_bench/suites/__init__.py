"""Registry of benchmark suites.

Each suite is a plain module exposing a small, concrete contract (see
`verilator.py` for the canonical shape) — there is no base class or framework.
Adding a suite means writing a module and adding one line here.
"""

from __future__ import annotations

from . import ivtest, verilator

REGISTRY = {
    "verilator": verilator,
    "ivtest": ivtest,
}
