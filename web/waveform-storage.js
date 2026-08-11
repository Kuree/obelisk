const DATABASE = 'obelisk-playground';
const STORE = 'waveforms';
const LATEST = 'latest';

function openDatabase() {
  return new Promise((resolve, reject) => {
    if (!globalThis.indexedDB) {
      reject(new Error('IndexedDB is unavailable'));
      return;
    }
    const request = indexedDB.open(DATABASE, 1);
    request.onupgradeneeded = () => {
      if (!request.result.objectStoreNames.contains(STORE))
        request.result.createObjectStore(STORE);
    };
    request.onsuccess = () => resolve(request.result);
    request.onerror = () => reject(request.error ?? new Error('could not open IndexedDB'));
  });
}

function requestResult(request) {
  return new Promise((resolve, reject) => {
    request.onsuccess = () => resolve(request.result);
    request.onerror = () => reject(request.error ?? new Error('IndexedDB request failed'));
  });
}

function transactionDone(transaction) {
  return new Promise((resolve, reject) => {
    transaction.oncomplete = () => resolve();
    transaction.onabort = () => reject(transaction.error ?? new Error('IndexedDB transaction aborted'));
    transaction.onerror = () => reject(transaction.error ?? new Error('IndexedDB transaction failed'));
  });
}

export async function saveWaveform({ name, data, createdAt = Date.now() }) {
  const database = await openDatabase();
  try {
    const transaction = database.transaction(STORE, 'readwrite');
    const done = transactionDone(transaction);
    transaction.objectStore(STORE).put({
      name,
      createdAt,
      blob: new Blob([data], { type: 'text/x-vcd' }),
    }, LATEST);
    await done;
  } finally {
    database.close();
  }
}

export async function loadWaveform() {
  const database = await openDatabase();
  try {
    const transaction = database.transaction(STORE, 'readonly');
    const done = transactionDone(transaction);
    const record = await requestResult(transaction.objectStore(STORE).get(LATEST));
    await done;
    if (!record) return null;
    return {
      name: record.name,
      createdAt: record.createdAt,
      data: new Uint8Array(await record.blob.arrayBuffer()),
    };
  } finally {
    database.close();
  }
}
