// Compile service for runnable snippets on other websites.
//
// runner.js places this page in a hidden iframe on the embedding site. The
// iframe is same-origin with the compiler assets, so it can start the compiler
// worker that a cross-origin page cannot. Requests arrive over postMessage and
// run one at a time through the same CompilerSession as the playground.
//
// Other sites load whatever is deployed, so the message protocol is a public
// interface: extend it compatibly and bump PROTOCOL only for a breaking change.
//
//   parent -> service  { obelisk, type: 'run', id, source, args, timeoutMs }
//                      { obelisk, type: 'stop', id }
//   service -> parent  { obelisk, type: 'ready' }
//                      { obelisk, type: 'status', id, phase }   loading | compiling | running
//                      { obelisk, type: 'log', id, stream, text }      compiler diagnostics
//                      { obelisk, type: 'output', id, stream, text }   simulation output
//                      { obelisk, type: 'done', id, result, ... }
//
// `result` is one of: exited (with code), aborted (the simulation trapped,
// with error), compile-error (with status), failed (with message), stopped,
// or timeout.

import { CompilerSession } from './compiler-session.js';
import { DEFAULTS, buildArgs } from './options.js';

export const PROTOCOL = 1;

export function createEmbedService({
  session = new CompilerSession(),
  post,
  setTimer = setTimeout,
  clearTimer = clearTimeout,
}) {
  const queue = [];
  let active = null;
  let compilerReady = false;

  const send = (message) => post({ obelisk: PROTOCOL, ...message });

  function finish(request, result) {
    request.finished = true;
    clearTimer(request.timer);
    send({ type: 'done', id: request.id, ...result });
    if (active !== request) return;
    active = null;
    next();
  }

  function onMessage(request, message) {
    const { id } = request;
    switch (message.type) {
      case 'log':
        send({ type: 'log', id, stream: message.stream, text: message.text });
        break;
      case 'compiled':
        if (!message.ok) {
          finish(request, {
            result: message.message ? 'failed' : 'compile-error',
            status: message.status,
            ...(message.message && { message: message.message }),
            compileMs: message.elapsedMs,
          });
          break;
        }
        request.compileMs = message.elapsedMs;
        send({ type: 'status', id, phase: 'running' });
        // Compilation time depends on the reader's device; only the run is
        // bounded, so a slow phone is not mistaken for a design that hangs.
        if (request.timeoutMs > 0) {
          request.timer = setTimer(() => {
            request.timedOut = true;
            request.job.stop();
          }, request.timeoutMs);
        }
        break;
      case 'output':
        send({ type: 'output', id, stream: message.stream, text: message.text });
        break;
      case 'exited':
        finish(request, {
          ...(message.error === undefined
            ? { result: 'exited', code: message.code }
            : { result: 'aborted', error: message.error }),
          compileMs: request.compileMs,
          runMs: message.runMs,
        });
        break;
      case 'failed':
        finish(request, { result: 'failed', message: message.message });
        break;
      case 'stopped':
        finish(request, { result: request.timedOut ? 'timeout' : 'stopped' });
        break;
    }
  }

  function next() {
    if (active || queue.length === 0) return;
    const request = queue.shift();
    active = request;
    send({ type: 'status', id: request.id, phase: compilerReady ? 'compiling' : 'loading' });
    if (!compilerReady) {
      session.preload().then(() => {
        compilerReady = true;
        if (!request.finished) {
          send({ type: 'status', id: request.id, phase: 'compiling' });
        }
      }, () => {
        // The request itself reports the failure.
      });
    }
    request.job = session.run({
      type: 'compile',
      source: request.source,
      args: buildArgs({ ...DEFAULTS, stage: 'run', extra: request.args }),
      stage: 'run',
      kind: 'binary',
    }, (message) => onMessage(request, message));
  }

  function receive(message) {
    if (message?.obelisk !== PROTOCOL) return;
    if (message.type === 'run') {
      if (typeof message.source !== 'string') return;
      queue.push({
        id: message.id,
        source: message.source,
        args: typeof message.args === 'string' ? message.args : '',
        timeoutMs: Number(message.timeoutMs) || 0,
        timer: undefined,
        finished: false,
      });
      next();
    } else if (message.type === 'stop') {
      if (active?.id === message.id) {
        active.job.stop();
        return;
      }
      const index = queue.findIndex((request) => request.id === message.id);
      if (index >= 0) finish(queue.splice(index, 1)[0], { result: 'stopped' });
    }
  }

  return { receive, ready: () => send({ type: 'ready' }) };
}

// Browser entry point. Only the embedding parent may drive the service, and
// replies go back to that parent's origin.
if (typeof window !== 'undefined' && window.parent !== window) {
  let parentOrigin = null;
  const service = createEmbedService({
    post: (message) => window.parent.postMessage(message, parentOrigin ?? '*'),
  });
  window.addEventListener('message', (event) => {
    if (event.source !== window.parent) return;
    parentOrigin ??= event.origin === 'null' ? '*' : event.origin;
    service.receive(event.data);
  });
  service.ready();
}
