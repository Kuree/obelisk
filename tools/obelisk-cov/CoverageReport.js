'use strict';
// The Obelisk coverage report: a single-page app over the payload obelisk-cov
// embeds in the page. Views are hash routes (#/scope/<name>, #/file/<id>,
// #/cg/<type>, #/tests) so every view has a link, and only the current view
// is rendered, which keeps large designs responsive. Everything it shows is
// derived from the payload; the page loads nothing else.

const d = JSON.parse(document.getElementById('coverage-data').textContent);
const $ = (selector, root = document) => root.querySelector(selector);
const esc = s => String(s).replace(/[&<>"]/g, c =>
  ({'&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;'}[c]));
const num = v => Number(v);

// Filters shared by every view.
const state = {test: '', uncov: false};

/* ------------------------------------------------------------ indices */

const files = new Map(d.files.map(f => [f.id, f]));
const scopes = new Map(d.scopes.map(s => [s.id, s]));
const scopeByName = new Map(d.scopes.map(s => [s.name, s]));
// numbered instances and files read in order: u_2 before u_10
const byName = new Intl.Collator(undefined, {numeric: true}).compare;
const children = new Map();
for (const s of d.scopes) {
  if (!children.has(s.parent)) children.set(s.parent, []);
  children.get(s.parent).push(s);
}
for (const kids of children.values()) kids.sort((a, b) => byName(a.name, b.name));
const isRoot = s => !scopes.has(s.parent) || scopes.get(s.parent).name === '$root';
const rootScopes = d.scopes.filter(s => s.name !== '$root' && isRoot(s));

// Tests that share a name are told apart by their UUID.
const runNameCounts = new Map();
for (const r of d.runs) runNameCounts.set(r.name, (runNameCounts.get(r.name) || 0) + 1);
const runLabels = new Map(d.runs.map(r =>
  [r.uuid, runNameCounts.get(r.name) > 1 ? r.name + ' (' + r.uuid + ')' : r.name]));
const uniqueIds = c => [...new Set(c.map(v => v.uuid))];
const testNames = c => uniqueIds(c).map(id => runLabels.get(id) || id).join(', ');

/* ----------------------------------------------------------- counters */

// A counter's hits, from every merged test or only the selected one. Counts
// are 64-bit and may be saturated, so they stay BigInt or text.
const countOf = c => state.test
  ? (c.contributors || []).filter(x => x.uuid === state.test)
      .reduce((sum, x) => sum + BigInt(x.count), 0n)
  : BigInt(c.count);
const counterText = c => state.test
  ? String(countOf(c))
  : String(c.count) + (c.overflow ? ' (saturated)' : '');
// Covered as obelisk-cov decided, or by the selected test's own hits.
const isHit = (c, atLeast = 1n) => state.test ? countOf(c) >= BigInt(atLeast) : !!c.covered;

/* -------------------------------------------- functional coverage text */

const itemKinds = {1: 'coverpoint', 2: 'cross'};
const binKinds = {1: 'state', 2: 'transition', 3: 'cross'};
const binRoles = f => [[1, 'default'], [2, 'default sequence'], [4, 'ignore'],
                       [8, 'illegal'], [16, 'wildcard'], [64, 'automatic'],
                       [128, 'empty']]
  .filter(([m]) => f & m).map(([, n]) => n);
const valueSets = new Map(d.resolved_functional_value_sets.map(
  v => [v.type + '|' + v.configuration + '|' + v.id, v]));
const hex = v => BigInt(v).toString(16).replace(/^0+(?=.)/, '') || '0';
const words = v => v.slice().reverse()
  .map((n, i) => i ? BigInt(n).toString(16).padStart(16, '0') : hex(n))
  .join('').replace(/^0+(?=.)/, '') || '0';
const endpoint = (a, high) => {
  const av = words(a.limbs.map(l => high ? l.high_aval : l.low_aval));
  const bv = words(a.limbs.map(l => high ? l.high_bval : l.low_bval));
  const wm = words(a.limbs.map(l => l.wildcard_mask));
  return bv !== '0' || wm !== '0'
    ? 'aval=0x' + av + ',bval=0x' + bv + ',wildcard=0x' + wm
    : '0x' + av;
};
const valueText = (type, configuration, id) => {
  const s = valueSets.get(type + '|' + configuration + '|' + id);
  if (!s) return 'set ' + id;
  return s.atoms.map(a => {
    if (num(a.kind) === 3)
      return 'real-bits[0x' + hex(a.real_low_bits) + ':0x' + hex(a.real_high_bits) + ']';
    const low = endpoint(a, false);
    return num(a.kind) === 2 ? '[' + low + ':' + endpoint(a, true) + ']' : low;
  }).join(', ');
};
const groupBy = (rows, key) => {
  const map = new Map();
  for (const row of rows) {
    const k = key(row);
    if (!map.has(k)) map.set(k, []);
    map.get(k).push(row);
  }
  return map;
};
const alternativesByBin = groupBy(d.resolved_transition_alternatives,
  a => a.type + '|' + a.configuration + '|' + a.bin);
const stepsByAlternative = groupBy(d.resolved_transition_steps,
  s => s.type + '|' + s.configuration + '|' + s.alternative);
const templateSteps = new Map(d.transition_steps.map(
  s => [s.bin + '|' + s.alternative_ordinal + '|' + s.ordinal, s]));
const repeatText = (kind, lo, hi) => {
  if (num(kind) === 1) return '';
  const op = num(kind) === 2 ? '*' : num(kind) === 3 ? '->' : '=';
  const upper = hi === '18446744073709551615' ? '$' : hi;
  return '[' + op + lo + (upper === lo ? '' : ':' + upper) + ']';
};
const transitionText = (g, b) =>
  (alternativesByBin.get(g.type + '|' + g.configuration + '|' + b.id) || []).map(a =>
    (stepsByAlternative.get(g.type + '|' + g.configuration + '|' + a.id) || [])
      .sort((x, y) => num(x.ordinal) - num(y.ordinal))
      .map(s => {
        const t = templateSteps.get(b.template_bin + '|' + a.template_alternative_ordinal + '|' + s.ordinal);
        return valueText(g.type, g.configuration, s.value_set) +
               repeatText(t ? t.repetition : 1, s.lower_bound, s.upper_bound);
      })
      .join(' => '))
    .join(' or ');
const templateItems = new Map(d.functional_item_templates.map(i => [i.id, i.name]));
const templateBins = new Map(d.functional_bin_templates.map(b => [b.id, b.name]));
const selectorNodes = new Map(d.cross_selector_nodes.map(n => [n.id, n]));
const selectorOperands = new Map();
for (const o of d.cross_selector_operands) {
  if (!selectorOperands.has(o.node)) selectorOperands.set(o.node, []);
  selectorOperands.get(o.node).push(o.operand);
}
const selectorBindings = new Map(d.resolved_cross_selector_bindings.map(
  b => [b.type + '|' + b.configuration + '|' + b.cross + '|' + b.node, b]));
const tuplesBySet = groupBy(d.resolved_functional_tuples, t => t.tuple_set);
const componentsByTuple = groupBy(d.resolved_functional_tuple_components, c => c.tuple);
const resolvedBins = new Map(d.resolved_functional_bins.map(
  b => [b.type + '|' + b.configuration + '|' + b.id, b]));
const automaticNodes = new Map(d.resolved_cross_automatic_nodes.map(
  n => [n.type + '|' + n.configuration + '|' + n.cross + '|' + n.id, n]));
const automaticEdges = groupBy(d.resolved_cross_automatic_edges,
  e => e.type + '|' + e.configuration + '|' + e.cross + '|' + e.node);
const automaticContains = (g, i, components) => {
  let node = i.automatic_root_node;
  for (let dimension = 0; dimension !== components.length; ++dimension) {
    const key = g.type + '|' + g.configuration + '|' + i.id + '|' + node;
    if (!automaticNodes.has(key)) return false;
    const edge = (automaticEdges.get(key) || []).find(e =>
      (resolvedBins.get(g.type + '|' + g.configuration + '|' + e.bin)?.name || '') === components[dimension]);
    if (!edge) return false;
    if (dimension + 1 === components.length) return edge.child === '0';
    if (edge.child === '0') return false;
    node = edge.child;
  }
  return false;
};
const tupleSetText = (g, id) => (tuplesBySet.get(id) || []).map(t => '<' +
  (componentsByTuple.get(t.id) || [])
    .sort((a, b) => num(a.ordinal) - num(b.ordinal))
    .map(c => c.bin !== '0'
      ? (resolvedBins.get(g.type + '|' + g.configuration + '|' + c.bin)?.name || c.bin)
      : valueText(g.type, g.configuration, c.value_set))
    .join(',') + '>').join(', ');
const selectorText = (g, i, root) => {
  const render = (id, active) => {
    if (active.has(id)) return '<cycle>';
    const n = selectorNodes.get(id);
    if (!n) return 'selector ' + id;
    const next = new Set(active);
    next.add(id);
    const ops = (selectorOperands.get(id) || []).map(v => render(v, next));
    const kind = num(n.kind);
    if (kind === 1)
      return 'binsof(' + (templateItems.get(n.target) || n.target) +
             (n.bin !== '0' ? '.' + (templateBins.get(n.bin) || n.bin) : '') + ')';
    if (kind === 2) return '!(' + ops[0] + ')';
    if (kind === 3 || kind === 4) return '(' + ops.join(kind === 3 ? ' && ' : ' || ') + ')';
    const binding = selectorBindings.get(g.type + '|' + g.configuration + '|' + i.id + '|' + id);
    if (kind === 5)
      return 'intersect {' +
             (binding && binding.tuple_set !== '0' ? tupleSetText(g, binding.tuple_set)
              : binding ? valueText(g.type, g.configuration, binding.value_set)
              : 'set ' + n.value_set) + '}';
    if (kind === 6) return 'all tuples';
    if (kind === 7)
      return (ops[0] || 'all tuples') + ' with expression ' +
             (binding ? binding.with_expression : n.with_expression);
    return 'selector ' + id;
  };
  return render(root, new Set());
};
const binName = (g, i, b) => num(b.kind) === 2 ? b.name + ' = ' + transitionText(g, b)
  : num(b.kind) === 3 && b.cross_selector !== '0' ? b.name + ' = ' + selectorText(g, i, b.cross_selector)
  : b.name;
const binStatus = b => {
  const states = [];
  if (b.flags & 8) states.push('illegal');
  if (b.flags & 4) states.push('ignored');
  if (b.flags & 128) states.push('empty');
  if (b.exclusion_reason) states.push('excluded: ' + b.exclusion_reason);
  if (!states.length)
    states.push(b.contributing ? (isHit(b, b.at_least) ? 'covered' : 'uncovered') : 'non-contributing');
  return states.join('; ');
};

/* ------------------------------------------------ unique contributions */

// A test's unique contributions are the obligations that would fall below
// their at_least threshold without it. This follows obelisk-cov's merge
// rules, including merge_instances and automatic cross containment.
function uniqueContributions() {
  const ranking = new Map(d.runs.map(r =>
    [r.uuid, {uuid: r.uuid, name: runLabels.get(r.uuid), line: 0, toggle: 0, functional: 0}]));
  const awardCritical = (contributors, atLeast, metric) => {
    const counts = new Map();
    for (const c of contributors) counts.set(c.uuid, (counts.get(c.uuid) || 0n) + BigInt(c.count));
    let total = 0n;
    for (const count of counts.values()) total += count;
    const threshold = BigInt(atLeast);
    if (total < threshold) return;
    for (const [uuid, count] of counts)
      if (total - count < threshold && ranking.has(uuid)) ranking.get(uuid)[metric]++;
  };
  for (const p of d.line_points)
    if (p.covered && !p.excluded) awardCritical(p.contributors, 1, 'line');
  for (const o of d.toggles)
    if (!o.excluded)
      for (const b of o.bits) {
        if (b.zero_to_one.covered) awardCritical(b.zero_to_one.contributors, 1, 'toggle');
        if (b.one_to_zero.covered) awardCritical(b.one_to_zero.contributors, 1, 'toggle');
      }
  for (const t of d.functional_types) {
    const groups = d.functional_instance_groups.filter(g => g.type === t.id);
    const cumulativeAtLeast = new Map();
    for (const g of groups)
      for (const i of g.items) {
        const threshold = BigInt(i.at_least);
        if (threshold > (cumulativeAtLeast.get(i.template_item) || 0n))
          cumulativeAtLeast.set(i.template_item, threshold);
      }
    if (!t.merge_instances) {
      for (const g of groups)
        for (const i of g.items) {
          const threshold = cumulativeAtLeast.get(i.template_item) || 0n;
          for (const b of i.bins)
            if (b.contributing) awardCritical(b.contributors, threshold, 'functional');
          for (const b of i.automatic_bins) awardCritical(b.contributors, threshold, 'functional');
        }
      continue;
    }
    const merged = new Map(), automaticProfiles = new Map();
    const addMerged = (key, b, threshold) => {
      let value = merged.get(key);
      if (!value) merged.set(key, value = {atLeast: 0n, contributors: []});
      if (threshold > value.atLeast) value.atLeast = threshold;
      value.contributors.push(...b.contributors);
      return value;
    };
    for (const g of groups)
      for (const i of g.items) {
        if (!i.aggregating) continue;
        const prefix = i.template_item + '|' + i.name + '|';
        const threshold = cumulativeAtLeast.get(i.template_item) || 0n;
        for (const b of i.bins)
          if (b.contributing) addMerged(prefix + 'bin|' + b.name, b, threshold);
        if (BigInt(i.automatic_total) !== 0n) {
          if (!automaticProfiles.has(prefix)) automaticProfiles.set(prefix, []);
          automaticProfiles.get(prefix).push({group: g, item: i});
        }
        for (const b of i.automatic_bins) {
          const value = addMerged(prefix + 'auto|' + b.name, b, threshold);
          value.profile = prefix;
          value.components = b.components.map(c => c.name);
        }
      }
    for (const value of merged.values()) {
      if (value.profile && !(automaticProfiles.get(value.profile) || []).some(
          p => automaticContains(p.group, p.item, value.components)))
        continue;
      awardCritical(value.contributors, value.atLeast, 'functional');
    }
  }
  return [...ranking.values()].sort((a, b) =>
    (b.line + b.toggle + b.functional) - (a.line + a.toggle + a.functional) ||
    a.name.localeCompare(b.name) || a.uuid.localeCompare(b.uuid));
}

/* -------------------------------------------------------------- totals */

const groupsByType = groupBy(d.functional_instance_groups, g => g.type);
// What each scope and file view lists, gathered once.
const pointsByScope = groupBy(d.line_points, p => p.scope);
const pointsByFile = groupBy(d.line_points, p => p.file);
const togglesByScope = groupBy(d.toggles, t => t.scope);
const typesByScope = groupBy(d.functional_types, t => t.scope);
const groupsOfType = t => groupsByType.get(t.id) || [];

// Directory tree of source files; each directory knows every file beneath it.
const fileTree = {dirs: new Map(), files: []};
for (const f of d.files) {
  // an absolute path's leading '' segment becomes a directory shown as "/"
  let dir = fileTree;
  for (const name of f.path.split('/').slice(0, -1)) {
    if (!dir.dirs.has(name)) dir.dirs.set(name, {dirs: new Map(), files: []});
    dir = dir.dirs.get(name);
  }
  dir.files.push(f);
}
(function sortTree(dir) {
  dir.files.sort((a, b) => byName(a.path, b.path));
  for (const sub of dir.dirs.values()) sortTree(sub);
})(fileTree);

// Totals for every scope, file and directory, computed in one pass and kept
// until the "Hits from" test changes. A source line (file:line) is covered
// when every included statement on it was hit in every instance counted, so
// subtree line totals merge per-line tallies instead of adding counts; the
// smaller map is merged into the larger one to keep the pass near linear.
// Functional coverage is obelisk-cov's weighted result, which is only known
// for all tests together.
let rollupCache = null;
function rollups() {
  if (rollupCache && rollupCache.test === state.test) return rollupCache;
  const tally = (map, key, hit) => {
    const u = map.get(key);
    if (u) { u.inc++; u.hit += hit; } else map.set(key, {inc: 1, hit});
  };
  const summarize = units => {
    let cov = 0, partial = 0;
    for (const u of units.values()) {
      if (u.hit === u.inc) cov++;
      else if (u.hit) partial++;
    }
    return {cov, tot: units.size, partial};
  };
  const scopeLines = new Map(), fileLines = new Map();
  for (const p of d.line_points) {
    if (p.excluded) continue;
    const hit = isHit(p) ? 1 : 0;
    if (!scopeLines.has(p.scope)) scopeLines.set(p.scope, new Map());
    tally(scopeLines.get(p.scope), p.file + ':' + p.line, hit);
    if (!fileLines.has(p.file)) fileLines.set(p.file, new Map());
    tally(fileLines.get(p.file), p.line, hit);
  }
  const scopeToggles = new Map();
  for (const t of d.toggles) {
    if (t.excluded) continue;
    const x = scopeToggles.get(t.scope) || {cov: 0, tot: 0};
    for (const b of t.bits) {
      x.tot += 2;
      if (isHit(b.zero_to_one)) x.cov++;
      if (isHit(b.one_to_zero)) x.cov++;
    }
    scopeToggles.set(t.scope, x);
  }
  const scopeFunctional = new Map();
  if (!state.test)
    for (const t of d.functional_types) {
      const x = scopeFunctional.get(t.scope) || {w: 0, s: 0};
      x.w += num(t.weight);
      x.s += num(t.weight) * num(t.percent);
      scopeFunctional.set(t.scope, x);
    }
  const result = {test: state.test, self: new Map(), tree: new Map(), file: new Map(), dir: new Map()};
  const pack = (line, toggle, fn) =>
    ({line, toggle, functional: fn.w ? {pct: fn.s / fn.w} : null, fnw: fn.w, fns: fn.s});
  const merge = maps => {
    let big = maps[0];
    for (const m of maps) if (m.size > big.size) big = m;
    for (const m of maps) {
      if (m === big) continue;
      for (const [k, v] of m) {
        const u = big.get(k);
        if (u) { u.inc += v.inc; u.hit += v.hit; } else big.set(k, v);
      }
    }
    return big;
  };
  // returns the subtree's per-line tallies for the parent to merge
  const visit = s => {
    const own = scopeLines.get(s.id) || new Map();
    const toggle = {...(scopeToggles.get(s.id) || {cov: 0, tot: 0})};
    const fn = {...(scopeFunctional.get(s.id) || {w: 0, s: 0})};
    result.self.set(s.id, pack(summarize(own), {...toggle}, fn));
    const maps = [new Map([...own].map(([k, v]) => [k, {...v}]))];
    for (const c of children.get(s.id) || []) {
      maps.push(visit(c));
      const ct = result.tree.get(c.id);
      toggle.cov += ct.toggle.cov;
      toggle.tot += ct.toggle.tot;
      fn.w += ct.fnw;
      fn.s += ct.fns;
    }
    const lines = merge(maps);
    result.tree.set(s.id, pack(summarize(lines), toggle, fn));
    return lines;
  };
  const tops = d.scopes.filter(s => !scopes.has(s.parent));
  const all = tops.length ? merge(tops.map(visit)) : new Map();
  const allToggle = {cov: 0, tot: 0}, allFn = {w: 0, s: 0};
  for (const s of tops) {
    const x = result.tree.get(s.id);
    allToggle.cov += x.toggle.cov;
    allToggle.tot += x.toggle.tot;
    allFn.w += x.fnw;
    allFn.s += x.fns;
  }
  result.all = pack(summarize(all), allToggle, allFn);
  for (const f of d.files) result.file.set(f.id, summarize(fileLines.get(f.id) || new Map()));
  // file line keys never overlap, so directory totals add
  (function dirVisit(dir) {
    const x = {cov: 0, tot: 0};
    for (const sub of dir.dirs.values()) {
      const y = dirVisit(sub);
      x.cov += y.cov;
      x.tot += y.tot;
    }
    for (const f of dir.files) {
      const y = result.file.get(f.id);
      x.cov += y.cov;
      x.tot += y.tot;
    }
    result.dir.set(dir, x);
    return x;
  })(fileTree);
  rollupCache = result;
  return result;
}
// mean of the metrics a scope has, for the sidebar
function score(x) {
  const parts = [];
  if (x.line.tot) parts.push(100 * x.line.cov / x.line.tot);
  if (x.toggle.tot) parts.push(100 * x.toggle.cov / x.toggle.tot);
  if (x.functional) parts.push(x.functional.pct);
  return parts.length ? parts.reduce((a, b) => a + b, 0) / parts.length : undefined;
}

/* -------------------------------------------------------- source text */

// Copied from web/sv-highlight.js and web/embed-snippet.js (a web test keeps
// the copies identical) so source is colored exactly like the website: token
// kinds come from slang's lexer, which obelisk-cov runs over each file.
const TYPES = [
  'logic', 'bit', 'reg', 'wire', 'byte', 'shortint', 'int', 'longint',
  'integer', 'time', 'real', 'shortreal', 'realtime', 'string', 'chandle',
  'event', 'void', 'signed', 'unsigned', 'tri', 'triand', 'trior', 'tri0',
  'tri1', 'trireg', 'wand', 'wor', 'supply0', 'supply1', 'uwire', 'var',
];
const TYPE_SET = new Set(TYPES);
// BEGIN tokensFromListing (copy of web/embed-snippet.js)
// slang TokenKind and TriviaKind names, as `obelisk -dump-tokens` prints them.
const LISTING_KINDS = {
  LineComment: 'comment',
  BlockComment: 'comment',
  StringLiteral: 'string',
  IncludeFileName: 'string',
  IntegerLiteral: 'number',
  IntegerBase: 'number',
  UnbasedUnsizedLiteral: 'number',
  RealLiteral: 'number',
  TimeLiteral: 'number',
  SystemIdentifier: 'system',
  Directive: 'directive',
  MacroUsage: 'directive',
};

/**
 * Turn the driver's token listing for `source` into [kind, text] pairs like
 * tokenize() returns. slang decides what is a keyword for the chosen language
 * version; TYPES only picks which keywords get the type color. Returns null
 * when the listing does not describe this source.
 */
function tokensFromListing(source, listing) {
  // Listing offsets count UTF-8 bytes; map them to string indices.
  const indexOfByte = new Map([[0, 0]]);
  let bytes = 0;
  for (let index = 0; index < source.length;) {
    const point = source.codePointAt(index);
    bytes += point < 0x80 ? 1 : point < 0x800 ? 2 : point < 0x10000 ? 3 : 4;
    index += point > 0xffff ? 2 : 1;
    indexOfByte.set(bytes, index);
  }

  const tokens = [];
  const push = (kind, text) => {
    if (!text) return;
    const last = tokens[tokens.length - 1];
    if (last && last[0] === kind) last[1] += text;
    else tokens.push([kind, text]);
  };
  let position = 0;
  // After a base ('h, 'sd, ...) slang lists the digits as separate tokens:
  // integers, identifiers (A5, ff), and ? for don't-care bits, possibly after
  // whitespace. They are all part of the number.
  let based = null;
  for (const line of listing.split('\n')) {
    const match = /^(\d+) (\d+) (\w+)$/.exec(line);
    if (!match) continue;
    const start = indexOfByte.get(Number(match[1]));
    const end = indexOfByte.get(Number(match[1]) + Number(match[2]));
    if (start === undefined || end === undefined || start < position) return null;
    const text = source.slice(start, end);
    let kind = LISTING_KINDS[match[3]] ?? '';
    if (match[3].endsWith('Keyword')) kind = TYPE_SET.has(text) ? 'type' : 'keyword';
    const gap = source.slice(position, start);
    if (based && /^[\da-fA-FxXzZ?_]+$/.test(text) &&
        (based === 'first' ? /^\s*$/.test(gap) : gap === '')) {
      kind = 'number';
      based = 'more';
    } else {
      based = match[3] === 'IntegerBase' ? 'first' : null;
    }
    push('', source.slice(position, start));
    push(kind, text);
    position = end;
  }
  push('', source.slice(position));
  return tokens.map(([, text]) => text).join('') === source ? tokens : null;
}
// END tokensFromListing

const sourceByFile = new Map((d.sources || []).map(s => [s.file, s]));
// Highlighted lines of a file as HTML strings, built the first time the file
// is shown. Without a listing that describes the text, it stays uncolored.
const highlighted = new Map();
function sourceLines(fid) {
  if (highlighted.has(fid)) return highlighted.get(fid);
  const s = sourceByFile.get(fid);
  let lines = null;
  if (s && typeof s.text === 'string') {
    const tokens = (s.tokens && tokensFromListing(s.text, s.tokens)) || [['', s.text]];
    lines = [''];
    for (const [kind, part] of tokens)
      part.split('\n').forEach((piece, i) => {
        if (i) lines.push('');
        if (piece) lines[lines.length - 1] += kind ? `<span class="tk-${kind}">${esc(piece)}</span>` : esc(piece);
      });
    if (s.text.endsWith('\n')) lines.pop();
  }
  highlighted.set(fid, lines);
  return lines;
}
const sourceStatus = fid => {
  const s = sourceByFile.get(fid);
  return !s || s.status === 'unavailable' ? 'Source unavailable'
    : s.status === 'mismatch' ? 'Source digest mismatch' : '';
};

/* ----------------------------------------------------------- rendering */

const lvl = p => p >= 90 ? 'good' : p >= 60 ? 'warn' : 'bad';
function covCell(cov, tot, pct) {
  if (pct === undefined) {
    if (!tot) return '<span class="dash">—</span>';
    pct = 100 * cov / tot;
  }
  const frac = tot !== undefined ? ` <span class="dash">${cov}/${tot}</span>` : '';
  return `<span class="cov"><span class="bar"><i class="lvl-${lvl(pct)}" data-w="${pct.toFixed(1)}"></i></span>` +
         `<span class="lvl-${lvl(pct)}">${pct.toFixed(1)}%</span>${frac}</span>`;
}
const pctCell = pct => pct === null || pct === undefined ? '<span class="dash">—</span>' : covCell(0, undefined, pct);
function statusTag(s) {
  const base = s.split(/[:;]/)[0];
  const cls = {covered: 'good', partial: 'warn', uncovered: 'bad', missing: 'bad', illegal: 'bad',
               excluded: 'none', ignored: 'none', empty: 'none', 'non-contributing': 'none'}[base] || 'none';
  return `<span class="tag ${cls}">${esc(s)}</span>`;
}
const table = (heads, rows) => rows.length
  ? '<table><thead><tr>' + heads.map(h => h.startsWith('#')
      ? `<th class="num">${h.slice(1)}</th>` : `<th>${h}</th>`).join('') +
    '</tr></thead><tbody>' + rows.join('') + '</tbody></table>'
  : '';
const row = cells => '<tr>' + cells.map(c => typeof c === 'object'
  ? `<td class="num">${c.num}</td>` : `<td>${c}</td>`).join('') + '</tr>';
const scopeHref = name => '#/scope/' + encodeURIComponent(name);
const fileHref = (id, line) => '#/file/' + id + (line ? '/L' + line : '');
const cgHref = (t, item) => '#/cg/' + t.id + (item ? '/' + encodeURIComponent(item) : '');
const fileLoc = (fid, line) =>
  `<a href="${fileHref(fid, line)}" class="mono">${esc((files.get(fid) || {}).path || fid)}:${line}</a>`;
const fmtTime = ns => {
  const t = new Date(num(ns) / 1e6);
  return num(ns) && !isNaN(t) ? t.toISOString().replace('T', ' ').slice(0, 19) : '—';
};
const plural = (n, word) => `${n.toLocaleString()} ${word}${n === 1 ? '' : 's'}`;

/* ------------------------------------------------------------- sidebar */

// The whole sidebar is in the page, so the browser's find (Ctrl-F) reaches
// every scope, covergroup and file. Collapsed levels, and the rows of a long
// level past its first SIDE_LIMIT, are hidden="until-found": find reveals
// them, and the beforematch handler records that they are now open.
const treeOpen = new Map();
let currentRoute = [];
const SIDE_LIMIT = 100;

// The sidebar entry for a route: a signal belongs to its scope, an item to
// its covergroup, a line to its file.
function sideHref(route) {
  if (route[0] === 'scope') return scopeHref(route[1]);
  if (route[0] === 'cg') return '#/cg/' + route[1];
  if (route[0] === 'file') return fileHref(route[1]);
  if (route[0] === 'tests') return '#/tests';
  return '#/';
}

function renderSide(route) {
  const totals = rollups();
  const cur = sideHref(route);
  const pctSpan = pct => pct === undefined || pct === null ? ''
    : `<span class="pct lvl-${lvl(pct)}">${pct.toFixed(0)}%</span>`;
  const link = (href, name, pct) =>
    `<a href="${href}" class="${cur === href ? 'current' : ''}"><span class="name">${name}</span>${pctSpan(pct)}</a>`;
  // A tree node is open if the user opened it, or by default when it is near
  // the root or on the path to what the main view is showing.
  const isOpen = (key, depth, onPath) => treeOpen.has(key) ? treeOpen.get(key) : depth < 1 || onPath;
  const node = (key, open, hasKids, inner) =>
    `<div class="row"><button class="caret" type="button" data-key="${esc(key)}" aria-expanded="${open}">` +
    `${hasKids ? (open ? '▾' : '▸') : ''}</button>${inner}</div>`;
  const kids = (key, open, inner) =>
    `<div class="kids" data-key="${esc(key)}"${open ? '' : ' hidden="until-found"'}>${inner}</div>`;
  // A long level: its first rows and whatever is on the current path, then
  // the rest hidden until found or until "Show N more".
  const level = (key, list, onPath, render) => {
    if (list.length <= SIDE_LIMIT || treeOpen.get('all:' + key)) return list.map(render).join('');
    const shown = list.slice(0, SIDE_LIMIT), rest = [];
    for (const x of list.slice(SIDE_LIMIT)) (onPath(x) ? shown : rest).push(x);
    if (!rest.length) return shown.map(render).join('');
    return shown.map(render).join('') +
      `<div class="rest" data-key="all:${esc(key)}" hidden="until-found">${rest.map(render).join('')}</div>` +
      `<div class="row" data-more="all:${esc(key)}"><button class="caret" type="button"></button>` +
      `<a href="#" class="more" data-key="all:${esc(key)}"><span class="name">Show ${rest.length.toLocaleString()} more</span></a></div>`;
  };

  let h = '<h3>Report</h3>' + link('#/', 'Summary') + link('#/tests', `Tests (${d.runs.length})`);
  h += '<h3 title="Mean of line, toggle and functional coverage below each scope">Hierarchy · score</h3>';
  const curScope = route[0] === 'scope' ? route[1] : '';
  const walk = (s, depth) => {
    const subscopes = children.get(s.id) || [];
    const key = 'scope:' + s.name;
    const open = isOpen(key, depth, curScope.startsWith(s.name + '.'));
    let row = node(key, open, subscopes.length,
                   link(scopeHref(s.name), esc(s.name.split('.').pop()), score(totals.tree.get(s.id))));
    if (subscopes.length)
      row += kids(key, open, level(key, subscopes,
                                   c => curScope === c.name || curScope.startsWith(c.name + '.'),
                                   c => walk(c, depth + 1)));
    return row;
  };
  h += rootScopes.map(s => walk(s, 0)).join('');

  if (d.functional_types.length) {
    h += '<h3>Covergroups</h3>';
    const types = [...d.functional_types].sort((a, b) => byName(a.hierarchy, b.hierarchy));
    h += level('covergroups', types, t => route[0] === 'cg' && route[1] === t.id,
               t => link(cgHref(t), esc(t.hierarchy), state.test ? undefined : num(t.percent)));
  }

  h += '<h3>Files</h3>';
  const curFile = route[0] === 'file' ? (files.get(route[1]) || {}).path || '' : '';
  const walkDir = (dir, path, depth) => {
    let out = '';
    for (let [name, sub] of [...dir.dirs.entries()].sort((a, b) => byName(a[0], b[0]))) {
      let full = path + name + '/';
      // show single-child chains as one row: rtl/core/alu
      while (!sub.files.length && sub.dirs.size === 1) {
        const [n, s] = [...sub.dirs.entries()][0];
        name += '/' + n;
        full += n + '/';
        sub = s;
      }
      const key = 'dir:' + full;
      const open = isOpen(key, depth, curFile.startsWith(full));
      const l = totals.dir.get(sub);
      out += node(key, open, true, `<a href="#" class="dir" data-key="${esc(key)}"><span class="name">${esc(name)}/</span>` +
                  pctSpan(l.tot ? 100 * l.cov / l.tot : undefined) + '</a>') +
             kids(key, open, walkDir(sub, full, depth + 1));
    }
    return out + level('files:' + path, dir.files, f => f.path === curFile, f => {
      const l = totals.file.get(f.id);
      return node('file:' + f.id, false, false,
                  link(fileHref(f.id), esc(f.path.split('/').pop()), l.tot ? 100 * l.cov / l.tot : undefined));
    });
  };
  h += walkDir(fileTree, '', 0);
  $('#side').innerHTML = h;
}

/* --------------------------------------------------------------- views */

function viewSummary() {
  const totals = rollups();
  const metric = (label, pct, frac, threshold) =>
    `<div class="metric"><div class="label">${label}</div>` +
    (pct === null ? '<div class="value dash">—</div>'
                  : `<div class="value lvl-${lvl(pct)}">${pct.toFixed(2)}%</div>`) +
    `<div class="frac">${frac}</div>${threshold}</div>`;
  const threshold = k => {
    const x = d.metrics[k];
    if (!x || x.threshold === undefined || state.test) return '';
    return `<div class="${x.threshold_pass ? 'pass' : 'fail'}">${x.threshold_pass ? 'PASS' : 'FAIL'} ` +
           `${Number(x.threshold).toFixed(2)}%</div>`;
  };
  // All tests together use obelisk-cov's own totals; one test's are counted
  // here from its hits.
  const m = d.metrics;
  const line = state.test ? totals.all.line
    : {cov: num(m.line.covered), tot: num(m.line.total), partial: num(m.line.partial), pct: m.line.percent};
  const toggle = state.test ? totals.all.toggle
    : {cov: num(m.toggle.covered), tot: num(m.toggle.total), pct: m.toggle.percent};
  const pctOf = x => x.pct !== undefined ? num(x.pct) : x.tot ? 100 * x.cov / x.tot : null;
  let h = `<h1>Summary<span class="sub">${plural(d.runs.length, 'test')} merged` +
          (state.test ? ` · hits from <b>${esc(runLabels.get(state.test))}</b>` : '') + '</span></h1>';
  h += '<div class="metrics">';
  if (!state.test && !m.line.available) h += metric('Line', null, 'not collected', '');
  else h += metric('Line', pctOf(line), `${line.cov} / ${line.tot} lines` + (line.partial ? `, ${line.partial} partial` : ''), threshold('line'));
  if (!state.test && !m.toggle.available) h += metric('Toggle', null, 'not collected', '');
  else h += metric('Toggle', pctOf(toggle), `${toggle.cov} / ${toggle.tot} transitions`, threshold('toggle'));
  if (d.functional_types.length)
    h += state.test
      ? metric('Functional', null, 'weighted across all tests only', '')
      : metric('Functional', num(m.functional.percent), `${plural(d.functional_types.length, 'covergroup type')}`, threshold('functional'));
  h += '</div>';

  // The first two levels of the hierarchy; deeper scopes are in the sidebar
  // and on each scope's page.
  const rows = [];
  let hidden = 0;
  const walk = (s, depth) => {
    const x = totals.tree.get(s.id);
    const low = Math.min(x.line.tot ? x.line.cov / x.line.tot : 1, x.toggle.tot ? x.toggle.cov / x.toggle.tot : 1,
                         x.functional ? x.functional.pct / 100 : 1);
    if (!state.uncov || low < 1) {
      if (rows.length < 200)
        rows.push(`<tr><td><span class="indent" data-d="${depth}"></span><a href="${scopeHref(s.name)}">${esc(s.name)}</a></td>` +
                  `<td class="mono">${esc(s.definition)}</td><td>${covCell(x.line.cov, x.line.tot)}</td>` +
                  `<td>${covCell(x.toggle.cov, x.toggle.tot)}</td><td>${pctCell(x.functional?.pct)}</td></tr>`);
      else hidden++;
    }
    if (depth < 1) for (const c of children.get(s.id) || []) walk(c, depth + 1);
  };
  for (const s of rootScopes) walk(s, 0);
  if (rows.length) {
    h += '<h2>Hierarchy</h2>' + table(['Scope', 'Module', 'Line', 'Toggle', 'Functional'], rows);
    if (hidden) h += `<p class="note">${plural(hidden, 'more scope')} in the sidebar.</p>`;
  }

  const cgRows = [];
  for (const t of [...d.functional_types].sort((a, b) => byName(a.hierarchy, b.hierarchy))) {
    if (state.uncov && num(t.percent) >= 100) continue;
    cgRows.push(row([`<a href="${cgHref(t)}" class="mono">${esc(t.hierarchy)}</a>`, {num: groupsOfType(t).length},
                     state.test ? pctCell(null) : pctCell(num(t.percent)), {num: t.goal + '%'}, esc(t.comment)]));
  }
  if (cgRows.length)
    h += '<h2>Covergroups</h2>' + table(['Covergroup', '#Instances', 'Coverage', '#Goal', 'Comment'], cgRows);

  if (d.exclusions.length)
    h += '<h2>Exclusions</h2>' + table(['Metric', 'Entity', 'Reason'],
      d.exclusions.map(v => row([esc(v.metric), `<span class="mono">${esc(v.entity)}</span>`, esc(v.reason)])));
  return h;
}

function viewTests() {
  let h = '<h1>Tests</h1>';
  h += table(['Test', 'Result', '#Sim time', '#Seed', 'Finished', 'Tags', 'UUID'], d.runs.map(r => row([
    `<b>${esc(runLabels.get(r.uuid))}</b>`,
    r.status === '0' ? '<span class="tag good">passed</span>' : `<span class="tag bad">exit ${esc(r.status)}</span>`,
    {num: esc(r.simulation_time)}, {num: esc(r.seed)}, `<span class="mono">${fmtTime(r.timestamp)}</span>`,
    esc(Object.entries(r.tags).map(([k, v]) => k + '=' + v).join(', ')), `<span class="mono dash">${esc(r.uuid)}</span>`])));
  h += '<h2>Unique contributions</h2><p class="note">What each test alone covers: ' +
       'the obligations that would fall below their at_least threshold without it.</p>';
  h += table(['#Rank', 'Test', '#Lines', '#Toggle directions', '#Functional bins', '#Total'],
    uniqueContributions().map((r, index) => row([{num: index + 1}, esc(r.name), {num: r.line}, {num: r.toggle},
      {num: r.functional}, {num: (r.line + r.toggle + r.functional) + ' unique'}])));
  if (d.illegal_bin_diagnostics.length)
    h += '<h2>Illegal bins hit</h2>' + table(['Test', 'Bin', 'Instance', '#Simulation time', '#Count', 'Message'],
      d.illegal_bin_diagnostics.map(v => row([esc(runLabels.get(v.run) || v.test),
        `<span class="mono">${esc(v.hierarchy || v.bin)}</span>`, esc(v.instance), {num: esc(v.simulation_time)},
        {num: esc(v.count + (v.overflow ? ' (saturated)' : ''))}, esc(v.message)])));
  return h;
}

function viewScope(name, focusSignal) {
  const s = scopeByName.get(name);
  if (!s) return `<h1>Unknown scope</h1><p class="note">${esc(name)}</p>`;
  const totals = rollups();
  const {line: l, toggle: t} = totals.self.get(s.id);
  const parts = name.split('.');
  // Enclosing names link to their scope when the report has one.
  const crumbs = parts.map((p, i) => {
    const prefix = parts.slice(0, i + 1).join('.');
    return i === parts.length - 1 || !scopeByName.has(prefix) ? esc(p)
      : `<a href="${scopeHref(prefix)}">${esc(p)}</a>`;
  }).join('.');
  let h = `<div class="crumbs">${crumbs}</div><h1>${esc(parts[parts.length - 1])}` +
          `<span class="sub">module ${esc(s.definition)}</span></h1>`;
  h += `<div class="metrics"><div class="metric"><div class="label">Line (this scope)</div><div>${covCell(l.cov, l.tot)}</div></div>` +
       `<div class="metric"><div class="label">Toggle (this scope)</div><div>${covCell(t.cov, t.tot)}</div></div></div>`;

  const kids = children.get(s.id) || [];
  if (kids.length) {
    const rows = [];
    for (const c of kids.slice(0, 500)) {
      const {line: cl, toggle: ct} = totals.tree.get(c.id);
      rows.push(row([`<a href="${scopeHref(c.name)}" class="mono">${esc(c.name.split('.').pop())}</a>`,
                     `<span class="mono">${esc(c.definition)}</span>`, covCell(cl.cov, cl.tot), covCell(ct.cov, ct.tot)]));
    }
    h += '<h2>Instances</h2>' + table(['Instance', 'Module', 'Line (subtree)', 'Toggle (subtree)'], rows);
    if (kids.length > 500) h += `<p class="note">${plural(kids.length - 500, 'more instance')} in the sidebar.</p>`;
  }

  const cgs = typesByScope.get(s.id) || [];
  if (cgs.length)
    h += '<h2>Covergroups</h2>' + table(['Covergroup', 'Coverage'], cgs.map(x =>
      row([`<a href="${cgHref(x)}" class="mono">${esc(x.name)}</a>`, state.test ? pctCell(null) : pctCell(num(x.percent))])));

  const pts = [...(pointsByScope.get(s.id) || [])]
    .sort((a, b) => byName(a.file, b.file) || num(a.line) - num(b.line) || num(a.column) - num(b.column));
  const shared = new Map();
  for (const p of pts) shared.set(p.file + ':' + p.line, (shared.get(p.file + ':' + p.line) || 0) + 1);
  const pointRows = [];
  for (const p of pts) {
    const status = p.excluded ? 'excluded: ' + p.exclusion_reason : isHit(p) ? 'covered' : 'uncovered';
    if (state.uncov && status !== 'uncovered') continue;
    const code = (sourceLines(p.file) || [])[num(p.line) - 1];
    pointRows.push(row([fileLoc(p.file, p.line) + (shared.get(p.file + ':' + p.line) > 1 ? `<span class="dash">:${p.column}</span>` : ''),
                        `<span class="mono">${code === undefined ? '' : code.replace(/^\s+/, '')}</span>`,
                        {num: esc(counterText(p))}, statusTag(status), esc(testNames(p.contributors))]));
  }
  if (pointRows.length) h += '<h2>Statements</h2>' + table(['Location', 'Code', '#Hits', 'Status', 'Hit by'], pointRows);

  const signals = [...(togglesByScope.get(s.id) || [])].sort((a, b) => byName(a.name, b.name));
  const signalRows = [];
  for (const x of signals) {
    const states = x.bits.map(b => {
      const up = isHit(b.zero_to_one), down = isHit(b.one_to_zero);
      return up && down ? 'both' : up || down ? 'one' : 'none';
    });
    const status = x.excluded ? 'excluded: ' + x.exclusion_reason
      : states.every(v => v === 'both') ? 'covered' : states.every(v => v === 'none') ? 'uncovered' : 'partial';
    if (state.uncov && (status === 'covered' || x.excluded)) continue;
    const up = x.bits.filter(b => isHit(b.zero_to_one)).length;
    const down = x.bits.filter(b => isHit(b.one_to_zero)).length;
    const strip = x.bits.map((b, i) => [b, states[i]]).reverse().map(([b, v]) =>
      `<i class="b-${x.excluded ? 'ex' : v}" title="${esc(b.paths.join(' | '))}"></i>`).join('');
    const bitRows = x.bits.map(b => row([`<span class="mono">${esc(b.paths.join(' | '))}</span>`,
      {num: esc(counterText(b.zero_to_one))}, {num: esc(counterText(b.one_to_zero))},
      {num: esc(counterText(b.to_unknown))}, {num: esc(counterText(b.from_unknown))},
      esc(testNames([...b.zero_to_one.contributors, ...b.one_to_zero.contributors,
                     ...b.to_unknown.contributors, ...b.from_unknown.contributors]))]));
    signalRows.push(`<tr id="sig-${esc(x.name)}"${focusSignal === x.name ? ' class="flash"' : ''}>` +
      `<td><details class="signal"><summary class="mono">${esc(x.name)}</summary>` +
      table(['Bit', '#0→1', '#1→0', '#To X/Z', '#From X/Z', 'Hit by'], bitRows) + '</details></td>' +
      `<td class="num">${esc(x.width)}</td><td><span class="bits">${strip}</span></td>` +
      `<td class="num">${up}/${x.bits.length}</td><td class="num">${down}/${x.bits.length}</td>` +
      `<td>${statusTag(status)}</td><td>${fileLoc(x.file, x.line)}</td></tr>`);
  }
  if (signalRows.length)
    h += '<h2>Signals</h2><p class="note">Each square is one bit, MSB first. Green: toggled both ways · ' +
         'amber: one way only · red: never toggled. Open a signal for its bits.</p>' +
         table(['Signal', '#Width', 'Bits', '#0→1', '#1→0', 'Status', 'Declared'], signalRows);
  return h;
}

function viewFile(fid, target) {
  const f = files.get(fid);
  if (!f) return '<h1>Unknown file</h1>';
  let h = `<h1 class="mono">${esc(f.path)}</h1>`;
  const lines = sourceLines(fid);
  if (!lines) return h + `<p class="note">${sourceStatus(fid)}</p>`;
  const byLine = groupBy(pointsByFile.get(fid) || [], p => num(p.line));
  const l = rollups().file.get(fid);
  h += `<div class="src-tools">${covCell(l.cov, l.tot)} <span>lines</span>` +
       '<button id="next-uncov" type="button">Next uncovered <kbd>n</kbd></button>' +
       '<button id="prev-uncov" type="button">Previous <kbd>p</kbd></button></div>';
  h += '<div class="src"><div class="ln head"><span class="no">Line</span>' +
       '<span class="hits" title="Times each statement on the line ran">Hits</span><span></span></div>';
  lines.forEach((html, idx) => {
    const n = idx + 1, ps = byLine.get(n) || [];
    let cls = '', hitText = '';
    if (ps.length) {
      const included = ps.filter(p => !p.excluded);
      const hit = included.filter(p => isHit(p)).length;
      cls = !included.length ? 'excluded' : hit === included.length ? 'full' : hit ? 'partial' : 'uncovered';
      // one number per line; the tooltip lists each statement
      const counts = ps.map(p => countOf(p));
      const lo = counts.reduce((a, b) => b < a ? b : a), hi = counts.reduce((a, b) => b > a ? b : a);
      hitText = !included.length ? 'excl' : lo === hi ? String(lo) : `${lo}–${hi}`;
    }
    const title = ps.map(p => `col ${p.column}: ${counterText(p)}` +
      (p.excluded ? ` (excluded: ${p.exclusion_reason})` : '') +
      (p.contributors.length ? ' — ' + testNames(p.contributors) : '')).join('\n');
    h += `<div class="ln ${cls}${n === target ? ' target' : ''}" id="L${n}"><span class="no">${n}</span>` +
         `<span class="hits" title="${esc(title)}">${hitText}</span><span>${html || ' '}</span></div>`;
  });
  return h + '</div>';
}

// A two-way cross as a grid of its automatic bins, so empty combinations
// stand out. Tuples outside the automatic cross (ignored or claimed by an
// explicit bin) are dots.
function crossMatrix(g, i) {
  if (i.targets.length !== 2 || BigInt(i.automatic_total) === 0n) return '';
  const targets = i.targets.map(t => g.items.find(x => x.id === t.id));
  if (targets.some(x => !x)) return '';
  const names = targets.map(x => x.bins.filter(b => b.contributing && !(num(b.flags) & 12)).map(b => b.name));
  if (!names[0].length || !names[1].length || names[0].length * names[1].length > 2500) return '';
  const cells = new Map(i.automatic_bins.map(b => [b.components.map(c => c.name).join('\u0000'), b]));
  let h = `<table class="matrix"><thead><tr><th class="corner">${esc(targets[0].name)} ↓ &nbsp; ${esc(targets[1].name)} →</th>` +
          names[1].map(n => `<th>${esc(n)}</th>`).join('') + '</tr></thead><tbody>';
  for (const r of names[0]) {
    h += `<tr><th>${esc(r)}</th>`;
    for (const c of names[1]) {
      const b = cells.get(r + '\u0000' + c);
      if (!automaticContains(g, i, [r, c])) {
        h += '<td class="none" title="not an automatic bin">·</td>';
        continue;
      }
      const hit = b && isHit(b, b.at_least);
      h += `<td class="${hit ? 'hit' : 'miss'}" title="${esc(b ? testNames(b.contributors) || 'never hit' : 'never hit')}">` +
           `${b ? esc(counterText(b)) : '0'}</td>`;
    }
    h += '</tr>';
  }
  return h + '</tbody></table>';
}

function viewCovergroup(tid, openItem) {
  const t = d.functional_types.find(x => x.id === tid);
  if (!t) return '<h1>Unknown covergroup</h1>';
  const scope = scopes.get(t.scope);
  let h = (scope ? `<div class="crumbs"><a href="${scopeHref(scope.name)}">${esc(scope.name)}</a></div>` : '') +
          `<h1>covergroup ${esc(t.name)}<span class="sub">${state.test ? '' : pctCell(num(t.percent)) + ' · '}` +
          `goal ${esc(t.goal)}% · weight ${esc(t.weight)}${t.merge_instances ? ' · merge_instances' : ''}</span></h1>`;
  if (t.comment) h += `<p class="note">${esc(t.comment)}</p>`;
  if (state.test)
    h += '<p class="note">Percentages are weighted across all tests; bins show hits from the selected test.</p>';
  for (const g of groupsOfType(t)) {
    h += `<h2>Instance <span class="mono">${esc(g.name || '<unnamed>')}</span> ` +
         `<span class="sub">${state.test ? '' : pctCell(num(g.percent))}</span></h2>`;
    if (g.comment) h += `<p class="note">${esc(g.comment)}</p>`;
    h += '<div class="head-row"><span></span><span>Item</span><span>Kind</span><span>Coverage</span>' +
         '<span class="num">Bins</span><span>Details</span></div>';
    for (const i of g.items) {
      if (state.uncov && !state.test && num(i.percent) >= 100) continue;
      const notes = [i.hierarchy + (i.targets.length ? ' targets ' + i.targets.map(x => x.name).join(', ') : ''),
                     i.comment, i.type_comment ? 'type: ' + i.type_comment : '',
                     !i.aggregating ? 'non-aggregating inherited item' : '',
                     `goal ${i.goal} / weight ${i.weight}`].filter(Boolean);
      const binRows = [];
      for (const b of i.bins) {
        const status = binStatus(b);
        if (state.uncov && status !== 'uncovered') continue;
        const roles = binRoles(num(b.flags));
        binRows.push(row([`<span class="mono">${esc(binName(g, i, b))}</span>`,
          esc((binKinds[b.kind] || b.kind) + (roles.length ? ' · ' + roles.join(', ') : '')),
          {num: esc(counterText(b))}, {num: esc(b.at_least)}, statusTag(status), esc(testNames(b.contributors))]));
      }
      for (const b of i.automatic_bins) {
        const status = b.missing ? 'missing' : isHit(b, b.at_least) ? 'covered' : 'uncovered';
        if (state.uncov && status === 'covered') continue;
        binRows.push(row([`<span class="mono">${esc(b.name)}</span>`, 'automatic cross tuple',
          {num: esc(counterText(b))}, {num: esc(b.at_least)}, statusTag(status), esc(testNames(b.contributors))]));
      }
      h += `<details class="item" id="item-${esc(i.name)}"${openItem === i.name ? ' open' : ''}><summary>` +
           `<span class="mono">${esc(i.name)}</span><span>${itemKinds[i.kind] || esc(i.kind)}</span>` +
           `<span>${state.test ? pctCell(null) : pctCell(num(i.percent))}</span>` +
           `<span class="num">${esc(i.covered)}/${esc(i.total)}</span>` +
           `<span class="notes">${notes.map(n => `<span>${esc(n)}</span>`).join(' · ')}</span></summary>` +
           `<div class="body">${num(i.kind) === 2 ? crossMatrix(g, i) : ''}` +
           table(['Bin', 'Kind', '#Hits', '#At least', 'Status', 'Hit by'], binRows) + '</div></details>';
    }
  }
  return h;
}

/* ------------------------------------------------------------- routing */

// Views are addressed by hash. A standalone report keeps the hash in its URL,
// so views can be bookmarked and Back works. Where the page may not change
// its own URL -- the website shows it from a blob: URL in an opaque-origin
// sandbox, where even a fragment navigation is refused -- it routes without.
let detachedHash = null;
const currentHash = () => detachedHash ?? location.hash;
// Browsers may normalize a fragment's percent-encoding.
const sameHash = (a, b) => {
  try { return decodeURIComponent(a) === decodeURIComponent(b); } catch { return a === b; }
};
function go(hash) {
  if (detachedHash === null) {
    try {
      if (!sameHash(location.hash, hash)) location.hash = hash;
    } catch {
      // treated like a refused navigation below
    }
    // The URL took it: hashchange routes.
    if (sameHash(location.hash, hash)) return;
  }
  detachedHash = hash;
  route();
}

function route() {
  // A mistyped or truncated link shows an unknown view rather than nothing.
  const decode = s => { try { return decodeURIComponent(s); } catch { return s; } };
  const parts = (currentHash().replace(/^#\/?/, '') || '').split('/').map(decode);
  document.body.classList.remove('show-side');
  let html, after = null;
  if (parts[0] === 'scope') {
    html = viewScope(parts[1], parts[2] === 'sig' ? parts[3] : null);
    if (parts[2] === 'sig')
      after = () => document.getElementById('sig-' + parts[3])?.scrollIntoView({block: 'center'});
  } else if (parts[0] === 'file') {
    const line = parts[2] ? num(parts[2].slice(1)) : 0;
    html = viewFile(parts[1], line);
    after = () => {
      if (line) document.getElementById('L' + line)?.scrollIntoView({block: 'center'});
      wireSourceNav();
    };
  } else if (parts[0] === 'cg') {
    html = viewCovergroup(parts[1], parts[2]);
    if (parts[2]) after = () => document.getElementById('item-' + parts[2])?.scrollIntoView({block: 'start'});
  } else if (parts[0] === 'tests') {
    html = viewTests();
  } else {
    html = viewSummary();
  }
  const main = $('#main');
  main.innerHTML = html;
  // Inline style attributes are blocked by the page's CSP; set them here.
  for (const i of main.querySelectorAll('.indent')) i.style.width = (num(i.dataset.d) * 16) + 'px';
  for (const i of main.querySelectorAll('.bar > i')) i.style.width = i.dataset.w + '%';
  currentRoute = parts;
  renderSide(parts);
  for (const i of $('#side').querySelectorAll('.bar > i')) i.style.width = i.dataset.w + '%';
  $('#side a.current')?.scrollIntoView({block: 'nearest'});
  if (after) after();
  else main.scrollTop = 0;
}

// n / p step through uncovered and partial lines of the open file.
let stepSource = null;
function wireSourceNav() {
  const main = $('#main');
  stepSource = dir => {
    const rows = [...main.querySelectorAll('.src .ln.uncovered, .src .ln.partial')];
    if (!rows.length) return;
    // Positions within the view's scrolled content, like scrollTop.
    const origin = main.getBoundingClientRect().top - main.scrollTop;
    const top = r => r.getBoundingClientRect().top - origin;
    const mid = main.scrollTop + main.clientHeight / 2;
    const next = dir > 0 ? rows.find(r => top(r) > mid + 2) || rows[0]
                         : [...rows].reverse().find(r => top(r) < mid - 2) || rows[rows.length - 1];
    for (const r of main.querySelectorAll('.src .target')) r.classList.remove('target');
    next.classList.add('target');
    next.scrollIntoView({block: 'center'});
  };
  $('#next-uncov')?.addEventListener('click', () => stepSource(1));
  $('#prev-uncov')?.addEventListener('click', () => stepSource(-1));
}
document.addEventListener('keydown', e => {
  if (e.target.closest?.('select, input') || e.metaKey || e.ctrlKey || e.altKey) return;
  if ((e.key === 'n' || e.key === 'p') && currentRoute[0] === 'file' && stepSource)
    stepSource(e.key === 'n' ? 1 : -1);
});

$('#side').addEventListener('click', e => {
  const target = e.target.closest('[data-key]');
  if (!target) return;
  const key = target.dataset.key;
  if (target.classList.contains('more')) {
    treeOpen.set(key, true);
  } else if (target.classList.contains('caret') || target.classList.contains('dir')) {
    if (target.classList.contains('caret') && !target.textContent) return;
    const caret = $('#side').querySelector(`.caret[data-key="${CSS.escape(key)}"]`);
    treeOpen.set(key, caret.getAttribute('aria-expanded') !== 'true');
  } else {
    return;
  }
  e.preventDefault();
  renderSide(currentRoute);
  for (const i of $('#side').querySelectorAll('.bar > i')) i.style.width = i.dataset.w + '%';
});
$('#side').addEventListener('beforematch', e => {
  const key = e.target.dataset?.key;
  if (!key) return;
  treeOpen.set(key, true);
  if (key.startsWith('all:')) {
    $('#side').querySelector(`[data-more="${CSS.escape(key)}"]`)?.remove();
  } else {
    const caret = $('#side').querySelector(`.caret[data-key="${CSS.escape(key)}"]`);
    if (caret) {
      caret.textContent = '▾';
      caret.setAttribute('aria-expanded', 'true');
    }
  }
});
$('#browse').addEventListener('click', e => {
  e.stopPropagation();
  document.body.classList.toggle('show-side');
});
// Plain clicks on view links route through go(); modified clicks keep the
// browser's own handling, such as opening a view in a new tab.
document.addEventListener('click', e => {
  const link = e.target.closest('a[href^="#/"]');
  if (!link || e.button !== 0 || e.metaKey || e.ctrlKey || e.shiftKey || e.altKey) return;
  e.preventDefault();
  go(link.getAttribute('href'));
});
document.addEventListener('click', e => {
  if (!$('#side').contains(e.target)) document.body.classList.remove('show-side');
});
window.addEventListener('hashchange', () => {
  if (detachedHash === null) route();
});

const testSelect = $('#test');
for (const r of d.runs) {
  const option = document.createElement('option');
  option.value = r.uuid;
  option.textContent = runLabels.get(r.uuid);
  testSelect.append(option);
}
testSelect.addEventListener('change', () => {
  state.test = testSelect.value;
  route();
});
$('#uncov').addEventListener('change', e => {
  state.uncov = e.target.checked;
  route();
});
route();
