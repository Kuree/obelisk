// Compiles and runs every example design the page offers, through the real
// browser artifacts. Run from the repo as:
//   TALOS_WEB_SITE=site node test/web/examples-test.mjs [-O0|-O3]
//
// The smoke test proves one small design works end to end; this proves the
// designs a visitor can actually click do, which is a much wider slice of the
// language and of the runtime.

import { createRequire } from 'node:module';
import { readFile } from 'node:fs/promises';
import { resolve } from 'node:path';
import { pathToFileURL } from 'node:url';

// The site is the assembled Pages directory: web/ plus the wasm compiler and
// its toolchain archives. Defaults to ./site, where the wasm CI job builds it.
const directory = `${resolve(process.env.TALOS_WEB_SITE ?? 'site')}/`;
const site = pathToFileURL(directory);
const { runSimulation } = await import(new URL('wasi.js', site));
const { EXAMPLES } = await import(new URL('examples.js', site));
await import(new URL('toolchain.js', site));

const require = createRequire(import.meta.url);
const createTalosModule = require(`${directory}talos.js`);
const optimization = process.argv[2] ?? '-O3';

function verifyWaveform(example, files) {
  const waveform = files.find((file) => file.name.endsWith('.vcd'));
  if (example.source.includes('$dumpfile') &&
      (!waveform || waveform.data.byteLength < 100))
    throw new Error('simulation did not capture a waveform');

  const required = {
    'Open-drain arbitration': {
      'open_drain_demo.sda': ['1', '0', 'x', '0', '1'],
      'open_drain_demo.remote_sda': ['1', '0', 'z', 'x', '0', '1'],
    },
    'Switch-level charge': {
      'charge_demo.storage': ['1', '0', 'x', '0'],
    },
  }[example.name];
  if (!required) return;

  // Read value changes by signal name, so X/Z in unrelated nets or header
  // text cannot satisfy the checks. These are ordered subsequences.
  const scopes = [];
  const names = new Map();
  const changes = new Map();
  for (const line of new TextDecoder().decode(waveform.data).split(/\r?\n/)) {
    const fields = line.trim().split(/\s+/);
    if (fields[0] === '$scope') scopes.push(fields[2]);
    else if (fields[0] === '$upscope') scopes.pop();
    else if (fields[0] === '$var') {
      names.set([...scopes, fields[4]].join('.'), fields[3]);
    } else {
      const value = line.match(/^([01xz])(\S+)$/i);
      if (!value) continue;
      const trace = changes.get(value[2]) ?? [];
      trace.push(value[1].toLowerCase());
      changes.set(value[2], trace);
    }
  }
  for (const [name, expected] of Object.entries(required)) {
    const trace = changes.get(names.get(name)) ?? [];
    let matched = 0;
    for (const value of trace)
      if (value === expected[matched]) matched++;
    if (matched !== expected.length)
      throw new Error(`VCD ${name}: expected ${expected.join(' -> ')}, ` +
                      `got ${trace.join(' -> ') || 'no signal'}`);
  }
}

let failures = 0;
for (const example of EXAMPLES) {
  const logs = [];
  const started = Date.now();
  try {
    // LLVM's compiler stack keeps process-global state, so each design gets a
    // fresh module exactly as the UI gives each run a fresh worker.
    const mod = await createTalosModule({
      noInitialRun: true,
      thisProgram: '/bin/talos',
      locateFile: (name) => `${directory}${name}`,
      print: (line) => logs.push(line),
      printErr: (line) => logs.push(line),
    });
    await globalThis.installTalosToolchain(mod, {
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
    if (exitCode !== 0)
      throw new Error(`simulation exited with ${exitCode}: ${output.trim()}`);
    if (!output.includes(`PASS: ${example.name};`))
      throw new Error('simulation did not reach its completion check');
    verifyWaveform(example, files);

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
