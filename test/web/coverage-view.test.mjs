// REQUIRES: node
// RUN: %node --experimental-default-type=module %s

import assert from 'node:assert/strict';

import { CoverageView, coverageSummary } from '../../web/coverage-view.js';

const page = (metrics, attributes = 'id="coverage-data" type="application/json"') =>
  `<!doctype html><script ${attributes}>${JSON.stringify({ metrics })}</script><script>app</script>`;
const metric = (percent, available = true) => ({ available, percent });

// The status line names each collected metric, wherever the page's data
// script puts its attributes, and degrades rather than failing.
assert.equal(coverageSummary(page({ line: metric(95.24), toggle: metric(71.64), functional: metric(0, false) })),
  'line 95.2% · toggle 71.6%');
assert.equal(coverageSummary(page({ line: metric(50) }, 'type="application/json" id="coverage-data"')),
  'line 50.0%');
assert.equal(coverageSummary(page({ line: metric(0, false) })), 'no coverage collected');
assert.equal(coverageSummary('<p>not a report</p>'), 'coverage report');
assert.equal(coverageSummary(page({}).replace('{"metrics":{}}', '{')), 'coverage report');

/* --------------------------------------------------------------- the view */

const element = () => ({ hidden: true, src: '', textContent: '' });
const created = [];
const revoked = [];
const urls = {
  createObjectURL: (blob) => {
    created.push(blob);
    return `blob:${created.length}`;
  },
  revokeObjectURL: (href) => revoked.push(href),
};
const frame = element();
const empty = element();
const download = element();
const view = new CoverageView({ frame, empty, download }, urls);

// Without a report it says why, depending on whether coverage is on.
assert.deepEqual(view.show(false), { text: 'no coverage', kind: '' });
assert.equal(empty.textContent, 'Coverage is off. Turn it on under Options, then run the design.');
assert.deepEqual([frame.hidden, empty.hidden, download.hidden], [true, false, true]);
view.show(true);
assert.equal(empty.textContent, 'No coverage yet. Run the design to collect it.');
view.save({ createElement: () => assert.fail('nothing to save') });

// A report is shown from a blob URL of exactly its HTML.
const first = page({ line: metric(80) });
view.update(first);
assert.deepEqual(view.show(true), { text: 'line 80.0%', kind: 'ok' });
assert.deepEqual([frame.src, frame.hidden, empty.hidden, download.hidden], ['blob:1', false, true, false]);
assert.equal(created[0].type, 'text/html');
assert.equal(await created[0].text(), first);

// Showing it again keeps the frame where it is; a new run's report replaces
// it and releases the old URL.
view.show(true);
assert.equal(created.length, 1);
view.update(page({ line: metric(90) }));
assert.equal(view.show(false).text, 'line 90.0%');
assert.deepEqual([frame.src, revoked], ['blob:2', ['blob:1']]);

// Saving hands a temporary link the report as coverage.html, then releases it.
const anchors = [];
view.save({
  createElement(tag) {
    const anchor = { tag, clicked: false, click() { this.clicked = true; } };
    anchors.push(anchor);
    return anchor;
  },
});
assert.deepEqual(anchors.map(({ tag, href, download: name, clicked }) => ({ tag, href, name, clicked })),
  [{ tag: 'a', href: 'blob:3', name: 'coverage.html', clicked: true }]);
await new Promise((resolve) => setTimeout(resolve));
assert.deepEqual(revoked, ['blob:1', 'blob:3']);

// A run with coverage that produced no report drops the previous one and
// says why; the next report is shown again.
view.fail('the simulation aborted');
assert.deepEqual(view.show(true), { text: 'no coverage report', kind: 'err' });
assert.equal(empty.textContent, 'The latest run produced no coverage report: the simulation aborted.');
assert.deepEqual([frame.hidden, empty.hidden, download.hidden], [true, false, true]);
assert.deepEqual(revoked, ['blob:1', 'blob:3', 'blob:2']);
view.save({ createElement: () => assert.fail('nothing to save') });
view.update(page({ line: metric(70) }));
assert.deepEqual(view.show(true), { text: 'line 70.0%', kind: 'ok' });
assert.equal(frame.src, 'blob:4');

console.log('web coverage view OK');
