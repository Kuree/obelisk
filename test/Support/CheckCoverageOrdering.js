'use strict';

const assert = require('assert');
const fs = require('fs');
const vm = require('vm');

// Execute the production ordering helpers without rendering a browser view.
const source = fs.readFileSync(process.argv[2], 'utf8');
const helpers = source.slice(0, source.indexOf('// Filters shared by every view.'));
const context = vm.createContext({
  document: {getElementById: () => ({textContent: '{}'})}
});
vm.runInContext(helpers, context);
const evaluate = code => vm.runInContext(code, context, {timeout: 1000});

assert.strictEqual(evaluate(`
  linearOrder(['u9', 'u2', 'u10', 'u0002', 'U1'], nameKey).join(',')
`), 'U1,u2,u0002,u9,u10');

assert.strictEqual(evaluate(`
  linearOrder(['18446744073709551615', '9007199254740993', '1',
               '9007199254740992', '0'], ordinalKey).join(',')
`), '0,1,9007199254740992,9007199254740993,18446744073709551615');

assert.strictEqual(evaluate(`
  linearOrder([-10, 1.25, 0, 10, 1.5, 3.2], x => numericKey(-x)).join(',')
`), '10,3.2,1.5,1.25,0,-10');

// Identical names retain source order, including numeric leading-zero ties.
assert.strictEqual(evaluate(`
  linearOrder([{name: 'a2', id: 1}, {name: 'b', id: 2},
               {name: 'a02', id: 3}, {name: 'a2', id: 4}],
              x => nameKey(x.name)).map(x => x.id).join(',')
`), '1,3,4,2');

// Composite fields distinguish prefixes, embedded zeroes, and high code units.
assert.strictEqual(evaluate(`
  linearOrder([{name: 'a\\u0001', line: 0, id: 1},
               {name: 'a', line: 2, id: 2},
               {name: 'a\\u0000', line: 0, id: 3},
               {name: 'a', line: 1, id: 4},
               {name: 'a\\uffff', line: 0, id: 5}],
              x => compositeKey(nameKey(x.name), ordinalKey(x.line)))
    .map(x => x.id).join(',')
`), '4,2,3,1,5');

assert.strictEqual(evaluate(`
  (() => {
    const prefix = 'shared'.repeat(128);
    const rows = Array.from({length: 32}, (_, i) => prefix + (32 - i));
    const ordered = linearOrder(rows, nameKey);
    return ordered.map(x => x.slice(prefix.length)).join(',');
  })()
`), Array.from({length: 32}, (_, i) => i + 1).join(','));

assert.strictEqual(evaluate(`
  (() => {
    const long = 'z' + 'x'.repeat(8192);
    const rows = ['a2', long, '', 'a1'];
    return linearOrder(rows, nameKey).map(x => rows.indexOf(x)).join(',');
  })()
`), '2,3,0,1');

assert.strictEqual(evaluate(`linearOrder([], nameKey).length`), 0);
assert.strictEqual(evaluate(`linearOrder(['only'], nameKey)[0]`), 'only');

context.selectorNodes = new Map();
context.selectorOperands = new Map();
context.selectorBindings = new Map();
context.templateItems = new Map([['cp', 'point']]);
context.templateBins = new Map([['bin', 'selected']]);
context.tupleSetText = () => '<selected>';
context.valueText = () => '0x2';
vm.runInContext(source.slice(source.indexOf('const selectorText ='),
                            source.indexOf('const binName =')), context);

assert.strictEqual(evaluate(`
  selectorNodes.set('leaf', {kind: 1, target: 'cp', bin: 'bin'});
  selectorNodes.set('not', {kind: 2});
  selectorNodes.set('root', {kind: 3});
  selectorOperands.set('not', ['leaf']);
  selectorOperands.set('root', ['leaf', 'not']);
  selectorText({type: '1', configuration: '0'}, {id: '2'}, 'root')
`), '(binsof(point.selected) && !(binsof(point.selected)))');

assert.strictEqual(evaluate(`
  selectorOperands.set('not', ['not']);
  selectorText({type: '1', configuration: '0'}, {id: '2'}, 'not')
`), '!(<cycle>)');

assert.strictEqual(evaluate(`
  (() => {
    selectorNodes.clear(); selectorOperands.clear();
    for (let id = 0; id < 1024; ++id) {
      selectorNodes.set(String(id), {kind: 2});
      selectorOperands.set(String(id), [String(id + 1)]);
    }
    const text = selectorText({type: '1', configuration: '0'}, {id: '2'}, '0');
    return text.includes('...') && text.length <= 2051;
  })()
`), true);

assert.strictEqual(evaluate(`
  selectorNodes.set('wide', {kind: 3});
  selectorOperands.set('wide', Array.from({length: 2048}, () => 'missing'));
  selectorText({type: '1', configuration: '0'}, {id: '2'}, 'wide').length <= 2051
`), true);

assert.strictEqual(evaluate(`
  selectorNodes.set('long', {kind: 1, target: 'long-name', bin: '0'});
  templateItems.set('long-name', 'x'.repeat(8192));
  selectorText({type: '1', configuration: '0'}, {id: '2'}, 'long').length <= 2051
`), true);
