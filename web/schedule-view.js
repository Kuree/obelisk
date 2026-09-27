// Renderer for the compiler-owned schedule JSON export.

const SVG_NS = 'http://www.w3.org/2000/svg';

const REGION_LABELS = {
  active: 'Active',
  nba: 'NBA',
  observed: 'Observed',
  reactive: 'Reactive',
  postponed: 'Postponed',
  unscheduled: 'Unscheduled',
};

const EDGE_GROUPS = {
  process_order: 'control',
  resume: 'control',
  spawn: 'spawn',
  sensitivity: 'sensitivity',
  nba_stage: 'nba',
  nba_activate: 'nba',
  conflict: 'conflict',
  deferred_stage: 'deferred',
  deferred_activate: 'deferred',
};

function prepareGraph(graph) {
  const { nodes, regions } = graph;
  let rank = 0;
  const placed = new Set();
  for (const region of regions) {
    for (const group of region.groups) {
      group.rank = rank++;
      group.nodes = group.members.map((index) => nodes[index]).filter(Boolean);
      for (const node of group.nodes) {
        node.region = region.kind;
        node.group = group;
        placed.add(node.index);
      }
    }
  }
  const unplaced = nodes.filter((node) => !placed.has(node.index));
  if (unplaced.length) {
    regions.push({
      kind: 'unscheduled',
      groups: unplaced.map((node) => ({
        members: [node.index], nodes: [node], schedule: 'acyclic', feedback: 0, rank: rank++,
      })),
    });
  }
  return graph;
}

export function parseSchedules(text) {
  const data = JSON.parse(text);
  if (data.schema !== 'schedule' || data.version !== 1 || !Array.isArray(data.graphs))
    throw new Error('unsupported schedule export schema');
  return data.graphs.map(prepareGraph);
}

function svg(tag, attributes = {}, text = '') {
  const node = document.createElementNS(SVG_NS, tag);
  for (const [name, value] of Object.entries(attributes)) node.setAttribute(name, value);
  if (text) node.textContent = text;
  return node;
}

function truncate(text, length) {
  return text.length > length ? `${text.slice(0, length - 1)}…` : text;
}

function nodeDescription(node) {
  const source = node.location
    ? `\nsource ${node.location.file}:${node.location.line}:${node.location.column}`
    : '';
  if (node.type === 'fragment') {
    return [
      `fragment #${node.id} @${node.function}`,
      `block ${node.block}, ${node.region} region, ${node.action}`,
      `${node.tier} tier, cost ${node.cost}, lane ${node.lane}`,
      `${node.twoState ? 'two-state eligible' : 'four-state'}, ${node.effects} effects${source}`,
    ].join('\n');
  }
  if (node.type === 'nba_commit') {
    const sites = node.slots.length + node.accumulatorSites.length + node.frontierSites.length;
    return `NBA commit #${node.id}\n${sites} staged update site${sites === 1 ? '' : 's'}`;
  }
  return `deferred event commit #${node.id}\n${node.sites.length} trigger site${node.sites.length === 1 ? '' : 's'}`;
}

function compactLocation(location) {
  if (!location) return '';
  const file = location.file.split(/[\\/]/).filter(Boolean).at(-1) ?? location.file;
  return `${truncate(file, 18)}:${location.line}:${location.column}`;
}

function nodeLines(node) {
  if (node.type === 'fragment') {
    return [
      `#${node.id}  @${truncate(node.function, node.location ? 17 : 34)}`,
      `bb${node.block} · ${node.action.replaceAll('_', ' ')}`,
      `${node.tier} · cost ${node.cost} · lane ${node.lane}${node.twoState ? ' · 2-state' : ''}`,
    ];
  }
  if (node.type === 'nba_commit') {
    const sites = node.slots.length + node.accumulatorSites.length + node.frontierSites.length;
    return [`#${node.id}  NBA commit`, `${sites} staged update site${sites === 1 ? '' : 's'}`, 'generated'];
  }
  return [
    `#${node.id}  Event commit`,
    `${node.sites.length} deferred trigger site${node.sites.length === 1 ? '' : 's'}`,
    'generated',
  ];
}

function edgeTitle(edge) {
  let title = `${edge.kind.replaceAll('_', ' ')}: #${edge.source} → #${edge.target}`;
  if (edge.resource) title += `\n${edge.resource}`;
  return title;
}

function renderGraph(graph, { onSourceLocation, onSourceDeselected } = {}) {
  const section = document.createElement('section');
  section.className = 'scheduleGraph';

  const summary = document.createElement('div');
  summary.className = 'scheduleSummary';
  const identity = document.createElement('strong');
  identity.textContent = `@${graph.name}`;
  summary.append(identity);
  for (const value of [
    `${graph.nodes.length} nodes`, `${graph.edges.length} edges`,
    `${graph.workers} worker${graph.workers === 1 ? '' : 's'}`, `VPI ${graph.vpi}`,
  ]) {
    const item = document.createElement('span');
    item.textContent = value;
    summary.append(item);
  }
  section.append(summary);

  const legend = document.createElement('div');
  legend.className = 'scheduleLegend';
  for (const [className, label] of [
    ['control', 'process order / resume'], ['spawn', 'spawn'],
    ['sensitivity', 'sensitivity'], ['nba', 'NBA'],
    ['deferred', 'deferred'], ['conflict', 'conflict'],
  ]) {
    const item = document.createElement('span');
    item.className = `scheduleKey ${className}`;
    item.textContent = label;
    legend.append(item);
  }
  section.append(legend);

  const nodeWidth = 310;
  const nodeHeight = 52;
  const nodeGap = 6;
  const groupGap = 10;
  const left = 104;
  const positions = new Map();
  const groupLayouts = new Map();
  let top = 16;
  const regionLayouts = [];

  for (const region of graph.regions) {
    const groups = region.groups;
    const regionTop = top;
    let cursor = top + 34;
    for (const group of groups) {
      const groupHeight = 22 + group.nodes.length * (nodeHeight + nodeGap) - nodeGap;
      groupLayouts.set(group, {
        x: left - 7, y: cursor, width: nodeWidth + 14, height: groupHeight,
      });
      group.nodes.forEach((node, row) => {
        positions.set(node.index, {
          x: left, y: cursor + 18 + row * (nodeHeight + nodeGap),
          width: nodeWidth, height: nodeHeight,
        });
      });
      cursor += groupHeight + groupGap;
    }
    const height = groups.length ? cursor - regionTop - groupGap + 10 : 42;
    regionLayouts.push({ region, y: regionTop, height });
    top += height + 12;
  }

  const width = 580;
  const height = Math.max(260, top + 4);
  const canvas = document.createElement('div');
  canvas.className = 'scheduleCanvas';
  const drawing = svg('svg', {
    viewBox: `0 0 ${width} ${height}`,
    width,
    height,
    role: 'img',
    'aria-label': `Compute schedule for ${graph.name}`,
  });
  const defs = svg('defs');
  for (const kind of ['control', 'spawn', 'sensitivity', 'nba', 'deferred', 'conflict']) {
    const marker = svg('marker', {
      id: `schedule-arrow-${kind}`, markerWidth: 7, markerHeight: 7,
      refX: 6, refY: 3.5, orient: 'auto', markerUnits: 'strokeWidth',
    });
    marker.append(svg('path', { d: 'M0,0 L7,3.5 L0,7 z', class: `edgeArrow ${kind}` }));
    defs.append(marker);
  }
  drawing.append(defs);

  const backgrounds = svg('g', { class: 'scheduleRegions' });
  const edgeLayer = svg('g', { class: 'scheduleEdges' });
  const nodeLayer = svg('g', { class: 'scheduleNodes' });
  drawing.append(backgrounds, edgeLayer, nodeLayer);

  for (const { region, y, height: regionHeight } of regionLayouts) {
    const regionGroup = svg('g', { class: `scheduleRegion region-${region.kind}` });
    regionGroup.append(svg('rect', { x: 8, y, width: width - 16, height: regionHeight, rx: 5 }));
    regionGroup.append(svg('text', { x: 18, y: y + 24, class: 'regionName' },
      REGION_LABELS[region.kind] ?? region.kind));
    if (!region.groups.length)
      regionGroup.append(svg('text', { x: left, y: y + 25, class: 'emptyRegion' }, 'no scheduled work'));
    backgrounds.append(regionGroup);

    for (const group of region.groups) {
      const first = positions.get(group.nodes[0]?.index);
      const layout = groupLayouts.get(group);
      if (!first || !layout) continue;
      const groupNode = svg('g', { class: `scheduleGroup group-${group.schedule}` });
      groupNode.append(svg('rect', {
        x: layout.x, y: layout.y, width: layout.width, height: layout.height, rx: 5,
      }));
      groupNode.append(svg('text', { x: first.x, y: layout.y + 13, class: 'groupName' },
        `rank ${group.rank} · ${group.schedule.replaceAll('_', ' ')}`));
      if (group.feedback)
        groupNode.append(svg('text', {
          x: first.x + nodeWidth - 4, y: layout.y + 13,
          class: 'feedbackCount', 'text-anchor': 'end',
        }, `${group.feedback} feedback`));
      nodeLayer.append(groupNode);
    }
  }

  graph.edges.forEach((edge, edgeIndex) => {
    const source = positions.get(edge.source);
    const target = positions.get(edge.target);
    if (!source || !target) return;
    const category = EDGE_GROUPS[edge.kind] ?? 'control';
    const forward = target.y >= source.y;
    const railOffset = 24 + (edgeIndex % 8) * 9;
    const startX = forward ? source.x + source.width : source.x;
    const startY = source.y + source.height / 2;
    const endX = forward ? target.x + target.width : target.x;
    const endY = target.y + target.height / 2;
    const rail = forward ? startX + railOffset : startX - railOffset;
    const path = `M${startX},${startY} C${rail},${startY} ${rail},${endY} ${endX},${endY}`;
    const edgeNode = svg('path', {
      d: path,
      class: `scheduleEdge edge-${category}`,
      'marker-end': `url(#schedule-arrow-${category})`,
    });
    edgeNode.append(svg('title', {}, edgeTitle(edge)));
    edgeLayer.append(edgeNode);
  });

  for (const node of graph.nodes) {
    const position = positions.get(node.index);
    if (!position) continue;
    const group = svg('g', {
      class: `scheduleNode tier-${node.tier}${node.location ? ' hasSource' : ''}`,
      transform: `translate(${position.x} ${position.y})`,
      tabindex: '0',
    });
    if (node.location && onSourceLocation) {
      group.setAttribute('role', 'button');
      group.setAttribute('aria-label',
        `Show source at ${node.location.file}:${node.location.line}:${node.location.column}`);
      group.addEventListener('click', () => {
        group.focus();
        onSourceLocation(node.location);
      });
      group.addEventListener('keydown', (event) => {
        if (event.key !== 'Enter' && event.key !== ' ') return;
        event.preventDefault();
        onSourceLocation(node.location);
      });
      group.addEventListener('blur', () => onSourceDeselected?.(node.location));
    }
    group.append(svg('title', {}, nodeDescription(node)));
    group.append(svg('rect', { width: nodeWidth, height: nodeHeight, rx: 4 }));
    const lines = nodeLines(node);
    lines.forEach((line, index) => {
      group.append(svg('text', {
        x: 10, y: 16 + index * 15, class: index === 0 ? 'nodeTitle' : 'nodeDetail',
      }, line));
    });
    if (node.location) {
      group.append(svg('text', {
        x: nodeWidth - 10, y: 16, class: 'nodeLocation', 'text-anchor': 'end',
      }, compactLocation(node.location)));
    }
    nodeLayer.append(group);
  }

  canvas.append(drawing);
  section.append(canvas);
  return section;
}

export function renderSchedules(container, schedules, options = {}) {
  const fragment = document.createDocumentFragment();
  for (const graph of schedules) fragment.append(renderGraph(graph, options));
  container.replaceChildren(fragment);
}
