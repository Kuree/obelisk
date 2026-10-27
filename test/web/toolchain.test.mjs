// REQUIRES: node
// RUN: %node --experimental-default-type=module %s

import assert from 'node:assert/strict';

await import('../../web/toolchain.js');

assert.equal(typeof globalThis.installTalosToolchain, 'function');

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

await globalThis.installTalosToolchain({ FS }, {
  load: async (name) => {
    loaded.push(name);
    return new TextEncoder().encode(name);
  },
});

assert.deepEqual(loaded, [
  'libtalos_rt.a', 'libstubs.a', 'libnoexit.a', 'libc.a', 'libdlmalloc.a',
  'libc++.a', 'libc++abi.a', 'libcompiler_rt.a', 'libunwind.a',
]);
const target = '/lib/talos/targets/wasm32-unknown-emscripten';
assert.equal(new TextDecoder().decode(files.get(`${target}/libtalos_rt.a`)),
  'libtalos_rt.a');
for (const name of loaded.slice(1)) {
  assert.ok(files.has(`/sysroot/lib/wasm32-emscripten/${name}`), name);
}
assert.equal(files.get(`${target}/.complete`), 'web toolchain\n');
assert.ok(directories.has(target));
assert.ok(directories.has('/sysroot/lib/wasm32-emscripten'));

await assert.rejects(
  globalThis.installTalosToolchain({ FS }, {
    load: async (name) => { throw new Error(`missing ${name}`); },
  }),
  /missing libtalos_rt\.a/,
);

console.log('web compiler toolchain installation OK');
