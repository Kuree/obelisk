import assert from 'node:assert/strict';

import { loadWaveform, saveWaveform } from './waveform-storage.js';

const records = new Map();
let storeCreated = false;
let closeCount = 0;

function complete(transaction) {
  queueMicrotask(() => transaction.oncomplete?.());
}

const database = {
  objectStoreNames: { contains: () => storeCreated },
  createObjectStore(name) {
    assert.equal(name, 'waveforms');
    storeCreated = true;
  },
  transaction(name, mode) {
    assert.equal(name, 'waveforms');
    assert.ok(mode === 'readonly' || mode === 'readwrite');
    const transaction = {
      objectStore(store) {
        assert.equal(store, 'waveforms');
        return {
          put(record, key) {
            records.set(key, record);
            complete(transaction);
          },
          get(key) {
            const request = {};
            queueMicrotask(() => {
              request.result = records.get(key);
              request.onsuccess?.();
              complete(transaction);
            });
            return request;
          },
        };
      },
    };
    return transaction;
  },
  close() { closeCount++; },
};

globalThis.indexedDB = {
  open(name, version) {
    assert.equal(name, 'obelisk-playground');
    assert.equal(version, 1);
    const request = {};
    queueMicrotask(() => {
      request.result = database;
      if (!storeCreated) request.onupgradeneeded?.();
      request.onsuccess?.();
    });
    return request;
  },
};

assert.equal(await loadWaveform(), null);
await saveWaveform({
  name: 'trace.vcd',
  createdAt: 1234,
  data: new Uint8Array([36, 101, 110, 100, 10]),
});
const loaded = await loadWaveform();
assert.equal(loaded.name, 'trace.vcd');
assert.equal(loaded.createdAt, 1234);
assert.deepEqual([...loaded.data], [36, 101, 110, 100, 10]);
assert.equal(storeCreated, true);
assert.equal(closeCount, 3);

delete globalThis.indexedDB;
await assert.rejects(loadWaveform(), /IndexedDB is unavailable/);
await assert.rejects(
  saveWaveform({ name: 'x.vcd', data: new Uint8Array() }),
  /IndexedDB is unavailable/,
);

console.log('web waveform IndexedDB persistence OK');
