"""Emit one process whose body is a long block chain closed into a loop.

The first argument is the chain length. Optional --acyclic ends it in a return
instead of a backedge, exercising region classification with many singleton
SCCs as well as traversal at a size no recursive implementation could survive.
"""

import sys

blocks = int(sys.argv[1])
out = sys.stdout.write

out("module {\n")
out("  simulation.design @deep {\n")
out("    simulation.code_unit.decl 1 in 0 initial hierarchy \"test.deep.chain\"\n")
out("    simulation.scope.decl 0\n")
out("    simulation.func @chain(\n")
out("        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})\n")
out("        attributes {code_unit_id = 1 : i64, entry_kind = 1 : i32} {\n")
out("      cf.br ^bb1\n")
for index in range(1, blocks):
    out("    ^bb%d:\n      cf.br ^bb%d\n" % (index, index + 1))
if sys.argv[2:] == ["--acyclic"]:
    out("    ^bb%d:\n      simulation.return\n" % blocks)
elif not sys.argv[2:]:
    # Closing the chain gives the schedule exactly one cyclic group to plan.
    out("    ^bb%d:\n      cf.br ^bb1\n" % blocks)
else:
    raise SystemExit("usage: gen-deep-cfg.py BLOCKS [--acyclic]")
out("    }\n")
out("  }\n")
out("}\n")
