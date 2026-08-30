import pathlib
import sys


prefix = pathlib.Path(sys.argv[1])
directory = pathlib.Path(str(prefix) + ".files")
directory.mkdir(parents=True, exist_ok=True)
sdf = """(DELAYFILE
  (SDFVERSION \"4.0\")
  (TIMESCALE 1ns)
  (CELL
    (CELLTYPE \"sdf_cache_leaf\")
    (INSTANCE)
    (DELAY (ABSOLUTE (IOPATH source destination (1))))))
"""
filenames = []
for index in range(257):
    filename = directory / f"annotation-{index}.sdf"
    filename.write_text(sdf)
    filenames.append(filename.as_posix())

calls = "\n".join(f'    $sdf_annotate("{name}", dut);' for name in filenames)
pathlib.Path(str(prefix) + ".sv").write_text(
    f"""`timescale 1ns / 1ns
module sdf_cache_leaf(input wire source, output wire destination);
  assign destination = source;
  specify (source => destination) = 1; endspecify
endmodule
module sdf_cache_file_limit;
  logic source;
  wire destination;
  sdf_cache_leaf dut(source, destination);
  initial begin
{calls}
  end
endmodule
"""
)
