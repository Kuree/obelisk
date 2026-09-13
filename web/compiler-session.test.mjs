import assert from 'node:assert/strict';

import { CompilerSession } from './compiler-session.js';

class FakeWorker {
  constructor(url) {
    this.url = url;
    this.posted = [];
    this.terminated = false;
    this.onmessage = null;
    this.onerror = null;
  }

  postMessage(message) { this.posted.push(message); }
  terminate() { this.terminated = true; }
  emit(message) { this.onmessage({ data: message }); }
  fail(message) { this.onerror({ message, preventDefault() {} }); }
}

const settle = () => new Promise((resolve) => setImmediate(resolve));

let workers = [];
const session = new CompilerSession({
  workerUrl: 'https://example.test/compiler-worker.js',
  createWorker: (url) => {
    const worker = new FakeWorker(url);
    workers.push(worker);
    return worker;
  },
});

// Preload starts one worker and resolves when it reports ready.
const preloaded = session.preload();
assert.equal(workers.length, 1);
assert.equal(session.preload(), preloaded);
assert.equal(workers[0].url, 'https://example.test/compiler-worker.js');
assert.deepEqual(workers[0].posted, [{ type: 'preload' }]);
workers[0].emit({ type: 'ready' });
await preloaded;

// A text request uses the warm worker, streams logs, and ends on `compiled`.
let received = [];
const text = { type: 'compile', source: 'module m; endmodule', kind: 'text' };
session.run(text, (message) => received.push(message));
assert.equal(session.busy, true);
assert.throws(() => session.run(text, () => {}), /already running/);
await settle();
assert.equal(workers.length, 1);
assert.deepEqual(workers[0].posted.at(-1), text);
workers[0].emit({ type: 'log', stream: 'stderr', text: 'warning\n' });
workers[0].emit({ type: 'compiled', ok: true, kind: 'text', text: 'module {}' });
assert.deepEqual(received.map((message) => message.type), ['log', 'compiled']);
assert.equal(workers[0].terminated, true);
assert.equal(session.busy, false);
// The next worker starts warming as soon as the request ends.
assert.equal(workers.length, 2);
workers[0].emit({ type: 'log', stream: 'stderr', text: 'late\n' });
assert.equal(received.length, 2);

// A run request continues past a successful link and ends on `exited`. It is
// queued until the warming worker is ready.
received = [];
const binary = { type: 'compile', source: 'module m; endmodule', kind: 'binary' };
session.run(binary, (message) => received.push(message));
await settle();
assert.deepEqual(workers[1].posted, [{ type: 'preload' }]);
workers[1].emit({ type: 'ready' });
await settle();
assert.deepEqual(workers[1].posted.at(-1), binary);
workers[1].emit({ type: 'compiled', ok: true, kind: 'binary', byteLength: 4 });
assert.equal(workers[1].terminated, false);
workers[1].emit({ type: 'output', stream: 'stdout', text: 'hello\n' });
workers[1].emit({ type: 'exited', code: 0, files: [] });
assert.deepEqual(received.map((message) => message.type), ['compiled', 'output', 'exited']);
assert.equal(workers[1].terminated, true);

// A failed compile ends the request without running anything.
received = [];
workers[2].emit({ type: 'ready' });
session.run(binary, (message) => received.push(message));
await settle();
workers[2].emit({ type: 'compiled', ok: false, kind: 'binary', status: 1 });
assert.deepEqual(received.map((message) => message.type), ['compiled']);
assert.equal(workers[2].terminated, true);

// Stopping terminates the worker once and ignores anything it already queued.
received = [];
workers[3].emit({ type: 'ready' });
const job = session.run(binary, (message) => received.push(message));
await settle();
workers[3].emit({ type: 'compiled', ok: true, kind: 'binary', byteLength: 4 });
job.stop();
job.stop();
workers[3].emit({ type: 'output', stream: 'stdout', text: 'late\n' });
assert.deepEqual(received.map((message) => message.type), ['compiled', 'stopped']);
assert.equal(workers[3].terminated, true);
assert.equal(session.busy, false);

// A worker that fails while loading rejects preload and is discarded, so the
// next request starts a fresh worker instead of reusing a broken one.
const failing = session.preload();
workers[4].emit({ type: 'failed', message: 'failed to load libc.a: HTTP 404' });
await assert.rejects(failing, /HTTP 404/);
assert.equal(workers[4].terminated, true);
received = [];
session.run(binary, (message) => received.push(message));
assert.equal(workers.length, 6);
workers[5].fail('script error');
await settle();
assert.deepEqual(received, [
  { type: 'failed', message: 'The compiler could not start: script error' },
]);
assert.equal(workers[5].terminated, true);

// A worker that breaks after it started reports through the request.
received = [];
workers[6].emit({ type: 'ready' });
session.run(binary, (message) => received.push(message));
await settle();
workers[6].fail('out of memory');
assert.deepEqual(received, [
  { type: 'failed', message: 'The compiler stopped unexpectedly: out of memory' },
]);

// A request started from another request's final message reuses the worker
// that began warming for it rather than starting a cold one.
received = [];
const chained = [];
const count = workers.length;
session.run(binary, (message) => {
  received.push(message);
  if (message.type === 'exited') session.run(text, (next) => chained.push(next));
});
workers.at(-1).emit({ type: 'ready' });
await settle();
workers.at(-1).emit({ type: 'exited', code: 0, files: [] });
assert.equal(workers.length, count + 1);
workers.at(-1).emit({ type: 'ready' });
await settle();
assert.deepEqual(workers.at(-1).posted.at(-1), text);

// Stopping before the worker is ready never sends the request, and the
// worker's late readiness is ignored.
received = [];
workers = [];
const cold = new CompilerSession({
  createWorker: (url) => {
    const worker = new FakeWorker(url);
    workers.push(worker);
    return worker;
  },
});
const early = cold.run(binary, (message) => received.push(message));
early.stop();
assert.deepEqual(received, [{ type: 'stopped' }]);
assert.equal(workers[0].terminated, true);
assert.deepEqual(workers[0].posted, [{ type: 'preload' }]);
workers[0].emit({ type: 'ready' });
await settle();
assert.deepEqual(workers[0].posted, [{ type: 'preload' }]);
assert.deepEqual(received, [{ type: 'stopped' }]);

// A handle from a finished request cannot stop the next one.
received = [];
workers[1].emit({ type: 'ready' });
const next = cold.run(binary, (message) => received.push(message));
await settle();
early.stop();
assert.equal(workers[1].terminated, false);
assert.equal(cold.busy, true);
next.stop();
assert.equal(workers[1].terminated, true);
assert.deepEqual(received, [{ type: 'stopped' }]);
// And stopping a finished request does nothing either.
next.stop();
assert.deepEqual(received, [{ type: 'stopped' }]);

// A load failure reported as a message fails a request already waiting on
// that worker, and the next request gets a fresh worker.
received = [];
cold.run(binary, (message) => received.push(message));
workers[2].emit({ type: 'failed', message: 'failed to load libc.a: HTTP 404' });
await settle();
assert.deepEqual(received, [{ type: 'failed', message: 'failed to load libc.a: HTTP 404' }]);
assert.equal(workers[2].terminated, true);
assert.equal(cold.busy, false);
assert.equal(workers.length, 4);
assert.deepEqual(workers[3].posted, [{ type: 'preload' }]);

// Errors from a terminated worker after its request finished are ignored.
workers[2].fail('late error');
workers[1].emit({ type: 'exited', code: 0 });
assert.equal(received.length, 1);

// The default worker URL is compiler-worker.js next to the session module.
let defaultUrl = null;
new CompilerSession({
  createWorker: (url) => {
    defaultUrl = url;
    return new FakeWorker(url);
  },
}).preload();
assert.equal(String(defaultUrl), new URL('./compiler-worker.js', import.meta.url).href);

console.log('web compiler session worker lifecycle OK');
