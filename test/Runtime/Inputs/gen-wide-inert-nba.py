"""Wide clocked holds, including an equal NBA after a different pending NBA."""
from contextlib import redirect_stdout
from io import StringIO
from pathlib import Path
import re
import runpy

text = StringIO()
with redirect_stdout(text):
    runpy.run_path(str(Path(__file__).with_name("gen-periodic-shared-nba.py")))
model = text.getvalue().replace("!simulation.logic<32>", "!simulation.logic<130>")
model = model.replace(": i32, 0 : i32 : !word", ": i130, 0 : i130 : !word")
model = model.replace("schedule.native_scheduler = 0 : i32",
                      "schedule.native_scheduler = 0 : i32, obelisk.native.closed_executable, obelisk.debug.native_timing")

def hold(match):
    body = match.group(0)
    index = int(match.group(1))
    if index == 0:
        return body
    body = body.replace("simulation.nba.enqueue %next to %q",
                        "simulation.nba.enqueue %old to %q")
    if index == 1:
        # LRM 4.6(b), 4.9.4, 10.4.2: the equal second capture must still
        # overwrite the different earlier capture in the same NBA window.
        body = body.replace("simulation.nba.enqueue %old to %q",
                            "simulation.nba.enqueue %next to %q : (!word, !ref) -> ()\n"
                            "      simulation.nba.enqueue %old to %q")
    return body

model = re.sub(r"simulation.func @writer(\d+)\(.*?\n    }", hold, model, flags=re.S)
model = model.replace(
    '      %fmt2 = simulation.bytes.constant "settled %0d %0d"',
    '      %pending = simulation.context.storage %ctx[2] : !ref\n'
    '      %captured = simulation.ref.load %pending : !ref -> !word\n'
    '      %fmt2 = simulation.bytes.constant "settled %0d %0d pending=%0d"')
model = model.replace('(%fmt2, %c, %d) newline',
                      '(%fmt2, %c, %d, %captured) newline')
model = model.replace('flags = [0, 0, 0] : !simulation.bytes, !word, !word',
                      'flags = [0, 0, 0, 0] : !simulation.bytes, !word, !word, !word')
print(model, end="")
