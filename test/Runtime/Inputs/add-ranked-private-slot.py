import pathlib
import sys

source = pathlib.Path(sys.argv[1]).read_text()
if "--whole-read" in sys.argv[2:]:
    source = source.replace(
        "%value = simulation.ref.load %in : !simulation.ref<!simulation.logic<4>> -> !simulation.logic<4>",
        "%whole = simulation.ref.load %input : !simulation.ref<!simulation.logic<16>> -> !simulation.logic<16>\n"
        "      %value = simulation.logic.dyn_extract %whole from %offset : (!simulation.logic<16>, i32) -> !simulation.logic<4>",
    )
store = "simulation.ref.store %value to %out"
assert source.count(store) == 1
print(source.replace(store, """%scratch = simulation.ref.alloc %value : !simulation.logic<4> -> !simulation.ref<!simulation.logic<4>>
      %choose = arith.cmpi eq, %i, %zero : i32
      %selected = arith.select %choose, %scratch, %in : !simulation.ref<!simulation.logic<4>>
      %copied = simulation.ref.load %selected : !simulation.ref<!simulation.logic<4>> -> !simulation.logic<4>
      simulation.ref.store %copied to %out"""))
