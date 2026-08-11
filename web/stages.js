// The compiler's actual pipeline, used as the inspector's tab strip.
//
// Order follows DESIGN.md: source → slang → obelisk → sim → schedule → LLVM →
// run. Running is the terminus, so it is both the last tab and the default
// one -- the page's job is to run your design; the IR stages are there when
// you want to look inside. A waveform view belongs after Run because it is a
// product of running, not another way of compiling.

export const STAGES = [
  {
    id: 'preprocess',
    label: 'Preprocess',
    flag: '-E',
    kind: 'text',
    language: 'systemverilog',
    blurb: 'Macro-expanded source, before parsing.',
  },
  {
    id: 'slang',
    label: 'Slang IR',
    flag: '-emit-slang',
    kind: 'text',
    language: 'mlir',
    blurb: 'Elaborated semantic AST, one op per slang dispatch kind.',
  },
  {
    id: 'obelisk',
    label: 'Obelisk IR',
    flag: '-emit-obelisk',
    kind: 'text',
    language: 'mlir',
    blurb: 'Typed obelisk.sv dialect after conversion from slang.',
  },
  {
    id: 'sim',
    label: 'Sim IR',
    flag: '-emit-sim',
    kind: 'text',
    language: 'mlir',
    blurb: 'Isolated simulation SSA after the lowering pipeline.',
  },
  {
    id: 'schedule',
    label: 'Schedule',
    flag: '-emit-schedule',
    kind: 'text',
    language: 'mlir',
    blurb: 'The derived compute graph and generated schedule.',
  },
  {
    id: 'llvm',
    label: 'LLVM IR',
    flag: '-emit-llvm',
    kind: 'text',
    language: 'llvm',
    blurb: 'LLVM IR handed to the WebAssembly backend.',
  },
  {
    id: 'run',
    label: 'Run',
    flag: null,
    kind: 'binary',
    blurb: 'Compile to WebAssembly and execute.',
  },
  {
    id: 'waveform',
    label: 'Waveform',
    flag: null,
    kind: 'waveform',
    blurb: 'Latest VCD captured locally from a run, displayed with Surfer.',
  },
];

/** Running is the point of the page, so it is what you land on. */
export const DEFAULT_STAGE = 'run';

export function findStage(id) {
  return STAGES.find((stage) => stage.id === id) ?? findStage(DEFAULT_STAGE);
}
