import assert from 'node:assert/strict';

import { EXAMPLES } from './examples.js';
import {
  DEFAULTS, INPUT_FILE, buildArgs, formatCommand, loadState, saveState,
  splitFlags, toPermalink,
} from './options.js';
import { DEFAULT_STAGE, STAGES, findStage } from './stages.js';

assert.deepEqual(splitFlags(`-DFOO='a b' plain "two words" x=y`), [
  '-DFOO=a b', 'plain', 'two words', 'x=y',
]);
assert.deepEqual(splitFlags('  A=1   B="two words"  EMPTY="" '), [
  'A=1', 'B=two words', 'EMPTY=',
]);

assert.deepEqual(buildArgs(DEFAULTS), ['--std=1800-2023', '-O3']);
assert.deepEqual(buildArgs({
  ...DEFAULTS,
  stage: 'schedule',
  std: '1800-2017',
  opt: '-O0',
  top: ' chip ',
  timescale: '1ns/1ps',
  defines: 'WIDTH=8 NAME="a b"',
  extra: '--error-limit=2 "-Xslang=one two"',
}), [
  '-emit-schedule', '--mlir-print-debuginfo', '--std=1800-2017', '-O0', '--top=chip',
  '--timescale=1ns/1ps', '-DWIDTH=8', '-DNAME=a b', '--error-limit=2',
  '-Xslang=one two',
]);
assert.deepEqual(buildArgs({
  ...DEFAULTS,
  stage: 'run',
  tier: 'bytecode',
  scheduler: 'aot',
  specialization: 'off',
}), [
  '--std=1800-2023', '-O3', '--execution-tier=bytecode',
  '--native-scheduler=aot', '--static-specialization=off',
]);
// Driver-only execution options must not leak into an IR emission command.
assert.deepEqual(buildArgs({
  ...DEFAULTS, stage: 'sim', tier: 'bytecode', scheduler: 'aot',
}), ['-emit-sim', '--std=1800-2023', '-O3']);
assert.equal(formatCommand({ ...DEFAULTS, stage: 'run', extra: '"a b"' }),
  `obelisk --std=1800-2023 -O3 'a b' ${INPUT_FILE}`);

assert.equal(DEFAULT_STAGE, 'run');
assert.equal(findStage('missing').id, DEFAULT_STAGE);
assert.deepEqual(STAGES.map((stage) => stage.id), [
  'preprocess', 'slang', 'obelisk', 'sim', 'schedule', 'llvm', 'run', 'waveform',
]);
assert.equal(findStage('schedule').language, 'mlir');
assert.equal(findStage('preprocess').language, 'systemverilog');
assert.equal(findStage('llvm').language, 'llvm');
assert.equal(findStage('waveform').kind, 'waveform');

const storage = new Map();
globalThis.localStorage = {
  getItem: (key) => storage.get(key) ?? null,
  setItem: (key, value) => storage.set(key, value),
};
globalThis.window = { location: new URL('https://example.test/playground/') };

const sharedOptions = { ...DEFAULTS, stage: 'schedule', top: 'μ_top' };
const permalink = toPermalink('module μ; endmodule', sharedOptions);
window.location = new URL(permalink);
assert.deepEqual(loadState(), {
  source: 'module μ; endmodule',
  options: sharedOptions,
});

window.location = new URL('https://example.test/playground/');
saveState('module saved; endmodule', { ...DEFAULTS, stage: 'sim' });
assert.deepEqual(loadState(), {
  source: 'module saved; endmodule',
  options: { ...DEFAULTS, stage: 'sim' },
});
window.location.hash = '#not-valid-base64';
assert.equal(loadState().source, 'module saved; endmodule');

assert.ok(EXAMPLES.length >= 4);
assert.equal(new Set(EXAMPLES.map((example) => example.name)).size, EXAMPLES.length);
for (const example of EXAMPLES) {
  assert.ok(example.name);
  assert.match(example.source, /\bmodule\b/);
  assert.match(example.source, /\bendmodule\b/);
  assert.doesNotMatch(example.source, /Obelisk compiles|ahead of time to WebAssembly/i);
}
const rv32im = EXAMPLES.find((example) => example.name === 'RV32IM core');
assert.ok(rv32im);
for (const instruction of [
  'LUI', 'AUIPC', 'JAL', 'JALR', 'BEQ', 'BNE', 'BLT', 'BGE', 'BLTU', 'BGEU',
  'LB', 'LH', 'LW', 'LBU', 'LHU', 'SB', 'SH', 'SW',
  'ADDI', 'SLTI', 'SLTIU', 'XORI', 'ORI', 'ANDI', 'SLLI', 'SRLI', 'SRAI',
  'ADD', 'SUB', 'SLL', 'SLT', 'SLTU', 'XOR', 'SRL', 'SRA', 'OR', 'AND',
  'MUL', 'MULH', 'MULHSU', 'MULHU', 'DIV', 'DIVU', 'REM', 'REMU',
]) assert.match(rv32im.source, new RegExp(`\\b${instruction}\\b`));
assert.match(rv32im.source, /result\s*==\s*1\s*&&\s*memory\[32\]\s*==\s*40/);
assert.ok(EXAMPLES.some((example) => example.name === 'UART transmitter'));
const tristate = EXAMPLES.find((example) => example.name === 'Tri-state buffer');
assert.ok(tristate);
assert.match(tristate.source, /module\s+tri_state_buffer/);
assert.equal((tristate.source.match(/tri_state_buffer\s+buffer_/g) ?? []).length, 2);
const randomization = EXAMPLES.find((example) => example.name === 'Randomization');
assert.doesNotMatch(randomization.source, /Packet\s+p\s*=\s*new/);

console.log('web options, stages, persistence, and examples OK');
