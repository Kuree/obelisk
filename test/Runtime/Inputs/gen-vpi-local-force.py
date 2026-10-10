import pathlib

# Two independent NBA counters share an external clock. Forcing one must not
# fracture the other's generated execution (DESIGN.md, LRM 38.34/10.6.2).
source = (pathlib.Path(__file__).parents[2] / "Conversion" /
          "simulation-ranked-group-external-clock.mlir").read_text()
source = source[source.index("module attributes"):]
source = source.replace("    simulation.func @root(", """    simulation.code_unit.decl 5 in 0 always hierarchy "external_clock.independent"
    simulation.storage.decl 4 in 0 : !simulation.logic<32> design hierarchy "external_clock.untouched"
    simulation.func @root(""", 1)
source = source.replace("      simulation.return", """      %independent = simulation.context.storage %ctx[4] : !simulation.ref<!simulation.logic<32>>
      simulation.ref.store %zero32 to %independent : !simulation.logic<32>, !simulation.ref<!simulation.logic<32>>
      %other = simulation.spawn @independent(%ctx, %clk) : !simulation.context, !simulation.ref<!simulation.logic<1>> -> !simulation.process
      simulation.return""", 1)
counter = source[source.index("    simulation.func @counter("):
                 source.index("    simulation.func @relay1(")]
counter = counter.replace("@counter(", "@independent(")
counter = counter.replace("code_unit_id = 2", "code_unit_id = 5")
counter = counter.replace("%ctx[1]", "%ctx[4]")
counter = counter.replace("id = 1>", "id = 4>")
source = source.replace("    simulation.func @relay1(", counter +
                        "    simulation.func @relay1(", 1)
print(source)
