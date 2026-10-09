import pathlib
import sys

source = pathlib.Path(sys.argv[1]).read_text()
source = source.replace('7 in 0 always hierarchy "step5"',
                        '7 in 0 always_comb hierarchy "step5"')
start = source.index('    simulation.func @step5(')
end = source.index('    simulation.func @check(', start)
function = source[start:end]
function = function.replace('entry_kind = 3 : i32', 'entry_kind = 4 : i32')
function = function.replace('cf.br ^wait', 'cf.br ^body', 1)
old = """      %high = simulation.ref.extract %feedback from 8 : !simulation.ref<!simulation.logic<16>> -> !vref
      simulation.ref.store %next to %high : !simulation.logic<8>, !vref"""
assert function.count(old) == 1
function = function.replace(old, """      %eight = arith.constant 8 : i32
      %sixteen = arith.constant 16 : i32
      %four = arith.constant 4 : i32
      cf.br ^loop(%eight : i32)
    ^loop(%i: i32):
      %more = arith.cmpi slt, %i, %sixteen : i32
      cf.cond_br %more, ^copy, ^finished
    ^copy:
      %offset = arith.subi %i, %eight : i32
      %value = simulation.logic.dyn_extract %next from %offset : (!simulation.logic<8>, i32) -> !simulation.logic<4>
      %high = simulation.ref.dyn_extract %feedback from %i : (!simulation.ref<!simulation.logic<16>>, i32) -> !simulation.ref<!simulation.logic<4>>
      simulation.ref.store %value to %high : !simulation.logic<4>, !simulation.ref<!simulation.logic<4>>
      %advance = arith.addi %i, %four : i32
      cf.br ^loop(%advance : i32)
    ^finished:""")
print(source[:start] + function + source[end:])
