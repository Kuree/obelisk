module blk #(parameter bit FAST = 0) (input logic clk, output int y);
  always_ff @(posedge clk)
    if (FAST) y <= 3;
    else      y <= 1;
endmodule

module top;
  logic clk = 0;
  int a, b;
  blk #(.FAST(1)) u0 (.clk(clk), .y(a));
  blk #(.FAST(0)) u1 (.clk(clk), .y(b));
  initial begin
    repeat (2) #5 clk = ~clk;
    $finish;
  end
endmodule

// The source above must stay the first bytes of this file: the report's
// token listing is compared against obelisk's own.

// Two instances of one module share its source lines. Each instance's page
// counts its own statements, while the enclosing scope merges them per line,
// so a line that one instance missed is partial, as in the line metric.
// Sources are named relative to the directory they were compiled from.
// RUN: cd %S && %obelisk --std=1800-2023 -O0 --target=native --coverage=line \
// RUN:   -o %t.sim html-report.sv
// RUN: %t.sim --coverage-output=%t.obcov --coverage-test=instances
// RUN: obelisk-cov report --format=text %t.obcov | FileCheck %s --check-prefix=TEXT
// TEXT: line: 60.00% (3/5), partial 2
// RUN: obelisk-cov report --format=html --source-root=%S -o %t.html %t.obcov
// RUN: %if node %{ %node %S/../Support/CheckCoverageHtml.js %t.html \
// RUN:   'row:top|top|60.0% 3/5|—|—' 'row:u0|blk|66.7% 2/3|—' 'row:u1|blk|66.7% 2/3|—' \
// RUN:   'row:html-report.sv:3:15|if (FAST) y <= 3;|1|covered|instances' \
// RUN:   'row:html-report.sv:4|else y <= 1;|0|uncovered|' \
// RUN:   '3 / 5 lines, 2 partial' %}

// The page colors source from slang's listing for the chosen standard,
// exactly as obelisk -dump-tokens lists it.
// RUN: obelisk -dump-tokens --std=1800-2023 %s -o %t.2023.tokens
// RUN: obelisk -dump-tokens --std=1800-2017 %s -o %t.2017.tokens
// RUN: obelisk-cov report --format=html --source-root=%S --std=1800-2017 -o %t.2017.html %t.obcov
// RUN: %python -c 'import json,sys; payload=lambda p: json.loads(open(p).read().split("type=\"application/json\">",1)[1].split("</script>",1)[0]); tokens=lambda p: [s["tokens"] for s in payload(p)["sources"]]; assert tokens(sys.argv[1]) == [open(sys.argv[2]).read()]; assert tokens(sys.argv[3]) == [open(sys.argv[4]).read()]' %t.html %t.2023.tokens %t.2017.html %t.2017.tokens
// RUN: not obelisk-cov report --format=html --std=1800-2005 %t.obcov 2>&1 | FileCheck %s --check-prefix=BAD-STD
// BAD-STD: usage:
// BAD-STD: [--std=1800-2017|1800-2023]

// Source that is not UTF-8 is shown with its invalid bytes replaced and no
// coloring, since a JSON payload carries only UTF-8.
// RUN: rm -rf %t.latin && mkdir %t.latin
// RUN: %python -c 'import sys; open(sys.argv[1], "wb").write(b"module latin; // caf\xe9\n  initial $finish;\nendmodule\n")' %t.latin/latin.sv
// RUN: cd %t.latin && %obelisk -O0 --target=native --coverage=line -o sim latin.sv 2>/dev/null
// RUN: %t.latin/sim --coverage-output=%t.latin/latin.obcov
// RUN: obelisk-cov report --format=html --source-root=%t.latin -o %t.latin/latin.html %t.latin/latin.obcov
// RUN: %python -c 'import json,sys; h=open(sys.argv[1], encoding="utf-8").read(); d=json.loads(h.split("type=\"application/json\">",1)[1].split("</script>",1)[0]); [s]=d["sources"]; assert s["status"] == "ok" and s.get("tokens") is None; assert s["text"] == "module latin; // caf�\n  initial $finish;\nendmodule\n"' %t.latin/latin.html
// RUN: %if node %{ %node %S/../Support/CheckCoverageHtml.js %t.latin/latin.html 'row:latin.sv:2|initial $finish;|1|covered|default' %}
