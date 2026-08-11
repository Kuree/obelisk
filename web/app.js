import { registerSystemVerilog, SV_LANGUAGE_ID } from './sv-language.js';
import { registerMlir, MLIR_LANGUAGE_ID } from './mlir-language.js';
import { registerLlvm } from './llvm-language.js';
import { parseSchedules, renderSchedules } from './schedule-view.js';
import { runSimulation } from './wasi.js';
import { loadWaveform, saveWaveform } from './waveform-storage.js';
import { EXAMPLES } from './examples.js';
import { STAGES, DEFAULT_STAGE, findStage } from './stages.js';
import {
  DEFAULTS, buildArgs, formatCommand, toPermalink, loadState, saveState,
} from './options.js';

const MONACO_CDN = 'https://cdn.jsdelivr.net/npm/monaco-editor@0.52.2/min';

const el = (id) => document.getElementById(id);
const ui = {
  editor: el('editor'), output: el('output'), status: el('status'),
  run: el('run'), runLabel: el('runLabel'), stages: el('stages'),
  stageBlurb: el('stageBlurb'), command: el('command'),
  flagsToggle: el('flagsToggle'), flagsPanel: el('flagsPanel'),
  examples: el('examples'), handle: el('handle'), workbench: document.querySelector('.workbench'),
  share: el('share'), copyCommand: el('copyCommand'), copyOutput: el('copyOutput'),
  irEditor: el('irEditor'), scheduleView: el('scheduleView'),
  scheduleToggle: el('scheduleToggle'),
  waveformView: el('waveformView'), waveformEmpty: el('waveformEmpty'),
  surfer: el('surfer'), downloadWaveform: el('downloadWaveform'),
};

const OPTION_FIELDS = [
  'std', 'opt', 'tier', 'scheduler', 'specialization',
  'top', 'timescale', 'defines', 'extra',
];

let monaco = null;
let editor = null;
let irEditor = null;
let irModel = null;
let irLanguage = MLIR_LANGUAGE_ID;
let scheduleSourceHighlight = null;
let worker = null;
let workerReady = false;
let pendingCompilation = null;
let options = { ...DEFAULTS };
let activeStage = DEFAULT_STAGE;
let busy = false;
let latestWaveform = null;
let surferReady = null;
let scheduleText = '';
let scheduleRaw = false;
const storedWaveform = loadWaveform()
  .then((waveform) => { latestWaveform ??= waveform; })
  .catch(() => {});

// Results are cached per stage and dropped whenever the source or options
// change, so switching tabs is instant but never shows something stale.
const results = new Map();

/* -------------------------------------------------------------- rendering */

function setStatus(text, kind = '') {
  ui.status.textContent = text;
  ui.status.className = `status ${kind}`;
}

function clearConsole() { ui.output.textContent = ''; }

function write(text, className) {
  const node = document.createElement('span');
  if (className) node.className = className;
  node.textContent = text;
  ui.output.appendChild(node);
  ui.output.scrollTop = ui.output.scrollHeight;
}

function renderCommand() {
  ui.command.textContent = formatCommand({ ...options, stage: activeStage });
}

/* ------------------------------------------------------------ diagnostics */

// slang reports as `design.sv:LINE:COL: severity: message`. Surfacing these in
// the gutter is the difference between reading a log and fixing the line.
const DIAGNOSTIC = /^(?:[^\s:]*):(\d+):(\d+):\s*(error|warning|note):\s*(.*)$/;

function applyDiagnostics(text) {
  if (!monaco || !editor) return { errors: 0, warnings: 0 };
  const markers = [];
  let errors = 0;
  let warnings = 0;

  for (const line of text.split('\n')) {
    const match = DIAGNOSTIC.exec(line.trim());
    if (!match) continue;
    const [, lineNumber, column, severity, message] = match;
    if (severity === 'error') errors++;
    else if (severity === 'warning') warnings++;
    markers.push({
      startLineNumber: Number(lineNumber),
      startColumn: Number(column),
      endLineNumber: Number(lineNumber),
      endColumn: Number(column) + 1,
      message,
      severity: severity === 'error' ? monaco.MarkerSeverity.Error
        : severity === 'warning' ? monaco.MarkerSeverity.Warning
          : monaco.MarkerSeverity.Info,
    });
  }
  monaco.editor.setModelMarkers(editor.getModel(), 'obelisk', markers);
  return { errors, warnings };
}

/* -------------------------------------------------------------- pipeline  */

function buildStageTabs() {
  for (const [index, stage] of STAGES.entries()) {
    const item = document.createElement('li');
    const button = document.createElement('button');
    button.className = 'stage';
    button.type = 'button';
    button.role = 'tab';
    button.dataset.stage = stage.id;
    button.title = stage.blurb;
    button.innerHTML = `<span class="ord">${String(index + 1).padStart(2, '0')}</span>`;
    button.appendChild(document.createTextNode(stage.label));
    button.addEventListener('click', () => selectStage(stage.id));
    item.appendChild(button);
    ui.stages.appendChild(item);
  }
}

function paintStageSelection() {
  for (const button of ui.stages.querySelectorAll('.stage')) {
    button.setAttribute('aria-selected', String(button.dataset.stage === activeStage));
  }
  ui.stageBlurb.textContent = findStage(activeStage).blurb;
  renderCommand();
}

function selectStage(id) {
  activeStage = id;
  paintStageSelection();

  const stage = findStage(id);
  if (stage.kind === 'waveform') {
    showWaveform();
    return;
  }

  showConsole();

  const cached = results.get(id);
  if (cached) {
    restore(cached);
    return;
  }
  // Landing on Run with nothing cached should not silently compile; the Run
  // button is the explicit action. Other stages compile on demand.
  if (stage.kind === 'text') compileStage(id);
  else { clearConsole(); setStatus('ready'); }
}

function restore(cached) {
  const text = cached.chunks.map((chunk) => chunk.text).join('');
  if (cached.stage === 'schedule') {
    showSchedule(text);
    setStatus(cached.status.text, cached.status.kind);
    return;
  }
  if (cached.language) {
    showIr(text, cached.language);
    setStatus(cached.status.text, cached.status.kind);
    return;
  }
  showConsole();
  clearConsole();
  for (const chunk of cached.chunks) write(chunk.text, chunk.className);
  setStatus(cached.status.text, cached.status.kind);
}

/* --------------------------------------------------------------- compiling */

let recording = null;

function record(text, className) {
  write(text, className);
  recording?.chunks.push({ text, className });
}

function finishRecording(statusText, statusKind) {
  // Replace the busy status immediately. Besides exposing the final timings,
  // changing the class removes the flashing .status.busy indicator.
  setStatus(statusText, statusKind);
  if (!recording) return;
  recording.status = { text: statusText, kind: statusKind };
  results.set(recording.stage, recording);
  recording = null;
}

function compileStage(stageId) {
  if (busy) return;
  const stage = findStage(stageId);
  showConsole();
  busy = true;
  setBusyLabel(true);
  clearConsole();
  setStatus(stage.kind === 'binary' ? 'compiling' : 'generating', 'busy');
  recording = { stage: stageId, chunks: [], status: {} };
  pendingCompilation = {
    type: 'compile',
    source: editor.getValue(),
    args: buildArgs({ ...options, stage: stageId }),
    stage: stageId,
    kind: stage.kind,
  };
  if (!worker) initWorker();
  else if (workerReady) dispatchCompilation();
}

function invalidate() {
  results.clear();
  scheduleSourceHighlight?.clear();
  if (monaco && editor) monaco.editor.setModelMarkers(editor.getModel(), 'obelisk', []);
}

function run() {
  if (busy || ui.run.disabled) return;
  activeStage = 'run';
  paintStageSelection();
  compileStage('run');
}

function setBusyLabel(isBusy) {
  ui.run.disabled = isBusy;
  ui.runLabel.textContent = isBusy ? 'Working' : 'Run';
}

/* ----------------------------------------------------------------- worker */

function disposeWorker() {
  worker?.terminate();
  worker = null;
  workerReady = false;
}

function dispatchCompilation() {
  if (!workerReady || !pendingCompilation) return;
  const request = pendingCompilation;
  pendingCompilation = null;
  worker.postMessage(request);
}

function initWorker() {
  disposeWorker();
  const freshWorker = new Worker('./compiler-worker.js');
  worker = freshWorker;
  freshWorker.onmessage = (event) => {
    if (worker === freshWorker) onMessage(event.data);
  };
  freshWorker.onerror = (event) => {
    if (worker !== freshWorker) return;
    disposeWorker();
    pendingCompilation = null;
    busy = false;
    ui.run.disabled = true;
    ui.runLabel.textContent = 'Unavailable';
    setStatus('compiler unavailable', 'err');
    write(`\nThe compiler could not start: ${event.message}\n`, 'stderr');
  };
  freshWorker.postMessage({ type: 'preload' });
}

let diagnosticText = '';

async function onMessage(message) {
  switch (message.type) {
    case 'ready':
      workerReady = true;
      if (pendingCompilation) dispatchCompilation();
      else if (findStage(activeStage).kind === 'waveform') showWaveform();
      else {
        setBusyLabel(false);
        setStatus('ready');
      }
      break;

    case 'log':
      diagnosticText += message.text;
      record(message.text, message.stream === 'stderr' ? 'stderr' : '');
      break;

    case 'compiled': {
      // LLVM, MLIR, Slang, and wasm LLD all carry process-global state. A
      // command-line invocation gets a fresh process; mirror that boundary in
      // the browser instead of calling main twice in one WebAssembly instance.
      disposeWorker();
      const counts = applyDiagnostics(diagnosticText);
      diagnosticText = '';
      const compileMs = Math.round(message.elapsedMs);

      if (!message.ok) {
        record(`\ncompilation failed (exit ${message.status})\n`, 'stderr');
        if (message.message) record(`${message.message}\n`, 'stderr');
        finishRecording(`${counts.errors} error${counts.errors === 1 ? '' : 's'}`, 'err');
        busy = false; setBusyLabel(false);
        return;
      }

      if (message.kind === 'text') {
        if (message.text) record(message.text.replace(/\s*$/, '\n'), '');
        recording.language = findStage(message.stage).language;
        const note = counts.warnings ? `${counts.warnings} warning${counts.warnings === 1 ? '' : 's'} · ` : '';
        finishRecording(`${note}${compileMs} ms`, 'ok');
        const cached = results.get(message.stage);
        if (cached?.language && activeStage === message.stage) {
          const text = cached.chunks.map((chunk) => chunk.text).join('');
          if (message.stage === 'schedule') showSchedule(text);
          else showIr(text, cached.language);
        }
        busy = false; setBusyLabel(false);
        return;
      }

      record(`compiled ${formatBytes(message.binary.byteLength)} in ${compileMs} ms\n\n`, 'note');
      setStatus('running', 'busy');
      await execute(message.binary, compileMs, counts);
      busy = false; setBusyLabel(false);
      break;
    }

    case 'failed':
      disposeWorker();
      record(`\n${message.message}\n`, 'stderr');
      finishRecording('failed', 'err');
      busy = false; setBusyLabel(false);
      break;
  }
}

async function execute(binary, compileMs, counts) {
  const started = performance.now();
  const files = [];
  try {
    const code = await runSimulation(binary, (text, stream) => {
      record(text, stream === 'stderr' ? 'stderr' : '');
    }, {
      onFile: (file) => files.push(file),
    });
    const runMs = Math.round(performance.now() - started);
    const waveform = files.find((file) => isVcd(file));
    if (waveform) {
      await storedWaveform;
      latestWaveform = { ...waveform, createdAt: Date.now() };
      let saved = true;
      try {
        await saveWaveform(latestWaveform);
      } catch {
        saved = false;
      }
      record(
        `${saved ? 'saved' : 'captured'} ${displayFilename(waveform.name)} ` +
        `(${formatBytes(waveform.data.byteLength)})${saved ? ' locally' : ''}\n`,
        'note',
      );
    }
    record(`\nexited with code ${code}\n`, code === 0 ? 'good' : 'stderr');
    const note = counts.warnings ? `${counts.warnings} warning${counts.warnings === 1 ? '' : 's'} · ` : '';
    finishRecording(`${note}compile ${compileMs} ms · run ${runMs} ms`, code === 0 ? 'ok' : 'err');
  } catch (error) {
    record(`\nsimulation aborted: ${error?.message ?? error}\n`, 'stderr');
    finishRecording('aborted', 'err');
  }
}

function isVcd(file) {
  const prefix = new TextDecoder().decode(file.data.subarray(0, 4096));
  const header = prefix.includes('$date') && prefix.includes('$version');
  return header && (/\.vcd$/i.test(file.name) || prefix.includes('$scope'));
}

function displayFilename(path) {
  return path.split(/[\\/]/).filter(Boolean).at(-1) ?? 'dump.vcd';
}

function showConsole() {
  scheduleSourceHighlight?.clear();
  ui.output.hidden = false;
  ui.irEditor.hidden = true;
  ui.scheduleView.hidden = true;
  ui.waveformView.hidden = true;
  ui.scheduleToggle.hidden = true;
  ui.copyOutput.hidden = false;
  ui.downloadWaveform.hidden = true;
}

function showIr(text, language = MLIR_LANGUAGE_ID) {
  scheduleSourceHighlight?.clear();
  ui.output.hidden = true;
  ui.irEditor.hidden = false;
  ui.scheduleView.hidden = true;
  ui.waveformView.hidden = true;
  ui.scheduleToggle.hidden = true;
  ui.copyOutput.hidden = false;
  ui.downloadWaveform.hidden = true;
  if (monaco && irModel && language !== irLanguage) {
    monaco.editor.setModelLanguage(irModel, language);
    irLanguage = language;
  }
  irModel?.setValue(text);
  requestAnimationFrame(() => irEditor?.layout());
}

function showSchedule(text) {
  scheduleText = text;
  if (scheduleRaw) {
    showIr(text);
    ui.scheduleToggle.hidden = false;
    ui.scheduleToggle.textContent = 'View graph';
    return;
  }
  try {
    const schedules = parseSchedules(text);
    renderSchedules(ui.scheduleView, schedules, {
      onSourceLocation: revealScheduleSource,
      onSourceDeselected: () => scheduleSourceHighlight?.clear(),
    });
  } catch (error) {
    console.error('could not visualize schedule', error);
    showIr(text);
    return;
  }
  ui.output.hidden = true;
  ui.irEditor.hidden = true;
  ui.scheduleView.hidden = false;
  ui.waveformView.hidden = true;
  ui.scheduleToggle.hidden = false;
  ui.scheduleToggle.textContent = 'View IR';
  ui.copyOutput.hidden = false;
  ui.downloadWaveform.hidden = true;
}

function revealScheduleSource(location) {
  if (!editor) return;
  const model = editor.getModel();
  const line = Math.max(1, Math.min(location.line, model.getLineCount()));
  scheduleSourceHighlight?.set([{
    range: new monaco.Range(line, 1, line, model.getLineMaxColumn(line)),
    options: {
      isWholeLine: true,
      className: 'scheduleSourceLine',
      linesDecorationsClassName: 'scheduleSourceLineMarker',
    },
  }]);
  editor.setPosition({
    lineNumber: line,
    column: Math.max(1, Math.min(location.column, model.getLineMaxColumn(line))),
  });
  editor.revealLineInCenter(line);
}

function ensureSurfer() {
  if (surferReady) return surferReady;
  const loading = new Promise((resolve, reject) => {
    let timeout;
    const finish = (callback, value) => {
      clearTimeout(timeout);
      window.removeEventListener('message', onMessage);
      callback(value);
    };
    const onMessage = (event) => {
      if (event.source !== ui.surfer.contentWindow) return;
      if (event.origin !== 'null') return;
      if (event.data?.type === 'surfer-ready') finish(resolve);
      else if (event.data?.type === 'surfer-error') {
        finish(reject, new Error(event.data.message || 'Surfer failed to load'));
      }
    };
    timeout = setTimeout(() => finish(
      reject, new Error('Surfer did not load within 30 seconds'),
    ), 30_000);
    window.addEventListener('message', onMessage);
    ui.surfer.src = `./surfer.html?parentOrigin=${encodeURIComponent(location.origin)}`;
  });
  surferReady = loading.catch((error) => {
    surferReady = null;
    throw error;
  });
  return surferReady;
}

async function showWaveform() {
  scheduleSourceHighlight?.clear();
  ui.output.hidden = true;
  ui.irEditor.hidden = true;
  ui.scheduleView.hidden = true;
  ui.waveformView.hidden = false;
  ui.scheduleToggle.hidden = true;
  ui.copyOutput.hidden = true;
  ui.downloadWaveform.hidden = !latestWaveform;
  ui.surfer.hidden = true;
  ui.waveformEmpty.hidden = false;
  ui.waveformEmpty.textContent = 'Loading the latest waveform saved in this browser…';
  setStatus('loading', 'busy');

  await storedWaveform;
  if (activeStage !== 'waveform') return;
  if (!latestWaveform) {
    ui.waveformEmpty.textContent =
      'No waveform yet. Add $dumpvars to the design and run it; $dumpfile is optional.';
    setStatus('no VCD');
    return;
  }

  ui.downloadWaveform.hidden = false;
  ui.waveformEmpty.textContent = 'Loading Surfer…';
  try {
    await ensureSurfer();
    if (activeStage !== 'waveform') return;
    const buffer = latestWaveform.data.slice().buffer;
    ui.surfer.contentWindow.postMessage(
      { type: 'load-waveform', buffer },
      '*',
      [buffer],
    );
    ui.waveformEmpty.hidden = true;
    ui.surfer.hidden = false;
    const name = displayFilename(latestWaveform.name);
    setStatus(`${name} · ${formatBytes(latestWaveform.data.byteLength)}`, 'ok');
  } catch (error) {
    if (activeStage !== 'waveform') return;
    ui.waveformEmpty.textContent = `Waveform viewer unavailable: ${error?.message ?? error}`;
    setStatus('viewer unavailable', 'err');
  }
}

/* ----------------------------------------------------------------- editor */

function loadMonaco() {
  return new Promise((resolve, reject) => {
    const loader = document.createElement('script');
    loader.src = `${MONACO_CDN}/vs/loader.js`;
    loader.onload = () => {
      window.require.config({ paths: { vs: `${MONACO_CDN}/vs` } });
      window.require(['vs/editor/editor.main'], () => resolve(window.monaco), reject);
    };
    loader.onerror = () => reject(new Error('the editor failed to load'));
    document.head.appendChild(loader);
  });
}

async function initEditor(initialSource) {
  monaco = await loadMonaco();
  registerSystemVerilog(monaco);
  registerMlir(monaco);
  registerLlvm(monaco);

  // Dracula, following the official palette's part of speech: pink keywords,
  // cyan italic types, green functions, purple numbers, yellow strings.
  monaco.editor.defineTheme('obelisk-dracula', {
    base: 'vs-dark',
    inherit: true,
    rules: [
      { token: 'keyword', foreground: 'ff79c6' },
      { token: 'type', foreground: '8be9fd', fontStyle: 'italic' },
      { token: 'predefined', foreground: '50fa7b' },
      { token: 'keyword.directive', foreground: 'ff79c6' },
      { token: 'comment', foreground: '6272a4', fontStyle: 'italic' },
      { token: 'number', foreground: 'bd93f9' },
      { token: 'number.hex', foreground: 'bd93f9' },
      { token: 'number.binary', foreground: 'bd93f9' },
      { token: 'number.octal', foreground: 'bd93f9' },
      { token: 'number.float', foreground: 'bd93f9' },
      { token: 'string', foreground: 'f1fa8c' },
      { token: 'string.escape', foreground: 'ff79c6' },
      { token: 'operator', foreground: 'ff79c6' },
      { token: 'delimiter', foreground: 'f8f8f2' },
      { token: 'identifier', foreground: 'f8f8f2' },
      { token: 'variable', foreground: '8be9fd' },
      { token: 'tag', foreground: 'bd93f9' },
      { token: 'function', foreground: '50fa7b' },
      { token: 'annotation', foreground: 'ffb86c' },
    ],
    colors: {
      'editor.background': '#282a36',
      'editor.foreground': '#f8f8f2',
      'editorLineNumber.foreground': '#6272a4',
      'editorLineNumber.activeForeground': '#f8f8f2',
      'editor.lineHighlightBackground': '#44475a45',
      'editor.selectionBackground': '#44475a',
      'editorCursor.foreground': '#f8f8f2',
      'editorIndentGuide.background1': '#3b3d4c',
      'editorWidget.background': '#21222c',
      'editorError.foreground': '#ff5555',
      'editorWarning.foreground': '#ffb86c',
    },
  });

  editor = monaco.editor.create(ui.editor, {
    value: initialSource,
    language: SV_LANGUAGE_ID,
    theme: 'obelisk-dracula',
    fontSize: 13,
    fontFamily: '"IBM Plex Mono", ui-monospace, monospace',
    minimap: { enabled: false },
    scrollBeyondLastLine: false,
    automaticLayout: true,
    tabSize: 2,
    padding: { top: 12 },
    smoothScrolling: true,
  });
  scheduleSourceHighlight = editor.createDecorationsCollection();

  irModel = monaco.editor.createModel('', MLIR_LANGUAGE_ID);
  irEditor = monaco.editor.create(ui.irEditor, {
    model: irModel,
    theme: 'obelisk-dracula',
    readOnly: true,
    domReadOnly: true,
    fontSize: 12.5,
    lineHeight: 20,
    fontFamily: '"IBM Plex Mono", ui-monospace, monospace',
    minimap: { enabled: false },
    automaticLayout: true,
    scrollBeyondLastLine: false,
    renderLineHighlight: 'none',
    folding: true,
    wordWrap: 'off',
    padding: { top: 12, bottom: 12 },
  });

  editor.addCommand(monaco.KeyMod.CtrlCmd | monaco.KeyCode.Enter, run);
  editor.onDidChangeModelContent(() => {
    invalidate();
    persist();
  });
}

/* ---------------------------------------------------------------- options */

function readOptions() {
  for (const field of OPTION_FIELDS) options[field] = el(field).value;
}

function writeOptions() {
  for (const field of OPTION_FIELDS) el(field).value = options[field];
}

function persist() {
  if (editor) saveState(editor.getValue(), { ...options, stage: activeStage });
}

function initOptions() {
  for (const field of OPTION_FIELDS) {
    el(field).addEventListener('input', () => {
      readOptions();
      invalidate();
      renderCommand();
      persist();
    });
  }

  ui.flagsToggle.addEventListener('click', () => {
    const open = ui.flagsPanel.hasAttribute('hidden');
    ui.flagsPanel.toggleAttribute('hidden', !open);
    ui.flagsToggle.setAttribute('aria-expanded', String(open));
  });
}

/* ------------------------------------------------------------------ chrome */

async function copy(text, button) {
  const original = button.textContent;
  try {
    await navigator.clipboard.writeText(text);
    button.textContent = 'Copied';
  } catch {
    button.textContent = 'Press ⌘C';
  }
  setTimeout(() => { button.textContent = original; }, 1400);
}

function initChrome() {
  ui.run.addEventListener('click', run);
  ui.copyCommand.addEventListener('click', () =>
    copy(formatCommand({ ...options, stage: activeStage }), ui.copyCommand));
  ui.scheduleToggle.addEventListener('click', () => {
    scheduleRaw = !scheduleRaw;
    showSchedule(scheduleText);
  });
  ui.copyOutput.addEventListener('click', () =>
    copy(!ui.scheduleView.hidden ? scheduleText
      : ui.irEditor.hidden ? ui.output.textContent : irModel.getValue(), ui.copyOutput));
  ui.downloadWaveform.addEventListener('click', () => {
    if (!latestWaveform) return;
    const url = URL.createObjectURL(new Blob(
      [latestWaveform.data], { type: 'text/x-vcd' },
    ));
    const anchor = document.createElement('a');
    anchor.href = url;
    anchor.download = displayFilename(latestWaveform.name);
    anchor.click();
    setTimeout(() => URL.revokeObjectURL(url));
  });
  ui.share.addEventListener('click', () => {
    const link = toPermalink(editor.getValue(), { ...options, stage: activeStage });
    history.replaceState(null, '', link);
    copy(link, ui.share);
  });

  for (const [index, example] of EXAMPLES.entries()) {
    const option = document.createElement('option');
    option.value = String(index);
    option.textContent = example.name;
    ui.examples.appendChild(option);
  }
  ui.examples.addEventListener('change', () => {
    const example = EXAMPLES[Number(ui.examples.value)];
    if (example) {
      editor.setValue(example.source);
      ui.examples.value = '';
      editor.focus();
    }
  });

  document.addEventListener('keydown', (event) => {
    if ((event.metaKey || event.ctrlKey) && event.key === 'Enter') {
      event.preventDefault();
      run();
    }
  });

  initHandle();
}

function initHandle() {
  let dragging = false;
  const stacked = () => window.matchMedia('(max-width: 860px)').matches;

  const move = (event) => {
    if (!dragging) return;
    const rect = ui.workbench.getBoundingClientRect();
    const fraction = stacked()
      ? (event.clientY - rect.top) / rect.height
      : (event.clientX - rect.left) / rect.width;
    const clamped = Math.min(0.85, Math.max(0.15, fraction));
    if (stacked()) ui.workbench.style.gridTemplateRows = `${clamped}fr 5px ${1 - clamped}fr`;
    else ui.workbench.style.gridTemplateColumns = `${clamped}fr 5px ${1 - clamped}fr`;
  };

  ui.handle.addEventListener('pointerdown', (event) => {
    dragging = true;
    ui.handle.setPointerCapture(event.pointerId);
    document.body.style.userSelect = 'none';
  });
  ui.handle.addEventListener('pointerup', (event) => {
    dragging = false;
    ui.handle.releasePointerCapture(event.pointerId);
    document.body.style.userSelect = '';
  });
  ui.handle.addEventListener('pointermove', move);
}

/* -------------------------------------------------------------------- misc */

function formatBytes(bytes) {
  if (bytes < 1024) return `${bytes} B`;
  if (bytes < 1024 * 1024) return `${(bytes / 1024).toFixed(1)} KB`;
  return `${(bytes / (1024 * 1024)).toFixed(1)} MB`;
}

async function main() {
  const saved = loadState();
  if (saved) {
    options = { ...saved.options };
    activeStage = saved.options.stage ?? DEFAULT_STAGE;
  }

  buildStageTabs();
  writeOptions();
  paintStageSelection();
  initOptions();
  initChrome();

  setStatus('loading editor', 'busy');
  try {
    await initEditor(saved?.source ?? EXAMPLES[0].source);
  } catch (error) {
    setStatus('editor failed', 'err');
    write(`${error.message}\n`, 'stderr');
    return;
  }

  setStatus('loading compiler', 'busy');
  initWorker();
}

main();
