import assert from 'node:assert/strict';

import { LLVM_LANGUAGE_ID, registerLlvm } from './llvm-language.js';
import { MLIR_LANGUAGE_ID, registerMlir } from './mlir-language.js';
import { SV_LANGUAGE_ID, registerSystemVerilog } from './sv-language.js';

function capture(register) {
  const result = { registrations: [], providers: new Map(), configurations: new Map() };
  const monaco = {
    languages: {
      register: (language) => result.registrations.push(language),
      setMonarchTokensProvider: (id, provider) => result.providers.set(id, provider),
      setLanguageConfiguration: (id, configuration) =>
        result.configurations.set(id, configuration),
    },
  };
  register(monaco);
  return result;
}

function hasRule(provider, sample, token) {
  return provider.tokenizer.root.some((rule) => {
    if (!Array.isArray(rule) || rule[1] !== token) return false;
    const expression = rule[0];
    expression.lastIndex = 0;
    const match = expression.exec(sample);
    return match?.index === 0 && match[0] === sample;
  });
}

const sv = capture(registerSystemVerilog);
assert.equal(sv.registrations[0].id, SV_LANGUAGE_ID);
assert.deepEqual(sv.registrations[0].extensions, ['.sv', '.svh', '.v', '.vh']);
const svProvider = sv.providers.get(SV_LANGUAGE_ID);
assert.ok(svProvider.keywords.includes('always_ff'));
assert.ok(svProvider.typeKeywords.includes('logic'));
assert.ok(hasRule(svProvider, '$display', 'predefined'));
assert.ok(hasRule(svProvider, '`timescale', 'keyword.directive'));
assert.ok(hasRule(svProvider, "32'hdead_beef", 'number.hex'));
assert.deepEqual(sv.configurations.get(SV_LANGUAGE_ID).comments.blockComment, ['/*', '*/']);

const mlir = capture(registerMlir);
assert.equal(mlir.registrations[0].id, MLIR_LANGUAGE_ID);
assert.deepEqual(mlir.registrations[0].extensions, ['.mlir']);
const mlirProvider = mlir.providers.get(MLIR_LANGUAGE_ID);
assert.ok(hasRule(mlirProvider, '%value.0', 'variable'));
assert.ok(hasRule(mlirProvider, '^bb3', 'tag'));
assert.ok(hasRule(mlirProvider, '@symbol', 'function'));
assert.ok(hasRule(mlirProvider, '!obelisk_sim.context', 'type'));
assert.ok(hasRule(mlirProvider, '#obelisk_sim.graph', 'annotation'));
assert.ok(hasRule(mlirProvider, 'obelisk_sim.return', 'keyword'));
assert.deepEqual(mlir.configurations.get(MLIR_LANGUAGE_ID).comments, { lineComment: '//' });

const llvm = capture(registerLlvm);
assert.equal(llvm.registrations[0].id, LLVM_LANGUAGE_ID);
assert.deepEqual(llvm.registrations[0].extensions, ['.ll']);
const llvmProvider = llvm.providers.get(LLVM_LANGUAGE_ID);
assert.ok(llvmProvider.keywords.includes('getelementptr'));
assert.ok(llvmProvider.typeKeywords.includes('ptr'));
assert.ok(hasRule(llvmProvider, '%value.0', 'variable'));
assert.ok(hasRule(llvmProvider, '@llvm.memcpy.p0.p0.i64', 'function'));
assert.ok(hasRule(llvmProvider, '!dbg', 'annotation'));
assert.ok(hasRule(llvmProvider, 'i64', 'type'));
assert.ok(hasRule(llvmProvider, '0x7FF8000000000000', 'number.hex'));
assert.deepEqual(llvm.configurations.get(LLVM_LANGUAGE_ID).comments, { lineComment: ';' });

console.log('web Monaco language registrations OK');
