"""Emit one process whose body is a long block chain closed into a loop.

The chain length is the only argument. This exists so the compute-graph passes
are exercised at a CFG size no recursive traversal could survive.
"""

import sys

blocks = int(sys.argv[1])
out = sys.stdout.write

out("module {\n")
out("  obelisk_sim.design @deep {\n")
out("    obelisk_sim.scope.decl 0\n")
out("    obelisk_sim.func @chain(\n")
out("        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})\n")
out("        attributes {entry_kind = 1 : i32} {\n")
out("      cf.br ^bb1\n")
for index in range(1, blocks):
    out("    ^bb%d:\n      cf.br ^bb%d\n" % (index, index + 1))
# Closing the chain gives the schedule exactly one cyclic group to plan.
out("    ^bb%d:\n      cf.br ^bb1\n" % blocks)
out("    }\n")
out("  }\n")
out("}\n")
