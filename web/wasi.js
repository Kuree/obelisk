// Minimal WASI preview1 host for running a compiled Obelisk simulation.
//
// Scope is deliberately narrow: enough for a design to print ($display,
// $write), read the clock, draw randomness, persist coverage and waveforms in
// an in-memory namespace, and exit ($finish). There is no host filesystem.

const WASI_ESUCCESS = 0;
const WASI_EBADF = 8;
const WASI_EINVAL = 28;
const WASI_ENOSYS = 52;
const LINUX_ENOENT = 2;
const LINUX_EEXIST = 17;
const LINUX_ENOTDIR = 20;
const LINUX_EINVAL = 22;
const LINUX_ENOTTY = 25;
const LINUX_ENOSYS = 38;

const LINUX_O_ACCMODE = 3;
const LINUX_O_RDONLY = 0;
const LINUX_O_WRONLY = 1;
const LINUX_O_RDWR = 2;
const LINUX_O_CREAT = 64;
const LINUX_O_EXCL = 128;
const LINUX_O_TRUNC = 512;
const LINUX_O_APPEND = 1024;

class MemoryFileNode {
  constructor(name, initial = new Uint8Array(), dirty = true) {
    this.name = name;
    this.bytes = new Uint8Array(Math.max(4096, initial.byteLength));
    this.bytes.set(initial);
    this.length = initial.byteLength;
    this.dirty = dirty;
  }

  #reserve(required) {
    if (!Number.isSafeInteger(required) || required < 0)
      throw new RangeError('in-memory file is too large');
    if (required <= this.bytes.length) return;
    let capacity = this.bytes.length;
    while (capacity < required) capacity = Math.max(capacity * 2, required);
    const grown = new Uint8Array(capacity);
    grown.set(this.bytes.subarray(0, this.length));
    this.bytes = grown;
  }

  write(position, source) {
    const end = position + source.byteLength;
    this.#reserve(end);
    if (position > this.length) this.bytes.fill(0, this.length, position);
    this.bytes.set(source, position);
    this.length = Math.max(this.length, end);
    this.dirty = true;
    return end;
  }

  truncate() {
    this.length = 0;
    this.dirty = true;
  }

  snapshot() {
    return { name: this.name, data: this.bytes.slice(0, this.length) };
  }
}

class MemoryFileDescriptor {
  constructor(node, { readable, writable, append }) {
    this.node = node;
    this.readable = readable;
    this.writable = writable;
    this.append = append;
    this.position = append ? node.length : 0;
  }

  write(source) {
    if (!this.writable) return false;
    if (this.append) this.position = this.node.length;
    this.position = this.node.write(this.position, source);
    return true;
  }

  read(length) {
    if (!this.readable) return null;
    const end = Math.min(this.node.length, this.position + length);
    const result = this.node.bytes.subarray(this.position, end);
    this.position = end;
    return result;
  }

  seek(offset, whence) {
    let position;
    if (whence === 0) position = offset;
    else if (whence === 1) position = this.position + offset;
    else if (whence === 2) position = this.node.length + offset;
    else return false;
    if (!Number.isSafeInteger(position) || position < 0) return false;
    this.position = position;
    return true;
  }
}

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
   * @param {{name: string, data: BufferSource}[]} options.files input files
   * @param {(text: string, stream: 'stdout'|'stderr') => void} options.onOutput
   * @param {(file: {name: string, data: Uint8Array}) => void} options.onFile
   */
  constructor({
    args = ['sim'], files = [], onOutput = () => {}, onFile = () => {},
  } = {}) {
    if (!Array.isArray(args) ||
        args.some((arg) => typeof arg !== 'string' || arg.includes('\0'))) {
      throw new TypeError('args must be strings without embedded NUL bytes');
    }
    this.encoder = new TextEncoder();
    this.args = [...args];
    this.encodedArgs = this.args.map((arg) => this.encoder.encode(arg));
    this.onOutput = onOutput;
    this.onFile = onFile;
    this.memory = null;
    this.exitCode = null;
    this.decoder = new TextDecoder('utf-8', { fatal: false });
    // stdout/stderr are line-buffered so partial writes do not fragment the
    // rendered output.
    this.buffers = { 1: '', 2: '' };
    this.fileNodes = new Map();
    this.descriptors = new Map();
    this.nextFileDescriptor = 3;
    for (const file of files) {
      if (!file || typeof file.name !== 'string' || !file.name ||
          this.fileNodes.has(file.name)) {
        throw new TypeError('preloaded files require unique nonempty names');
      }
      let data;
      if (file.data instanceof ArrayBuffer) {
        data = new Uint8Array(file.data);
      } else if (ArrayBuffer.isView(file.data)) {
        data = new Uint8Array(
          file.data.buffer, file.data.byteOffset, file.data.byteLength,
        );
      } else {
        throw new TypeError('preloaded file data must be a BufferSource');
      }
      this.fileNodes.set(
        file.name, new MemoryFileNode(file.name, data, false),
      );
    }
  }

  bindMemory(memory) {
    this.memory = memory;
  }

  get view() {
    return new DataView(this.memory.buffer);
  }

  // The module is wasm32, so pointers and WASI sizes are both 32-bit. Offsets
  // into files are the exception: WASI filesize stays 64-bit at either width.
  #readPtr(offset) {
    return this.view.getUint32(offset, true);
  }

  #writeSize(offset, value) {
    this.view.setUint32(offset, value, true);
  }

  #writeFilesize(offset, value) {
    this.view.setBigUint64(offset, BigInt(value), true);
  }

  #readString(pointer) {
    const bytes = new Uint8Array(this.memory.buffer);
    const start = Number(pointer);
    let end = start;
    while (end < bytes.length && bytes[end] !== 0) ++end;
    if (end === bytes.length) throw new RangeError('unterminated path');
    return this.decoder.decode(bytes.subarray(start, end));
  }

  #readPath(pathPointer) {
    let name;
    try {
      name = this.#readString(pathPointer);
    } catch {
      return null;
    }
    return name || null;
  }

  #openFile(pathPointer, flags) {
    const name = this.#readPath(pathPointer);
    if (!name) return -LINUX_EINVAL;
    const access = flags & LINUX_O_ACCMODE;
    if (access !== LINUX_O_RDONLY && access !== LINUX_O_WRONLY &&
        access !== LINUX_O_RDWR) return -LINUX_EINVAL;
    const readable = access === LINUX_O_RDONLY || access === LINUX_O_RDWR;
    const writable = access === LINUX_O_WRONLY || access === LINUX_O_RDWR;
    let node = this.fileNodes.get(name);
    if (node && (flags & LINUX_O_CREAT) && (flags & LINUX_O_EXCL))
      return -LINUX_EEXIST;
    if (!node && !(flags & LINUX_O_CREAT)) return -LINUX_ENOENT;
    if (!node) {
      node = new MemoryFileNode(name);
      this.fileNodes.set(name, node);
    }
    if ((flags & LINUX_O_TRUNC) && !writable) return -LINUX_EINVAL;
    if (flags & LINUX_O_TRUNC) node.truncate();
    const descriptor = this.nextFileDescriptor++;
    this.descriptors.set(descriptor, new MemoryFileDescriptor(node, {
      readable, writable, append: Boolean(flags & LINUX_O_APPEND),
    }));
    return descriptor;
  }

  #closeFile(descriptor) {
    if (!this.descriptors.has(descriptor)) return false;
    this.descriptors.delete(descriptor);
    return true;
  }

  closeAllFiles() {
    this.descriptors.clear();
    for (const node of this.fileNodes.values()) {
      if (!node.dirty) continue;
      this.onFile(node.snapshot());
      node.dirty = false;
    }
  }

  #renameFile(oldPathPointer, newPathPointer) {
    const oldName = this.#readPath(oldPathPointer);
    const newName = this.#readPath(newPathPointer);
    if (!oldName || !newName) return -LINUX_EINVAL;
    const node = this.fileNodes.get(oldName);
    if (!node) return -LINUX_ENOENT;
    if (oldName === newName) return 0;
    this.fileNodes.delete(oldName);
    this.fileNodes.delete(newName);
    node.name = newName;
    node.dirty = true;
    this.fileNodes.set(newName, node);
    return 0;
  }

  #unlinkFile(pathPointer, flags) {
    if (flags !== 0) return -LINUX_EINVAL;
    const name = this.#readPath(pathPointer);
    if (!name) return -LINUX_EINVAL;
    return this.fileNodes.delete(name) ? 0 : -LINUX_ENOENT;
  }

  #removeDirectory(pathPointer) {
    const name = this.#readPath(pathPointer);
    if (!name) return -LINUX_EINVAL;
    return this.fileNodes.has(name) ? -LINUX_ENOTDIR : -LINUX_ENOENT;
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
        __syscall_openat(_directory, path, flags) {
          return self.#openFile(path, flags);
        },
        __syscall_renameat(_oldDirectory, oldPath, _newDirectory, newPath) {
          return self.#renameFile(oldPath, newPath);
        },
        __syscall_unlinkat(_directory, path, flags) {
          return self.#unlinkFile(path, flags);
        },
        __syscall_rmdir(path) {
          return self.#removeDirectory(path);
        },
        __syscall_fcntl64: () => -LINUX_ENOSYS,
        __syscall_ioctl: () => -LINUX_ENOTTY,
        // Emscripten's libc references system() from paths a design never
        // reaches, but the import still has to resolve for the module to
        // instantiate at all. There is no shell in a browser, and system()
        // reports failure as -1 rather than as a negative errno.
        _emscripten_system: () => -1,
        emscripten_get_now: () => performance.now(),
        emscripten_date_now: () => Date.now(),
        _tzset_js(timezonePtr, daylightPtr, stdNamePtr, dstNamePtr) {
          const currentYear = new Date().getFullYear();
          const winterOffset = new Date(currentYear, 0, 1).getTimezoneOffset();
          const summerOffset = new Date(currentYear, 6, 1).getTimezoneOffset();
          const standardOffset = Math.max(winterOffset, summerOffset);
          // `timezone` is a long, which is 32-bit on wasm32.
          self.view.setInt32(Number(timezonePtr), standardOffset * 60, true);
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
          // tm_gmtoff is a long following nine ints, so it sits at 36 on
          // wasm32 rather than at 40.
          view.setInt32(pointer + 36, -date.getTimezoneOffset() * 60, true);
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
            self.memory.grow(pages);
            return 1;
          } catch {
            return 0;
          }
        },
      },
      wasi_snapshot_preview1: {
        fd_write(fd, iovsPtr, iovsLen, nwrittenPtr) {
          const view = self.view;
          const bytes = new Uint8Array(self.memory.buffer);
          let written = 0;
          const file = self.descriptors.get(fd);
          if (fd !== 1 && fd !== 2 && !file) return WASI_EBADF;
          let text = '';
          // Each iovec is {ptr, len}, both 32-bit, so the stride is 8 bytes.
          for (let i = 0; i < Number(iovsLen); i++) {
            const base = Number(iovsPtr) + i * 8;
            const ptr = view.getUint32(base, true);
            const len = view.getUint32(base + 4, true);
            if (len === 0) continue;
            if (file && !file.write(bytes.subarray(ptr, ptr + len)))
              return WASI_EBADF;
            else text += self.decoder.decode(bytes.subarray(ptr, ptr + len));
            written += len;
          }
          if (!file) {
            self.buffers[fd] = (self.buffers[fd] ?? '') + text;
            self.#flush(fd);
          }
          self.#writeSize(Number(nwrittenPtr), written);
          return WASI_ESUCCESS;
        },

        proc_exit(code) {
          self.exitCode = Number(code);
          self.flushAll();
          throw new WasiExit(Number(code));
        },

        args_sizes_get(countPtr, bufSizePtr) {
          const size = self.encodedArgs.reduce(
            (total, argument) => total + argument.byteLength + 1, 0,
          );
          self.#writeSize(Number(countPtr), self.args.length);
          self.#writeSize(Number(bufSizePtr), size);
          return WASI_ESUCCESS;
        },

        args_get(argvPtr, argvBufPtr) {
          const bytes = new Uint8Array(self.memory.buffer);
          let bufOffset = Number(argvBufPtr);
          let ptrOffset = Number(argvPtr);
          for (const arg of self.encodedArgs) {
            self.#writeSize(ptrOffset, bufOffset);
            ptrOffset += 4;
            bytes.set(arg, bufOffset);
            bufOffset += arg.byteLength;
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

        fd_close(fd) {
          if (fd >= 0 && fd <= 2) return WASI_ESUCCESS;
          return self.#closeFile(fd) ? WASI_ESUCCESS : WASI_EBADF;
        },
        fd_fdstat_get: () => WASI_ESUCCESS,
        fd_seek(fd, offset, whence, newOffsetPtr) {
          const file = self.descriptors.get(fd);
          if (!file) return WASI_EBADF;
          const numericOffset = Number(offset);
          if (!Number.isSafeInteger(numericOffset)) return WASI_EINVAL;
          if (!file.seek(numericOffset, Number(whence))) return WASI_EINVAL;
          self.#writeFilesize(Number(newOffsetPtr), file.position);
          return WASI_ESUCCESS;
        },
        fd_read(fd, iovsPtr, iovsLen, nreadPtr) {
          const file = self.descriptors.get(fd);
          if (!file) return WASI_EBADF;
          const view = self.view;
          const bytes = new Uint8Array(self.memory.buffer);
          let read = 0;
          for (let i = 0; i < Number(iovsLen); ++i) {
            const base = Number(iovsPtr) + i * 8;
            const pointer = view.getUint32(base, true);
            const length = view.getUint32(base + 4, true);
            const chunk = file.read(length);
            if (chunk === null) return WASI_EBADF;
            bytes.set(chunk, pointer);
            read += chunk.byteLength;
            if (chunk.byteLength !== length) break;
          }
          self.#writeSize(Number(nreadPtr), read);
          return WASI_ESUCCESS;
        },
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
 * @param {object} options
 * @param {string[]} options.args exact simulation argv including program name
 * @param {{name: string, data: BufferSource}[]} options.files preloaded files
 * @param {(file: {name: string, data: Uint8Array}) => void} options.onFile
 * @returns {Promise<number>} the process exit code
 */
export async function runSimulation(
  wasmBinary, onOutput, {
    args = ['sim'], files = [], onFile = () => {},
    onSimulatedTime = () => {},
  } = {},
) {
  const wasi = new Wasi({ args, files, onOutput, onFile });
  const { instance } = await WebAssembly.instantiate(wasmBinary, wasi.imports);
  wasi.bindMemory(instance.exports.memory);

  // Reported through a callback rather than the return value so the exit code
  // stays the thing runSimulation resolves to. A module built before these
  // exports existed simply never reports.
  const reportSimulatedTime = () => {
    const readTime = instance.exports.obelisk_final_time;
    const readPrecision = instance.exports.obelisk_time_precision_fs;
    if (typeof readTime !== 'function' || typeof readPrecision !== 'function')
      return;
    try {
      onSimulatedTime({
        ticks: BigInt(readTime()),
        precisionFs: BigInt(readPrecision()),
      });
    } catch {
      // A module that trapped on the way out has nothing meaningful to report.
    }
  };

  try {
    if (typeof instance.exports._start === 'function') {
      instance.exports._start();
    } else if (typeof instance.exports.main === 'function') {
      const allocate = instance.exports.malloc;
      const release = instance.exports.free;
      if (typeof allocate !== 'function' || typeof release !== 'function') {
        throw new Error(
          'direct-main simulation module exports neither malloc nor free',
        );
      }
      const pointerBytes = (wasi.encodedArgs.length + 1) * 4;
      const stringBytes = wasi.encodedArgs.reduce(
        (total, argument) => total + argument.byteLength + 1, 0,
      );
      const allocationBytes = pointerBytes + stringBytes;
      if (!Number.isSafeInteger(allocationBytes) ||
          allocationBytes > 0xffffffff) {
        throw new RangeError('simulation argv exceeds wasm32 address space');
      }
      const argv = Number(allocate(allocationBytes)) >>> 0;
      if (argv === 0 ||
          argv + allocationBytes > instance.exports.memory.buffer.byteLength) {
        throw new Error('simulation module could not allocate argv');
      }
      try {
        const memoryBytes = new Uint8Array(instance.exports.memory.buffer);
        const memoryView = new DataView(instance.exports.memory.buffer);
        let stringPointer = argv + pointerBytes;
        wasi.encodedArgs.forEach((argument, index) => {
          memoryView.setUint32(argv + index * 4, stringPointer, true);
          memoryBytes.set(argument, stringPointer);
          stringPointer += argument.byteLength;
          memoryBytes[stringPointer++] = 0;
        });
        memoryView.setUint32(argv + wasi.encodedArgs.length * 4, 0, true);
        const result = Number(
          instance.exports.main(wasi.encodedArgs.length, argv),
        );
        if (!Number.isInteger(result))
          throw new Error('simulation main returned a non-integer status');
        wasi.exitCode = result;
      } finally {
        release(argv);
      }
    } else {
      throw new Error('simulation module exports neither _start nor main');
    }
  } catch (error) {
    if (!(error instanceof WasiExit)) throw error;
    return error.code;
  } finally {
    reportSimulatedTime();
    wasi.flushAll();
    wasi.closeAllFiles();
  }
  return wasi.exitCode ?? 0;
}
