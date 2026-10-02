"""Exercise execution bounds across large diamonds and loop backedges."""

print("""
module {
  simulation.design @execution {
    func.func private @opaque()
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : i64 design
""")
for number, name in enumerate(("diamond", "loop", "call"), 1):
    print(f'    simulation.code_unit.decl {number} in 0 function hierarchy "{name}"')
    print(f"""
    simulation.func @{name}(
        %ctx: !simulation.context {{simulation.capture_kind = 0 : i32}},
        %condition: i1 {{simulation.capture_kind = 2 : i32}},
        %input: i64 {{simulation.capture_kind = 2 : i32}})
        attributes {{entry_kind = 8 : i32, code_unit_id = {number} : i64}} {{
      %root = simulation.context.storage %ctx[0] : !simulation.ref<i64>
      cf.cond_br %condition, ^left, ^right
    """)
    for arm in ("left", "right"):
        print(f"    ^{arm}:")
        if name == "call":
            print("      func.call @opaque() : () -> ()")
        value = "%input"
        for index in range(65):
            # Arithmetic between writes must preserve the execution facts.
            for step in range(4):
                result = f"%{arm}_{index}_{step}"
                print(f"      {result} = arith.addi {value}, %input : i64")
                value = result
            print(f"      simulation.ref.store {value} to %root : i64, !simulation.ref<i64>")
        target = arm if name == "loop" else "exit"
        print(f"      cf.br ^{target}")
    print("    ^exit:\n      simulation.return\n    }")
print("  }\n}")
