// Owns the compiler worker lifecycle for every page that compiles in the
// browser: the playground and the embed service.
//
// LLVM, MLIR, Slang, and wasm LLD all carry process-global state. A
// command-line invocation gets a fresh process, so each worker serves exactly
// one request and is then terminated. Instantiating the driver and installing
// the target archives costs a few hundred milliseconds, so the session keeps
// the next worker warm while the page is idle.
//
// A request streams its worker messages to the caller and ends with exactly
// one final message: `compiled` for a text stage or a failed compile, `exited`
// once a simulation returns, `failed` when the worker itself breaks, or
// `stopped` when the caller stops it.

function isFinal(message) {
  if (message.type === 'compiled') return !message.ok || message.kind !== 'binary';
  return message.type === 'exited' || message.type === 'failed';
}

export class CompilerSession {
  #workerUrl;
  #createWorker;
  #warm = null;
  #job = null;

  constructor({
    workerUrl = new URL('./compiler-worker.js', import.meta.url),
    createWorker = (url) => new Worker(url),
  } = {}) {
    this.#workerUrl = workerUrl;
    this.#createWorker = createWorker;
  }

  get busy() {
    return this.#job !== null;
  }

  /** Resolves once a worker is ready to serve the next request. */
  preload() {
    this.#warm ??= this.#spawn();
    return this.#warm.ready;
  }

  /**
   * Starts a compile request on the warm worker, or a new one. Returns a
   * handle whose stop() terminates the worker, which is the only way to end a
   * simulation that never calls $finish.
   */
  run(request, onMessage) {
    if (this.#job) throw new Error('the compiler is already running a request');
    const entry = this.#warm ?? this.#spawn();
    this.#warm = null;

    let finished = false;
    const finish = (message) => {
      if (finished) return;
      finished = true;
      entry.worker.terminate();
      this.#job = null;
      // Warm first, so a caller that starts its next request from this
      // message gets the warm worker instead of a cold one.
      this.preload().catch(() => {
        // Reported by the next request, which starts its own worker.
      });
      onMessage(message);
    };

    entry.listener = (message) => {
      if (finished) return;
      if (isFinal(message)) finish(message);
      else onMessage(message);
    };
    entry.ready.then(
      () => { if (!finished) entry.worker.postMessage(request); },
      (error) => finish({ type: 'failed', message: error.message }),
    );
    this.#job = { stop: () => finish({ type: 'stopped' }) };
    return this.#job;
  }

  #spawn() {
    const worker = this.#createWorker(this.#workerUrl);
    const entry = { worker, listener: null, ready: null };
    entry.ready = new Promise((resolve, reject) => {
      let started = false;
      worker.onmessage = (event) => {
        const message = event.data;
        if (!started && message.type === 'ready') {
          started = true;
          resolve();
        } else if (!started && message.type === 'failed') {
          reject(new Error(message.message));
        } else {
          entry.listener?.(message);
        }
      };
      worker.onerror = (event) => {
        event.preventDefault?.();
        const reason = event.message ?? 'worker error';
        if (!started) reject(new Error(`The compiler could not start: ${reason}`));
        else entry.listener?.({ type: 'failed', message: `The compiler stopped unexpectedly: ${reason}` });
      };
    });
    entry.ready.catch(() => {
      if (this.#warm !== entry) return;
      this.#warm = null;
      worker.terminate();
    });
    worker.postMessage({ type: 'preload' });
    return entry;
  }
}
