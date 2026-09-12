import assert from 'node:assert/strict';

import { runSimulation, Wasi, WasiExit } from './wasi.js';

const encoder = new TextEncoder();
const decoder = new TextDecoder();
const viewBacking = encoder.encode('xxviewyy');
const outputs = [];
const files = [];
const wasi = new Wasi({
  args: ['sim', '--coverage-test=β'],
  files: [
    { name: 'input.obcov', data: encoder.encode('coverage') },
    { name: 'coverage.obcov', data: encoder.encode('old snapshot') },
    {
      name: 'view.bin',
      data: new DataView(viewBacking.buffer, viewBacking.byteOffset + 2, 4),
    },
  ],
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

function read(fd, length, iovec = 128, data = 3072, nread = 256) {
  setIovec(iovec, data, length);
  assert.equal(host.fd_read(fd, iovec, 1, nread), 0);
  const count = view.getUint32(nread, true);
  return bytes.slice(data, data + count);
}

assert.equal(host.args_sizes_get(16, 24), 0);
assert.equal(view.getUint32(16, true), 2);
const encodedCoverageArgument = encoder.encode('--coverage-test=β');
assert.equal(view.getUint32(24, true), 4 + encodedCoverageArgument.length + 1);
assert.equal(host.args_get(32, 64), 0);
assert.equal(view.getUint32(32, true), 64);
assert.equal(view.getUint32(36, true), 68);
assert.equal(decoder.decode(bytes.subarray(64, 67)), 'sim');
assert.equal(
  decoder.decode(bytes.subarray(68, 68 + encodedCoverageArgument.length)),
  '--coverage-test=β',
);
assert.throws(
  () => new Wasi({ args: ['sim', 'bad\0argument'] }),
  /embedded NUL/,
);

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
assert.equal(imports.env.__syscall_openat(-1, 2048, 0), -2);
const descriptor = imports.env.__syscall_openat(-1, 2048, 705);
assert.equal(descriptor, 3);
assert.equal(imports.env.__syscall_openat(-1, 2048, 705), -17);
write(descriptor, 'abcd');
assert.equal(host.fd_seek(descriptor, 1n, 0, 272), 0);
assert.equal(view.getBigUint64(272, true), 1n);
write(descriptor, 'XY');
assert.equal(host.fd_seek(descriptor, -1n, 0, 272), 28);
assert.equal(host.fd_close(descriptor), 0);
assert.equal(files.length, 0);
assert.equal(host.fd_close(descriptor), 8);
assert.equal(host.fd_write(99, 128n, 1, 256n), 8);

const reopened = imports.env.__syscall_openat(-1, 2048, 0);
assert.equal(decoder.decode(read(reopened, 8)), 'aXYd');
assert.equal(decoder.decode(read(reopened, 8)), '');
assert.equal(host.fd_seek(reopened, 1n, 0, 272), 0);
assert.equal(decoder.decode(read(reopened, 2)), 'XY');
assert.equal(host.fd_write(reopened, 128, 1, 256), 8);
assert.equal(host.fd_close(reopened), 0);

writeCString(2080, 'input.obcov');
const input = imports.env.__syscall_openat(-1, 2080, 0);
assert.equal(decoder.decode(read(input, 32)), 'coverage');
assert.equal(host.fd_close(input), 0);
writeCString(2304, 'view.bin');
const viewInput = imports.env.__syscall_openat(-1, 2304, 0);
assert.equal(decoder.decode(read(viewInput, 16)), 'view');
assert.equal(host.fd_close(viewInput), 0);

writeCString(2256, 'hole.bin');
const stale = imports.env.__syscall_openat(-1, 2256, 577);
write(stale, 'stale');
assert.equal(host.fd_close(stale), 0);
const holeWriter = imports.env.__syscall_openat(-1, 2256, 513);
assert.equal(host.fd_seek(holeWriter, 8n, 0, 272), 0);
write(holeWriter, 'x');
assert.equal(host.fd_close(holeWriter), 0);
const holeReader = imports.env.__syscall_openat(-1, 2256, 0);
assert.deepEqual([...read(holeReader, 16)], [0, 0, 0, 0, 0, 0, 0, 0, 120]);
assert.equal(host.fd_close(holeReader), 0);
assert.equal(imports.env.__syscall_unlinkat(-1, 2256, 0), 0);

writeCString(2112, 'coverage.obcov.tmp.7');
const temporary = imports.env.__syscall_openat(-1, 2112, 705);
write(temporary, 'snapshot');
assert.equal(host.fd_close(temporary), 0);
writeCString(2160, 'coverage.obcov');
assert.equal(imports.env.__syscall_renameat(-1, 2112, -1, 2160), 0);
assert.equal(imports.env.__syscall_renameat(-1, 2112, -1, 2160), -2);

writeCString(2208, 'remove.me');
const removed = imports.env.__syscall_openat(-1, 2208, 577);
write(removed, 'discarded');
assert.equal(host.fd_close(removed), 0);
assert.equal(imports.env.__syscall_rmdir(2208), -20);
assert.equal(imports.env.__syscall_unlinkat(-1, 2208, 0), 0);
assert.equal(imports.env.__syscall_unlinkat(-1, 2208, 0), -2);
assert.equal(imports.env.__syscall_rmdir(2208), -2);

wasi.closeAllFiles();
assert.equal(files.length, 2);
assert.deepEqual(files.map((file) => file.name).sort(), [
  'coverage.obcov', 'waves.vcd',
]);
assert.equal(
  decoder.decode(files.find((file) => file.name === 'waves.vcd').data),
  'aXYd',
);
assert.equal(
  decoder.decode(files.find((file) => file.name === 'coverage.obcov').data),
  'snapshot',
);
assert.equal(files.some((file) => file.name.includes('.tmp.')), false);
assert.equal(files.some((file) => file.name === 'input.obcov'), false);
assert.equal(files.some((file) => file.name === 'remove.me'), false);

write(1, 'before exit');
assert.throws(() => host.proc_exit(9), (error) => {
  assert.ok(error instanceof WasiExit);
  assert.equal(error.code, 9);
  return true;
});
assert.equal(wasi.exitCode, 9);
assert.deepEqual(outputs.at(-1), { text: 'before exit', stream: 'stdout' });

assert.equal(host.fd_read(99, 0, 0, 0), 8);
assert.equal(host.path_open(), 52);
assert.equal(host.fd_close(1), 0);

// A direct-main Obelisk module has no WASI `_start`. This compact fixture
// exports memory, malloc, free, and a main that returns the first UTF-8 byte of
// argv[1], proving runSimulation marshals argv and preserves main's status.
const directMainModule = new Uint8Array([
  0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00,
  0x01, 0x10, 0x03,
  0x60, 0x01, 0x7f, 0x01, 0x7f,
  0x60, 0x01, 0x7f, 0x00,
  0x60, 0x02, 0x7f, 0x7f, 0x01, 0x7f,
  0x03, 0x04, 0x03, 0x00, 0x01, 0x02,
  0x05, 0x03, 0x01, 0x00, 0x01,
  0x07, 0x21, 0x04,
  0x06, 0x6d, 0x65, 0x6d, 0x6f, 0x72, 0x79, 0x02, 0x00,
  0x06, 0x6d, 0x61, 0x6c, 0x6c, 0x6f, 0x63, 0x00, 0x00,
  0x04, 0x66, 0x72, 0x65, 0x65, 0x00, 0x01,
  0x04, 0x6d, 0x61, 0x69, 0x6e, 0x00, 0x02,
  0x0a, 0x18, 0x03,
  0x05, 0x00, 0x41, 0x80, 0x08, 0x0b,
  0x02, 0x00, 0x0b,
  0x0d, 0x00, 0x20, 0x01, 0x41, 0x04, 0x6a,
  0x28, 0x02, 0x00, 0x2d, 0x00, 0x00, 0x0b,
]);
assert.equal(
  await runSimulation(directMainModule, () => {}, { args: ['sim', 'β'] }),
  0xce,
);

console.log('web WASI host output and file capture OK');
