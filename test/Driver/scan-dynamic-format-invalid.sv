// RUN: obelisk -O0 --vpi=off %s -o %t.native
// RUN: obelisk -O0 --vpi=off --execution-tier=bytecode %s -o %t.bytecode
// RUN: not %t.native +CASE=malformed 2>&1 | FileCheck %s --check-prefix=MALFORMED
// RUN: not %t.native +CASE=mismatch_count 2>&1 | FileCheck %s --check-prefix=COUNT
// RUN: not %t.native +CASE=mismatch_type 2>&1 | FileCheck %s --check-prefix=TYPE
// RUN: not %t.native +CASE=eof_count 2>&1 | FileCheck %s --check-prefix=COUNT
// RUN: not %t.native +CASE=eof_type 2>&1 | FileCheck %s --check-prefix=TYPE
// RUN: not %t.native +CASE=suppressed 2>&1 | FileCheck %s --check-prefix=SUPPRESSED
// RUN: not %t.bytecode +CASE=malformed 2>&1 | FileCheck %s --check-prefix=MALFORMED
// RUN: not %t.bytecode +CASE=mismatch_count 2>&1 | FileCheck %s --check-prefix=COUNT
// RUN: not %t.bytecode +CASE=mismatch_type 2>&1 | FileCheck %s --check-prefix=TYPE
// RUN: not %t.bytecode +CASE=eof_count 2>&1 | FileCheck %s --check-prefix=COUNT
// RUN: not %t.bytecode +CASE=eof_type 2>&1 | FileCheck %s --check-prefix=TYPE
// RUN: not %t.bytecode +CASE=suppressed 2>&1 | FileCheck %s --check-prefix=SUPPRESSED

module scan_dynamic_format_invalid;
  string selected;
  string format;
  string text;
  logic [31:0] value;
  integer status;

  initial begin
    if (!$value$plusargs("CASE=%s", selected))
      $fatal(0, "missing case");
    case (selected)
      "raw": begin
        format = "%u";
        status = $sscanf("ABCD", format, value);
      end
      "malformed": begin
        format = "%q";
        status = $sscanf("1", format, value);
      end
      "mismatch_count": begin
        format = "%d %d";
        status = $sscanf("q", format, value);
      end
      "mismatch_type": begin
        format = "%d %F";
        status = $sscanf("q", format, value, text);
      end
      "mismatch_raw": begin
        format = "%d %u";
        status = $sscanf("q", format, value, value);
      end
      "eof_count": begin
        format = "%d %d";
        status = $fscanf(0, format, value);
      end
      "eof_type": begin
        format = "%d %F";
        status = $fscanf(0, format, value, text);
      end
      "eof_raw": begin
        format = "%d %Z";
        status = $fscanf(0, format, value, value);
      end
      default: begin
        format = "%*u%c";
        status = $sscanf("AQ", format, value);
      end
    endcase
  end
endmodule

// MALFORMED: malformed dynamic $sscanf conversion '%q'
// COUNT: format has more conversions than destinations
// TYPE: %F is incompatible with its destination
// SUPPRESSED: assignment suppression for raw %u requires an explicit byte count
