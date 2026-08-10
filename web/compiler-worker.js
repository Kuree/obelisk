// Runs the wasm build of the Obelisk driver off the main thread.
//
// Contract with the driver module (emscripten MODULARIZE build of
// tools/driver): a factory on self.createObeliskModule, an in-memory FS, and
// callMain(argv). The driver writes a linked wasm simulation to the output
// path, which this worker reads back and transfers to the page.

let modulePromise = null;

function loadDriver() {
  if (modulePromise) return modulePromise;
  modulePromise = (async () => {
    // obelisk.js is the emscripten glue emitted next to obelisk.wasm.
    self.importScripts('./obelisk.js');
    if (typeof self.createObeliskModule !== 'function') {
      throw new Error('obelisk.js did not expose createObeliskModule');
    }
    return self.createObeliskModule({
      noInitialRun: true,
      print: (line) => post('log', { stream: 'stdout', text: line + '\n' }),
      printErr: (line) => post('log', { stream: 'stderr', text: line + '\n' }),
      locateFile: (path) => new URL(path, self.location.href).href,
    });
  })();
  return modulePromise;
}

function post(type, payload) {
  self.postMessage({ type, ...payload });
}

async function compile({ source, args, stage, kind }) {
  const mod = await loadDriver();
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

  const argv = [...args, '-o', output, input];
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

  const buffer = binary.buffer.slice(binary.byteOffset, binary.byteOffset + binary.byteLength);
  self.postMessage(
    { type: 'compiled', ok: true, stage, kind, status, elapsedMs, binary: buffer },
    [buffer],
  );
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
