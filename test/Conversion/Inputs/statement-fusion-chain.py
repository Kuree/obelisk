"""Generate an MLIR actor SCC whose statements form a 66-actor DAG."""

print("module { simulation.design @chain {")
print("simulation.scope.decl 0")
for index in range(68):
    print(f"simulation.storage.decl {index} in 0 : i8 design")
for index in range(66):
    print(f'simulation.code_unit.decl {index + 1} in 0 always_comb hierarchy "p{index}"')
print("simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32} {")
for index in range(66):
    print(f"%p{index} = simulation.spawn @p{index}(%ctx) : !simulation.context -> !simulation.process")
print("simulation.return }")
for index in range(66):
    print(f"simulation.func @p{index}(%ctx: !simulation.context {{simulation.capture_kind = 0 : i32}}) attributes {{entry_kind = 4 : i32, code_unit_id = {index + 1} : i64}} {{")
    print(f"%in = simulation.context.storage %ctx[{index}] : !simulation.ref<i8>")
    print(f"%out = simulation.context.storage %ctx[{index + 1}] : !simulation.ref<i8>")
    if index == 0:
        print("%last = simulation.context.storage %ctx[66] : !simulation.ref<i8>")
        print("%result = simulation.context.storage %ctx[67] : !simulation.ref<i8>")
    print("cf.br ^body\n^body:")
    print("%value = simulation.ref.load %in : !simulation.ref<i8> -> i8")
    print("simulation.ref.store %value to %out : i8, !simulation.ref<i8>")
    if index == 0:
        print("%tail = simulation.ref.load %last : !simulation.ref<i8> -> i8")
        print("simulation.ref.store %tail to %result : i8, !simulation.ref<i8>")
        print("simulation.suspend.any %in, %last edges [0, 0] to ^body : !simulation.ref<i8>, !simulation.ref<i8>")
    else:
        print("simulation.suspend.change %in to ^body : !simulation.ref<i8>")
    print("}")
print("} }")
