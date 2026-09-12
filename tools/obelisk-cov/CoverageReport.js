const d = JSON.parse(document.getElementById('coverage-data').textContent);
const files = new Map(d.files.map(f => [f.id, f.path]));
const scopes = new Map(d.scopes.map(s => [s.id, s.name]));
const runsById = new Map(d.runs.map(r => [r.uuid, r]));
const runNameCounts = new Map;
for (const r of d.runs)
  runNameCounts.set(r.name, (runNameCounts.get(r.name) || 0) + 1);
const runLabels = new Map(d.runs.map(
    r => [r.uuid, runNameCounts.get(r.name) > 1 ? r.name + ' (' + r.uuid + ')'
                                                : r.name]));
const filterRows = [];
const hierarchyTargets = new Map;
let nextHierarchyId = 0;
const counterText = c => String(c.count) + (c.overflow ? ' (saturated)' : '');
const addRow = (selector, values, tests = null, hierarchy = '') => {
  const tr = document.createElement('tr');
  for (const value of values) {
    const td = document.createElement('td');
    td.textContent = String(value);
    tr.append(td)
  }
  if (tests !== null) {
    tr.coverageTests = new Set(tests);
    filterRows.push(tr)
  }
  if (hierarchy && !hierarchyTargets.has(hierarchy)) {
    tr.id = 'hierarchy-' + nextHierarchyId++;
    hierarchyTargets.set(hierarchy, tr.id)
  }
  document.querySelector(selector).append(tr);
  return tr
};
const appendLink = (id, text, href) => {
  const a = document.createElement('a');
  a.textContent = text;
  a.href = href;
  document.getElementById(id).append(a)
};
for (const f of d.files)
  appendLink('file-nav', f.path, '#source-' + f.id);
const testFilter = document.getElementById('test-filter');
for (const r of d.runs) {
  const option = document.createElement('option');
  option.value = r.uuid;
  option.textContent = runLabels.get(r.uuid);
  testFilter.append(option)
}
const applyTestFilter = () => {
  const selected = testFilter.value;
  for (const row of filterRows)
    row.hidden = selected !== '' && !row.coverageTests.has(selected)
};
testFilter.addEventListener('change', applyTestFilter);
document.getElementById('identity').textContent =
    'schema ' + d.schema_fingerprint + ' · codec v' + d.codec_version;
for (const [n, m] of Object.entries(d.metrics)) {
  const tr = document.createElement('tr');
  for (let column = 0; column !== 5; ++column)
    tr.append(document.createElement('td'));
  tr.children[0].textContent = n;
  tr.children[1].textContent =
      m.available ? m.covered + '/' + m.total : 'unavailable';
  tr.children[2].textContent =
      m.available ? Number(m.percent).toFixed(2) + '%' : '—';
  tr.children[3].textContent = m.threshold === undefined
                                   ? '—'
                                   : (m.threshold_pass ? 'PASS ' : 'FAIL ') +
                                         Number(m.threshold).toFixed(2) + '%';
  if (m.threshold !== undefined)
    tr.children[3].className = m.threshold_pass ? 'pass' : 'fail';
  const progress = document.createElement('progress');
  progress.max = 100;
  progress.value = m.available ? m.percent : 0;
  tr.children[4].append(progress);
  document.querySelector('#metrics tbody').append(tr)
}
for (const r of d.runs)
  addRow('#runs tbody',
         [
           r.name, r.uuid, r.status, r.simulation_time, r.timestamp, r.seed,
           Object.entries(r.tags).map(([ k, v ]) => k + '=' + v).join(', ')
         ],
         [ r.uuid ]);
for (const p of d.line_points) {
  const status = p.excluded ? 'excluded: ' + p.exclusion_reason
                            : (p.covered ? 'covered' : 'uncovered');
  const names = p.contributors.map(c => runLabels.get(c.uuid) || c.name);
  const testIds = p.contributors.map(c => c.uuid);
  const hierarchy = scopes.get(p.scope) || String(p.scope);
  addRow('#line-points tbody',
         [
           hierarchy,
           (files.get(p.file) || p.file) + ':' + p.line + ':' + p.column,
           'point ' + p.id, p.count + (p.overflow ? ' (saturated)' : ''),
           status, names.join(', ')
         ],
         testIds, hierarchy)
}
for (const o of d.toggles)
  for (const b of o.bits) {
    const status =
        o.excluded
            ? 'excluded: ' + o.exclusion_reason
            : ((b.zero_to_one.covered && b.one_to_zero.covered) ? 'covered'
               : (b.zero_to_one.covered || b.one_to_zero.covered)
                   ? 'partial'
                   : 'uncovered');
    const contributorIds = [...new Set([
      ...b.zero_to_one.contributors, ...b.one_to_zero.contributors,
      ...b.to_unknown.contributors, ...b.from_unknown.contributors
    ].map(c => c.uuid)) ];
    const contributors =
        contributorIds.map(id => runLabels.get(id) || id).join(', ');
    addRow('#toggle-bits tbody',
           [
             scopes.get(o.scope) || o.scope, b.paths.join(' | '),
             counterText(b.zero_to_one), counterText(b.one_to_zero),
             'to X/Z ' + counterText(b.to_unknown) + ', from X/Z ' +
                 counterText(b.from_unknown),
             status, contributors
           ],
           contributorIds, scopes.get(o.scope) || String(o.scope))
  }
const uniqueIds = c => [...new Set(c.map(v => v.uuid))];
const testNames = c =>
    uniqueIds(c).map(id => runLabels.get(id) || id).join(', ');
const itemKinds = {
  1 : 'coverpoint',
  2 : 'cross'
};
const binKinds = {
  1 : 'state',
  2 : 'transition',
  3 : 'cross'
};
const binRoles = f => [[ 1, 'default' ], [ 2, 'default sequence' ],
                       [ 4, 'ignore' ], [ 8, 'illegal' ], [ 16, 'wildcard' ],
                       [ 64, 'automatic' ], [ 128, 'empty' ]]
                          .filter(([ m ]) => f & m)
                          .map(([, n ]) => n);
const valueSets = new Map(d.resolved_functional_value_sets.map(
    v => [v.type + '|' + v.configuration + '|' + v.id, v]));
const hex = v => {
  const s = BigInt(v).toString(16).replace(/^0+(?=.)/, '');
  return s || '0'
};
const words = v =>
    v.slice()
        .reverse()
        .map((n, i) => i ? BigInt(n).toString(16).padStart(16, '0') : hex(n))
        .join('')
        .replace(/^0+(?=.)/, '') ||
    '0';
const endpoint = (s, a, high) => {
  const av = words(a.limbs.map(l => high ? l.high_aval : l.low_aval));
  const bv = words(a.limbs.map(l => high ? l.high_bval : l.low_bval));
  const wm = words(a.limbs.map(l => l.wildcard_mask));
  return bv !== '0' || wm !== '0'
             ? 'aval=0x' + av + ',bval=0x' + bv + ',wildcard=0x' + wm
             : '0x' + av
};
const valueText = (type, configuration, id) => {
  const s = valueSets.get(type + '|' + configuration + '|' + id);
  if (!s)
    return 'set ' + id;
  return s.atoms
      .map(a => {
        if (Number(a.kind) === 3)
          return 'real-bits[0x' + hex(a.real_low_bits) + ':0x' +
                 hex(a.real_high_bits) + ']';
        const low = endpoint(s, a, false);
        return Number(a.kind) === 2
                   ? '[' + low + ':' + endpoint(s, a, true) + ']'
                   : low
      })
      .join(', ')
};
const alternativesByBin = new Map;
for (const a of d.resolved_transition_alternatives) {
  const k = a.type + '|' + a.configuration + '|' + a.bin;
  if (!alternativesByBin.has(k))
    alternativesByBin.set(k, []);
  alternativesByBin.get(k).push(a)
}
const stepsByAlternative = new Map;
for (const s of d.resolved_transition_steps) {
  const k = s.type + '|' + s.configuration + '|' + s.alternative;
  if (!stepsByAlternative.has(k))
    stepsByAlternative.set(k, []);
  stepsByAlternative.get(k).push(s)
}
const templateSteps = new Map(d.transition_steps.map(
    s => [s.bin + '|' + s.alternative_ordinal + '|' + s.ordinal, s]));
const repeatText = (kind, lo, hi) => {
  if (Number(kind) === 1)
    return '';
  const op = Number(kind) === 2 ? '*' : Number(kind) === 3 ? '->' : '=';
  const upper = hi === '18446744073709551615' ? '$' : hi;
  return '[' + op + lo + (upper === lo ? '' : ':' + upper) + ']'
};
const transitionText = (g, i, b) => {
  const as =
      alternativesByBin.get(g.type + '|' + g.configuration + '|' + b.id) || [];
  return as.map(a => (stepsByAlternative.get(g.type + '|' + g.configuration +
                                             '|' + a.id) ||
                      [])
                         .sort((x, y) => Number(x.ordinal) - Number(y.ordinal))
                         .map(s => {
                           const t = templateSteps.get(
                               b.template_bin + '|' +
                               a.template_alternative_ordinal + '|' +
                               s.ordinal);
                           return valueText(g.type, g.configuration,
                                            s.value_set) +
                                  repeatText(t ? t.repetition : 1,
                                             s.lower_bound, s.upper_bound)
                         })
                         .join(' => '))
               .join(' or ')
};
const templateItems =
    new Map(d.functional_item_templates.map(i => [i.id, i.name]));
const templateBins =
    new Map(d.functional_bin_templates.map(b => [b.id, b.name]));
const selectorNodes = new Map(d.cross_selector_nodes.map(n => [n.id, n]));
const selectorOperands = new Map;
for (const o of d.cross_selector_operands) {
  if (!selectorOperands.has(o.node))
    selectorOperands.set(o.node, []);
  selectorOperands.get(o.node).push(o.operand)
}
const selectorBindings = new Map(d.resolved_cross_selector_bindings.map(
    b => [b.type + '|' + b.configuration + '|' + b.cross + '|' + b.node, b]));
const tuplesBySet = new Map;
for (const t of d.resolved_functional_tuples) {
  if (!tuplesBySet.has(t.tuple_set))
    tuplesBySet.set(t.tuple_set, []);
  tuplesBySet.get(t.tuple_set).push(t)
}
const componentsByTuple = new Map;
for (const c of d.resolved_functional_tuple_components) {
  if (!componentsByTuple.has(c.tuple))
    componentsByTuple.set(c.tuple, []);
  componentsByTuple.get(c.tuple).push(c)
}
const resolvedBins = new Map(d.resolved_functional_bins.map(
    b => [b.type + '|' + b.configuration + '|' + b.id, b]));
const automaticNodes = new Map(d.resolved_cross_automatic_nodes.map(
    n => [n.type + '|' + n.configuration + '|' + n.cross + '|' + n.id, n]));
const automaticEdges = new Map;
for (const e of d.resolved_cross_automatic_edges) {
  const key = e.type + '|' + e.configuration + '|' + e.cross + '|' + e.node;
  if (!automaticEdges.has(key))
    automaticEdges.set(key, []);
  automaticEdges.get(key).push(e)
}
const automaticContains = (g, i, components) => {
  let node = i.automatic_root_node;
  for (let dimension = 0; dimension !== components.length; ++dimension) {
    const key = g.type + '|' + g.configuration + '|' + i.id + '|' + node;
    if (!automaticNodes.has(key))
      return false;
    const edge =
        (automaticEdges.get(key) || [])
            .find(e => (resolvedBins
                            .get(g.type + '|' + g.configuration + '|' + e.bin)
                            ?.name ||
                        '') === components[dimension]);
    if (!edge)
      return false;
    if (dimension + 1 === components.length)
      return edge.child === '0';
    if (edge.child === '0')
      return false;
    node = edge.child
  }
  return false
};
const tupleSetText = (g, i, id) =>
    (tuplesBySet.get(id) || [])
        .map(t => '<' +
                  (componentsByTuple.get(t.id) || [])
                      .sort((a, b) => Number(a.ordinal) - Number(b.ordinal))
                      .map(c => c.bin !== '0'
                                    ? (resolvedBins
                                           .get(g.type + '|' + g.configuration +
                                                '|' + c.bin)
                                           ?.name ||
                                       c.bin)
                                    : valueText(g.type, g.configuration,
                                                c.value_set))
                      .join(',') +
                  '>')
        .join(', ');
const selectorText = (g, i, root) => {
  const render = (id, active) => {
    if (active.has(id))
      return '<cycle>';
    const n = selectorNodes.get(id);
    if (!n)
      return 'selector ' + id;
    const next = new Set(active);
    next.add(id);
    const ops = (selectorOperands.get(id) || []).map(v => render(v, next));
    const kind = Number(n.kind);
    if (kind === 1)
      return 'binsof(' + (templateItems.get(n.target) || n.target) +
             (n.bin !== '0' ? '.' + (templateBins.get(n.bin) || n.bin) : '') +
             ')';
    if (kind === 2)
      return '!(' + ops[0] + ')';
    if (kind === 3 || kind === 4)
      return '(' + ops.join(kind === 3 ? ' && ' : ' || ') + ')';
    if (kind === 5) {
      const b = selectorBindings.get(g.type + '|' + g.configuration + '|' +
                                     i.id + '|' + id);
      return 'intersect {' +
             (b && b.tuple_set !== '0' ? tupleSetText(g, i, b.tuple_set)
              : b ? valueText(g.type, g.configuration, b.value_set)
                  : 'set ' + n.value_set) +
             '}'
    }
    if (kind === 6)
      return 'all tuples';
    if (kind === 7) {
      const b = selectorBindings.get(g.type + '|' + g.configuration + '|' +
                                     i.id + '|' + id);
      return (ops[0] || 'all tuples') + ' with expression ' +
             (b ? b.with_expression : n.with_expression)
    }
    return 'selector ' + id
  };
  return render(root, new Set)
};
const functionalTypes = new Map(d.functional_types.map(t => [t.id, t]));
for (const t of d.functional_types) {
  const testIds = uniqueIds(
      d.functional_instance_groups.filter(g => g.type === t.id)
          .flatMap(g => g.items)
          .flatMap(i => [...i.bins.flatMap(b => b.contributors),
                         ...i.automatic_bins.flatMap(b => b.contributors)]));
  addRow('#functional-types tbody',
         [ t.hierarchy, Number(t.percent).toFixed(2) + '%', t.comment ],
         testIds, t.hierarchy)
}
for (const g of d.functional_instance_groups) {
  const instance = g.name || '<unnamed>';
  const groupContributors =
      g.items.flatMap(i => [...i.bins.flatMap(b => b.contributors),
                            ...i.automatic_bins.flatMap(b => b.contributors)]);
  const groupTests = uniqueIds(groupContributors);
  const typeHierarchy = functionalTypes.get(g.type)?.hierarchy || '';
  const groupHierarchy =
      instance === '<unnamed>' ? typeHierarchy : typeHierarchy + '.' + instance;
  const items =
      g.items
          .map(i => i.name + (i.comment ? ': ' + i.comment : '') +
                    (i.type_comment ? ' [type: ' + i.type_comment + ']' : ''))
          .join(', ');
  addRow('#functional tbody',
         [ instance, Number(g.percent).toFixed(2) + '%', g.comment, items ],
         groupTests, groupHierarchy);
  for (const i of g.items) {
    const contributorRows = [
      ...i.bins.flatMap(b => b.contributors),
      ...i.automatic_bins.flatMap(b => b.contributors)
    ];
    const contributorIds = uniqueIds(contributorRows);
    const contributors =
        contributorIds.map(id => runLabels.get(id) || id).join(', ');
    const itemStatus = !i.aggregating ? 'non-aggregating inherited item'
                       : String(i.total) === '0'  ? 'no contributing bins'
                       : Number(i.percent) >= 100 ? 'covered'
                                                  : 'incomplete';
    addRow('#functional-items tbody',
           [
             instance, itemKinds[i.kind] || i.kind,
             i.hierarchy +
                 (i.targets.length
                      ? ' targets ' + i.targets.map(t => t.name).join(', ')
                      : ''),
             i.covered + '/' + i.total + ' (' + Number(i.percent).toFixed(2) +
                 '%)',
             i.goal + ' / ' + i.weight, itemStatus, contributors
           ],
           contributorIds, i.hierarchy);
    for (const b of i.bins) {
      const roles = binRoles(b.flags);
      const states = [];
      if (b.flags & 8)
        states.push('illegal');
      if (b.flags & 4)
        states.push('ignored');
      if (b.flags & 128)
        states.push('empty');
      if (b.exclusion_reason)
        states.push('excluded: ' + b.exclusion_reason);
      if (!states.length)
        states.push(b.contributing ? (b.covered ? 'covered' : 'uncovered')
                                   : 'non-contributing');
      const status = states.join('; ');
      const detail = Number(b.kind) === 2
                         ? b.name + ' = ' + transitionText(g, i, b)
                     : Number(b.kind) === 3 && b.cross_selector !== '0'
                         ? b.name + ' = ' + selectorText(g, i, b.cross_selector)
                         : b.name;
      addRow('#functional-bins tbody',
             [
               instance, i.name,
               (binKinds[b.kind] || b.kind) +
                   (roles.length ? ' · ' + roles.join(', ') : ''),
               detail, counterText(b), b.at_least, status,
               testNames(b.contributors)
             ],
             uniqueIds(b.contributors), b.hierarchy)
    }
    for (const b of i.automatic_bins) {
      const status = b.missing   ? 'missing'
                     : b.covered ? 'covered'
                                 : 'uncovered';
      addRow('#functional-bins tbody',
             [
               instance, i.name, 'automatic cross tuple', b.name,
               counterText(b), b.at_least, status, testNames(b.contributors)
             ],
             uniqueIds(b.contributors), i.hierarchy)
    }
  }
}
for (const v of d.illegal_bin_diagnostics)
  addRow('#illegal-bins tbody',
         [
           runLabels.get(v.run) || v.test, v.hierarchy || v.bin, v.instance,
           v.simulation_time, v.count + (v.overflow ? ' (saturated)' : ''),
           v.message
         ],
         [ v.run ], v.hierarchy);
const ranking = new Map(d.runs.map(r => [r.uuid, {
                                     uuid : r.uuid,
                                     name : runLabels.get(r.uuid),
                                     line : 0,
                                     toggle : 0,
                                     functional : 0
                                   }]));
const awardCritical = (contributors, atLeast, metric) => {
  const counts = new Map;
  for (const c of contributors)
    counts.set(c.uuid, (counts.get(c.uuid) || 0n) + BigInt(c.count));
  let total = 0n;
  for (const count of counts.values())
    total += count;
  const threshold = BigInt(atLeast);
  if (total < threshold)
    return;
  for (const [uuid, count] of counts)
    if (total - count < threshold && ranking.has(uuid))
      ranking.get(uuid)[metric]++
};
for (const p of d.line_points)
  if (p.covered && !p.excluded)
    awardCritical(p.contributors, 1, 'line');
for (const o of d.toggles)
  if (!o.excluded)
    for (const b of o.bits) {
      if (b.zero_to_one.covered)
        awardCritical(b.zero_to_one.contributors, 1, 'toggle');
      if (b.one_to_zero.covered)
        awardCritical(b.one_to_zero.contributors, 1, 'toggle')
    }
const mergedFunctional = new Map;
const automaticProfiles = new Map;
const addMergedFunctional = (key, b, atLeast = null) => {
  let value = mergedFunctional.get(key);
  if (!value) {
    value = {atLeast : 0n, contributors : []};
    mergedFunctional.set(key, value)
  }
  const threshold = atLeast === null ? BigInt(b.at_least) : atLeast;
  if (threshold > value.atLeast)
    value.atLeast = threshold;
  value.contributors.push(...b.contributors)
};
for (const t of d.functional_types) {
  const groups = d.functional_instance_groups.filter(g => g.type === t.id);
  if (!t.merge_instances) {
    const cumulativeAtLeast = new Map;
    for (const g of groups)
      for (const i of g.items) {
        const threshold = BigInt(i.at_least);
        const previous = cumulativeAtLeast.get(i.template_item) || 0n;
        if (threshold > previous)
          cumulativeAtLeast.set(i.template_item, threshold)
      }
    for (const g of groups)
      for (const i of g.items) {
        for (const b of i.bins)
          if (b.contributing)
            awardCritical(b.contributors,
                          cumulativeAtLeast.get(i.template_item) || 0n,
                          'functional');
        for (const b of i.automatic_bins)
          awardCritical(b.contributors,
                        cumulativeAtLeast.get(i.template_item) || 0n,
                        'functional')
      }
  } else {
    mergedFunctional.clear();
    automaticProfiles.clear();
    const cumulativeAtLeast = new Map;
    for (const g of groups)
      for (const i of g.items) {
        const threshold = BigInt(i.at_least);
        const previous = cumulativeAtLeast.get(i.template_item) || 0n;
        if (threshold > previous)
          cumulativeAtLeast.set(i.template_item, threshold)
      }
    for (const g of groups)
      for (const i of g.items)
        if (i.aggregating) {
          const prefix = i.template_item + '|' + i.name + '|';
          const threshold = cumulativeAtLeast.get(i.template_item) || 0n;
          for (const b of i.bins)
            if (b.contributing)
              addMergedFunctional(prefix + 'bin|' + b.name, b, threshold);
          if (BigInt(i.automatic_total) !== 0n) {
            if (!automaticProfiles.has(prefix))
              automaticProfiles.set(prefix, []);
            automaticProfiles.get(prefix).push({group : g, item : i})
          }
          for (const b of i.automatic_bins) {
            const key = prefix + 'auto|' + b.name;
            addMergedFunctional(key, b, threshold);
            const value = mergedFunctional.get(key);
            value.profile = prefix;
            value.components = b.components.map(c => c.name)
          }
        }
    for (const value of mergedFunctional.values()) {
      if (value.profile) {
        const contained = (automaticProfiles.get(value.profile) || []).some(
            profile => automaticContains(profile.group, profile.item,
                                          value.components));
        if (!contained)
          continue
      }
      awardCritical(value.contributors, value.atLeast, 'functional')
    }
  }
}
const ranked = [...ranking.values() ].sort(
    (a, b) => (b.line + b.toggle + b.functional) -
                  (a.line + a.toggle + a.functional) ||
              a.name.localeCompare(b.name) || a.uuid.localeCompare(b.uuid));
ranked.forEach((r, index) =>
                   addRow('#unique-contributions tbody',
                          [
                            index + 1, r.name, r.line, r.toggle, r.functional,
                            (r.line + r.toggle + r.functional) + ' unique'
                          ],
                          [ r.uuid ]));
for (const [hierarchy, id] of hierarchyTargets)
  appendLink('hierarchy-nav', hierarchy, '#' + id);
applyTestFilter();
for (const v of d.exclusions)
  addRow('#exclusions tbody', [ v.metric, v.entity, v.reason ]);
