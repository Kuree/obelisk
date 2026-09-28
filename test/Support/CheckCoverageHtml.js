// Runs the self-contained coverage report app against a minimal DOM, renders
// every view it can route to, and verifies that requested text survives JSON
// transport and rendering exactly.
//
// usage: CheckCoverageHtml.js REPORT.html [--script SOURCE.js] EXPECTED...
//
// EXPECTED is text that must equal some rendered text node or table cell, or
// "row:a|b|c" for a table row whose cells are exactly a, b and c.
//
// Required as a module, it exports loadReport() for renderer unit tests.

const fs = require("fs");
const vm = require("vm");

/* ------------------------------------------------------------ fake DOM */

class Element {
  constructor(tag, id = "") {
    this.tag = tag;
    this.id = id;
    this.innerHTML = "";
    this.textContent = "";
    this.value = "";
    this.checked = false;
    this.children = [];
    this.listeners = new Map();
    this.style = {};
    this.dataset = {};
    const classes = new Set();
    this.classList = {
      add: (c) => classes.add(c),
      remove: (c) => classes.delete(c),
      toggle: (c) => (classes.has(c) ? classes.delete(c) : classes.add(c)),
      contains: (c) => classes.has(c),
    };
  }
  append(child) {
    this.children.push(child);
  }
  addEventListener(kind, callback) {
    this.listeners.set(kind, callback);
  }
  dispatch(kind, event = {}) {
    const callback = this.listeners.get(kind);
    if (callback) callback({ target: this, preventDefault() {}, ...event });
  }
  querySelector() {
    return null;
  }
  querySelectorAll() {
    return [];
  }
  contains() {
    return false;
  }
  closest() {
    return null;
  }
  scrollIntoView() {}
  focus() {}
  blur() {}
  select() {}
}

/* ------------------------------------------------------- HTML to text */

const entities = { amp: "&", lt: "<", gt: ">", quot: '"', "#39": "'", nbsp: " " };
const decode = (s) => s.replace(/&(amp|lt|gt|quot|#39|nbsp);/g, (_, e) => entities[e]);
const VOID = new Set(["input", "br", "img", "meta", "hr"]);
// A tree of {tag, attrs, children} nodes and text strings.
function parse(source) {
  const root = { tag: "#root", attrs: "", children: [] };
  const stack = [root];
  for (const m of source.matchAll(/<!--[\s\S]*?-->|<(\/?)([a-zA-Z][\w-]*)([^>]*)>|[^<]+/g)) {
    const top = stack[stack.length - 1];
    if (m[2] === undefined) {
      if (!m[0].startsWith("<!--")) top.children.push(decode(m[0]));
    } else if (m[1]) {
      const at = stack.map((n) => n.tag).lastIndexOf(m[2].toLowerCase());
      if (at > 0) stack.length = at;
    } else {
      const node = { tag: m[2].toLowerCase(), attrs: m[3], children: [] };
      top.children.push(node);
      if (!VOID.has(node.tag) && !m[3].endsWith("/")) stack.push(node);
    }
  }
  return root;
}
const textOf = (node) =>
  typeof node === "string" ? node : node.children.map(textOf).join("");
const clean = (s) => s.replace(/\s+/g, " ").trim();
const BLOCKS = new Set(["td", "th", "span", "div", "a", "summary", "p", "h1", "h2"]);
// Rendered text: every text node, and the whole text of cells and inline
// elements; rows: the <td> texts of every table row.
function extract(html) {
  const segments = new Set();
  const rows = [];
  const visit = (node) => {
    if (typeof node === "string") {
      const text = clean(node);
      if (text) segments.add(text);
      return;
    }
    if (BLOCKS.has(node.tag)) segments.add(clean(textOf(node)));
    if (node.tag === "tr")
      rows.push(node.children.filter((c) => typeof c !== "string" && c.tag === "td")
        .map((c) => clean(textOf(c))));
    node.children.forEach(visit);
  };
  visit(parse(html));
  return { segments, rows };
}

/* ---------------------------------------------------------- the report */

// The sidebar answers the report's lookups of its own rows from the HTML it
// holds: a caret by key, and a "Show N more" row. Changes are recorded.
class Sidebar extends Element {
  constructor() {
    super("nav", "side");
    this.changes = [];
  }
  querySelector(selector) {
    const changes = this.changes;
    let m = selector.match(/^\.caret\[data-key="(.*)"\]$/);
    if (m) {
      const key = m[1];
      const quoted = key.replace(/[.*+?^${}()|[\]\\]/g, "\\$&");
      const row = this.innerHTML.match(new RegExp(
        `<button class="caret" type="button" data-key="${quoted}" aria-expanded="(true|false)">([^<]*)<`));
      if (!row) return null;
      return {
        getAttribute: () => row[1],
        setAttribute: (name, value) => changes.push([key, name, value]),
        get textContent() { return row[2]; },
        set textContent(value) { changes.push([key, "text", value]); },
      };
    }
    m = selector.match(/^\[data-more="(.*)"\]$/);
    if (m && this.innerHTML.includes(`data-more="${m[1]}"`))
      return { remove: () => changes.push([m[1], "remove"]) };
    return null;
  }
  // Targets made by click() with `inSide` are inside the sidebar.
  contains(target) {
    return Boolean(target?.inSide);
  }
}

// The main view answers queries for source rows (.src .ln with classes, or
// .target) from its HTML, placing line n at n * 100 within its scrolled
// content. Like a browser page, it sits HEADER below the top of the window,
// and offsetTop is measured from <body>, not from the view. Classes added
// through classList last until the view is rendered again.
const HEADER = 46;
class MainView extends Element {
  constructor() {
    super("main", "main");
    this.scrollTop = 0;
    this.clientHeight = 100;
    this.getBoundingClientRect = () => ({ top: HEADER });
    this.scrolledTo = [];
    this.added = new Map();
    this.renderedFor = "";
  }
  querySelectorAll(selector) {
    if (!selector.includes(".src")) return [];
    if (this.renderedFor !== this.innerHTML) {
      this.added = new Map();
      this.renderedFor = this.innerHTML;
    }
    const rows = [...this.innerHTML.matchAll(/<div class="ln ([^"]*)" id="L(\d+)"/g)].map((m) => {
      const line = Number(m[2]);
      if (!this.added.has(line)) this.added.set(line, new Set(m[1].split(" ").filter(Boolean)));
      const classes = this.added.get(line);
      return {
        line, classes,
        offsetTop: HEADER + line * 100,
        getBoundingClientRect: () => ({ top: HEADER + line * 100 - this.scrollTop }),
        classList: { add: (c) => classes.add(c), remove: (c) => classes.delete(c) },
        scrollIntoView: () => this.scrolledTo.push(line),
      };
    });
    const wanted = selector.split(",").map((alternative) =>
      alternative.trim().split(" ").pop().split(".").filter(Boolean).filter((c) => c !== "ln"));
    return rows.filter((row) => wanted.some((classes) => classes.every((c) => row.classes.has(c))));
  }
}

// Run the report script over `payload` (JSON text). Returns helpers to render
// a route, click a view link, pick the "Hits from" test, toggle "Uncovered
// only", and drive the sidebar, keyboard and drawer. With `frozenUrl`,
// assigning location.hash is silently refused, as in the website's sandboxed
// blob: frame.
function loadReport(script, payload, { frozenUrl = false } = {}) {
  const byId = new Map();
  byId.set("side", new Sidebar());
  byId.set("main", new MainView());
  for (const [id, tag] of [
    ["test", "select"], ["uncov", "input"], ["browse", "button"],
  ]) byId.set(id, new Element(tag, id));
  const windowListeners = new Map();
  const documentListeners = [];
  let hash = "";
  // Like a browser, a changed hash fires hashchange.
  const location = {
    get hash() {
      return hash;
    },
    set hash(value) {
      if (frozenUrl || value === hash) return;
      hash = value;
      windowListeners.get("hashchange")?.();
    },
  };
  const body = new Element("body");
  const document = {
    body,
    createElement: (tag) => new Element(tag),
    getElementById: (id) =>
      id === "coverage-data" ? { textContent: payload } : byId.get(id) || null,
    querySelector: (selector) =>
      /^#[\w-]+$/.test(selector) ? byId.get(selector.slice(1)) || null : null,
    querySelectorAll: () => [],
    addEventListener: (kind, callback) => documentListeners.push([kind, callback]),
  };
  const window = {
    addEventListener: (kind, callback) => windowListeners.set(kind, callback),
  };
  vm.runInNewContext(script, {
    document, window, location, CSS: { escape: (s) => s }, console,
  }, { filename: "CoverageReport.js" });
  const main = byId.get("main");
  const side = byId.get("side");
  const testSelect = byId.get("test");
  const uncovered = byId.get("uncov");
  const data = JSON.parse(payload);
  return {
    // Every route the report's data implies.
    routes() {
      const routes = ["#/", "#/tests"];
      for (const s of data.scopes)
        if (s.name !== "$root") routes.push("#/scope/" + encodeURIComponent(s.name));
      for (const f of data.files) routes.push("#/file/" + f.id);
      for (const t of data.functional_types) {
        routes.push("#/cg/" + t.id);
        for (const g of data.functional_instance_groups.filter((g) => g.type === t.id))
          for (const i of g.items) routes.push("#/cg/" + t.id + "/" + encodeURIComponent(i.name));
      }
      return routes;
    },
    render(route) {
      if (location.hash === route) windowListeners.get("hashchange")();
      else location.hash = route;
      return { main: main.innerHTML, side: side.innerHTML };
    },
    // A plain left click on a link to `href`, or with `modifiers` held;
    // `inSide` puts the link in the sidebar.
    click(href, { inSide = false, ...modifiers } = {}) {
      const link = { getAttribute: () => href };
      const target = { inSide, closest: (selector) => (selector.startsWith("a[") ? link : null) };
      let prevented = false;
      for (const [kind, callback] of documentListeners)
        if (kind === "click")
          callback({ target, button: 0, ...modifiers, preventDefault() { prevented = true; } });
      return { main: main.innerHTML, side: side.innerHTML, hash: location.hash, prevented };
    },
    // A click in the sidebar on the element with `key` and class `kind`
    // (caret, dir, or more); `textContent` is what a caret shows.
    clickSide(key, kind, textContent = "▸") {
      const element = {
        dataset: { key }, textContent,
        classList: { contains: (c) => c === kind },
      };
      element.closest = (selector) => (selector === "[data-key]" ? element : null);
      let prevented = false;
      side.dispatch("click", { target: element, preventDefault() { prevented = true; } });
      return { side: side.innerHTML, prevented };
    },
    // Find (Ctrl-F) revealing the hidden element with `key`.
    reveal(key) {
      side.dispatch("beforematch", { target: { dataset: { key } } });
      return side.changes;
    },
    // A key press; `inField` puts focus in a form control.
    press(key, { inField = false, ...modifiers } = {}) {
      const target = { closest: () => (inField ? {} : null) };
      for (const [kind, callback] of documentListeners)
        if (kind === "keydown") callback({ key, target, ...modifiers, preventDefault() {} });
      return main.scrolledTo;
    },
    main,
    // Opening the narrow-layout drawer, and a click elsewhere on the page.
    browse() {
      byId.get("browse").dispatch("click", { stopPropagation() {} });
      return body.classList.contains("show-side");
    },
    clickElsewhere() {
      const target = { closest: () => null };
      for (const [kind, callback] of documentListeners)
        if (kind === "click") callback({ target, button: 0, preventDefault() {} });
      return body.classList.contains("show-side");
    },
    drawerOpen: () => body.classList.contains("show-side"),
    testOptions: () => testSelect.children,
    selectTest(uuid) {
      testSelect.value = uuid;
      testSelect.dispatch("change");
    },
    setUncovered(on) {
      uncovered.checked = on;
      uncovered.dispatch("change");
    },
  };
}

module.exports = { loadReport, extract, decode };

/* ------------------------------------------------------------- the CLI */

if (require.main === module) {
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
  const payloadMatch = html.match(/<script\b[^>]*\bid="coverage-data"[^>]*>([\s\S]*?)<\/script>/);
  const scripts = [
    ...html.matchAll(/<script(?![^>]*id="coverage-data")[^>]*>([\s\S]*?)<\/script>/g),
  ];
  if (!payloadMatch || scripts.length !== 1) {
    console.error("unable to locate coverage payload and report script");
    process.exit(2);
  }
  if (scriptPath && scripts[0][1] !== fs.readFileSync(scriptPath, "utf8")) {
    console.error("embedded report script differs from its source asset");
    process.exit(1);
  }

  const report = loadReport(scripts[0][1], payloadMatch[1]);
  const segments = new Set();
  const rows = [];
  const links = new Set();
  const render = (route) => {
    const out = report.render(route);
    if (/^<h1>Unknown/.test(out.main)) {
      console.error(`route ${route} renders an unknown view`);
      process.exit(1);
    }
    for (const m of (out.main + out.side).matchAll(/href="(#\/[^"]*)"/g))
      links.add(decode(m[1]));
    for (const part of [out.main, out.side]) {
      const found = extract(part);
      for (const s of found.segments) segments.add(s);
      rows.push(...found.rows);
    }
  };
  const routes = report.routes();
  for (const route of routes) render(route);
  // Every view with hits from each test alone must render as well...
  for (const option of report.testOptions()) {
    report.selectTest(option.value);
    for (const route of routes) render(route);
  }
  report.selectTest("");
  // ...and so must every view with only uncovered rows.
  report.setUncovered(true);
  for (const route of routes) render(route);
  report.setUncovered(false);
  // Links found in the views lead to views that exist.
  for (const link of links) render(link);

  for (const expected of expectedArguments) {
    if (expected.startsWith("row:")) {
      const cells = expected.slice(4).split("|");
      if (!rows.some((r) => r.length === cells.length && r.every((c, i) => c === cells[i]))) {
        console.error(`missing rendered row: ${cells.join(" | ")}`);
        for (const r of rows)
          if (r.length === cells.length) console.error(`candidate rendered row: ${r.join(" | ")}`);
        process.exit(1);
      }
      continue;
    }
    if (!segments.has(expected)) {
      console.error(`missing rendered text: ${expected}`);
      console.error([...segments].join("\n"));
      process.exit(1);
    }
  }
}
