import assert from 'node:assert/strict';

import { Wasi, WasiExit } from './wasi.js';

const encoder = new TextEncoder();
const decoder = new TextDecoder();
const outputs = [];
const files = [];
const wasi = new Wasi({
  args: ['sim', '--seed=7'],
  onOutput: (text, stream) => outputs.push({ text, stream }),
  onFile: (file) => files.push(file),
});
const memory = new WebAssembly.Memory({ initial: 1 });
wasi.bindMemory(memory);
const bytes = new Uint8Array(memory.buffer);
const view = new DataView(memory.buffer);
const imports = wasi.imports;
const host = imports.wasi_snapshot_preview1;

function writeBytes(pointer, value) {
  const data = typeof value === 'string' ? encoder.encode(value) : value;
  bytes.set(data, pointer);
  return data.byteLength;
}

function writeCString(pointer, value) {
  const length = writeBytes(pointer, value);
  bytes[pointer + length] = 0;
}

// wasm32: an iovec is two 32-bit fields, and WASI sizes and pointers are
// 32-bit with it. File offsets stay 64-bit at either pointer width.
function setIovec(pointer, dataPointer, length) {
  view.setUint32(pointer, dataPointer, true);
  view.setUint32(pointer + 4, length, true);
}

function write(fd, text, iovec = 128, data = 1024, written = 256) {
  const length = writeBytes(data, text);
  setIovec(iovec, data, length);
  assert.equal(host.fd_write(fd, iovec, 1, written), 0);
  assert.equal(view.getUint32(written, true), length);
}

assert.equal(host.args_sizes_get(16, 24), 0);
assert.equal(view.getUint32(16, true), 2);
assert.equal(view.getUint32(24, true), 13);
assert.equal(host.args_get(32, 64), 0);
assert.equal(view.getUint32(32, true), 64);
assert.equal(view.getUint32(36, true), 68);
assert.equal(decoder.decode(bytes.subarray(64, 67)), 'sim');
assert.equal(decoder.decode(bytes.subarray(68, 76)), '--seed=7');

assert.equal(host.environ_sizes_get(16, 24), 0);
assert.equal(view.getUint32(16, true), 0);
assert.equal(view.getUint32(24, true), 0);

write(1, 'partial');
assert.deepEqual(outputs, []);
write(1, ' line\nrest');
assert.deepEqual(outputs, [{ text: 'partial line\n', stream: 'stdout' }]);
write(2, 'warning\n');
assert.deepEqual(outputs.at(-1), { text: 'warning\n', stream: 'stderr' });
wasi.flushAll();
assert.deepEqual(outputs.at(-1), { text: 'rest', stream: 'stdout' });

writeCString(2048, 'waves.vcd');
assert.equal(imports.env.__syscall_openat(-1, 2048, 0), -38);
const descriptor = imports.env.__syscall_openat(-1, 2048, 1);
assert.equal(descriptor, 3);
write(descriptor, 'abcd');
assert.equal(host.fd_seek(descriptor, 1n, 0, 272), 0);
assert.equal(view.getBigUint64(272, true), 1n);
write(descriptor, 'XY');
assert.equal(host.fd_seek(descriptor, -1n, 0, 272), 28);
assert.equal(host.fd_close(descriptor), 0);
assert.equal(files.length, 1);
assert.equal(files[0].name, 'waves.vcd');
assert.equal(decoder.decode(files[0].data), 'aXYd');
assert.equal(host.fd_close(descriptor), 8);
assert.equal(host.fd_write(99, 128n, 1, 256n), 8);

writeCString(2080, 'second.vcd');
const second = imports.env.__syscall_openat(-1, 2080n, 2);
write(second, '$end\n');
wasi.closeAllFiles();
assert.equal(files.length, 2);
assert.equal(files[1].name, 'second.vcd');

write(1, 'before exit');
assert.throws(() => host.proc_exit(9), (error) => {
  assert.ok(error instanceof WasiExit);
  assert.equal(error.code, 9);
  return true;
});
assert.equal(wasi.exitCode, 9);
assert.deepEqual(outputs.at(-1), { text: 'before exit', stream: 'stdout' });

assert.equal(host.fd_read(), 52);
assert.equal(host.path_open(), 52);
assert.equal(host.fd_close(1), 0);

console.log('web WASI host output and file capture OK');
