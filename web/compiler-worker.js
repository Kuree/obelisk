// Runs the wasm build of the Obelisk driver off the main thread.
//
// Contract with the driver module (emscripten MODULARIZE build of
// tools/driver): a factory on self.createObeliskModule, an in-memory FS, and
// callMain(argv). The driver writes a linked wasm simulation to the output
// path, which this worker then runs itself. Keeping the simulation here leaves
// the page responsive, and lets it stop a design that never calls $finish by
// terminating the worker.

let modulePromise = null;

// Emscripten's terminal hooks can retain ANSI color even though their output
// is rendered in a browser rather than a terminal. Remove CSI color/control
// sequences before they reach both the console and diagnostic parser.
function stripAnsi(text) {
  return text.replace(/\x1b\[[0-?]*[ -/]*[@-~]/g, '');
}

function loadDriver() {
  if (modulePromise) return modulePromise;
  modulePromise = (async () => {
    // obelisk.js is the emscripten glue emitted next to obelisk.wasm.
    self.importScripts('./toolchain.js');
    self.importScripts('./obelisk.js');
    if (typeof self.createObeliskModule !== 'function') {
      throw new Error('obelisk.js did not expose createObeliskModule');
    }
    const mod = await self.createObeliskModule({
      noInitialRun: true,
      thisProgram: '/bin/obelisk',
      print: (line) => post('log', { stream: 'stdout', text: stripAnsi(line) + '\n' }),
      printErr: (line) => post('log', { stream: 'stderr', text: stripAnsi(line) + '\n' }),
      locateFile: (path) => new URL(path, self.location.href).href,
    });
    await self.installObeliskToolchain(mod);
    return mod;
  })();
  return modulePromise;
}

function post(type, payload) {
  self.postMessage({ type, ...payload });
}

// A design can $display in a tight loop. The worker is synchronously inside
// the simulation, so no timer can flush a buffer later: post the first few
// writes of each window immediately, and coalesce the rest of a flood until
// the next window or a size limit.
const OUTPUT_WINDOW_MS = 50;
const OUTPUT_MESSAGES_PER_WINDOW = 20;
const OUTPUT_BATCH_CHARS = 64 * 1024;

function createOutputBatcher() {
  let stream = null;
  let text = '';
  let windowStart = performance.now();
  let posted = 0;
  const flush = () => {
    if (!text) return;
    post('output', { stream, text });
    text = '';
    posted++;
  };
  const write = (chunk, chunkStream) => {
    if (chunkStream !== stream) {
      flush();
      stream = chunkStream;
    }
    text += chunk;
    const now = performance.now();
    if (now - windowStart >= OUTPUT_WINDOW_MS) {
      windowStart = now;
      posted = 0;
    }
    if (posted < OUTPUT_MESSAGES_PER_WINDOW || text.length >= OUTPUT_BATCH_CHARS) flush();
  };
  return { write, flush };
}

async function execute(binary, stage) {
  const { runSimulation } = await import(new URL('./wasi.js', self.location.href).href);
  const output = createOutputBatcher();
  const files = [];
  let simulatedTime = null;
  let code = null;
  let error;
  const started = performance.now();
  try {
    code = await runSimulation(binary, output.write, {
      onFile: (file) => files.push(file),
      onSimulatedTime: (time) => { simulatedTime = time; },
    });
  } catch (caught) {
    error = caught?.message ?? String(caught);
  }
  output.flush();
  const buffers = [...new Set(files.map((file) => file.data.buffer))];
  self.postMessage({
    type: 'exited',
    stage,
    code,
    ...(error !== undefined && { error }),
    runMs: performance.now() - started,
    simulatedTime,
    files,
  }, buffers);
}

async function compile({ source, args, stage, kind }) {
  let mod = await loadDriver();
  const input = '/work/design.sv';
  // Text stages print IR; the Run stage produces a linked wasm module.
  const output = kind === 'binary' ? '/work/design.wasm' : '/work/design.out';

  try {
    mod.FS.mkdir('/work');
  } catch {
    // already present on a second run
  }
  mod.FS.writeFile(input, source);
  try {
    mod.FS.unlink(output);
  } catch {
    // no previous artifact
  }

  const argv = kind === 'binary'
    ? ['--compile-threads=1', '--sysroot=/sysroot', ...args, '-o', output, input]
    : ['--compile-threads=1', ...args, '-o', output, input];
  const started = performance.now();
  let status = 0;
  try {
    status = mod.callMain(argv) ?? 0;
  } catch (error) {
    // emscripten throws ExitStatus for a non-zero exit.
    if (typeof error === 'object' && error !== null && 'status' in error) {
      status = error.status;
    } else {
      throw error;
    }
  }
  const elapsedMs = performance.now() - started;

  if (status !== 0) {
    post('compiled', { ok: false, stage, kind, status, elapsedMs });
    return;
  }

  if (kind === 'text') {
    let text;
    try {
      text = mod.FS.readFile(output, { encoding: 'utf8' });
    } catch {
      text = '';
    }
    post('compiled', { ok: true, stage, kind, status, elapsedMs, text });
    return;
  }

  let binary;
  try {
    binary = mod.FS.readFile(output);
  } catch {
    post('compiled', {
      ok: false,
      stage,
      kind,
      status,
      elapsedMs,
      message: 'compiler reported success but produced no output module',
    });
    return;
  }

  post('compiled', {
    ok: true, stage, kind, status, elapsedMs, byteLength: binary.byteLength,
  });
  // This worker never compiles again, so release the driver's heap before the
  // simulation allocates its own.
  mod = null;
  modulePromise = null;
  await execute(binary, stage);
}

self.onmessage = async (event) => {
  const { type } = event.data;
  try {
    if (type === 'compile') {
      await compile(event.data);
    } else if (type === 'preload') {
      await loadDriver();
      post('ready', {});
    }
  } catch (error) {
    post('failed', { message: error?.message ?? String(error) });
  }
};
