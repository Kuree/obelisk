import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';

if (process.argv.length !== 3) {
  console.error('usage: CoverageReport.test.mjs CoverageReport.js');
  process.exit(2);
}

const reportScript = fs.readFileSync(process.argv[2], 'utf8');

class Element {
  constructor(tag) {
    this.tag = tag;
    this.children = [];
    this.textContent = '';
    this.value = '';
    this.hidden = false;
    this.className = '';
    this.id = '';
    this.href = '';
    this.listeners = new Map();
  }

  append(child) {
    this.children.push(child);
  }

  addEventListener(kind, callback) {
    this.listeners.set(kind, callback);
  }

  dispatch(kind) {
    this.listeners.get(kind)?.();
  }

  set innerHTML(value) {
    throw new Error(`renderer used innerHTML for ${value}`);
  }
}

const emptyReport = () => ({
  codec_version: 1,
  schema_fingerprint: '0123456789abcdef',
  files: [],
  scopes: [],
  runs: [],
  metrics: {
    line: {available: false, covered: 0, total: 0, percent: 0},
    toggle: {available: false, covered: 0, total: 0, percent: 0},
    functional: {available: false, covered: 0, total: 0, percent: 0},
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
});

const render = (data) => {
  const sinks = new Map();
  const elements = new Map();
  for (const id of ['identity', 'test-filter', 'file-nav', 'hierarchy-nav'])
    elements.set(id, new Element(id === 'test-filter' ? 'select' : 'div'));
  const document = {
    createElement: (tag) => new Element(tag),
    getElementById: (id) =>
      id === 'coverage-data'
        ? {textContent: JSON.stringify(data)}
        : elements.get(id) ?? new Element('div'),
    querySelector: (selector) => {
      if (!sinks.has(selector)) sinks.set(selector, new Element('container'));
      return sinks.get(selector);
    },
  };
  vm.runInNewContext(reportScript, {document});
  return {
    elements,
    rows: (selector) =>
      (sinks.get(selector)?.children ?? []).map((row) => ({
        element: row,
        cells: row.children.map((cell) => String(cell.textContent)),
      })),
  };
};

// Metric formatting keeps unavailable metrics distinct and exposes threshold
// state without inventing a combined coverage percentage.
{
  const data = emptyReport();
  data.metrics.line = {
    available: true,
    covered: 1,
    total: 2,
    percent: 50,
    threshold: 75,
    threshold_pass: false,
  };
  data.metrics.functional = {
    available: true,
    covered: 3,
    total: 3,
    percent: 100,
    threshold: 100,
    threshold_pass: true,
  };
  const view = render(data);
  const rows = view.rows('#metrics tbody');
  assert.deepEqual(rows.map((row) => row.cells.slice(0, 4)), [
    ['line', '1/2', '50.00%', 'FAIL 75.00%'],
    ['toggle', 'unavailable', '—', '—'],
    ['functional', '3/3', '100.00%', 'PASS 100.00%'],
  ]);
  assert.equal(rows[0].element.children[3].className, 'fail');
  assert.equal(rows[2].element.children[3].className, 'pass');
  assert.equal(rows[0].element.children[4].children[0].value, 50);
}

// Metadata is rendered as text, duplicate test names stay distinguishable,
// and selecting a test hides only rows to which that test did not contribute.
{
  const data = emptyReport();
  const unsafeName = '<img src=x onerror=alert(1)>';
  data.files = [{id: '10', path: 'rtl/<dut>&.sv'}];
  data.scopes = [{id: '20', name: 'top.<dut>'}];
  data.runs = [
    {
      uuid: 'run-a', name: unsafeName, status: 0, simulation_time: 4,
      timestamp: 10, seed: 11, tags: {suite: '<nightly>'},
    },
    {
      uuid: 'run-b', name: unsafeName, status: 0, simulation_time: 5,
      timestamp: 12, seed: 13, tags: {},
    },
  ];
  data.line_points = [{
    id: '30', file: '10', scope: '20', line: 7, column: 3,
    count: '1', overflow: false, covered: true, excluded: false,
    exclusion_reason: '',
    contributors: [{uuid: 'run-a', name: unsafeName, count: '1'}],
  }];
  data.toggles = [{
    scope: '20', excluded: false, exclusion_reason: '',
    bits: [{
      paths: ['signal[0]'],
      zero_to_one: {
        count: '1', overflow: false, covered: true,
        contributors: [{uuid: 'run-b', name: unsafeName, count: '1'}],
      },
      one_to_zero: {count: '0', overflow: false, covered: false, contributors: []},
      to_unknown: {count: '0', overflow: false, contributors: []},
      from_unknown: {count: '0', overflow: false, contributors: []},
    }],
  }];
  const view = render(data);
  const labels = view.elements.get('test-filter').children;
  assert.equal(labels[0].textContent, `${unsafeName} (run-a)`);
  assert.equal(labels[1].textContent, `${unsafeName} (run-b)`);
  assert.deepEqual(view.rows('#line-points tbody')[0].cells, [
    'top.<dut>', 'rtl/<dut>&.sv:7:3', 'point 30', '1', 'covered',
    `${unsafeName} (run-a)`,
  ]);
  assert.deepEqual(view.rows('#toggle-bits tbody')[0].cells.slice(0, 6), [
    'top.<dut>', 'signal[0]', '1', '0', 'to X/Z 0, from X/Z 0', 'partial',
  ]);
  const filter = view.elements.get('test-filter');
  filter.value = 'run-a';
  filter.dispatch('change');
  assert.equal(view.rows('#line-points tbody')[0].element.hidden, false);
  assert.equal(view.rows('#toggle-bits tbody')[0].element.hidden, true);
  filter.value = '';
  filter.dispatch('change');
  assert.equal(view.rows('#toggle-bits tbody')[0].element.hidden, false);
  assert.equal(view.elements.get('file-nav').children[0].textContent,
               'rtl/<dut>&.sv');
}

// Transition ranges and repetition are reconstructed from the resolved v1
// schema, while saturating counters retain their diagnostic annotation.
{
  const data = emptyReport();
  data.runs = [{
    uuid: 'run-a', name: 'transition-test', status: 0, simulation_time: 9,
    timestamp: 10, seed: 11, tags: {},
  }];
  data.functional_types = [{
    id: '50', hierarchy: 'top.cg', percent: 100, comment: '',
    merge_instances: false,
  }];
  data.functional_item_templates = [{id: '60', name: 'cp'}];
  data.functional_bin_templates = [{id: '70', name: 'round_trip'}];
  data.transition_steps = [
    {bin: '70', alternative_ordinal: 0, ordinal: 0, repetition: 1},
    {bin: '70', alternative_ordinal: 0, ordinal: 1, repetition: 2},
  ];
  data.resolved_functional_value_sets = [
    {
      type: '50', configuration: '500', id: '90', atoms: [{
        kind: 1,
        limbs: [{
          low_aval: '5', high_aval: '5', low_bval: '0', high_bval: '0',
          wildcard_mask: '0',
        }],
      }],
    },
    {
      type: '50', configuration: '500', id: '91', atoms: [{
        kind: 2,
        limbs: [{
          low_aval: '8', high_aval: '9', low_bval: '0', high_bval: '0',
          wildcard_mask: '0',
        }],
      }],
    },
  ];
  data.resolved_transition_alternatives = [{
    type: '50', configuration: '500', bin: '170', id: '80',
    template_alternative_ordinal: 0,
  }];
  data.resolved_transition_steps = [
    {
      type: '50', configuration: '500', alternative: '80', ordinal: 0,
      value_set: '90', lower_bound: '1', upper_bound: '1',
    },
    {
      type: '50', configuration: '500', alternative: '80', ordinal: 1,
      value_set: '91', lower_bound: '2', upper_bound: '4',
    },
  ];
  const contributor = {uuid: 'run-a', name: 'transition-test', count: '1'};
  data.functional_instance_groups = [{
    type: '50', configuration: '500', name: 'instance', percent: 100,
    comment: '',
    items: [{
      id: '160', template_item: '60', kind: 1, name: 'cp', comment: '',
      type_comment: '', hierarchy: 'top.cg.instance.cp', targets: [],
      covered: '1', total: '1', percent: 100, goal: 100, weight: 1,
      aggregating: true, at_least: '1', automatic_total: '0',
      automatic_at_least: '1',
      bins: [{
        id: '170', template_bin: '70', kind: 2, flags: 0,
        name: 'round_trip', hierarchy: 'top.cg.instance.cp.round_trip',
        count: '18446744073709551615', overflow: true, at_least: '1',
        contributing: true, covered: true, exclusion_reason: '',
        cross_selector: '0', contributors: [contributor],
      }],
      automatic_bins: [],
    }],
  }];
  const view = render(data);
  assert.deepEqual(view.rows('#functional-bins tbody')[0].cells, [
    'instance', 'cp', 'transition',
    'round_trip = 0x5 => [0x8:0x9][*2:4]',
    '18446744073709551615 (saturated)', '1', 'covered', 'transition-test',
  ]);
}

// Sparse automatic cross tuples retain covered/missing state and contribute a
// valid hierarchy navigation target.
{
  const data = emptyReport();
  data.functional_types = [{
    id: '50', hierarchy: 'top.cg', percent: 50, comment: '',
    merge_instances: false,
  }];
  data.functional_instance_groups = [{
    type: '50', configuration: '500', name: 'instance', percent: 50,
    comment: '',
    items: [{
      id: '160', template_item: '60', kind: 2, name: 'cp_x_cp2', comment: '',
      type_comment: '', hierarchy: 'top.cg.instance.cp_x_cp2', targets: [],
      covered: '1', total: '2', percent: 50, goal: 100, weight: 1,
      aggregating: true, at_least: '1', automatic_total: '2',
      automatic_at_least: '1',
      bins: [],
      automatic_bins: [
        {
          name: '<one,two>', components: [{name: 'one'}, {name: 'two'}],
          count: '1', overflow: false, at_least: '1', covered: true,
          missing: false, contributors: [],
        },
        {
          name: '<zero,two>', components: [{name: 'zero'}, {name: 'two'}],
          count: '0', overflow: false, at_least: '1', covered: false,
          missing: true, contributors: [],
        },
      ],
    }],
  }];
  const view = render(data);
  const rows = view.rows('#functional-bins tbody');
  assert.equal(rows[0].cells[2], 'automatic cross tuple');
  assert.equal(rows[0].cells[3], '<one,two>');
  assert.equal(rows[0].cells[6], 'covered');
  assert.equal(rows[1].cells[3], '<zero,two>');
  assert.equal(rows[1].cells[6], 'missing');
  const links = view.elements.get('hierarchy-nav').children;
  assert.ok(links.some((link) =>
    link.textContent === 'top.cg.instance.cp_x_cp2' &&
    /^#hierarchy-\d+$/.test(link.href)));
}

// A zero-bin resolved configuration still raises the cumulative at_least for
// unique-contribution ranking when merge_instances is enabled.
{
  const data = emptyReport();
  data.runs = [
    {uuid: 'low', name: 'low', status: 0, simulation_time: 0,
     timestamp: 0, seed: 0, tags: {}},
    {uuid: 'high', name: 'high', status: 0, simulation_time: 0,
     timestamp: 0, seed: 0, tags: {}},
  ];
  data.functional_types = [{
    id: '50', hierarchy: 'top.cg', percent: 0, comment: '',
    merge_instances: true,
  }];
  const item = (id, atLeast, bins) => ({
    id, template_item: '60', kind: 1, name: 'cp', comment: '',
    type_comment: '', hierarchy: `top.cg.${id}.cp`, targets: [],
    covered: '0', total: String(bins.length), percent: 0, goal: 100,
    weight: 1, aggregating: true, at_least: String(atLeast),
    automatic_total: '0', automatic_at_least: String(atLeast), bins,
    automatic_bins: [],
  });
  data.functional_instance_groups = [
    {
      type: '50', configuration: 'low-config', name: 'low', percent: 100,
      comment: '', items: [item('160', 1, [{
        id: '170', template_bin: '70', kind: 1, flags: 0, name: 'only-low',
        hierarchy: 'top.cg.low.cp.only-low', count: '1', overflow: false,
        at_least: '1', contributing: true, covered: true,
        exclusion_reason: '', cross_selector: '0',
        contributors: [{uuid: 'low', name: 'low', count: '1'}],
      }])],
    },
    {
      type: '50', configuration: 'high-config', name: 'high', percent: 0,
      comment: '', items: [item('161', 2, [])],
    },
  ];
  const view = render(data);
  assert.deepEqual(
      view.rows('#unique-contributions tbody').map(
          row => [row.cells[1], row.cells[4]]),
      [['high', '0'], ['low', '0']]);
}

console.log('coverage report renderer JS tests OK');
