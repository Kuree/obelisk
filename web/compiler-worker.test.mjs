import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';

const messages = [];
const transfers = [];
const importedScripts = [];
const calls = [];
const files = new Map();
let behavior = 'success';
let factoryCalls = 0;
let installCalls = 0;

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
    files.set(output, output.endsWith('.wasm')
      ? new Uint8Array([0, 97, 115, 109])
      : 'module {\n}\n');
    return 0;
  },
};

globalThis.self = {
  location: { href: 'https://example.test/web/compiler-worker.js' },
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
        assert.equal(options.noInitialRun, true);
        assert.equal(options.thisProgram, '/bin/obelisk');
        assert.equal(options.locateFile('obelisk.wasm'),
          'https://example.test/web/obelisk.wasm');
        return module;
      };
    }
  },
};

const source = await readFile(new URL('./compiler-worker.js', import.meta.url), 'utf8');
await import(`data:text/javascript;base64,${Buffer.from(source).toString('base64')}`);
assert.equal(typeof self.onmessage, 'function');

await self.onmessage({ data: { type: 'preload' } });
assert.deepEqual(messages.at(-1), { type: 'ready' });
assert.deepEqual(importedScripts, ['./toolchain.js', './obelisk.js']);
assert.equal(factoryCalls, 1);
assert.equal(installCalls, 1);

await self.onmessage({
  data: { type: 'compile', source: 'module m; endmodule', args: ['-emit-sim'], stage: 'sim', kind: 'text' },
});
assert.deepEqual(calls.at(-1), [
  '--compile-threads=1', '-emit-sim', '-o', '/work/design.out', '/work/design.sv',
]);
assert.equal(files.get('/work/design.sv'), 'module m; endmodule');
assert.equal(messages.at(-1).type, 'compiled');
assert.equal(messages.at(-1).ok, true);
assert.equal(messages.at(-1).text, 'module {\n}\n');

await self.onmessage({
  data: { type: 'compile', source: 'module m; endmodule', args: ['-O3'], stage: 'run', kind: 'binary' },
});
assert.deepEqual(calls.at(-1), [
  '--compile-threads=1', '--sysroot=/sysroot', '-O3', '-o',
  '/work/design.wasm', '/work/design.sv',
]);
assert.equal(messages.at(-1).ok, true);
assert.ok(messages.at(-1).binary instanceof ArrayBuffer);
assert.deepEqual([...new Uint8Array(messages.at(-1).binary)], [0, 97, 115, 109]);
assert.deepEqual(transfers.at(-1), [messages.at(-1).binary]);

behavior = 'status';
await self.onmessage({
  data: { type: 'compile', source: '', args: [], stage: 'llvm', kind: 'text' },
});
assert.deepEqual(
  { type: messages.at(-1).type, ok: messages.at(-1).ok, status: messages.at(-1).status },
  { type: 'compiled', ok: false, status: 7 },
);

behavior = 'no-output';
await self.onmessage({
  data: { type: 'compile', source: '', args: [], stage: 'run', kind: 'binary' },
});
assert.equal(messages.at(-1).ok, false);
assert.match(messages.at(-1).message, /produced no output module/);

behavior = 'throw';
await self.onmessage({
  data: { type: 'compile', source: '', args: [], stage: 'run', kind: 'binary' },
});
assert.deepEqual(messages.at(-1), { type: 'failed', message: 'driver crashed' });
assert.equal(factoryCalls, 1);

console.log('web compiler worker message and argv contract OK');
