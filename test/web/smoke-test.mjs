// End-to-end smoke test for the generated web artifacts. Run from the repo as:
//   OBELISK_WEB_SITE=site node test/web/smoke-test.mjs

import { createRequire } from 'node:module';
import { readFile } from 'node:fs/promises';
import { resolve } from 'node:path';
import { pathToFileURL } from 'node:url';

// The site is the assembled Pages directory: web/ plus the wasm compiler and
// its toolchain archives. Defaults to ./site, where the wasm CI job builds it.
const directory = `${resolve(process.env.OBELISK_WEB_SITE ?? 'site')}/`;
const site = pathToFileURL(directory);
const { runSimulation } = await import(new URL('wasi.js', site));
await import(new URL('toolchain.js', site));

const require = createRequire(import.meta.url);
const createObeliskModule = require(`${directory}obelisk.js`);
const logs = [];
let phase = 'loading the compiler';

try {
  const source = `
module web_smoke;
  logic value = 0;
  logic other = 0;

  covergroup cg;
    value_point: coverpoint value {
      bins low = {0};
      bins high = {1};
      bins round_trip = (0 => 1 => 0);
    }
    other_point: coverpoint other;
    product: cross value_point, other_point;
  endgroup

  cg coverage;
  initial begin
    int covered, total;
    real percentage;
    coverage = new;
    $dumpfile("web-smoke.vcd");
    $dumpvars(0, web_smoke);
    coverage.sample();
    #1;
    value = 1;
    other = 1;
    coverage.sample();
    #1;
    value = 0;
    coverage.sample();
    percentage = coverage.get_inst_coverage(covered, total);
    $display("wasm-web-ok coverage=%.2f bins=%0d/%0d",
             percentage, covered, total);
  end
endmodule
`;

  const verifyCoverageImage = (file, label) => {
    if (!file) throw new Error(`no coverage snapshot captured at ${label}`);
    const expectedMagic = [79, 66, 67, 79, 86, 13, 10, 26];
    if (file.data.byteLength < 12 ||
        !expectedMagic.every((byte, index) => file.data[index] === byte)) {
      throw new Error(`invalid coverage magic at ${label}`);
    }
    const view = new DataView(
      file.data.buffer, file.data.byteOffset, file.data.byteLength,
    );
    if (view.getUint32(8, true) !== 1)
      throw new Error(`coverage image at ${label} is not exact v1`);
  };

  // The unit tests mock the compiler worker so they stay runnable before the
  // wasm artifact exists. This artifact smoke test additionally verifies that
  // the real browser compiler carries the schedule provenance consumed by the
  // clickable graph nodes.
  phase = 'loading the compiler for schedule provenance';
  const scheduleModule = await createObeliskModule({
    noInitialRun: true,
    thisProgram: '/bin/obelisk',
    locateFile: (name) => `${directory}${name}`,
    print: (line) => logs.push(line),
    printErr: (line) => logs.push(line),
  });
  phase = 'installing the toolchain for schedule provenance';
  await globalThis.installObeliskToolchain(scheduleModule, {
    load: (name) => readFile(`${directory}toolchain/${name}`),
  });
  scheduleModule.FS.mkdir('/work');
  scheduleModule.FS.writeFile('/work/design.sv', source);
  phase = 'emitting schedule provenance';
  const scheduleStatus = scheduleModule.callMain([
    '--compile-threads=1', '-emit-schedule', '--mlir-print-debuginfo',
    '-o', '/work/design.schedule', '/work/design.sv',
  ]) ?? 0;
  const schedule = scheduleModule.FS.readFile(
    '/work/design.schedule', { encoding: 'utf8' },
  );
  if (scheduleStatus !== 0 || !schedule.includes('source_locations = [') ||
      !schedule.includes('design.sv":')) {
    throw new Error(`missing schedule source provenance: ${schedule}`);
  }

  // The UI creates a fresh worker for every invocation. Exercise the same
  // boundary here: LLVM's compiler stack contains process-global state and is
  // not safely reusable through repeated Emscripten callMain() calls.
  const configurations = [
    { name: '-O3', arguments: ['-O3'] },
    { name: '-O0', arguments: ['-O0'] },
    {
      name: '-O3 bytecode',
      arguments: ['-O3', '--execution-tier=bytecode'],
    },
    {
      name: '-O0 bytecode',
      arguments: ['-O0', '--execution-tier=bytecode'],
    },
  ];
  for (const configuration of configurations) {
    const optimization = configuration.name;
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
      '--compile-threads=1', '--sysroot=/sysroot', '--target=wasm32',
      '--coverage', ...configuration.arguments,
      '-o', '/work/design.wasm', '/work/design.sv',
    ]) ?? 0;
    if (status !== 0) {
      throw new Error(
        `compiler exited with ${status} at ${optimization}: ${logs.join('\n')}`,
      );
    }

    phase = `running the ${optimization} design`;
    const binary = mod.FS.readFile('/work/design.wasm');
    let output = '';
    const files = [];
    const exitCode = await runSimulation(binary, (text) => { output += text; }, {
      args: [
        'sim', '--coverage-output=coverage.obcov',
        `--coverage-test=web-${optimization.replaceAll(' ', '-')}`,
      ],
      onFile: (file) => files.push(file),
    });
    if (exitCode !== 0)
      throw new Error(`simulation exited with ${exitCode} at ${optimization}`);
    if (!output.includes('wasm-web-ok')) {
      throw new Error(
        `unexpected ${optimization} simulation output: ${JSON.stringify(output)}`,
      );
    }
    const waveform = files.find((file) => file.name === 'web-smoke.vcd');
    if (!waveform) throw new Error(`no VCD captured at ${optimization}`);
    const vcd = new TextDecoder().decode(waveform.data);
    if (!vcd.includes('$enddefinitions $end') || !vcd.includes('#1')) {
      throw new Error(`invalid VCD captured at ${optimization}: ${vcd}`);
    }
    if (files.some((file) => file.name.includes('.tmp.')))
      throw new Error(`temporary file leaked at ${optimization}`);
    const snapshot = files.find((file) => file.name === 'coverage.obcov');
    verifyCoverageImage(snapshot, optimization);

    phase = `loading the ${optimization} coverage snapshot`;
    const loadedFiles = [];
    const loadedExitCode = await runSimulation(binary, () => {}, {
      args: [
        'sim', '--coverage-load=coverage.obcov',
        '--coverage-output=coverage-loaded.obcov',
        `--coverage-test=web-loaded-${optimization.replaceAll(' ', '-')}`,
      ],
      files: [snapshot],
      onFile: (file) => loadedFiles.push(file),
    });
    if (loadedExitCode !== 0) {
      throw new Error(
        `loaded simulation exited with ${loadedExitCode} at ${optimization}`,
      );
    }
    if (loadedFiles.some((file) => file.name.includes('.tmp.')))
      throw new Error(`loaded temporary file leaked at ${optimization}`);
    verifyCoverageImage(
      loadedFiles.find((file) => file.name === 'coverage-loaded.obcov'),
      `${optimization} loaded`,
    );
  }
  console.log('wasm-web-ok (native and bytecode O0/O3 coverage round trip)');
} catch (error) {
  if (logs.length) console.error(logs.join('\n'));
  console.error(`web smoke test failed while ${phase}`);
  throw error;
}
