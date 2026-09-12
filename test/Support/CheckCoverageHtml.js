// Executes the self-contained coverage report script against a minimal DOM and
// verifies that requested text survives JSON transport and rendering exactly.

const fs = require("fs");
const vm = require("vm");

if (process.argv.length < 4) {
  console.error(
    "usage: CheckCoverageHtml.js REPORT.html [--script SOURCE.js] EXPECTED...",
  );
  process.exit(2);
}

const reportPath = process.argv[2];
let expectedArguments = process.argv.slice(3);
let scriptPath;
if (expectedArguments[0] === "--script") {
  if (expectedArguments.length < 3) {
    console.error("--script requires a source path and an expectation");
    process.exit(2);
  }
  scriptPath = expectedArguments[1];
  expectedArguments = expectedArguments.slice(2);
}

const html = fs.readFileSync(reportPath, "utf8");
const payloadMatch = html.match(
  /<script[^>]*id="coverage-data"[^>]*type="application\/json"[^>]*>([\s\S]*?)<\/script>/,
);
const scripts = [
  ...html.matchAll(
    /<script(?![^>]*id="coverage-data")[^>]*>([\s\S]*?)<\/script>/g,
  ),
];
if (!payloadMatch || scripts.length !== 1) {
  console.error("unable to locate coverage payload and report script");
  process.exit(2);
}
if (scriptPath && scripts[0][1] !== fs.readFileSync(scriptPath, "utf8")) {
  console.error("embedded report script differs from its source asset");
  process.exit(1);
}

class Element {
  constructor(tag) {
    this.tag = tag;
    this.children = [];
    this.textContent = "";
    this.value = "";
    this.hidden = false;
    this.className = "";
    this.id = "";
    this.href = "";
    this.listeners = new Map();
  }

  append(child) {
    this.children.push(child);
  }

  addEventListener(kind, callback) {
    this.listeners.set(kind, callback);
  }

  dispatch(kind) {
    const callback = this.listeners.get(kind);
    if (callback) callback();
  }

}

const sinks = new Map();
const elements = new Map();
for (const id of ["identity", "test-filter", "file-nav", "hierarchy-nav"])
  elements.set(id, new Element(id === "test-filter" ? "select" : "div"));
const identity = elements.get("identity");
const document = {
  createElement: (tag) => new Element(tag),
  getElementById: (id) =>
    id === "coverage-data"
      ? {textContent: payloadMatch[1]}
      : elements.get(id) || new Element("div"),
  querySelector: (selector) => {
    if (!sinks.has(selector)) sinks.set(selector, new Element("container"));
    return sinks.get(selector);
  },
};

vm.runInNewContext(scripts[0][1], {document});

const testFilter = elements.get("test-filter");
if (testFilter.children.length > 1) {
  testFilter.value = testFilter.children[1].value;
  testFilter.dispatch("change");
  const taggedRows = [...sinks.values()].flatMap((sink) => sink.children).filter(
    (row) => row.coverageTests,
  );
  if (!taggedRows.some((row) => row.coverageTests.has(testFilter.value) && !row.hidden)) {
    console.error("test filter hid every matching row");
    process.exit(1);
  }
  if (
    taggedRows.some(
      (row) => !row.coverageTests.has(testFilter.value) && !row.hidden,
    )
  ) {
    console.error("test filter left a nonmatching row visible");
    process.exit(1);
  }
  testFilter.value = "";
  testFilter.dispatch("change");
}

const targetIds = new Set(
  [...html.matchAll(/\sid="([^"]+)"/g)].map((match) => match[1]),
);
for (const sink of sinks.values())
  for (const row of sink.children)
    if (row.id) targetIds.add(row.id);
for (const id of ["file-nav", "hierarchy-nav"])
  for (const link of elements.get(id).children)
    if (!link.href.startsWith("#") || !targetIds.has(link.href.slice(1))) {
      console.error(`broken ${id} link: ${link.href}`);
      process.exit(1);
    }

const rendered = [identity.textContent];
for (const sink of sinks.values())
  for (const row of sink.children)
    for (const cell of row.children) rendered.push(String(cell.textContent));
for (const element of elements.values())
  for (const child of element.children) rendered.push(String(child.textContent));

for (const expected of expectedArguments) {
  if (expected.startsWith("row:")) {
    const cells = expected.slice(4).split("|");
    const found = [...sinks.values()].some((sink) =>
      sink.children.some(
        (row) =>
          row.children.length === cells.length &&
          row.children.every(
            (cell, index) => String(cell.textContent) === cells[index],
          ),
      ),
    );
    if (!found) {
      console.error(`missing rendered row: ${cells.join(" | ")}`);
      for (const sink of sinks.values())
        for (const row of sink.children)
          if (row.children.length === cells.length)
            console.error(
              `candidate rendered row: ${row.children.map((cell) => String(cell.textContent)).join(" | ")}`,
            );
      process.exit(1);
    }
    continue;
  }
  if (!rendered.includes(expected)) {
    console.error(`missing rendered cell: ${expected}`);
    console.error(rendered.join("\n"));
    process.exit(1);
  }
}
