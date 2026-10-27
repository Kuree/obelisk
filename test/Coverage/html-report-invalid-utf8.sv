// Source that is not UTF-8 (Inputs/latin.sv: "caf" followed by byte 0xE9) is
// shown with its invalid bytes replaced and no coloring, since a JSON payload
// carries only UTF-8.
// RUN: cd %S/Inputs && %talos --std=1800-2023 -O0 --coverage=line -o %t.sim latin.sv
// RUN: %t.sim --coverage-output=%t.obcov
// RUN: talos-cov report --format=html --source-root=%S/Inputs -o %t.html %t.obcov
// RUN: FileCheck %s < %t.html
// RUN: %if node %{ %node %S/../Support/CheckCoverageHtml.js %t.html 'row:latin.sv:2|initial $finish;|1|covered|default' %}

// CHECK: "path":"latin.sv","status":"ok","text":"module latin; // caf�\n  initial $finish;\nendmodule\n"}]
