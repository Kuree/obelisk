// REQUIRES: node
// RUN: %node --experimental-default-type=module %s

import assert from 'node:assert/strict';

import { PROTOCOL, createEmbedService } from '../../web/embed.js';

const settle = () => new Promise((resolve) => setImmediate(resolve));

class FakeSession {
  constructor() {
    this.requests = [];
    this.stopped = 0;
    this.preloads = [];
  }

  preload() {
    return new Promise((resolve, reject) => this.preloads.push({ resolve, reject }));
  }

  run(request, onMessage) {
    const entry = { request, onMessage, done: false };
    const finish = (message) => {
      if (entry.done) return;
      entry.done = true;
      onMessage(message);
    };
    entry.emit = (message) => {
      const final = message.type === 'exited' || message.type === 'failed' ||
        (message.type === 'compiled' && !message.ok);
      if (final) finish(message);
      else onMessage(message);
    };
    this.requests.push(entry);
    return { stop: () => { this.stopped++; finish({ type: 'stopped' }); } };
  }
}

const sent = [];
const timers = [];
const session = new FakeSession();
const service = createEmbedService({
  session,
  post: (message) => sent.push(message),
  setTimer: (callback, ms) => timers.push({ callback, ms, cleared: false }) - 1,
  clearTimer: (index) => { if (timers[index]) timers[index].cleared = true; },
});
const drain = () => sent.splice(0);

service.ready();
assert.deepEqual(drain(), [{ obelisk: PROTOCOL, type: 'ready' }]);

// Messages without the protocol marker, or with another version, are ignored.
service.receive({ type: 'run', id: 1, source: 'module m; endmodule' });
service.receive({ obelisk: PROTOCOL + 1, type: 'run', id: 1, source: '' });
service.receive(null);
assert.equal(session.requests.length, 0);

// A run compiles with the playground defaults plus the snippet's flags, which
// come last so they override them.
service.receive({
  obelisk: PROTOCOL, type: 'run', id: 1, source: 'module a; endmodule',
  args: '-O0 --top=a', timeoutMs: 5000,
});
assert.equal(session.requests.length, 1);
const first = session.requests[0];
assert.deepEqual(first.request, {
  type: 'compile',
  source: 'module a; endmodule',
  args: ['--std=1800-2023', '-O3', '-O0', '--top=a'],
  stage: 'run',
  kind: 'binary',
});
assert.deepEqual(drain(), [{ obelisk: PROTOCOL, type: 'status', id: 1, phase: 'loading' }]);
session.preloads[0].resolve();
await settle();
assert.deepEqual(drain(), [{ obelisk: PROTOCOL, type: 'status', id: 1, phase: 'compiling' }]);

// A second request waits for the first.
service.receive({ obelisk: PROTOCOL, type: 'run', id: 2, source: 'module b; endmodule' });
service.receive({ obelisk: PROTOCOL, type: 'run', id: 3, source: 'module c; endmodule' });
assert.equal(session.requests.length, 1);

first.emit({ type: 'log', stream: 'stderr', text: 'design.sv:1:1: warning: w\n' });
first.emit({ type: 'compiled', ok: true, kind: 'binary', elapsedMs: 120, byteLength: 10 });
assert.equal(timers.length, 1);
assert.equal(timers[0].ms, 5000);
first.emit({ type: 'output', stream: 'stdout', text: 'hello\n' });
first.emit({ type: 'exited', code: 0, runMs: 7, files: [] });
assert.deepEqual(drain(), [
  { obelisk: PROTOCOL, type: 'log', id: 1, stream: 'stderr', text: 'design.sv:1:1: warning: w\n' },
  { obelisk: PROTOCOL, type: 'status', id: 1, phase: 'running' },
  { obelisk: PROTOCOL, type: 'output', id: 1, stream: 'stdout', text: 'hello\n' },
  { obelisk: PROTOCOL, type: 'done', id: 1, result: 'exited', code: 0, compileMs: 120, runMs: 7 },
  { obelisk: PROTOCOL, type: 'status', id: 2, phase: 'compiling' },
]);
assert.equal(timers[0].cleared, true);

// Stopping a queued request never starts it.
service.receive({ obelisk: PROTOCOL, type: 'stop', id: 3 });
assert.deepEqual(drain(), [{ obelisk: PROTOCOL, type: 'done', id: 3, result: 'stopped' }]);

// A compile error ends the request with the driver's status.
const second = session.requests[1];
assert.equal(second.request.source, 'module b; endmodule');
assert.deepEqual(second.request.args, ['--std=1800-2023', '-O3']);
second.emit({ type: 'compiled', ok: false, kind: 'binary', status: 1, elapsedMs: 30 });
assert.deepEqual(drain(), [
  { obelisk: PROTOCOL, type: 'done', id: 2, result: 'compile-error', status: 1, compileMs: 30 },
]);
assert.equal(session.requests.length, 2);

// The timeout bounds only the simulation and reports itself distinctly from a
// reader pressing Stop.
service.receive({
  obelisk: PROTOCOL, type: 'run', id: 4, source: 'module d; endmodule', timeoutMs: 100,
});
const fourth = session.requests[2];
fourth.emit({ type: 'compiled', ok: true, kind: 'binary', elapsedMs: 1 });
timers.at(-1).callback();
assert.equal(session.stopped, 1);
assert.deepEqual(drain().at(-1), { obelisk: PROTOCOL, type: 'done', id: 4, result: 'timeout' });

service.receive({ obelisk: PROTOCOL, type: 'run', id: 5, source: '', timeoutMs: 0 });
const fifth = session.requests[3];
fifth.emit({ type: 'compiled', ok: true, kind: 'binary', elapsedMs: 1 });
assert.equal(timers.length, 2);
service.receive({ obelisk: PROTOCOL, type: 'stop', id: 5 });
assert.deepEqual(drain().at(-1), { obelisk: PROTOCOL, type: 'done', id: 5, result: 'stopped' });

// A trapped simulation and a broken worker are both reported, and the queue
// keeps going.
service.receive({ obelisk: PROTOCOL, type: 'run', id: 6, source: '' });
service.receive({ obelisk: PROTOCOL, type: 'run', id: 7, source: '' });
session.requests[4].emit({ type: 'compiled', ok: true, kind: 'binary', elapsedMs: 2 });
session.requests[4].emit({ type: 'exited', code: null, error: 'unreachable', runMs: 1 });
session.requests[5].emit({ type: 'failed', message: 'The compiler could not start: boom' });
const results = drain().filter((message) => message.type === 'done');
assert.deepEqual(results, [
  { obelisk: PROTOCOL, type: 'done', id: 6, result: 'aborted', error: 'unreachable', compileMs: 2, runMs: 1 },
  { obelisk: PROTOCOL, type: 'done', id: 7, result: 'failed', message: 'The compiler could not start: boom' },
]);

console.log('web embed service protocol OK');
