import assert from 'node:assert/strict';
import fs from 'node:fs';
import {createRequire} from 'node:module';

const require = createRequire(import.meta.url);
const {loadReport, extract} = require('./CheckCoverageHtml.js');

if (process.argv.length !== 3) {
  console.error('usage: CoverageReport.test.mjs CoverageReport.js');
  process.exit(2);
}

const reportScript = fs.readFileSync(process.argv[2], 'utf8');

const emptyReport = () => ({
  codec_version: '1',
  schema_fingerprint: '0123456789abcdef',
  files: [],
  scopes: [],
  runs: [],
  metrics: {
    line: {available: false, covered: '0', total: '0', partial: '0', percent: 0},
    toggle: {available: false, covered: '0', total: '0', partial: '0', percent: 0},
    functional: {available: false, covered: '0', total: '0', partial: '0', percent: 0},
  },
  line_points: [],
  toggles: [],
  functional_types: [],
  functional_item_templates: [],
  functional_bin_templates: [],
  functional_instance_groups: [],
  resolved_functional_bins: [],
  resolved_functional_value_sets: [],
  resolved_transition_alternatives: [],
  resolved_transition_steps: [],
  transition_steps: [],
  cross_selector_nodes: [],
  cross_selector_operands: [],
  resolved_cross_selector_bindings: [],
  resolved_functional_tuples: [],
  resolved_functional_tuple_components: [],
  resolved_cross_automatic_nodes: [],
  resolved_cross_automatic_edges: [],
  illegal_bin_diagnostics: [],
  exclusions: [],
  sources: [],
});

const run = (uuid, name) => ({
  uuid, name, status: '0', simulation_time: '0', timestamp: '0', seed: '0', tags: {},
});
const covergroup = (fields) => ({
  id: '50', scope: '20', name: 'cg', hierarchy: 'top.cg', percent: 100, goal: '100',
  weight: '1', comment: '', merge_instances: false, ...fields,
});

// Render one route; returns its HTML with the text and table rows in it.
const view = (report, route) => {
  const out = report.render(route);
  const {segments, rows} = extract(out.main);
  // header rows have no <td> cells
  return {html: out.main, side: out.side, segments, rows: rows.filter(r => r.length)};
};

// Metric formatting keeps metrics that were not collected distinct and shows
// threshold results without inventing a combined percentage.
{
  const data = emptyReport();
  data.metrics.line = {
    available: true, covered: '1', total: '2', partial: '0', percent: 50,
    threshold: 75, threshold_pass: false,
  };
  data.metrics.functional = {
    available: true, covered: '3', total: '3', partial: '0', percent: 100,
    threshold: 100, threshold_pass: true,
  };
  data.functional_types = [covergroup({scope: '0'})];
  const summary = view(loadReport(reportScript, JSON.stringify(data)), '#/');
  for (const text of ['50.00%', '1 / 2 lines', 'FAIL 75.00%', 'not collected', '100.00%', 'PASS 100.00%'])
    assert.ok(summary.segments.has(text), `summary shows ${text}`);
  assert.match(summary.html, /class="fail">FAIL 75\.00%/);
  assert.match(summary.html, /class="pass">PASS 100\.00%/);
}

// Names, paths and tags from the design and the runs are rendered as text,
// never as markup; tests that share a name are told apart by UUID; and hits
// from one test are that test's alone.
{
  const data = emptyReport();
  const unsafeName = '<img src=x onerror=alert(1)>';
  data.files = [{id: '10', path: 'rtl/<dut>&.sv'}];
  data.scopes = [{id: '20', parent: '0', name: 'top.<dut>', definition: '<dut>'}];
  data.runs = [
    {...run('run-a', unsafeName), tags: {suite: '<nightly>'}},
    run('run-b', unsafeName),
  ];
  data.sources = [{file: '10', path: 'rtl/<dut>&.sv', status: 'ok',
                   text: 'a\nb\nc\nd\ne\nf\nx <= "<evil>";\n'}];
  data.line_points = [{
    id: '30', file: '10', scope: '20', line: '7', column: '3',
    count: '1', overflow: false, covered: true, excluded: false, exclusion_reason: '',
    contributors: [{uuid: 'run-a', name: unsafeName, count: '1'}],
  }];
  data.toggles = [{
    id: '40', scope: '20', file: '10', name: 'signal', width: '1', line: '7',
    excluded: false, exclusion_reason: '',
    bits: [{
      paths: ['signal[0]'],
      zero_to_one: {count: '1', overflow: false, covered: true,
                    contributors: [{uuid: 'run-b', name: unsafeName, count: '1'}]},
      one_to_zero: {count: '0', overflow: false, covered: false, contributors: []},
      to_unknown: {count: '0', overflow: false, contributors: []},
      from_unknown: {count: '0', overflow: false, contributors: []},
    }],
  }];
  const report = loadReport(reportScript, JSON.stringify(data));
  const labels = report.testOptions().map(option => option.textContent);
  assert.deepEqual(labels, [`${unsafeName} (run-a)`, `${unsafeName} (run-b)`]);
  for (const route of [...report.routes(), '#/scope/top.%3Cdut%3E/sig/signal']) {
    const out = report.render(route);
    for (const part of [out.main, out.side])
      assert.doesNotMatch(part, /<img|<dut>|<nightly>|<evil>/, `${route} escapes text`);
  }
  const scope = view(report, '#/scope/' + encodeURIComponent('top.<dut>'));
  assert.deepEqual(scope.rows.find(r => r[0].startsWith('rtl/')),
                   ['rtl/<dut>&.sv:7', 'x <= "<evil>";', '1', 'covered', `${unsafeName} (run-a)`]);
  assert.ok(scope.rows.some(r => r[0].startsWith('signal') && r[5] === 'partial'));
  const tests = view(report, '#/tests');
  assert.ok(tests.segments.has('suite=<nightly>'));

  report.selectTest('run-a');
  const onlyA = view(report, '#/scope/' + encodeURIComponent('top.<dut>'));
  assert.equal(onlyA.rows.find(r => r[0].startsWith('rtl/'))[3], 'covered');
  assert.ok(onlyA.rows.some(r => r[0].startsWith('signal') && r[5] === 'uncovered'));
  report.selectTest('run-b');
  const onlyB = view(report, '#/scope/' + encodeURIComponent('top.<dut>'));
  assert.deepEqual(onlyB.rows.find(r => r[0].startsWith('rtl/')).slice(2, 4), ['0', 'uncovered']);
  report.selectTest('');
}

// Transition ranges and repetition are reconstructed from the resolved v1
// schema, while saturating counters keep their annotation.
{
  const data = emptyReport();
  data.runs = [run('run-a', 'transition-test')];
  data.functional_types = [covergroup({})];
  data.functional_item_templates = [{id: '60', name: 'cp'}];
  data.functional_bin_templates = [{id: '70', name: 'round_trip'}];
  data.transition_steps = [
    {bin: '70', alternative_ordinal: '0', ordinal: '0', repetition: '1'},
    {bin: '70', alternative_ordinal: '0', ordinal: '1', repetition: '2'},
  ];
  const limb = (low, high) =>
    ({low_aval: low, high_aval: high, low_bval: '0', high_bval: '0', wildcard_mask: '0'});
  data.resolved_functional_value_sets = [
    {type: '50', configuration: '500', id: '90', atoms: [{kind: '1', limbs: [limb('5', '5')]}]},
    {type: '50', configuration: '500', id: '91', atoms: [{kind: '2', limbs: [limb('8', '9')]}]},
  ];
  data.resolved_transition_alternatives = [{
    type: '50', configuration: '500', bin: '170', id: '80', template_alternative_ordinal: '0',
  }];
  data.resolved_transition_steps = [
    {type: '50', configuration: '500', alternative: '80', ordinal: '0',
     value_set: '90', lower_bound: '1', upper_bound: '1'},
    {type: '50', configuration: '500', alternative: '80', ordinal: '1',
     value_set: '91', lower_bound: '2', upper_bound: '4'},
  ];
  data.functional_instance_groups = [{
    type: '50', configuration: '500', name: 'instance', percent: 100, comment: '',
    items: [{
      id: '160', template_item: '60', kind: '1', name: 'cp', comment: '', type_comment: '',
      hierarchy: 'top.cg.instance.cp', targets: [], covered: '1', total: '1', percent: 100,
      goal: '100', weight: '1', aggregating: true, at_least: '1', automatic_total: '0',
      automatic_at_least: '1',
      bins: [{
        id: '170', template_bin: '70', kind: '2', flags: '0', name: 'round_trip',
        hierarchy: 'top.cg.instance.cp.round_trip', count: '18446744073709551615',
        overflow: true, at_least: '1', contributing: true, covered: true,
        exclusion_reason: '', cross_selector: '0',
        contributors: [{uuid: 'run-a', name: 'transition-test', count: '1'}],
      }],
      automatic_bins: [],
    }],
  }];
  const cg = view(loadReport(reportScript, JSON.stringify(data)), '#/cg/50/cp');
  assert.deepEqual(cg.rows[0], [
    'round_trip = 0x5 => [0x8:0x9][*2:4]', 'transition',
    '18446744073709551615 (saturated)', '1', 'covered', 'transition-test',
  ]);
}

// Sparse automatic cross tuples keep their covered and missing state.
{
  const data = emptyReport();
  data.functional_types = [covergroup({percent: 50})];
  data.functional_instance_groups = [{
    type: '50', configuration: '500', name: 'instance', percent: 50, comment: '',
    items: [{
      id: '160', template_item: '60', kind: '2', name: 'cp_x_cp2', comment: '', type_comment: '',
      hierarchy: 'top.cg.instance.cp_x_cp2', targets: [], covered: '1', total: '2', percent: 50,
      goal: '100', weight: '1', aggregating: true, at_least: '1', automatic_total: '2',
      automatic_at_least: '1', automatic_root_node: '0', bins: [],
      automatic_bins: [
        {name: '<one,two>', components: [{name: 'one'}, {name: 'two'}], count: '1',
         overflow: false, at_least: '1', covered: true, missing: false, contributors: []},
        {name: '<zero,two>', components: [{name: 'zero'}, {name: 'two'}], count: '0',
         overflow: false, at_least: '1', covered: false, missing: true, contributors: []},
      ],
    }],
  }];
  const cg = view(loadReport(reportScript, JSON.stringify(data)), '#/cg/50/cp_x_cp2');
  assert.deepEqual(cg.rows.map(r => [r[0], r[1], r[4]]), [
    ['<one,two>', 'automatic cross tuple', 'covered'],
    ['<zero,two>', 'automatic cross tuple', 'missing'],
  ]);
}

// A zero-bin resolved configuration still raises the cumulative at_least for
// unique-contribution ranking when merge_instances is enabled.
{
  const data = emptyReport();
  data.runs = [run('low', 'low'), run('high', 'high')];
  data.functional_types = [covergroup({percent: 0, merge_instances: true})];
  const item = (id, atLeast, bins) => ({
    id, template_item: '60', kind: '1', name: 'cp', comment: '', type_comment: '',
    hierarchy: `top.cg.${id}.cp`, targets: [], covered: '0', total: String(bins.length),
    percent: 0, goal: '100', weight: '1', aggregating: true, at_least: String(atLeast),
    automatic_total: '0', automatic_at_least: String(atLeast), bins, automatic_bins: [],
  });
  data.functional_instance_groups = [
    {type: '50', configuration: 'low-config', name: 'low', percent: 100, comment: '',
     items: [item('160', 1, [{
       id: '170', template_bin: '70', kind: '1', flags: '0', name: 'only-low',
       hierarchy: 'top.cg.low.cp.only-low', count: '1', overflow: false, at_least: '1',
       contributing: true, covered: true, exclusion_reason: '', cross_selector: '0',
       contributors: [{uuid: 'low', name: 'low', count: '1'}],
     }])]},
    {type: '50', configuration: 'high-config', name: 'high', percent: 0, comment: '',
     items: [item('161', 2, [])]},
  ];
  const tests = view(loadReport(reportScript, JSON.stringify(data)), '#/tests');
  assert.deepEqual(tests.rows.filter(r => r.length === 6).map(r => [r[1], r[4]]),
                   [['high', '0'], ['low', '0']]);
}

// Links change the view through the URL where the page may change it, and
// without it where the URL is frozen, as in the website's sandboxed frame.
{
  const data = emptyReport();
  data.runs = [run('run-a', 'only')];
  data.scopes = [{id: '20', parent: '0', name: 'top', definition: 'top'}];
  for (const frozenUrl of [false, true]) {
    const report = loadReport(reportScript, JSON.stringify(data), {frozenUrl});
    let out = report.click('#/tests');
    assert.match(out.main, /^<h1>Tests<\/h1>/, `tests view, frozenUrl=${frozenUrl}`);
    assert.equal(out.hash, frozenUrl ? '' : '#/tests');
    out = report.click('#/scope/top');
    assert.match(out.main, /<h1>top<span class="sub">module top/);
    assert.match(out.side, /<a href="#\/scope\/top" class="current">/);
    out = report.click('#/');
    assert.match(out.main, /^<h1>Summary/);
  }
}

// The whole sidebar is in the page so the browser's find reaches every
// entry: collapsed levels and rows past a long level's first 100 are hidden
// until found rather than left out. What the main view shows is marked, even
// past the cut or on a sub-route such as a covergroup item.
{
  const data = emptyReport();
  const scope = (id, parent, name) => ({id, parent, name, definition: 'blk'});
  data.scopes = [scope('1', '0', 'top')];
  for (let i = 0; i < 150; i++) data.scopes.push(scope(String(100 + i), '1', `top.u_${i}`));
  data.scopes.push(scope('999', '101', 'top.u_1.leaf'));
  data.functional_types = [covergroup({scope: '1'})];
  data.functional_instance_groups = [{
    type: '50', configuration: '500', name: 'i', percent: 100, comment: '',
    items: [{id: '160', template_item: '60', kind: '1', name: 'cp', comment: '', type_comment: '',
             hierarchy: 'top.cg.cp', targets: [], covered: '0', total: '0', percent: 100,
             goal: '100', weight: '1', aggregating: true, at_least: '1', automatic_total: '0',
             automatic_at_least: '1', bins: [], automatic_bins: []}],
  }];
  const report = loadReport(reportScript, JSON.stringify(data));
  const {side} = report.render('#/scope/top.u_120');
  for (let i = 0; i < 150; i++)
    assert.match(side, new RegExp(`<span class="name">u_${i}</span>`), `u_${i} is in the sidebar`);
  assert.match(side, /<span class="name">leaf<\/span>/);
  // u_1's children are collapsed but findable
  assert.match(side, /<div class="kids" data-key="scope:top\.u_1" hidden="until-found">[^]*?leaf/);
  // natural order puts u_100..u_149 past the first 100; the current u_120 stays out
  const rest = side.match(/<div class="rest" data-key="all:scope:top" hidden="until-found">([^]*?)<\/div><div class="row" data-more/)[1];
  assert.match(rest, /u_149/);
  assert.doesNotMatch(rest, /u_120</);
  assert.match(side, /Show 49 more/);
  assert.match(side, /<a href="#\/scope\/top\.u_120" class="current">/);
  assert.match(report.render('#/cg/50/cp').side, /<a href="#\/cg\/50" class="current">/);
}

/* ------------------------------------------- totals and the source view */

// Two instances of one module in rtl/blk.sv, and a testbench line in
// rtl/tb/top.sv. u0 hits both statements on line 3 (test a) and misses line
// 4; u1 misses one statement on line 3 and hits line 4 (test b).
const point = (id, scope, file, line, column, count, runs, extra = {}) => ({
  id, scope, file, line, column, count: String(count), overflow: false,
  covered: count > 0, excluded: false, exclusion_reason: '',
  contributors: runs.map(uuid => ({uuid, name: uuid, count: String(count)})), ...extra,
});
const twoInstances = () => {
  const data = emptyReport();
  data.runs = [run('a', 'a'), run('b', 'b')];
  data.files = [{id: '10', path: 'rtl/blk.sv'}, {id: '11', path: 'rtl/tb/top.sv'}];
  data.scopes = [
    {id: '1', parent: '0', name: 'top', definition: 'top'},
    {id: '2', parent: '1', name: 'top.u0', definition: 'blk'},
    {id: '3', parent: '1', name: 'top.u1', definition: 'blk'},
  ];
  data.line_points = [
    point('20', '2', '10', '3', '5', 1, ['a']), point('21', '2', '10', '3', '15', 1, ['a']),
    point('22', '2', '10', '4', '5', 0, []),
    point('23', '3', '10', '3', '5', 1, ['b']), point('24', '3', '10', '3', '15', 0, []),
    point('25', '3', '10', '4', '5', 1, ['b']),
    point('26', '1', '11', '2', '3', 2, ['a']),
    point('27', '1', '11', '3', '3', 0, [], {excluded: true, exclusion_reason: 'generated'}),
  ];
  data.metrics.line = {available: true, covered: '1', total: '3', partial: '2', percent: 33.333333};
  // module m;
  // wire [3:0] v = 4'hA; // c
  // endmodule
  const text = "module m;\nwire [3:0] v = 4'hA; // c\nendmodule\n";
  const tokens = [
    [0, 6, 'ModuleKeyword'], [7, 1, 'Identifier'], [8, 1, 'Semicolon'], [10, 4, 'WireKeyword'],
    [15, 1, 'OpenBracket'], [16, 1, 'IntegerLiteral'], [17, 1, 'Colon'], [18, 1, 'IntegerLiteral'],
    [19, 1, 'CloseBracket'], [21, 1, 'Identifier'], [23, 1, 'Equals'], [25, 1, 'IntegerLiteral'],
    [26, 2, 'IntegerBase'], [28, 1, 'Identifier'], [29, 1, 'Semicolon'], [31, 4, 'LineComment'],
    [36, 9, 'EndModuleKeyword'],
  ].map(t => t.join(' ')).join('\n') + '\n';
  data.sources = [
    {file: '10', path: 'rtl/blk.sv', status: 'ok', text: 'a\nb\nc\nd\n'},
    {file: '11', path: 'rtl/tb/top.sv', status: 'ok', text, tokens},
  ];
  return data;
};

// Each instance counts its own lines; the parent merges them per line, so a
// line either instance missed is partial. Files and folders total their
// lines, and one test's hits are recounted from its own contributions.
{
  const report = loadReport(reportScript, JSON.stringify(twoInstances()));
  const top = view(report, '#/scope/top');
  assert.deepEqual(top.rows.filter(r => r.length === 4), [
    ['u0', 'blk', '50.0% 1/2', '—'],
    ['u1', 'blk', '50.0% 1/2', '—'],
  ]);
  const summary = view(report, '#/');
  assert.deepEqual(summary.rows[0], ['top', 'top', '33.3% 1/3', '—', '—']);
  assert.ok(summary.segments.has('1 / 3 lines, 2 partial'));
  // rtl/ holds both files: blk.sv's two partial lines and top.sv's covered one
  assert.match(summary.side, /<span class="name">rtl\/<\/span><span class="pct lvl-bad">33%<\/span>/);
  assert.match(summary.side, /<span class="name">blk\.sv<\/span><span class="pct lvl-bad">0%<\/span>/);
  report.selectTest('a');
  assert.ok(view(report, '#/').segments.has('1 / 3 lines, 1 partial'));
  assert.deepEqual(view(report, '#/scope/top').rows.filter(r => r.length === 4).map(r => r[2]),
                   ['50.0% 1/2', '0.0% 0/2']);
  report.selectTest('');
}

// The source view colors slang's tokens, marks each line by its statements,
// gives the range of their hits, and shows excluded lines as such.
{
  const report = loadReport(reportScript, JSON.stringify(twoInstances()));
  const lines = file => [...view(report, file).html.matchAll(
    /<div class="ln ([^"]*)" id="L(\d+)"><span class="no">\d+<\/span><span class="hits"[^>]*>([^<]*)<\/span><span>(.*?)<\/span><\/div>/g)]
    .map(m => ({cls: m[1], line: Number(m[2]), hits: m[3], html: m[4]}));
  const blk = lines('#/file/10/L4');
  assert.deepEqual(blk.map(l => [l.line, l.cls, l.hits]), [
    [1, '', ''], [2, '', ''], [3, 'partial', '0–1'], [4, 'partial target', '0–1'],
  ]);
  const [first, second, third] = lines('#/file/11');
  assert.equal(first.html, '<span class="tk-keyword">module</span> m;');
  assert.equal(second.html, '<span class="tk-type">wire</span> [<span class="tk-number">3</span>:' +
    '<span class="tk-number">0</span>] v = <span class="tk-number">4\'hA</span>; <span class="tk-comment">// c</span>');
  assert.deepEqual([second.cls, second.hits], ['full', '2']);
  assert.deepEqual([third.cls, third.hits], ['excluded', 'excl']);
  assert.ok(view(report, '#/scope/top').rows.some(r =>
    r[0] === 'rtl/tb/top.sv:3' && r[3] === 'excluded: generated'));
}

/* ----------------------------------------------------------- interaction */

// Folders and scopes open and close from their caret or name, and a long
// level's "Show N more" lists everything; the state survives re-rendering.
{
  const report = loadReport(reportScript, JSON.stringify(twoInstances()));
  report.render('#/');
  const hiddenKids = side => [...side.matchAll(/<div class="kids" data-key="([^"]*)" hidden="until-found">/g)].map(m => m[1]);
  assert.ok(!hiddenKids(report.render('#/').side).includes('dir:rtl/'));
  let out = report.clickSide('dir:rtl/', 'dir');
  assert.ok(out.prevented);
  assert.ok(hiddenKids(out.side).includes('dir:rtl/'), 'the folder closes');
  assert.ok(hiddenKids(report.render('#/tests').side).includes('dir:rtl/'), 'and stays closed');
  out = report.clickSide('dir:rtl/', 'caret');
  assert.ok(!hiddenKids(out.side).includes('dir:rtl/'), 'its caret opens it again');
  // a caret without children does nothing
  assert.equal(report.clickSide('file:10', 'caret', '').prevented, false);
}
{
  const data = emptyReport();
  data.scopes = [{id: '1', parent: '0', name: 'top', definition: 'top'}];
  for (let i = 0; i < 120; i++)
    data.scopes.push({id: String(100 + i), parent: '1', name: `top.u_${i}`, definition: 'blk'});
  data.scopes.push({id: '999', parent: '101', name: 'top.u_1.leaf', definition: 'leaf'});
  // "Show N more" lists the rest for good
  let report = loadReport(reportScript, JSON.stringify(data));
  assert.match(report.render('#/').side, /Show 20 more/);
  let out = report.clickSide('all:scope:top', 'more');
  assert.ok(out.prevented);
  assert.doesNotMatch(out.side, /class="rest"|Show 20 more/);
  assert.doesNotMatch(report.render('#/tests').side, /class="rest"/);
  // Find revealing hidden rows keeps them shown: the "Show N more" row goes,
  // a revealed level's caret shows it open, and both stay so on re-render.
  report = loadReport(reportScript, JSON.stringify(data));
  report.render('#/');
  assert.deepEqual(report.reveal('all:scope:top'), [['all:scope:top', 'remove']]);
  assert.doesNotMatch(report.render('#/').side, /class="rest"/);
  const changes = report.reveal('scope:top.u_1').slice(1);
  assert.deepEqual(changes, [['scope:top.u_1', 'text', '▾'], ['scope:top.u_1', 'aria-expanded', 'true']]);
  assert.match(report.render('#/').side, /<div class="kids" data-key="scope:top\.u_1">/);
}

// n and p step through uncovered and partial lines around the middle of the
// view, measured within the view, wrapping at the ends; keys typed into a
// form control, or with a modifier, are left alone, and other views ignore
// them.
{
  const report = loadReport(reportScript, JSON.stringify(twoInstances()));
  report.render('#/file/10');
  // lines sit at offsetTop line * 100 and the view is 100 high
  assert.deepEqual(report.press('n'), [3]);
  report.main.scrollTop = 250;
  assert.deepEqual(report.press('n'), [3, 4]);
  report.main.scrollTop = 350;
  assert.deepEqual(report.press('p'), [3, 4, 3]);
  report.main.scrollTop = 0;
  assert.deepEqual(report.press('p'), [3, 4, 3, 4]);
  assert.equal(report.press('n', {inField: true}).length, 4);
  assert.equal(report.press('n', {ctrlKey: true}).length, 4);
  report.render('#/');
  assert.equal(report.press('n').length, 4);
}

// The narrow layout's drawer opens from Browse and closes on a click
// elsewhere or on navigation. Modified clicks on links are the browser's.
{
  const report = loadReport(reportScript, JSON.stringify(twoInstances()));
  report.render('#/');
  assert.equal(report.browse(), true);
  assert.equal(report.clickElsewhere(), false);
  report.browse();
  report.render('#/tests');
  assert.equal(report.drawerOpen(), false);
  const out = report.click('#/scope/top', {ctrlKey: true});
  assert.equal(out.prevented, false);
  assert.match(out.main, /^<h1>Tests<\/h1>/);
}
// Picking an entry in the drawer closes it, also where views change without
// the URL, as in the website's sandboxed frame.
{
  const report = loadReport(reportScript, JSON.stringify(twoInstances()), {frozenUrl: true});
  report.render('#/');
  report.browse();
  const out = report.click('#/scope/top', {inSide: true});
  assert.match(out.main, /<h1>top<span class="sub">/);
  assert.equal(report.drawerOpen(), false);
}

// A mistyped or truncated link shows an unknown view instead of breaking the
// page.
{
  const report = loadReport(reportScript, JSON.stringify(twoInstances()));
  assert.match(report.render('#/scope/top%E0').main, /^<h1>Unknown scope<\/h1>/);
  assert.match(report.render('#/').main, /^<h1>Summary/);
}

/* ---------------------------------------------- functional coverage text */

const bin = (fields) => ({
  template_bin: '0', flags: '0', hierarchy: 'top.cg.i.cp.' + fields.name, count: '1',
  overflow: false, at_least: '1', contributing: true, covered: true, exclusion_reason: '',
  cross_selector: '0', contributors: [], ...fields,
});
const covergroupWith = (bins, fields = {}) => {
  const data = emptyReport();
  data.functional_types = [covergroup({})];
  data.functional_item_templates = [{id: '60', name: 'cp'}];
  data.functional_bin_templates = [{id: '70', name: 'low'}];
  data.functional_instance_groups = [{
    type: '50', configuration: '500', name: 'i', percent: 100, comment: '',
    items: [{
      id: '160', template_item: '60', kind: '2', name: 'x', comment: '', type_comment: '',
      hierarchy: 'top.cg.i.x', targets: [], covered: '1', total: '1', percent: 100,
      goal: '100', weight: '1', aggregating: true, at_least: '1', automatic_total: '0',
      automatic_at_least: '1', automatic_root_node: '0', bins, automatic_bins: [],
    }],
  }];
  return Object.assign(data, fields);
};
const limb = (fields) => ({low_aval: '0', high_aval: '0', low_bval: '0', high_bval: '0', wildcard_mask: '0', ...fields});
const valueSet = (id, atoms) => ({type: '50', configuration: '500', id, atoms});

// Values with X or Z bits or wildcards spell out their planes; real values
// show their bit patterns.
{
  const data = covergroupWith([bin({id: '170', kind: '2', name: 'b'})], {
    transition_steps: [],
    resolved_functional_value_sets: [
      valueSet('90', [{kind: '1', limbs: [limb({low_aval: '5', low_bval: '2'})]}]),
      valueSet('91', [{kind: '3', real_low_bits: '4607182418800017408', real_high_bits: '4611686018427387904', limbs: []}]),
      valueSet('92', [{kind: '1', limbs: [limb({low_aval: '1', wildcard_mask: '6'})]}]),
    ],
    resolved_transition_alternatives: [{type: '50', configuration: '500', bin: '170', id: '80', template_alternative_ordinal: '0'}],
    resolved_transition_steps: ['90', '91', '92'].map((value_set, ordinal) => ({
      type: '50', configuration: '500', alternative: '80', ordinal: String(ordinal), value_set,
      lower_bound: '1', upper_bound: '1'})),
  });
  const cg = view(loadReport(reportScript, JSON.stringify(data)), '#/cg/50/x');
  assert.equal(cg.rows[0][0], 'b = aval=0x5,bval=0x2,wildcard=0x0 => ' +
    'real-bits[0x3ff0000000000000:0x4000000000000000] => aval=0x1,bval=0x0,wildcard=0x6');
}

// Cross bins spell out their selector: binsof, negation, conjunction and
// disjunction, intersect with values or tuples (whose parts may name bins),
// with expressions, and nodes it cannot name or that refer back to themselves.
{
  const node = (id, kind, fields = {}) => ({id, kind: String(kind), target: '0', bin: '0', value_set: '0', with_expression: '', ...fields});
  const binding = (node, fields) => ({type: '50', configuration: '500', cross: '160', node, tuple_set: '0', value_set: '0', with_expression: '', ...fields});
  const data = covergroupWith([bin({id: '171', kind: '3', name: 'sel', cross_selector: '1'})], {
    cross_selector_nodes: [
      node('1', 3), node('2', 1, {target: '60', bin: '70'}), node('3', 2), node('5', 6), node('4', 4),
      node('6', 5, {value_set: '93'}), node('7', 7, {with_expression: 'unused'}), node('8', 5), node('9', 9),
      node('10', 2),
    ],
    cross_selector_operands: [
      ['1', '2'], ['1', '3'], ['1', '4'], ['3', '5'],
      ['4', '6'], ['4', '7'], ['4', '8'], ['4', '9'], ['4', '10'], ['10', '10'],
    ].map(([node, operand]) => ({node, operand})),
    resolved_cross_selector_bindings: [
      binding('6', {value_set: '93'}), binding('7', {with_expression: 'a > b'}), binding('8', {tuple_set: '300'}),
    ],
    resolved_functional_value_sets: [valueSet('93', [{kind: '1', limbs: [limb({low_aval: '5'})]}])],
    resolved_functional_bins: [{type: '50', configuration: '500', id: '172', name: 'low'}],
    resolved_functional_tuples: [{id: '301', tuple_set: '300'}],
    resolved_functional_tuple_components: [
      {tuple: '301', ordinal: '1', bin: '0', value_set: '93'}, {tuple: '301', ordinal: '0', bin: '172', value_set: '0'},
    ],
  });
  const cg = view(loadReport(reportScript, JSON.stringify(data)), '#/cg/50/x');
  assert.equal(cg.rows[0][0], 'sel = (binsof(cp.low) && !(all tuples) && (intersect {0x5} || ' +
    'all tuples with expression a > b || intersect {<low,0x5>} || selector 9 || !(<cycle>)))');
}

// With merge_instances, an inherited item that does not aggregate earns its
// test no unique contribution.
{
  const data = emptyReport();
  data.runs = [run('x', 'x'), run('y', 'y')];
  data.functional_types = [covergroup({merge_instances: true})];
  const item = (id, aggregating, uuid) => ({
    id, template_item: id, kind: '1', name: 'cp' + id, comment: '', type_comment: '',
    hierarchy: `top.cg.${id}`, targets: [], covered: '1', total: '1', percent: 100, goal: '100',
    weight: '1', aggregating, at_least: '1', automatic_total: '0', automatic_at_least: '1',
    bins: [bin({id: id + '0', kind: '1', name: 'b', contributors: [{uuid, name: uuid, count: '1'}]})],
    automatic_bins: [],
  });
  data.functional_instance_groups = [{type: '50', configuration: '500', name: 'i', percent: 100, comment: '',
                                      items: [item('61', false, 'x'), item('62', true, 'y')]}];
  const tests = view(loadReport(reportScript, JSON.stringify(data)), '#/tests');
  assert.deepEqual(tests.rows.filter(r => r.length === 6).map(r => [r[1], r[4]]), [['y', '1'], ['x', '0']]);
}

// The summary lists at most 200 scopes of the top two levels and says where
// the rest are.
{
  const data = emptyReport();
  data.scopes = [{id: '1', parent: '0', name: 'top', definition: 'top'}];
  for (let i = 0; i < 250; i++)
    data.scopes.push({id: String(100 + i), parent: '1', name: `top.u_${i}`, definition: 'blk'});
  const summary = view(loadReport(reportScript, JSON.stringify(data)), '#/');
  assert.equal(summary.rows.length, 200);
  assert.ok(summary.segments.has('51 more scopes in the sidebar.'));
}

// With merge_instances, an automatic cross tuple counts only where some
// instance's cross contains it.
{
  const data = emptyReport();
  data.runs = [run('x', 'x'), run('y', 'y')];
  data.functional_types = [covergroup({merge_instances: true})];
  const auto = (name, uuid) => ({
    name, components: name.slice(1, -1).split(',').map(n => ({name: n})), count: '1', overflow: false,
    at_least: '1', covered: true, missing: false, contributors: [{uuid, name: uuid, count: '1'}],
  });
  data.functional_instance_groups = [{type: '50', configuration: '500', name: 'i', percent: 100, comment: '',
    items: [{id: '160', template_item: '60', kind: '2', name: 'x', comment: '', type_comment: '',
             hierarchy: 'top.cg.x', targets: [], covered: '1', total: '2', percent: 50, goal: '100',
             weight: '1', aggregating: true, at_least: '1', automatic_total: '2', automatic_at_least: '1',
             automatic_root_node: '500', bins: [], automatic_bins: [auto('<one,two>', 'x'), auto('<zero,two>', 'y')]}]}];
  const key = {type: '50', configuration: '500', cross: '160'};
  data.resolved_cross_automatic_nodes = [{...key, id: '500'}, {...key, id: '501'}];
  data.resolved_cross_automatic_edges = [{...key, node: '500', bin: '172', child: '501'},
                                         {...key, node: '501', bin: '173', child: '0'}];
  data.resolved_functional_bins = [{type: '50', configuration: '500', id: '172', name: 'one'},
                                   {type: '50', configuration: '500', id: '173', name: 'two'}];
  const tests = view(loadReport(reportScript, JSON.stringify(data)), '#/tests');
  assert.deepEqual(tests.rows.filter(r => r.length === 6).map(r => [r[1], r[4]]), [['x', '1'], ['y', '0']]);
}

// A click inside an open level lands on its container, which is not a toggle:
// the link in it keeps its own handling.
{
  const report = loadReport(reportScript, JSON.stringify(twoInstances()));
  const before = report.render('#/').side;
  const out = report.clickSide('scope:top', 'kids');
  assert.equal(out.prevented, false);
  assert.equal(out.side, before);
}

console.log('coverage report renderer JS tests OK');
