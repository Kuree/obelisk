// REQUIRES: node
// RUN: %node --experimental-default-type=module %s

import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import test from 'node:test';
import vm from 'node:vm';

import { CoverageView } from '../../web/coverage-view.js';
import { MLIR_LANGUAGE_ID } from '../../web/mlir-language.js';
import { DEFAULTS, buildArgs, formatCommand } from '../../web/options.js';
import { DEFAULT_STAGE, STAGES, findStage } from '../../web/stages.js';

const source = await readFile(
  process.env.TALOS_TEST_APP ?? new URL('../../web/app.js', import.meta.url), 'utf8',
);

class Element {
  hidden = true;
  disabled = false;
  className = '';
  children = [];
  attributes = new Map();
  dataset = {};
  src = '';
  listeners = new Map();
  #text = '';

  set textContent(text) { this.#text = text; this.children = []; }
  get textContent() { return this.#text + this.children.map(c => c.textContent).join(''); }
  appendChild(child) { this.children.push(child); return child; }
  setAttribute(name, value) { this.attributes.set(name, value); }
  addEventListener(type, listener) {
    const listeners = this.listeners.get(type) ?? [];
    listeners.push(listener);
    this.listeners.set(type, listeners);
  }
  click() { for (const listener of this.listeners.get('click') ?? []) listener(); }
  scrollIntoView() {}
  querySelectorAll(selector) {
    return this.children.flatMap(child => [
      ...(selector === '.stage' && child.className === 'stage' ? [child] : []),
      ...child.querySelectorAll(selector),
    ]);
  }
}

function app({ savedWaveform = null } = {}) {
  const elements = new Map();
  const element = id => {
    if (!elements.has(id)) elements.set(id, new Element());
    return elements.get(id);
  };
  const document = {
    getElementById: element,
    querySelector: element,
    createElement: () => new Element(),
    createTextNode: text => Object.assign(new Element(), { textContent: text }),
  };
  const loads = [];
  element('surfer').contentWindow = {
    postMessage(message) { loads.push(new TextDecoder().decode(message.buffer)); },
  };
  const blobs = new Map();
  let nextURL = 0;
  const urls = {
    createObjectURL(blob) {
      const href = `blob:report-${++nextURL}`;
      blobs.set(href, blob);
      return href;
    },
    revokeObjectURL(href) { blobs.delete(href); },
  };
  const storage = { waveform: savedWaveform };
  const editor = { source: 'module first; endmodule', getValue() { return this.source; } };
  let session;
  class Session {
    constructor() { session = this; }
    run(request, receive) {
      this.request = request;
      this.receive = receive;
      return { stop: () => receive({ type: 'stopped' }) };
    }
    emit(message) { return this.receive(message); }
  }
  const context = vm.createContext({
    document, Blob, URL, TextDecoder,
    DEFAULTS, buildArgs, formatCommand, DEFAULT_STAGE, STAGES, findStage, MLIR_LANGUAGE_ID,
    CompilerSession: Session,
    CoverageView: class extends CoverageView {
      constructor(elements) { super(elements, urls); }
    },
    loadWaveform: async () => storage.waveform,
    saveWaveform: async waveform => { storage.waveform = waveform; },
    clearWaveform: async () => { storage.waveform = null; },
    testEditor: editor,
  });
  // Run the real application handlers without its network/bootstrap step.
  // Browser objects and the compiler session are the test boundaries.
  const body = source.replace(/^import[\s\S]*?;\n/gm, '')
    .replace(/\nmain\(\);\s*$/, '');
  vm.runInContext(body + `
    editor = testEditor;
    surferReady = Promise.resolve();
    buildStageTabs();
    globalThis.playground = {
      run,
      setCoverage(value) { options.coverage = value; },
    };
  `, context, { filename: 'web/app.js' });
  const settle = async () => { await new Promise(resolve => setImmediate(resolve)); };
  const open = async stage => {
    const button = element('stages').querySelectorAll('.stage')
      .find(button => button.dataset.stage === stage);
    assert.ok(button, `missing ${stage} tab`);
    button.click();
    await settle();
  };
  const start = async (name, coverage = 'off') => {
    editor.source = `module ${name}; endmodule`;
    context.playground.setCoverage(coverage);
    context.playground.run();
    assert.equal(session.request.source, editor.source);
    await session.emit({ type: 'compiled', ok: true, kind: 'binary',
                         stage: 'run', byteLength: 32, elapsedMs: 10 });
  };
  const exit = async (files = [], coveragePending = false) => {
    await session.emit({ type: 'exited', code: 0, files, runMs: 1, coveragePending });
    await settle();
  };
  return {
    element, loads, storage, open, start, exit, settle,
    report: async () => blobs.get(element('coverageFrame').src)?.text(),
    emit: async message => { await session.emit(message); await settle(); },
  };
}

function waveform(scope) {
  return {
    name: 'trace.vcd',
    data: new TextEncoder().encode(
      `$date today $end\n$version Talos $end\n$scope module ${scope} $end\n`,
    ),
  };
}

const report = (label, percent) => '<!doctype html><script id="coverage-data">' +
  JSON.stringify({ label, metrics: { line: { available: true, percent } } }) + '</script>';

test('Waveform refreshes after another design finishes while its tab is open', async () => {
  const ui = app();
  await ui.start('first');
  await ui.exit([waveform('first')]);
  await ui.open('waveform');
  assert.match(ui.loads.at(-1), /module first/);

  await ui.start('second');
  await ui.open('waveform');
  await ui.exit([waveform('second')]);
  assert.match(ui.loads.at(-1), /module second/);
  assert.equal(ui.element('surfer').hidden, false);
  assert.equal(ui.element('downloadWaveform').hidden, false);
  assert.match(new TextDecoder().decode(ui.storage.waveform.data), /module second/);
});

test('A run without VCD clears the viewer, download, and persisted waveform', async () => {
  const ui = app();
  await ui.start('first');
  await ui.exit([waveform('first')]);
  await ui.open('waveform');
  await ui.start('without_dumpvars');
  await ui.open('waveform');
  await ui.exit();
  assert.equal(ui.element('surfer').hidden, true);
  assert.equal(ui.element('downloadWaveform').hidden, true);
  assert.equal(ui.element('waveformEmpty').hidden, false);
  assert.equal(ui.storage.waveform, null);

  const reloaded = app({ savedWaveform: ui.storage.waveform });
  await reloaded.open('waveform');
  assert.equal(reloaded.element('surfer').hidden, true);
  assert.equal(reloaded.loads.length, 0);
});

test('A saved waveform still loads before a new run', async () => {
  const ui = app({ savedWaveform: waveform('saved') });
  await ui.open('waveform');
  assert.match(ui.loads.at(-1), /module saved/);
});

test('Coverage refreshes after another design finishes while its tab is open', async () => {
  const ui = app();
  await ui.start('first', 'on');
  await ui.exit([], true);
  await ui.emit({ type: 'coverage', coverage: report('first', 70) });
  await ui.open('coverage');
  assert.match(await ui.report(), /"label":"first"/);

  await ui.start('second', 'on');
  await ui.open('coverage');
  assert.equal(ui.element('coverageFrame').hidden, true);
  assert.equal(ui.element('downloadCoverage').hidden, true);
  await ui.exit([], true);
  assert.equal(ui.element('coverageFrame').hidden, true);
  await ui.emit({ type: 'coverage', coverage: report('second', 90) });
  assert.match(await ui.report(), /"label":"second"/);
  assert.equal(ui.element('coverageFrame').hidden, false);
  assert.equal(ui.element('downloadCoverage').hidden, false);
  assert.equal(ui.element('status').textContent, 'line 90.0%');
});

test('Running with coverage off clears the previous report and download', async () => {
  const ui = app();
  await ui.start('first', 'on');
  await ui.exit([], true);
  await ui.emit({ type: 'coverage', coverage: report('first', 70) });
  await ui.open('coverage');

  await ui.start('second', 'off');
  await ui.open('coverage');
  await ui.exit();
  assert.equal(ui.element('coverageFrame').hidden, true);
  assert.equal(ui.element('downloadCoverage').hidden, true);
  assert.match(ui.element('coverageEmpty').textContent, /Coverage is off/);
  assert.equal(await ui.report(), undefined);
});

for (const terminal of ['failed', 'stopped']) {
  test(`Coverage ${terminal} during report generation clears the previous report`, async () => {
    const ui = app();
    await ui.start('first', 'on');
    await ui.exit([], true);
    await ui.emit({ type: 'coverage', coverage: report('first', 70) });
    await ui.open('coverage');

    await ui.start('second', 'on');
    await ui.open('coverage');
    await ui.exit([], true);
    await ui.emit({ type: terminal, message: 'report failed' });
    assert.equal(ui.element('coverageFrame').hidden, true);
    assert.equal(ui.element('downloadCoverage').hidden, true);
    assert.match(ui.element('coverageEmpty').textContent, /no coverage report/);
    assert.equal(await ui.report(), undefined);
  });
}
