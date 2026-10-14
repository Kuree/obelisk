// RUN: %obelisk --std=1800-2023 -O0 --coverage=functional -o %t %s
// RUN: %t --coverage-output=%t.obcov --coverage-test=comments
// RUN: obelisk-cov report --format=text %t.obcov | FileCheck %s --check-prefix=REPORT
// RUN: obelisk-cov report --format=json %t.obcov | FileCheck %s --check-prefix=JSON
// RUN: obelisk-cov report --format=html %t.obcov -o %t.html
// RUN: FileCheck %s --check-prefix=HTML-BLOCK < %t.html
// RUN: FileCheck %s --check-prefix=HTML < %t.html
// RUN: %if node %{ %node %S/../Support/CheckCoverageHtml.js %t.html 'functional <group> type & comment' 'functional "group" \ path </script> & <tag>' 'functional >point< & comment' 'type: functional point type </script> comment' %}

// IEEE 19.7: option.comment and type_option.comment on a covergroup and on a
// coverpoint are string options. The type comment belongs to the type, the
// instance comment to the instance group, and the point comment and its type
// comment to the item; each is reported verbatim in text and JSON.

module top;
  bit sampled;

  covergroup cg(input string group_comment, input string point_comment);
    option.comment = group_comment;
    type_option.comment = "functional <group> type & comment";
    cp: coverpoint sampled {
      option.comment = point_comment;
      type_option.comment = "functional point type </script> comment";
      bins zero = {0};
      bins one = {1};
    }
  endgroup

  cg cov;
  initial begin
    cov = new("functional \"group\" \\ path\n</script> & <tag>",
              "functional >point< & comment");
    sampled = 1;
    cov.sample();
    $finish;
  end
endmodule

// REPORT: type comment: "functional <group> type & comment"
// REPORT: instance $auto$1: 50.00% (1/2) [per_instance=false, distribute_first=false]
// REPORT-NEXT: comment: "functional \"group\" \\ path\n</script> & <tag>"
// REPORT: coverpoint cp: 1/2 (50.00%)
// REPORT-NEXT: comment: "functional >point< & comment"
// REPORT-NEXT: type comment: "functional point type </script> comment"

// JSON: "functional_types":[{"id":{{[0-9]+}},"scope":{{[0-9]+}},"name":"cg","base":0,"comment":"functional <group> type & comment"
// JSON: "functional_instance_groups":[{"type":{{[0-9]+}},"name":"$auto$1","configuration":"{{[0-9a-f]+}}","comment":"functional \"group\" \\ path\n</script> & <tag>"
// JSON: "items":[{"id":{{[0-9]+}},"template_item":{{[0-9]+}},"name":"cp","comment":"functional >point< & comment"
// JSON-SAME: "type_comment":"functional point type </script> comment"

// The HTML data block escapes every '<' so no comment can close the script
// element early; the comments decode to the same text as in JSON.
// HTML-BLOCK: <script id="coverage-data" type="application/json">{{[^<]*$}}
// HTML-BLOCK-NEXT: </script>
// HTML: "comment":"functional \u003cgroup> type & comment"
// HTML: "comment":"functional \"group\" \\ path\n\u003c/script> & \u003ctag>"
// HTML: "comment":"functional >point\u003c & comment"
// HTML: "type_comment":"functional point type \u003c/script> comment"
