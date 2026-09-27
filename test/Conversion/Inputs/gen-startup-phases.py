"""Generate startup fanout, optionally with an initial -> startup return path."""
import sys

count = int(sys.argv[1])
feedback = "--feedback" in sys.argv[2:]
boundaries = "--boundaries" in sys.argv[2:]


def name(kind, index):
    prefix = ("a_" if kind == "initial" else "z_") if boundaries else ""
    return f"{prefix}{kind}{index}"


print("module { simulation.design @startup_phases {")
print("simulation.scope.decl 0")
print('simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"')
if feedback:
    print("simulation.storage.decl 0 in 0 : i1 design")
for kind, base in [("initial", 2), ("always", count + 2)]:
    for i in range(count):
        print(f'simulation.code_unit.decl {base + i} in 0 {kind} '
              f'hierarchy "{kind}{i}"')
print('simulation.func @root(%ctx: !simulation.context '
      '{simulation.capture_kind = 0 : i32}) '
      'attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {')
for i in range(count):
    if boundaries and i == 1:
        print("cf.br ^second\n^second:")
    for kind in ["initial", "always"]:
        if boundaries and kind == "always" and i == count - 1:
            continue  # An unspawned startup must not constrain any initial.
        print(f'%p_{kind}{i} = simulation.spawn @{name(kind, i)}(%ctx) : '
              '!simulation.context -> !simulation.process')
print("simulation.return }")
for kind, base, entry in [("initial", 2, 1), ("always", count + 2, 3)]:
    for i in range(count):
        extra = ""
        if boundaries and kind == "always" and i == 0:
            extra = ", schedule.starts_without_waiting"
        if boundaries and kind == "initial" and i == count - 1:
            extra = ", home_region = 10 : i32"
        print(f'simulation.func @{name(kind, i)}(%ctx: !simulation.context '
              '{simulation.capture_kind = 0 : i32}) '
              f'attributes {{entry_kind = {entry} : i32, '
              f'code_unit_id = {base + i} : i64{extra}}} {{')
        if feedback:
            print('%ref = simulation.context.storage %ctx[0] : '
                  '!simulation.ref<i1>')
            if kind == "initial":
                print('%one = arith.constant true')
                print('simulation.ref.store %one to %ref : i1, !simulation.ref<i1>')
            else:
                print('simulation.suspend.change %ref to ^done : !simulation.ref<i1>')
                print('^done:')
        print("simulation.return }")
print("} }")
