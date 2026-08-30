import pathlib
import sys


mode = sys.argv[1]
prefix = pathlib.Path(sys.argv[2])


def write_driver(name, filenames):
    calls = "\n".join(
        f'    $sdf_annotate("{filename.as_posix()}", dut);'
        for filename in filenames
    )
    pathlib.Path(str(prefix) + ".sv").write_text(
        f"""`timescale 1fs / 1fs
module sdf_resource_leaf(input wire source, output wire destination);
  assign destination = source;
  specify (source => destination) = 1; endspecify
endmodule
module {name};
  logic source;
  wire destination;
  sdf_resource_leaf dut(source, destination);
  initial begin
{calls}
  end
endmodule
"""
    )


if mode == "exponent":
    filename = pathlib.Path(str(prefix) + ".sdf")
    exponent = "9" * 8192
    paths = []
    for _ in range(64):
        paths.append(f"(IOPATH source destination (1e{exponent}))")
        paths.append(f"(IOPATH source destination (1e-{exponent}))")
    filename.write_text(
        "(DELAYFILE\n"
        "  (SDFVERSION \"4.0\")\n"
        "  (TIMESCALE 1fs)\n"
        "  (CELL\n"
        "    (CELLTYPE \"sdf_resource_leaf\")\n"
        "    (INSTANCE)\n"
        "    (DELAY (ABSOLUTE\n      "
        + "\n      ".join(paths)
        + "))))\n"
    )
    write_driver("sdf_exponent_resource", [filename])
elif mode == "translation-exponent":
    # Exercise the transient importer directly: exponent spelling is lexical
    # SDF state, while destination-precision conversion belongs to application.
    filename = pathlib.Path(str(prefix) + ".sdf")
    exponent = "9" * 8192
    paths = []
    for _ in range(64):
        paths.append(f"(IOPATH source destination (1e{exponent}))")
        paths.append(f"(IOPATH source destination (1e-{exponent}))")
    filename.write_text(
        "(DELAYFILE\n"
        "  (SDFVERSION \"4.0\")\n"
        "  (TIMESCALE 1fs)\n"
        "  (CELL\n"
        "    (CELLTYPE \"sdf_resource_leaf\")\n"
        "    (INSTANCE)\n"
        "    (DELAY (ABSOLUTE\n      "
        + "\n      ".join(paths)
        + "))))\n"
    )
elif mode == "translation-size":
    filename = pathlib.Path(str(prefix) + ".sdf")
    with filename.open("wb") as stream:
        stream.write(b"(DELAYFILE ")
        stream.seek(64 * 1024 * 1024)
        stream.write(b")")
elif mode == "scaled-boundary":
    filename = pathlib.Path(str(prefix) + ".sdf")
    significand = "9223372036854775799" + "1" * (8192 - 19)
    exponent = -(len(significand) - 19)
    filename.write_text(
        "(DELAYFILE\n"
        "  (SDFVERSION \"4.0\")\n"
        "  (TIMESCALE 1fs)\n"
        "  (CELL\n"
        "    (CELLTYPE \"sdf_resource_leaf\")\n"
        "    (INSTANCE)\n"
        "    (DELAY (ABSOLUTE\n"
        f"      (IOPATH source destination ({significand}e{exponent}))\n"
        "    ))\n"
        "  ))\n"
    )
    write_driver("sdf_scaled_decimal_boundary", [filename])
elif mode == "fractional-boundaries":
    filename = pathlib.Path(str(prefix) + ".sdf")
    tail = 8192
    below = "4." + "9" * tail
    exact = "5." + "0" * tail
    above = "5." + "0" * (tail - 1) + "1"
    filename.write_text(
        "(DELAYFILE\n"
        "  (SDFVERSION \"4.0\")\n"
        "  (TIMESCALE 1fs)\n"
        "  (CELL (CELLTYPE \"sdf_fractional_leaf\") (INSTANCE below)\n"
        f"    (DELAY (ABSOLUTE (IOPATH source destination ({below})))))\n"
        "  (CELL (CELLTYPE \"sdf_fractional_leaf\") (INSTANCE exact)\n"
        f"    (DELAY (ABSOLUTE (IOPATH source destination ({exact})))))\n"
        "  (CELL (CELLTYPE \"sdf_fractional_leaf\") (INSTANCE above)\n"
        f"    (DELAY (ABSOLUTE (IOPATH source destination ({above})))))\n"
        ")\n"
    )
    pathlib.Path(str(prefix) + ".sv").write_text(
        f"""module sdf_fractional_leaf(input wire source,
                                      output wire destination);
  timeunit 1ns;
  timeprecision 10fs;
  assign destination = source;
  specify (source => destination) = 1; endspecify
endmodule
module sdf_fractional_boundaries;
  timeunit 1ns;
  timeprecision 10fs;
  logic source;
  wire below_destination, exact_destination, above_destination;
  sdf_fractional_leaf below(source, below_destination);
  sdf_fractional_leaf exact(source, exact_destination);
  sdf_fractional_leaf above(source, above_destination);
  initial $sdf_annotate("{filename.as_posix()}");
endmodule
"""
    )
elif mode == "aggregate-bytes":
    directory = pathlib.Path(str(prefix) + ".files")
    directory.mkdir(parents=True, exist_ok=True)
    filenames = []
    for index in range(5):
        filename = directory / f"bytes-{index}.sdf"
        with filename.open("wb") as stream:
            stream.write(b")")
            if index < 4:
                stream.seek(64 * 1024 * 1024 - 1)
                stream.write(b"x")
        filenames.append(filename)
    write_driver("sdf_aggregate_byte_resource", filenames)
elif mode == "parsed-entries":
    directory = pathlib.Path(str(prefix) + ".files")
    directory.mkdir(parents=True, exist_ok=True)
    filenames = []
    # countParsedSDFEntries charges both each compact IOPATH record and its
    # retained delay field. Four files cross 2^20 entries by a small margin.
    # Use data consumed by production folding, not transient-only LABEL ops.
    paths = "      (IOPATH source destination (1))\n" * (1 << 17)
    for index in range(4):
        filename = directory / f"entries-{index}.sdf"
        filename.write_text(
            "(DELAYFILE\n"
            "  (SDFVERSION \"4.0\")\n"
            "  (TIMESCALE 1fs)\n"
            "  (CELL\n"
            "    (CELLTYPE \"does_not_match\")\n"
            "    (INSTANCE)\n"
            "    (DELAY (ABSOLUTE\n"
            + paths
            + "    ))))\n"
        )
        filenames.append(filename)
    write_driver("sdf_parsed_entry_resource", filenames)
elif mode == "matched-updates":
    filename = pathlib.Path(str(prefix) + ".sdf")
    count = 1025
    updates = "\n      ".join(
        "(IOPATH source destination (1))" for _ in range(count)
    )
    filename.write_text(
        "(DELAYFILE\n"
        "  (SDFVERSION \"4.0\")\n"
        "  (TIMESCALE 1fs)\n"
        "  (CELL\n"
        "    (CELLTYPE \"sdf_update_leaf\")\n"
        "    (INSTANCE *)\n"
        "    (DELAY (ABSOLUTE\n      "
        + updates
        + "))))\n"
    )
    instances = "\n".join(
        f"  sdf_update_leaf leaf_{index}(source, destination[{index}]);"
        for index in range(count)
    )
    pathlib.Path(str(prefix) + ".sv").write_text(
        f"""`timescale 1fs / 1fs
module sdf_update_leaf(input wire source, output wire destination);
  assign destination = source;
  specify (source => destination) = 1; endspecify
endmodule
module sdf_matched_update_resource;
  logic source;
  wire [{count - 1}:0] destination;
{instances}
  initial $sdf_annotate("{filename.as_posix()}");
endmodule
"""
    )
elif mode == "match-attempts":
    filename = pathlib.Path(str(prefix) + ".sdf")
    count = 2049
    updates = "\n      ".join(
        f"(IOPATH source[{count - 1}] destination[{count - 1}] (1))"
        for _ in range(count)
    )
    filename.write_text(
        "(DELAYFILE\n"
        "  (SDFVERSION \"4.0\")\n"
        "  (TIMESCALE 1fs)\n"
        "  (CELL\n"
        "    (CELLTYPE \"sdf_match_attempt_leaf\")\n"
        "    (INSTANCE dut)\n"
        "    (DELAY (ABSOLUTE\n      "
        + updates
        + "))))\n"
    )
    paths = "\n".join(
        f"    (source[{index}] => destination[{index}]) = 1;"
        for index in range(count)
    )
    pathlib.Path(str(prefix) + ".sv").write_text(
        f"""`timescale 1fs / 1fs
module sdf_match_attempt_leaf(
    input wire [{count - 1}:0] source,
    output wire [{count - 1}:0] destination);
  assign destination = source;
  specify
{paths}
  endspecify
endmodule
module sdf_match_attempt_resource;
  logic [{count - 1}:0] source;
  wire [{count - 1}:0] destination;
  sdf_match_attempt_leaf dut(source, destination);
  initial $sdf_annotate("{filename.as_posix()}");
endmodule
"""
    )
elif mode == "call-comparisons":
    filename = pathlib.Path(str(prefix) + ".sdf")
    filename.write_text(
        "(DELAYFILE\n"
        "  (SDFVERSION \"4.0\")\n"
        "  (TIMESCALE 1fs)\n"
        "  (CELL (CELLTYPE \"sdf_resource_leaf\") (INSTANCE dut)))\n"
    )
    # 2900 calls require 4,203,550 pair-order probes, crossing the 2^22
    # application-work cap without approaching the distinct-file cache cap.
    write_driver("sdf_call_comparison_resource", [filename] * 2900)
elif mode == "wildcard-probes":
    filename = pathlib.Path(str(prefix) + ".sdf")
    count = 2049
    cells = "\n".join(
        "  (CELL (CELLTYPE \"sdf_wildcard_probe_leaf\") (INSTANCE *))"
        for _ in range(count)
    )
    filename.write_text(
        "(DELAYFILE\n"
        "  (SDFVERSION \"4.0\")\n"
        "  (TIMESCALE 1fs)\n"
        + cells
        + "\n)\n"
    )
    instances = "\n".join(
        f"  sdf_wildcard_probe_leaf leaf_{index}();"
        for index in range(count)
    )
    pathlib.Path(str(prefix) + ".sv").write_text(
        f"""`timescale 1fs / 1fs
module sdf_wildcard_probe_leaf;
endmodule
module sdf_wildcard_probe_resource;
{instances}
  initial $sdf_annotate("{filename.as_posix()}");
endmodule
"""
    )
else:
    raise SystemExit(f"unknown mode: {mode}")
