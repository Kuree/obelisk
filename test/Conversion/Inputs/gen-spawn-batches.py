# Independent parents exercise module-global creation across worker bodies.
count = 32
print('''module attributes {
  llvm.data_layout = "e-p:64:64-i64:64-i32:32-i16:16-i8:8",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @batches {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 initial hierarchy "child"
''')
for i in range(count):
    print(f'    simulation.code_unit.decl {i+3} in 0 initial hierarchy "parent_{i}"')
context = '%ctx: !simulation.context {simulation.capture_kind = 0 : i32}'
print(f'''    simulation.func @root({context})
        attributes {{entry_kind = 0 : i32, code_unit_id = 1 : i64}} {{''')
for i in range(count):
    print(f'      %p{i} = simulation.spawn @parent_{i}(%ctx) : !simulation.context -> !simulation.process')
print('      simulation.return\n    }')
for i in range(count):
    print(f'''    simulation.func @parent_{i}({context})
        attributes {{entry_kind = 1 : i32, code_unit_id = {i+3} : i64}} {{
      %value = arith.constant {i} : i64
      %a = simulation.spawn @child(%ctx, %value) : !simulation.context, i64 -> !simulation.process
      %b = simulation.spawn @child(%ctx, %value) : !simulation.context, i64 -> !simulation.process
      simulation.return
    }}''')
print(f'''    simulation.func @child({context},
        %value: i64 {{simulation.capture_kind = 2 : i32}})
        attributes {{entry_kind = 1 : i32, code_unit_id = 2 : i64}} {{
      simulation.return
    }}
  }}
}}''')
