// Installs the target files used by the compiler's in-process wasm linker.
// This is a classic script so compiler-worker.js can load it with
// importScripts; the smoke test uses the same global entry point under Node.

(() => {
  const TARGET = '/lib/obelisk/targets/wasm64-unknown-emscripten';
  const ASSETS = [
    ['libobelisk_rt.a', `${TARGET}/libobelisk_rt.a`],
    ['libstubs.a', '/sysroot/lib/wasm64-emscripten/libstubs.a'],
    ['libnoexit.a', '/sysroot/lib/wasm64-emscripten/libnoexit.a'],
    ['libc.a', '/sysroot/lib/wasm64-emscripten/libc.a'],
    ['libdlmalloc.a', '/sysroot/lib/wasm64-emscripten/libdlmalloc.a'],
    ['libc++.a', '/sysroot/lib/wasm64-emscripten/libc++.a'],
    ['libc++abi.a', '/sysroot/lib/wasm64-emscripten/libc++abi.a'],
    ['libcompiler_rt.a', '/sysroot/lib/wasm64-emscripten/libcompiler_rt.a'],
    ['libunwind.a', '/sysroot/lib/wasm64-emscripten/libunwind.a'],
  ];

  function ensureDirectory(FS, path) {
    let current = '';
    for (const component of path.split('/').filter(Boolean)) {
      current += `/${component}`;
      try { FS.mkdir(current); } catch { /* already exists */ }
    }
  }

  async function defaultLoad(name) {
    const response = await fetch(new URL(`./toolchain/${name}`, self.location.href));
    if (!response.ok) throw new Error(`failed to load ${name}: HTTP ${response.status}`);
    return new Uint8Array(await response.arrayBuffer());
  }

  globalThis.installObeliskToolchain = async function installObeliskToolchain(
    mod, { load = defaultLoad } = {},
  ) {
    for (const [name, path] of ASSETS) {
      ensureDirectory(mod.FS, path.slice(0, path.lastIndexOf('/')));
      mod.FS.writeFile(path, await load(name));
    }
    mod.FS.writeFile(`${TARGET}/.complete`, 'web toolchain\n');
  };
})();
