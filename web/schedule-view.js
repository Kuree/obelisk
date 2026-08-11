// Parser and renderer for `obelisk -emit-schedule`. The emitted graph is a
// verified, versioned MLIR attribute, so a small balanced-delimiter reader is
// enough here; regular expressions alone would break on nested effect attrs.

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

function balancedEnd(text, start) {
  const pairs = { '<': '>', '[': ']', '{': '}', '(': ')' };
  const stack = [];
  let quote = null;
  let escaped = false;
  for (let index = start; index < text.length; index++) {
    const character = text[index];
    if (quote) {
      if (escaped) escaped = false;
      else if (character === '\\') escaped = true;
      else if (character === quote) quote = null;
      continue;
    }
    if (character === '"' || character === "'") {
      quote = character;
      continue;
    }
    if (pairs[character]) stack.push(pairs[character]);
    else if (stack.at(-1) === character) {
      stack.pop();
      if (stack.length === 0) return index;
    }
  }
  return -1;
}

function splitTopLevel(text, delimiter = ',') {
  const parts = [];
  const pairs = { '<': '>', '[': ']', '{': '}', '(': ')' };
  const stack = [];
  let quote = null;
  let escaped = false;
  let start = 0;
  for (let index = 0; index < text.length; index++) {
    const character = text[index];
    if (quote) {
      if (escaped) escaped = false;
      else if (character === '\\') escaped = true;
      else if (character === quote) quote = null;
      continue;
    }
    if (character === '"' || character === "'") quote = character;
    else if (pairs[character]) stack.push(pairs[character]);
    else if (stack.at(-1) === character) stack.pop();
    else if (character === delimiter && stack.length === 0) {
      parts.push(text.slice(start, index).trim());
      start = index + 1;
    }
  }
  parts.push(text.slice(start).trim());
  return parts.filter(Boolean);
}

function parseFields(text) {
  const fields = new Map();
  for (const part of splitTopLevel(text)) {
    const equal = part.indexOf('=');
    if (equal !== -1)
      fields.set(part.slice(0, equal).trim(), part.slice(equal + 1).trim());
  }
  return fields;
}

function attributes(text, kinds) {
  const wanted = new Set(Array.isArray(kinds) ? kinds : [kinds]);
  const pattern = /#obelisk_sim\.([a-z_]+)</g;
  const found = [];
  for (const match of text.matchAll(pattern)) {
    if (!wanted.has(match[1])) continue;
    const open = match.index + match[0].length - 1;
    const end = balancedEnd(text, open);
    if (end === -1) throw new Error(`unterminated #obelisk_sim.${match[1]} attribute`);
    found.push({ kind: match[1], start: match.index, end, body: text.slice(open + 1, end) });
  }
  return found;
}

function arrayNumbers(value = '[]') {
  const body = value.trim().replace(/^\[/, '').replace(/\]$/, '');
  if (!body.trim()) return [];
  return splitTopLevel(body).map(Number).filter(Number.isFinite);
}

function number(fields, name, fallback = 0) {
  const value = Number(fields.get(name));
  return Number.isFinite(value) ? value : fallback;
}

function word(fields, name, fallback = '') {
  return fields.get(name)?.replace(/^@/, '').replace(/^"|"$/g, '') ?? fallback;
}

function countEffects(value = '[]') {
  return attributes(value, 'effect').length;
}

function parseSourceLocations(text) {
  const marker = /\bsource_locations\s*=\s*\[/.exec(text);
  if (!marker) return new Map();
  const open = marker.index + marker[0].length - 1;
  const end = balancedEnd(text, open);
  if (end === -1) throw new Error('unterminated schedule source location list');
  const locations = new Map();
  const entry = /#(\d+)\s*=\s*("(?:\\.|[^"\\])*"):(\d+):(\d+)/g;
  for (const match of text.slice(open + 1, end).matchAll(entry)) {
    let file;
    try {
      file = JSON.parse(match[2]);
    } catch {
      continue;
    }
    locations.set(Number(match[1]), {
      file, line: Number(match[3]), column: Number(match[4]),
    });
  }
  return locations;
}

function parseNode(attribute, index) {
  const fields = parseFields(attribute.body);
  const node = { index, id: number(fields, 'id', index), type: attribute.kind };
  if (attribute.kind === 'fragment') {
    Object.assign(node, {
      function: word(fields, 'function', '<anonymous>'),
      block: number(fields, 'block'),
      region: word(fields, 'region'),
      action: word(fields, 'action'),
      tier: word(fields, 'tier'),
      cost: number(fields, 'cost'),
      lane: number(fields, 'lane'),
      twoState: word(fields, 'twoState') === 'true',
      effects: countEffects(fields.get('effects')),
    });
  } else if (attribute.kind === 'nba_commit') {
    Object.assign(node, {
      slots: arrayNumbers(fields.get('slots')),
      accumulatorSites: arrayNumbers(fields.get('accumulatorSites')),
      frontierSites: arrayNumbers(fields.get('frontierSites')),
      tier: 'generated',
      action: 'commit',
    });
  } else {
    Object.assign(node, {
      sites: arrayNumbers(fields.get('sites')),
      tier: 'generated',
      action: 'commit',
    });
  }
  return node;
}

function parseEdge(attribute) {
  const fields = parseFields(attribute.body);
  const resource = fields.get('resource') ?? '';
  return {
    source: number(fields, 'source', -1),
    target: number(fields, 'target', -1),
    kind: word(fields, 'kind', 'unknown'),
    resource: resource.replace(/^</, '').replace(/>$/, ''),
  };
}

function parseRegion(attribute) {
  const fields = parseFields(attribute.body);
  const groups = attributes(fields.get('groups') ?? '[]', 'group').map((group) => {
    const groupFields = parseFields(group.body);
    return {
      members: arrayNumbers(groupFields.get('fragments')),
      schedule: word(groupFields, 'schedule', 'acyclic'),
      feedback: countEffects(groupFields.get('feedback')),
    };
  });
  return { kind: word(fields, 'kind', 'unknown'), groups };
}

function scheduleName(text, graphStart, fallback) {
  const prefix = text.slice(0, graphStart);
  const matches = [...prefix.matchAll(/schedule\s+@(?:"([^"]+)"|([^\s]+))/g)];
  const match = matches.at(-1);
  return match?.[1] ?? match?.[2] ?? fallback;
}

export function parseSchedules(text) {
  const graphs = attributes(text, 'graph');
  if (!graphs.length) throw new Error('schedule output contains no compute graph');

  return graphs.map((graphAttribute, graphIndex) => {
    const fields = parseFields(graphAttribute.body);
    const nodeAttrs = attributes(fields.get('nodes') ?? '[]', [
      'fragment', 'nba_commit', 'event_commit',
    ]);
    const nodes = nodeAttrs.map(parseNode);
    const following = text.slice(
      graphAttribute.end + 1,
      graphs[graphIndex + 1]?.start ?? text.length,
    );
    const sourceLocations = parseSourceLocations(following);
    for (const node of nodes) node.location = sourceLocations.get(node.id) ?? null;
    const edges = attributes(fields.get('edges') ?? '[]', 'edge').map(parseEdge);
    const regions = attributes(fields.get('regions') ?? '[]', 'region').map(parseRegion);
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
    return {
      name: scheduleName(text, graphAttribute.start, `design-${graphIndex + 1}`),
      version: number(fields, 'version'),
      vpi: word(fields, 'vpi', 'off'),
      workers: number(fields, 'workers', 1),
      nodes,
      edges,
      regions,
    };
  });
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
