// RUN: %obelisk --std=1800-2023 -O0 --target=native \
// RUN:   --coverage=functional -o %t.native %s
// RUN: %t.native --coverage-output=%t.native.obcov \
// RUN:   --coverage-test=comments
// RUN: obelisk-cov report --format=text %t.native.obcov -o %t.native.txt
// RUN: obelisk-cov report --format=json %t.native.obcov -o %t.native.json
// RUN: obelisk-cov report --format=html %t.native.obcov -o %t.native.html
// RUN: %python -c 'import json,sys; d=json.load(open(sys.argv[1])); t=d["functional_types"][0]; g=d["functional_instance_groups"][0]; assert t["comment"] == "functional <group> type & comment"; assert g["comment"] == "functional \"group\" \\ path\n</script> & <tag>"; assert g["items"][0]["comment"] == "functional >point< & comment"; assert g["items"][0]["type_comment"] == "functional point type </script> comment"' %t.native.json
// RUN: %python -c 'import json,re,sys; h=open(sys.argv[1]).read(); m=re.search(r"<script[^>]*id=\"coverage-data\"[^>]*>(.*?)</script>",h,re.S); assert m; p=m.group(1); assert all(c != "<" for c in p) and r"\u003c/script>" in p; d=json.loads(p); assert d["functional_types"][0]["comment"] == "functional <group> type & comment"; assert d["functional_instance_groups"][0]["items"][0]["type_comment"] == "functional point type </script> comment"' %t.native.html
// RUN: %obelisk --std=1800-2023 -O3 --execution-tier=bytecode \
// RUN:   --coverage=functional -o %t.bytecode %s
// RUN: %t.bytecode --coverage-output=%t.bytecode.obcov \
// RUN:   --coverage-test=comments
// RUN: obelisk-cov report --format=text %t.bytecode.obcov -o %t.bytecode.txt
// RUN: diff -u %t.native.txt %t.bytecode.txt
// RUN: FileCheck %s --check-prefix=REPORT < %t.native.txt
// RUN: FileCheck %s --check-prefix=JSON < %t.native.json
// RUN: FileCheck %s --check-prefix=HTML < %t.native.html
// RUN: %if node %{ %node %S/../Support/CheckCoverageHtml.js %t.native.html 'functional <group> type & comment' 'functional "group" \ path </script> & <tag>' 'functional >point< & comment' 'type: functional point type </script> comment' %}

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
// REPORT: instance $auto$1: 50.00% (1/2) [per_instance=false]
// REPORT-NEXT: comment:
// REPORT: coverpoint cp: 1/2 (50.00%)
// REPORT-NEXT: comment: "functional >point< & comment"
// REPORT-NEXT: type comment: "functional point type </script> comment"
// JSON: "name":"$auto$1","configuration":{{.*}},"comment":"functional \"group\" \\ path\n</script> & <tag>"
// JSON: "name":"cp","comment":"functional >point< & comment"
// JSON: "type_comment":"functional point type </script> comment"
// HTML: "comment":"functional \u003cgroup> type & comment"
// HTML: "comment":"functional \"group\" \\ path\n\u003c/script> & \u003ctag>"
// HTML: "comment":"functional >point\u003c & comment"
// HTML: "type_comment":"functional point type \u003c/script> comment"
