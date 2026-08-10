// End-to-end smoke test for the generated web artifacts. Run from the repo as:
//   node web/smoke-test.mjs

import { createRequire } from 'node:module';
import { readFile } from 'node:fs/promises';
import { fileURLToPath } from 'node:url';

import { runSimulation } from './wasi.js';
import './toolchain.js';

const directory = fileURLToPath(new URL('.', import.meta.url));
const require = createRequire(import.meta.url);
const createObeliskModule = require('./obelisk.js');
const logs = [];
let phase = 'loading the compiler';

try {
  const source = `
module web_smoke;
  initial begin
    #1;
    $display("wasm-web-ok");
  end
endmodule
`;

  // The UI creates a fresh worker for every invocation. Exercise the same
  // boundary here: LLVM's compiler stack contains process-global state and is
  // not safely reusable through repeated Emscripten callMain() calls.
  for (const optimization of ['-O3', '-O0']) {
    logs.length = 0;
    phase = `loading the compiler for ${optimization}`;
    const mod = await createObeliskModule({
      noInitialRun: true,
      thisProgram: '/bin/obelisk',
      locateFile: (name) => `${directory}${name}`,
      print: (line) => logs.push(line),
      printErr: (line) => logs.push(line),
    });

    phase = `installing the toolchain for ${optimization}`;
    await globalThis.installObeliskToolchain(mod, {
      load: (name) => readFile(`${directory}toolchain/${name}`),
    });
    mod.FS.mkdir('/work');
    mod.FS.writeFile('/work/design.sv', source);

    phase = `compiling the design at ${optimization}`;
    const status = mod.callMain([
      '--compile-threads=1', '--sysroot=/sysroot', '--target=wasm64',
      optimization, '-o', '/work/design.wasm', '/work/design.sv',
    ]) ?? 0;
    if (status !== 0) {
      throw new Error(
        `compiler exited with ${status} at ${optimization}: ${logs.join('\n')}`,
      );
    }

    phase = `running the ${optimization} design`;
    const binary = mod.FS.readFile('/work/design.wasm');
    let output = '';
    const exitCode = await runSimulation(binary, (text) => { output += text; });
    if (exitCode !== 0)
      throw new Error(`simulation exited with ${exitCode} at ${optimization}`);
    if (!output.includes('wasm-web-ok')) {
      throw new Error(
        `unexpected ${optimization} simulation output: ${JSON.stringify(output)}`,
      );
    }
  }
  console.log('wasm-web-ok (-O3 then -O0)');
} catch (error) {
  if (logs.length) console.error(logs.join('\n'));
  console.error(`web smoke test failed while ${phase}`);
  throw error;
}
