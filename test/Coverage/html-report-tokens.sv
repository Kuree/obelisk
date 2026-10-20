module t;
  initial $finish;
endmodule

/*
  The HTML report colors source from slang's token listing for the chosen
  standard, exactly as obelisk -dump-tokens lists it. A standard the page
  cannot list tokens for is a usage error. This comment is one token, which
  keeps the listing short.

  RUN: cd %S && %obelisk --std=1800-2023 -O0 --coverage=line -o %t.sim html-report-tokens.sv
  RUN: %t.sim --coverage-output=%t.obcov
  RUN: %obelisk -dump-tokens --std=1800-2023 %s -o - | FileCheck %s --check-prefix=TOKENS
  RUN: %obelisk -dump-tokens --std=1800-2017 %s -o - | FileCheck %s --check-prefix=TOKENS
  RUN: obelisk-cov report --format=html --source-root=%S --std=1800-2023 %t.obcov | FileCheck %s --check-prefix=HTML
  RUN: obelisk-cov report --format=html --source-root=%S --std=1800-2017 %t.obcov | FileCheck %s --check-prefix=HTML
  RUN: not obelisk-cov report --format=html --std=1800-2005 %t.obcov 2>&1 | FileCheck %s --check-prefix=BAD-STD

  TOKENS: 0 6 ModuleKeyword
  TOKENS-NEXT: 7 1 Identifier
  TOKENS-NEXT: 8 1 Semicolon
  TOKENS-NEXT: 12 7 InitialKeyword
  TOKENS-NEXT: 20 7 SystemIdentifier
  TOKENS-NEXT: 27 1 Semicolon
  TOKENS-NEXT: 29 9 EndModuleKeyword
  TOKENS-NEXT: 40 {{[0-9]+}} BlockComment
  HTML: "tokens":"0 6 ModuleKeyword\n7 1 Identifier\n8 1 Semicolon\n12 7 InitialKeyword\n20 7 SystemIdentifier\n27 1 Semicolon\n29 9 EndModuleKeyword\n40 {{[0-9]+}} BlockComment\n"}]
  BAD-STD: usage:
  BAD-STD: [--std=1800-2017|1800-2023]
*/
