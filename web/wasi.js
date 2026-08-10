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
      env: {
        __assert_fail() {
          throw new WebAssembly.RuntimeError('assertion failed in simulation runtime');
        },
        __syscall_openat: () => -WASI_ENOSYS,
        __syscall_fcntl64: () => -WASI_ENOSYS,
        __syscall_ioctl: () => -WASI_ENOSYS,
        emscripten_get_now: () => performance.now(),
        emscripten_date_now: () => Date.now(),
        _tzset_js(timezonePtr, daylightPtr, stdNamePtr, dstNamePtr) {
          const currentYear = new Date().getFullYear();
          const winterOffset = new Date(currentYear, 0, 1).getTimezoneOffset();
          const summerOffset = new Date(currentYear, 6, 1).getTimezoneOffset();
          const standardOffset = Math.max(winterOffset, summerOffset);
          self.view.setBigInt64(Number(timezonePtr), BigInt(standardOffset * 60), true);
          self.view.setInt32(
            Number(daylightPtr), Number(winterOffset !== summerOffset), true,
          );

          const zoneName = (offset) => {
            const sign = offset >= 0 ? '-' : '+';
            const absolute = Math.abs(offset);
            const hours = String(Math.floor(absolute / 60)).padStart(2, '0');
            const minutes = String(absolute % 60).padStart(2, '0');
            return `UTC${sign}${hours}${minutes}`;
          };
          const writeString = (pointer, value) => {
            const bytes = new Uint8Array(self.memory.buffer);
            let offset = Number(pointer);
            for (const byte of new TextEncoder().encode(value)) bytes[offset++] = byte;
            bytes[offset] = 0;
          };
          const winterName = zoneName(winterOffset);
          const summerName = zoneName(summerOffset);
          if (summerOffset < winterOffset) {
            writeString(stdNamePtr, winterName);
            writeString(dstNamePtr, summerName);
          } else {
            writeString(dstNamePtr, winterName);
            writeString(stdNamePtr, summerName);
          }
        },
        _localtime_js(time, tmPtr) {
          const date = new Date(Number(time) * 1000);
          if (Number.isNaN(date.getTime())) return 1;
          const pointer = Number(tmPtr);
          const view = self.view;
          view.setInt32(pointer, date.getSeconds(), true);
          view.setInt32(pointer + 4, date.getMinutes(), true);
          view.setInt32(pointer + 8, date.getHours(), true);
          view.setInt32(pointer + 12, date.getDate(), true);
          view.setInt32(pointer + 16, date.getMonth(), true);
          view.setInt32(pointer + 20, date.getFullYear() - 1900, true);
          view.setInt32(pointer + 24, date.getDay(), true);
          const leap = date.getFullYear() % 4 === 0
            && (date.getFullYear() % 100 !== 0 || date.getFullYear() % 400 === 0);
          const cumulativeDays = leap
            ? [0, 31, 60, 91, 121, 152, 182, 213, 244, 274, 305, 335]
            : [0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334];
          const dayOfYear = cumulativeDays[date.getMonth()] + date.getDate() - 1;
          view.setInt32(pointer + 28, dayOfYear, true);
          const startOfYear = new Date(date.getFullYear(), 0, 1);
          const winterOffset = startOfYear.getTimezoneOffset();
          const summerOffset = new Date(date.getFullYear(), 6, 1).getTimezoneOffset();
          const daylight = winterOffset !== summerOffset
            && date.getTimezoneOffset() === Math.min(winterOffset, summerOffset);
          view.setInt32(pointer + 32, Number(daylight), true);
          view.setBigInt64(pointer + 40, BigInt(-date.getTimezoneOffset() * 60), true);
          return 0;
        },
        _abort_js() {
          throw new WebAssembly.RuntimeError('simulation runtime aborted');
        },
        emscripten_resize_heap(requestedSize) {
          const requested = Number(requestedSize);
          const current = self.memory.buffer.byteLength;
          if (requested <= current) return 1;
          const pages = Math.ceil((requested - current) / 65536);
          try {
            self.memory.grow(BigInt(pages));
            return 1;
          } catch {
            return 0;
          }
        },
      },
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
      instance.exports.main(0, 0n);
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
