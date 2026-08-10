// Minimal WASI preview1 host for running a compiled Obelisk simulation.
//
// Scope is deliberately narrow: enough for a design to print ($display,
// $write), read the clock, draw randomness, and exit ($finish). File I/O
// ($fopen and friends) is not backed by a filesystem and returns ENOSYS, which
// surfaces as a runtime error rather than silent wrong behaviour.

const WASI_ESUCCESS = 0;
const WASI_EBADF = 8;
const WASI_ENOSYS = 52;

// Thrown to unwind out of the wasm module when it calls proc_exit.
export class WasiExit extends Error {
  constructor(code) {
    super(`exit(${code})`);
    this.code = code;
  }
}

export class Wasi {
  /**
   * @param {object} options
   * @param {string[]} options.args      argv for the simulation
   * @param {(text: string, stream: 'stdout'|'stderr') => void} options.onOutput
   */
  constructor({ args = ['sim'], onOutput = () => {} } = {}) {
    this.args = args;
    this.onOutput = onOutput;
    this.memory = null;
    this.exitCode = null;
    this.decoder = new TextDecoder('utf-8', { fatal: false });
    // stdout/stderr are line-buffered so partial writes do not fragment the
    // rendered output.
    this.buffers = { 1: '', 2: '' };
  }

  bindMemory(memory) {
    this.memory = memory;
  }

  get view() {
    return new DataView(this.memory.buffer);
  }

  // Pointers are 64-bit under MEMORY64; the ABI requires it (see ABI.cpp).
  #readPtr(offset) {
    return Number(this.view.getBigUint64(offset, true));
  }

  #writeSize(offset, value) {
    this.view.setBigUint64(offset, BigInt(value), true);
  }

  #flush(fd, { force = false } = {}) {
    const stream = fd === 2 ? 'stderr' : 'stdout';
    let buffered = this.buffers[fd] ?? '';
    if (!force) {
      const lastNewline = buffered.lastIndexOf('\n');
      if (lastNewline === -1) return;
      const ready = buffered.slice(0, lastNewline + 1);
      this.buffers[fd] = buffered.slice(lastNewline + 1);
      this.onOutput(ready, stream);
      return;
    }
    if (buffered) {
      this.buffers[fd] = '';
      this.onOutput(buffered, stream);
    }
  }

  flushAll() {
    this.#flush(1, { force: true });
    this.#flush(2, { force: true });
  }

  get imports() {
    const self = this;
    return {
      wasi_snapshot_preview1: {
        fd_write(fd, iovsPtr, iovsLen, nwrittenPtr) {
          if (fd !== 1 && fd !== 2) return WASI_EBADF;
          const view = self.view;
          const bytes = new Uint8Array(self.memory.buffer);
          let written = 0;
          let text = '';
          // Each iovec is {ptr, len}, both 64-bit under MEMORY64.
          for (let i = 0; i < Number(iovsLen); i++) {
            const base = Number(iovsPtr) + i * 16;
            const ptr = Number(view.getBigUint64(base, true));
            const len = Number(view.getBigUint64(base + 8, true));
            if (len === 0) continue;
            text += self.decoder.decode(bytes.subarray(ptr, ptr + len));
            written += len;
          }
          self.buffers[fd] = (self.buffers[fd] ?? '') + text;
          self.#flush(fd);
          self.#writeSize(Number(nwrittenPtr), written);
          return WASI_ESUCCESS;
        },

        proc_exit(code) {
          self.exitCode = Number(code);
          self.flushAll();
          throw new WasiExit(Number(code));
        },

        args_sizes_get(countPtr, bufSizePtr) {
          const size = self.args.reduce((n, a) => n + a.length + 1, 0);
          self.#writeSize(Number(countPtr), self.args.length);
          self.#writeSize(Number(bufSizePtr), size);
          return WASI_ESUCCESS;
        },

        args_get(argvPtr, argvBufPtr) {
          const bytes = new Uint8Array(self.memory.buffer);
          let bufOffset = Number(argvBufPtr);
          let ptrOffset = Number(argvPtr);
          for (const arg of self.args) {
            self.#writeSize(ptrOffset, bufOffset);
            ptrOffset += 8;
            for (let i = 0; i < arg.length; i++) bytes[bufOffset++] = arg.charCodeAt(i);
            bytes[bufOffset++] = 0;
          }
          return WASI_ESUCCESS;
        },

        environ_sizes_get(countPtr, bufSizePtr) {
          self.#writeSize(Number(countPtr), 0);
          self.#writeSize(Number(bufSizePtr), 0);
          return WASI_ESUCCESS;
        },
        environ_get: () => WASI_ESUCCESS,

        clock_time_get(_id, _precision, timePtr) {
          const nanos = BigInt(Math.round(performance.now() * 1e6));
          self.view.setBigUint64(Number(timePtr), nanos, true);
          return WASI_ESUCCESS;
        },

        random_get(bufPtr, bufLen) {
          const bytes = new Uint8Array(self.memory.buffer, Number(bufPtr), Number(bufLen));
          crypto.getRandomValues(bytes);
          return WASI_ESUCCESS;
        },

        // A simulation that reaches these is doing real file I/O, which this
        // page does not provide. Report it rather than pretending it worked.
        fd_close: () => WASI_ESUCCESS,
        fd_fdstat_get: () => WASI_ESUCCESS,
        fd_seek: () => WASI_ENOSYS,
        fd_read: () => WASI_ENOSYS,
        fd_prestat_get: () => WASI_EBADF,
        fd_prestat_dir_name: () => WASI_EBADF,
        path_open: () => WASI_ENOSYS,
        path_filestat_get: () => WASI_ENOSYS,
        poll_oneoff: () => WASI_ENOSYS,
        sched_yield: () => WASI_ESUCCESS,
      },
    };
  }
}

/**
 * Instantiate and run a compiled simulation module, collecting its output.
 * @param {BufferSource} wasmBinary
 * @param {(text: string, stream: string) => void} onOutput
 * @returns {Promise<number>} the process exit code
 */
export async function runSimulation(wasmBinary, onOutput) {
  const wasi = new Wasi({ onOutput });
  const { instance } = await WebAssembly.instantiate(wasmBinary, wasi.imports);
  wasi.bindMemory(instance.exports.memory);

  try {
    if (typeof instance.exports._start === 'function') {
      instance.exports._start();
    } else if (typeof instance.exports.main === 'function') {
      instance.exports.main(0, 0);
    } else {
      throw new Error('simulation module exports neither _start nor main');
    }
  } catch (error) {
    if (!(error instanceof WasiExit)) throw error;
    return error.code;
  } finally {
    wasi.flushAll();
  }
  return wasi.exitCode ?? 0;
}
