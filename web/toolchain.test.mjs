import assert from 'node:assert/strict';

await import('./toolchain.js');

assert.equal(typeof globalThis.installObeliskToolchain, 'function');

const directories = new Set();
const files = new Map();
const loaded = [];
const FS = {
  mkdir(path) {
    if (directories.has(path)) throw new Error('already exists');
    directories.add(path);
  },
  writeFile(path, data) { files.set(path, data); },
};

await globalThis.installObeliskToolchain({ FS }, {
  load: async (name) => {
    loaded.push(name);
    return new TextEncoder().encode(name);
  },
});

assert.deepEqual(loaded, [
  'libobelisk_rt.a', 'libstubs.a', 'libnoexit.a', 'libc.a', 'libdlmalloc.a',
  'libc++.a', 'libc++abi.a', 'libcompiler_rt.a', 'libunwind.a',
]);
const target = '/lib/obelisk/targets/wasm64-unknown-emscripten';
assert.equal(new TextDecoder().decode(files.get(`${target}/libobelisk_rt.a`)),
  'libobelisk_rt.a');
for (const name of loaded.slice(1)) {
  assert.ok(files.has(`/sysroot/lib/wasm64-emscripten/${name}`), name);
}
assert.equal(files.get(`${target}/.complete`), 'web toolchain\n');
assert.ok(directories.has(target));
assert.ok(directories.has('/sysroot/lib/wasm64-emscripten'));

await assert.rejects(
  globalThis.installObeliskToolchain({ FS }, {
    load: async (name) => { throw new Error(`missing ${name}`); },
  }),
  /missing libobelisk_rt\.a/,
);

console.log('web compiler toolchain installation OK');
