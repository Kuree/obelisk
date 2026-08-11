import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';

// The site is intentionally package-free, so load its browser module through
// a data URL instead of requiring a package.json solely for this test.
const source = await readFile(new URL('./schedule-view.js', import.meta.url), 'utf8');
const { parseSchedules, renderSchedules } = await import(
  `data:text/javascript;base64,${Buffer.from(source).toString('base64')}`
);

const schedule = `schedule @test #obelisk_sim.graph<
  version = 1, vpi = read, workers = 2,
  nodes = [
    #obelisk_sim.fragment<id = 0, function = @root, block = 0,
      region = active, action = suspend_edge, tier = native, cost = 7,
      lane = 1, twoState = true,
      effects = [#obelisk_sim.effect<effect = watch, resource = net>]>,
    #obelisk_sim.nba_commit<id = 1, slots = [2, 3], accumulatorSites = [4],
      frontierSites = [], effect = <effect = write, resource = storage>>,
    #obelisk_sim.event_commit<id = 2, sites = [5],
      effect = <effect = trigger, resource = event>>],
  edges = [
    #obelisk_sim.edge<source = 0, target = 1, kind = nba_stage,
      resource = <effect = nba, resource = storage>>,
    #obelisk_sim.edge<source = 0, target = 2, kind = deferred_stage,
      resource = <effect = trigger, resource = event>>],
  regions = [
    #obelisk_sim.region<kind = active, groups = [
      #obelisk_sim.group<fragments = [0], schedule = convergence,
        feedback = [#obelisk_sim.effect<effect = drive, resource = net>]>]>,
    #obelisk_sim.region<kind = nba, groups = [
      #obelisk_sim.group<fragments = [1], schedule = acyclic, feedback = []>]>,
    #obelisk_sim.region<kind = observed, groups = []>,
    #obelisk_sim.region<kind = reactive, groups = [
      #obelisk_sim.group<fragments = [2], schedule = acyclic, feedback = []>]>,
    #obelisk_sim.region<kind = postponed, groups = []>]>
  source_locations = [#0 = "design.sv":7:3, #2 = "inc/worker.sv":11:5]`;

const [graph] = parseSchedules(schedule);
assert.equal(graph.name, 'test');
assert.deepEqual([graph.version, graph.vpi, graph.workers], [1, 'read', 2]);
assert.deepEqual(graph.nodes.map((node) => node.type), [
  'fragment', 'nba_commit', 'event_commit',
]);
assert.equal(graph.nodes[0].effects, 1);
assert.deepEqual(graph.nodes[0].location, { file: 'design.sv', line: 7, column: 3 });
assert.equal(graph.nodes[1].location, null);
assert.deepEqual(graph.nodes[2].location, { file: 'inc/worker.sv', line: 11, column: 5 });
assert.deepEqual(graph.nodes[1].slots, [2, 3]);
assert.deepEqual(graph.nodes[2].sites, [5]);
assert.deepEqual(graph.edges.map((edge) => edge.kind), ['nba_stage', 'deferred_stage']);
assert.deepEqual(graph.regions.map((region) => region.groups.length), [1, 1, 0, 1, 0]);
assert.equal(graph.regions[0].groups[0].feedback, 1);

// Keep this list in lockstep with SimulationEnums.td. Together with all node,
// tier, group, and region forms below, this covers the complete graph schema
// emitted by `-emit-schedule`, rather than only the common counter shape.
const actions = [
  'continue', 'suspend_delay', 'suspend_change', 'suspend_edge',
  'suspend_any', 'suspend_event', 'suspend_await', 'suspend_join',
  'terminate', 'task_call', 'suspend_children', 'suspend_observe',
];
const edgeKinds = [
  'process_order', 'resume', 'spawn', 'sensitivity', 'nba_stage',
  'nba_activate', 'conflict', 'deferred_stage', 'deferred_activate',
];
const resourceEdges = new Set(edgeKinds.slice(3));
const fragments = actions.map((action, id) =>
  `#obelisk_sim.fragment<id = ${id}, function = @fragment_${id}, block = ${id},
    region = active, action = ${action}, tier = ${['native', 'bytecode', 'generated'][id % 3]},
    cost = ${id + 1}, lane = ${id % 2}, twoState = ${id % 2 === 0}, effects = []>`,
);
const edges = edgeKinds.map((kind, index) =>
  `#obelisk_sim.edge<source = ${index}, target = ${index + 1}, kind = ${kind}${
    resourceEdges.has(kind) ? ', resource = <effect = drive, resource = net>' : ''}>`,
);
const completeSchedule = `schedule @complete #obelisk_sim.graph<
  version = 1, vpi = full, workers = 2,
  nodes = [${fragments.join(',')},
    #obelisk_sim.nba_commit<id = 12, slots = [0], accumulatorSites = [],
      frontierSites = [], effect = <effect = write, resource = storage>>,
    #obelisk_sim.event_commit<id = 13, sites = [0],
      effect = <effect = trigger, resource = event>>],
  edges = [${edges.join(',')}],
  regions = [
    #obelisk_sim.region<kind = active, groups = [
      #obelisk_sim.group<fragments = [0, 1, 2, 3], schedule = convergence,
        feedback = [#obelisk_sim.effect<effect = drive, resource = net>]>,
      #obelisk_sim.group<fragments = [4, 5, 6, 7], schedule = control_loop,
        feedback = []>,
      #obelisk_sim.group<fragments = [8, 9, 10, 11], schedule = acyclic,
        feedback = []>]>,
    #obelisk_sim.region<kind = nba, groups = [
      #obelisk_sim.group<fragments = [12], schedule = acyclic, feedback = []>]>,
    #obelisk_sim.region<kind = observed, groups = []>,
    #obelisk_sim.region<kind = reactive, groups = [
      #obelisk_sim.group<fragments = [13], schedule = acyclic, feedback = []>]>,
    #obelisk_sim.region<kind = postponed, groups = []>]>`;

const [complete] = parseSchedules(completeSchedule);
assert.deepEqual(complete.nodes.slice(0, actions.length).map((node) => node.action), actions);
assert.deepEqual(complete.edges.map((edge) => edge.kind), edgeKinds);
assert.deepEqual([...new Set(complete.nodes.slice(0, actions.length).map((node) => node.tier))],
  ['native', 'bytecode', 'generated']);
assert.deepEqual(complete.regions[0].groups.map((group) => group.schedule),
  ['convergence', 'control_loop', 'acyclic']);

class TestNode {
  constructor(tag) {
    this.tag = tag;
    this.children = [];
    this.attributes = {};
    this.className = '';
    this.textContent = '';
    this.listeners = new Map();
    this.focused = false;
  }
  setAttribute(name, value) { this.attributes[name] = String(value); }
  addEventListener(name, callback) { this.listeners.set(name, callback); }
  focus() { this.focused = true; }
  append(...children) { this.children.push(...children); }
  replaceChildren(...children) { this.children = children; }
}

globalThis.document = {
  createElement: (tag) => new TestNode(tag),
  createElementNS: (_namespace, tag) => new TestNode(tag),
  createDocumentFragment: () => new TestNode('fragment'),
};

const container = new TestNode('div');
renderSchedules(container, [complete]);
const descendants = [];
const visit = (node) => {
  descendants.push(node);
  for (const child of node.children) visit(child);
};
visit(container);
const hasClass = (node, name) =>
  node.attributes.class?.split(/\s+/).includes(name) ?? false;
assert.equal(descendants.filter((node) => hasClass(node, 'scheduleNode')).length, 14);
assert.equal(descendants.filter((node) => hasClass(node, 'scheduleEdge')).length, 9);
assert.equal(descendants.filter((node) => hasClass(node, 'scheduleRegion')).length, 5);

const sourceContainer = new TestNode('div');
const activated = [];
const deselected = [];
renderSchedules(sourceContainer, [graph], {
  onSourceLocation: (location) => activated.push(location),
  onSourceDeselected: (location) => deselected.push(location),
});
const sourceDescendants = [];
const visitSource = (node) => {
  sourceDescendants.push(node);
  for (const child of node.children) visitSource(child);
};
visitSource(sourceContainer);
assert.equal(sourceDescendants.filter((node) => hasClass(node, 'hasSource')).length, 2);
assert.deepEqual(
  sourceDescendants.filter((node) => hasClass(node, 'nodeLocation')).map((node) => node.textContent),
  ['design.sv:7:3', 'worker.sv:11:5'],
);
const sourceNodes = sourceDescendants.filter((node) => hasClass(node, 'hasSource'));
assert.equal(sourceNodes[0].attributes.role, 'button');
sourceNodes[0].listeners.get('click')();
assert.equal(sourceNodes[0].focused, true);
let prevented = false;
sourceNodes[1].listeners.get('keydown')({
  key: 'Enter', preventDefault: () => { prevented = true; },
});
assert.equal(prevented, true);
assert.deepEqual(activated, [graph.nodes[0].location, graph.nodes[2].location]);
sourceNodes[1].listeners.get('blur')();
assert.deepEqual(deselected, [graph.nodes[2].location]);

console.log('schedule view schema and renderer OK');
