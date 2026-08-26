// Compiles and runs every example design the page offers, through the real
// browser artifacts. Run from the assembled site as:
//   node site/examples-test.mjs [-O0|-O3]
//
// The smoke test proves one small design works end to end; this proves the
// designs a visitor can actually click do, which is a much wider slice of the
// language and of the runtime.

import { createRequire } from 'node:module';
import { readFile } from 'node:fs/promises';
import { fileURLToPath } from 'node:url';

import { runSimulation } from './wasi.js';
import { EXAMPLES } from './examples.js';
import './toolchain.js';

const directory = fileURLToPath(new URL('.', import.meta.url));
const require = createRequire(import.meta.url);
const createObeliskModule = require('./obelisk.js');
const optimization = process.argv[2] ?? '-O3';

let failures = 0;
for (const example of EXAMPLES) {
  const logs = [];
  const started = Date.now();
  try {
    // LLVM's compiler stack keeps process-global state, so each design gets a
    // fresh module exactly as the UI gives each run a fresh worker.
    const mod = await createObeliskModule({
      noInitialRun: true,
      thisProgram: '/bin/obelisk',
      locateFile: (name) => `${directory}${name}`,
      print: (line) => logs.push(line),
      printErr: (line) => logs.push(line),
    });
    await globalThis.installObeliskToolchain(mod, {
      load: (name) => readFile(`${directory}toolchain/${name}`),
    });
    mod.FS.mkdir('/work');
    mod.FS.writeFile('/work/design.sv', example.source);

    const status = mod.callMain([
      '--compile-threads=1', '--sysroot=/sysroot', '--target=wasm32',
      optimization, '-o', '/work/design.wasm', '/work/design.sv',
    ]) ?? 0;
    if (status !== 0) throw new Error(`compiler exited with ${status}`);

    const binary = mod.FS.readFile('/work/design.wasm');
    let output = '';
    const files = [];
    let simulated = null;
    const exitCode = await runSimulation(
      binary, (text) => { output += text; },
      { onFile: (file) => files.push(file),
        onSimulatedTime: (time) => { simulated = time; } });
    if (exitCode !== 0) throw new Error(`simulation exited with ${exitCode}`);
    if (!output.trim()) throw new Error('simulation produced no output');

    const elapsed = ((Date.now() - started) / 1000).toFixed(1);
    const first = output.trim().split('\n')[0].slice(0, 40);
    const time = simulated
      ? `${simulated.ticks * simulated.precisionFs} fs`
      : 'NO SIMULATED TIME';
    console.log(`ok       ${example.name.padEnd(26)} ${elapsed}s  ` +
                `${time.padEnd(18)} ${JSON.stringify(first)}`);
  } catch (error) {
    failures++;
    console.log(`FAILED   ${example.name.padEnd(26)} ${error.message}`);
    const detail = logs.join('\n');
    if (detail) console.log(detail.split('\n').slice(0, 12).join('\n'));
  }
}

console.log(`\n${EXAMPLES.length - failures}/${EXAMPLES.length} examples ` +
            `passed at ${optimization}`);
process.exit(failures ? 1 : 0);
