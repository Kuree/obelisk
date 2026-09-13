import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';

/* ------------------------------------------------------ simulation modules */

// Assembles a direct-main simulation module whose `main` runs `steps` in
// order through the real WASI host in wasi.js:
//
//   { write: 'text', fd = 1, times = 1 }  fd_write, `times` times
//   { file: 'name', text }                 openat + fd_write + fd_close
//   { exit: code }                         proc_exit
//   { trap: true }                         unreachable
//
// and then returns `status`. `time` adds the exports that report final
// simulated time.
function simulationModule({ steps = [], status = 0, time = null } = {}) {
  const leb = (value) => {
    const bytes = [];
    do {
      let byte = value & 0x7f;
      value >>>= 7;
      if (value) byte |= 0x80;
      bytes.push(byte);
    } while (value);
    return bytes;
  };
  // i32.const and i64.const take signed LEB128.
  const signed = (value) => {
    value = BigInt(value);
    const bytes = [];
    for (;;) {
      const byte = Number(value & 0x7fn);
      value >>= 7n;
      if ((value === 0n && !(byte & 0x40)) || (value === -1n && (byte & 0x40))) {
        return [...bytes, byte];
      }
      bytes.push(byte | 0x80);
    }
  };
  const name = (text) => [...leb(text.length), ...Buffer.from(text)];
  const vector = (items) => [...leb(items.length), ...items.flat()];
  const section = (id, content) => [id, ...leb(content.length), ...content];
  const body = (locals, code) => {
    const content = [...locals, ...code, 0x0b];
    return [...leb(content.length), ...content];
  };
  const i32 = 0x7f;
  const i64 = 0x7e;
  const constant = (value) => [0x41, ...signed(value)];
  const call = (index) => [0x10, index];

  // Function indices: imports first, then malloc, free, main, and the
  // optional time exports.
  const FD_WRITE = 0;
  const PROC_EXIT = 1;
  const OPENAT = 2;
  const FD_CLOSE = 3;
  const MAIN = 6;
  const NWRITTEN = 8;
  // Two pages: fixture data below, argv allocations above.
  const ARENA = 0x10000;
  const O_WRONLY_CREAT_TRUNC = 1 | 64 | 512;

  const segments = [];
  let next = 0x100;
  const place = (bytes) => {
    const address = next;
    segments.push([0x00, ...constant(address), 0x0b, ...leb(bytes.length), ...bytes]);
    next += bytes.length + (4 - (bytes.length % 4)) % 4;
    return address;
  };
  const iovec = (text) => {
    const bytes = Buffer.from(text);
    const pointer = place([...bytes]);
    const vectorBytes = Buffer.alloc(8);
    vectorBytes.writeUInt32LE(pointer, 0);
    vectorBytes.writeUInt32LE(bytes.length, 4);
    return place([...vectorBytes]);
  };
  const writeCall = (fd, iov) => [
    ...(typeof fd === 'number' ? constant(fd) : fd),
    ...constant(iov), ...constant(1), ...constant(NWRITTEN), ...call(FD_WRITE), 0x1a,
  ];

  // Locals after argc and argv: 2 is a loop counter, 3 an opened descriptor.
  const code = [];
  for (const step of steps) {
    if (step.write !== undefined) {
      const iov = iovec(step.write);
      const times = step.times ?? 1;
      code.push(
        ...constant(0), 0x21, 0x02,
        0x02, 0x40, 0x03, 0x40,
        0x20, 0x02, ...constant(times), 0x4e, 0x0d, 0x01,
        ...writeCall(step.fd ?? 1, iov),
        0x20, 0x02, ...constant(1), 0x6a, 0x21, 0x02,
        0x0c, 0x00, 0x0b, 0x0b,
      );
    } else if (step.file !== undefined) {
      const path = place([...Buffer.from(step.file), 0]);
      const iov = iovec(step.text);
      code.push(
        ...constant(-100), ...constant(path), ...constant(O_WRONLY_CREAT_TRUNC),
        ...constant(0), ...call(OPENAT), 0x21, 0x03,
        ...writeCall([0x20, 0x03], iov),
        0x20, 0x03, ...call(FD_CLOSE), 0x1a,
      );
    } else if (step.exit !== undefined) {
      code.push(...constant(step.exit), ...call(PROC_EXIT));
    } else if (step.trap) {
      code.push(0x00);
    }
  }
  code.push(...constant(status));
  assert.ok(next < ARENA, 'fixture data overlaps the malloc arena');

  const exports = [
    [...name('memory'), 0x02, 0x00],
    [...name('malloc'), 0x00, 4],
    [...name('free'), 0x00, 5],
    [...name('main'), 0x00, MAIN],
  ];
  const functions = [[1], [2], [3]];
  const bodies = [
    body([0x00], constant(ARENA)),
    body([0x00], []),
    body([0x01, 0x02, i32], code),
  ];
  if (time) {
    functions.push([4], [4]);
    exports.push(
      [...name('obelisk_final_time'), 0x00, 7],
      [...name('obelisk_time_precision_fs'), 0x00, 8],
    );
    bodies.push(
      body([0x00], [0x42, ...signed(time.ticks)]),
      body([0x00], [0x42, ...signed(time.precisionFs)]),
    );
  }

  return new Uint8Array([
    0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00,
    ...section(1, vector([
      [0x60, 4, i32, i32, i32, i32, 1, i32],
      [0x60, 1, i32, 1, i32],
      [0x60, 1, i32, 0],
      [0x60, 2, i32, i32, 1, i32],
      [0x60, 0, 1, i64],
    ])),
    ...section(2, vector([
      [...name('wasi_snapshot_preview1'), ...name('fd_write'), 0x00, 0x00],
      [...name('wasi_snapshot_preview1'), ...name('proc_exit'), 0x00, 0x02],
      [...name('env'), ...name('__syscall_openat'), 0x00, 0x00],
      [...name('wasi_snapshot_preview1'), ...name('fd_close'), 0x00, 0x01],
    ])),
    ...section(3, vector(functions)),
    ...section(5, vector([[0x00, 0x02]])),
    ...section(7, vector(exports)),
    ...section(10, vector(bodies)),
    ...section(11, vector(segments)),
  ]);
}

/* ------------------------------------------------------------ worker host */

const messages = [];
const transfers = [];
const importedScripts = [];
const calls = [];
const files = new Map();
let behavior = 'success';
let simulation = simulationModule();
let factoryCalls = 0;
let installCalls = 0;
let factoryOptions = null;

// The output batcher decides by elapsed time. Each read of the clock advances
// it by `clockStep`, so a test chooses whether writes land in one window.
let clock = 0;
let clockStep = 0;
Object.defineProperty(globalThis, 'performance', {
  configurable: true,
  value: { now: () => (clock += clockStep) },
});

const FS = {
  mkdir() {},
  writeFile(path, data) { files.set(path, data); },
  unlink(path) {
    if (!files.delete(path)) throw new Error('missing');
  },
  readFile(path, options) {
    if (!files.has(path)) throw new Error('missing');
    const value = files.get(path);
    if (options?.encoding === 'utf8') return String(value);
    return value;
  },
};

const module = {
  FS,
  callMain(argv) {
    calls.push(argv);
    if (behavior === 'status') throw { status: 7 };
    if (behavior === 'throw') throw new Error('driver crashed');
    if (behavior === 'no-output') return 0;
    const output = argv[argv.indexOf('-o') + 1];
    files.set(output, output.endsWith('.wasm') ? simulation : 'module {\n}\n');
    return 0;
  },
};

globalThis.self = {
  location: { href: new URL('./compiler-worker.js', import.meta.url).href },
  postMessage(message, transfer = []) {
    messages.push(message);
    transfers.push(transfer);
  },
  importScripts(path) {
    importedScripts.push(path);
    if (path === './toolchain.js') {
      self.installObeliskToolchain = async (mod) => {
        assert.equal(mod, module);
        installCalls++;
      };
    } else if (path === './obelisk.js') {
      self.createObeliskModule = async (options) => {
        factoryCalls++;
        factoryOptions = options;
        assert.equal(options.noInitialRun, true);
        assert.equal(options.thisProgram, '/bin/obelisk');
        assert.equal(options.locateFile('obelisk.wasm'),
          new URL('./obelisk.wasm', import.meta.url).href);
        return module;
      };
    }
  },
};

const source = await readFile(new URL('./compiler-worker.js', import.meta.url), 'utf8');
await import(`data:text/javascript;base64,${Buffer.from(source).toString('base64')}`);
assert.equal(typeof self.onmessage, 'function');

/** Sends one request and returns only the messages it produced. */
async function request(data) {
  const start = messages.length;
  await self.onmessage({ data: { type: 'compile', source: 'module m; endmodule', args: [], ...data } });
  return { messages: messages.slice(start), transfers: transfers.slice(start) };
}

/** Runs `module` as a linked simulation. */
function simulate(built, { step = 0 } = {}) {
  simulation = built;
  clockStep = step;
  return request({ stage: 'run', kind: 'binary' });
}

const outputOf = (run) => run.messages.filter((message) => message.type === 'output');
const join = (output) => output.map((message) => message.text).join('');

/* ------------------------------------------------------- preload and text */

await self.onmessage({ data: { type: 'preload' } });
assert.deepEqual(messages.at(-1), { type: 'ready' });
assert.deepEqual(importedScripts, ['./toolchain.js', './obelisk.js']);
assert.equal(factoryCalls, 1);
assert.equal(installCalls, 1);
factoryOptions.printErr('\x1b[36mwork/design.sv\x1b[0m:\x1b[96m11:12\x1b[0m: warning');
assert.deepEqual(messages.at(-1), {
  type: 'log',
  stream: 'stderr',
  text: 'work/design.sv:11:12: warning\n',
});

// A second preload reuses the loaded driver.
await self.onmessage({ data: { type: 'preload' } });
assert.equal(factoryCalls, 1);

let result = await request({ args: ['-emit-sim'], stage: 'sim', kind: 'text' });
assert.deepEqual(calls.at(-1), [
  '--compile-threads=1', '-emit-sim', '-o', '/work/design.out', '/work/design.sv',
]);
assert.equal(files.get('/work/design.sv'), 'module m; endmodule');
assert.equal(result.messages.length, 1);
assert.equal(result.messages[0].type, 'compiled');
assert.equal(result.messages[0].ok, true);
assert.equal(result.messages[0].text, 'module {\n}\n');

// Text stages keep the driver loaded.
await request({ args: ['-emit-llvm'], stage: 'llvm', kind: 'text' });
assert.equal(factoryCalls, 1);

/* ------------------------------------------------------ run and its result */

simulation = simulationModule({
  steps: [{ write: 'hi\n', times: 3 }],
  status: 5,
  time: { ticks: 42, precisionFs: 1000 },
});
result = await request({ args: ['-O3'], stage: 'run', kind: 'binary' });
assert.deepEqual(calls.at(-1), [
  '--compile-threads=1', '--sysroot=/sysroot', '-O3', '-o',
  '/work/design.wasm', '/work/design.sv',
]);
let [compiled, ...rest] = result.messages;
assert.deepEqual(
  { type: compiled.type, ok: compiled.ok, kind: compiled.kind, stage: compiled.stage },
  { type: 'compiled', ok: true, kind: 'binary', stage: 'run' },
);
// The page is told the size of the module, but the module stays in the worker.
assert.equal(compiled.byteLength, simulation.byteLength);
assert.equal('binary' in compiled, false);
assert.equal(join(outputOf(result)), 'hi\nhi\nhi\n');
let exited = rest.at(-1);
assert.equal(exited.type, 'exited');
assert.equal(exited.stage, 'run');
assert.equal(exited.code, 5);
assert.equal('error' in exited, false);
assert.equal(typeof exited.runMs, 'number');
assert.deepEqual(exited.simulatedTime, { ticks: 42n, precisionFs: 1000n });
assert.deepEqual(exited.files, []);
assert.deepEqual(result.transfers.at(-1), []);
assert.equal(result.messages.length, outputOf(result).length + 2);

// A module without the time exports reports no simulated time.
result = await simulate(simulationModule({ steps: [{ write: 'x\n' }] }));
assert.equal(result.messages.at(-1).simulatedTime, null);
assert.equal(result.messages.at(-1).code, 0);

// A linked run releases the driver, so the next request loads a fresh one.
const loadsBefore = factoryCalls;
await request({ args: ['-emit-sim'], stage: 'sim', kind: 'text' });
assert.equal(factoryCalls, loadsBefore + 1);

/* --------------------------------------------------------- output batching */

// Everything in one time window: the first 20 writes post immediately and the
// rest of the flood is coalesced into one final message.
result = await simulate(simulationModule({ steps: [{ write: 'hi\n', times: 100 }] }));
let output = outputOf(result);
assert.equal(join(output), 'hi\n'.repeat(100));
assert.equal(output.length, 21);
assert.ok(output.slice(0, 20).every((message) => message.text === 'hi\n'));
assert.equal(output[20].text, 'hi\n'.repeat(80));

// When every write starts a new window, nothing is held back.
result = await simulate(
  simulationModule({ steps: [{ write: 'hi\n', times: 30 }] }),
  { step: 60 },
);
output = outputOf(result);
assert.equal(output.length, 30);
assert.ok(output.every((message) => message.text === 'hi\n'));

// A coalesced batch is flushed once it reaches 64 KiB even inside a window.
const long = `${'x'.repeat(40000)}\n`;
result = await simulate(simulationModule({ steps: [{ write: long, times: 30 }] }));
output = outputOf(result);
assert.equal(join(output), long.repeat(30));
assert.deepEqual(output.map((message) => message.text.length), [
  ...Array(20).fill(long.length), ...Array(5).fill(long.length * 2),
]);

// A change of stream flushes what was held, so stdout and stderr keep their
// relative order even while the window is saturated.
result = await simulate(simulationModule({
  steps: [
    { write: 'flood\n', times: 20 },
    { write: 'a\n' },
    { write: 'b\n', fd: 2 },
    { write: 'c\n' },
    { write: 'd\n' },
  ],
}));
output = outputOf(result).slice(20);
assert.deepEqual(output.map(({ stream, text }) => ({ stream, text })), [
  { stream: 'stdout', text: 'a\n' },
  { stream: 'stderr', text: 'b\n' },
  { stream: 'stdout', text: 'c\nd\n' },
]);

// Output that does not end in a newline is still delivered before `exited`.
result = await simulate(simulationModule({ steps: [{ write: 'partial' }] }));
assert.deepEqual(result.messages.at(-2), { type: 'output', stream: 'stdout', text: 'partial' });
assert.equal(result.messages.at(-1).type, 'exited');

/* ------------------------------------------------ files, exit, and traps */

// Files a simulation writes are returned with `exited`, and their buffers are
// transferred rather than copied.
result = await simulate(simulationModule({
  steps: [
    { file: 'dump.vcd', text: '$date today $end\n' },
    { file: 'cov.obcov', text: 'OBCOV' },
    { write: 'done\n' },
  ],
}));
exited = result.messages.at(-1);
assert.equal(exited.type, 'exited');
assert.deepEqual(
  exited.files.map((file) => [file.name, Buffer.from(file.data).toString()]),
  [['dump.vcd', '$date today $end\n'], ['cov.obcov', 'OBCOV']],
);
assert.equal(result.transfers.at(-1).length, 2);
for (const file of exited.files) {
  assert.ok(result.transfers.at(-1).includes(file.data.buffer));
}

// $finish-style proc_exit reports its code, flushes pending output first, and
// still returns files written before it.
result = await simulate(simulationModule({
  steps: [
    { file: 'wave.vcd', text: 'wave' },
    { write: 'bye' },
    { exit: 3 },
    { write: 'unreachable\n' },
  ],
  status: 9,
}));
assert.equal(join(outputOf(result)), 'bye');
exited = result.messages.at(-1);
assert.equal(exited.code, 3);
assert.equal('error' in exited, false);
assert.deepEqual(exited.files.map((file) => file.name), ['wave.vcd']);

// A trap is reported as an error on `exited`, after the output that preceded
// it, and does not escape as a worker failure.
result = await simulate(simulationModule({
  steps: [{ write: 'before trap\n' }, { file: 'partial.vcd', text: 'x' }, { trap: true }],
}));
assert.equal(join(outputOf(result)), 'before trap\n');
exited = result.messages.at(-1);
assert.equal(exited.type, 'exited');
assert.equal(exited.code, null);
assert.match(exited.error, /unreachable/);
assert.deepEqual(exited.files.map((file) => file.name), ['partial.vcd']);
assert.equal(result.messages.some((message) => message.type === 'failed'), false);

// Bytes that are not a module fail instantiation the same way.
result = await simulate(new Uint8Array([0, 97, 115, 109, 1, 0, 0, 0, 0xff]));
assert.equal(result.messages.length, 2);
assert.equal(result.messages[1].type, 'exited');
assert.equal(result.messages[1].code, null);
assert.equal(typeof result.messages[1].error, 'string');

/* ---------------------------------------------------------- compile errors */

behavior = 'status';
result = await request({ stage: 'llvm', kind: 'text' });
assert.deepEqual(
  { type: result.messages[0].type, ok: result.messages[0].ok, status: result.messages[0].status },
  { type: 'compiled', ok: false, status: 7 },
);

// A failed link never reaches the simulation.
result = await request({ stage: 'run', kind: 'binary' });
assert.equal(result.messages.length, 1);
assert.deepEqual(
  { type: result.messages[0].type, ok: result.messages[0].ok, status: result.messages[0].status },
  { type: 'compiled', ok: false, status: 7 },
);

behavior = 'no-output';
result = await request({ stage: 'run', kind: 'binary' });
assert.equal(result.messages.length, 1);
assert.equal(result.messages[0].ok, false);
assert.match(result.messages[0].message, /produced no output module/);

behavior = 'throw';
result = await request({ stage: 'run', kind: 'binary' });
assert.deepEqual(result.messages, [{ type: 'failed', message: 'driver crashed' }]);

console.log('web compiler worker message and argv contract OK');
