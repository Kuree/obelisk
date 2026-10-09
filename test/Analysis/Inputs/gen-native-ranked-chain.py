"""A fragmented owner still has one bounded inventory of activation ranges."""
import contextlib
import io
import pathlib
import runpy

buffer = io.StringIO()
with contextlib.redirect_stdout(buffer):
    runpy.run_path(str(pathlib.Path(__file__).with_name("gen-activation-value-chain.py")))
source = buffer.getvalue().replace("module {", """module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu",
  schedule.native_scheduler = 3 : i32, obelisk.debug.native_timing} {""", 1)
source = source.replace("    simulation.code_unit.decl 1", """    simulation.code_unit.decl 2 in 0 root_initializer hierarchy "root"
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 2 : i64} {
      %p = simulation.spawn @chain(%ctx) : !simulation.context -> !simulation.process
      simulation.return
    }
    simulation.code_unit.decl 1""", 1)
for index in range(128):
    source = source.replace(f"    ^block{index}:\n", f"""    ^block{index}:
      %old{index} = simulation.ref.load %root{index} : !simulation.ref<i1> -> i1
      %changed{index} = arith.xori %old{index}, %one : i1
""")
    source = source.replace(f"store %one to %root{index} :", f"store %changed{index} to %root{index} :")
    source = source.replace(f"store %zero to %root{index} :", f"store %old{index} to %root{index} :")
print(source)
