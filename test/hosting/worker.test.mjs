import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { after, beforeEach, test } from 'node:test';
import worker from '../../utils/cloudflare/worker.mjs';

const cacheEntries = new Map();
const originalCaches = globalThis.caches;
globalThis.caches = { default: {
  async match(key) { return cacheEntries.get(key.url)?.clone(); },
  async put(key, response) { cacheEntries.set(key.url, response); },
} };
after(() => { globalThis.caches = originalCaches; });
beforeEach(() => cacheEntries.clear());

function fixture() {
  const calls = [];
  const objects = new Map();
  function object(key, body) {
    const value = objects.get(key);
    if (!value) return null;
    const result = {
      size: value.size ?? Buffer.byteLength(value.text),
      httpEtag: `"${value.text}"`,
      uploaded: new Date('2026-10-26T00:00:00Z'),
      writeHttpMetadata(headers) { headers.set('Content-Type', value.type ?? 'text/html'); },
      arrayBuffer() { throw new Error('Must stream the object'); },
    };
    if (body) result.body = new Response(value.text).body;
    return result;
  }
  const env = { SITE_RELEASE: 'release-a', SITES: {
    async get(key) { calls.push(['get', key]); return object(key, true); },
    async head(key) { calls.push(['head', key]); return object(key, false); },
  } };
  function add(site, path, text, extra = {}, release = 'release-a') {
    objects.set(`releases/${release}/${site}/${path}`, { text, ...extra });
  }
  async function fetch(path, options = {}, host = 'docs.talos-sv.org') {
    const pending = [];
    const response = await worker.fetch(new Request(`https://${host}${path}`, options), env,
      { waitUntil(promise) { pending.push(promise); } });
    await Promise.all(pending);
    return response;
  }
  return { env, add, fetch, calls };
}

test('hosts select separate R2 prefixes and serve directory indexes', async () => {
  const site = fixture();
  site.add('docs', 'index.html', 'documentation');
  site.add('playground', 'index.html', 'compiler');
  site.add('docs', 'dialects/sdf/index.html', 'sdf');
  assert.equal(await (await site.fetch('/')).text(), 'documentation');
  assert.equal(await (await site.fetch('/', {}, 'playground.talos-sv.org')).text(), 'compiler');
  assert.equal(await (await site.fetch('/dialects/sdf/')).text(), 'sdf');
  const redirect = await site.fetch('/dialects/sdf?query=1');
  assert.equal(redirect.status, 308);
  assert.equal(redirect.headers.get('Location'), 'https://docs.talos-sv.org/dialects/sdf/?query=1');
  assert.equal((await site.fetch('/missing/')).status, 404);
  assert.equal((await site.fetch('/missing')).status, 404);
});

test('apex serves the landing page and shared assets while the playground keeps its index', async () => {
  const site = fixture();
  site.add('playground', 'landing.html', 'landing');
  site.add('playground', 'index.html', 'compiler');
  site.add('playground', 'landing.css', 'styles', { type: 'text/css' });
  site.add('playground', 'diagram.svg', '<svg/>', { type: 'image/svg+xml' });
  for (const path of ['/', '/?campaign=docs', '/index.html']) {
    const response = await site.fetch(path, {}, 'talos-sv.org');
    assert.equal(response.status, 200);
    assert.equal(await response.text(), 'landing');
  }
  assert.equal(await (await site.fetch('/', {}, 'playground.talos-sv.org')).text(), 'compiler');
  assert.equal(await (await site.fetch('/index.html', {}, 'playground.talos-sv.org')).text(), 'compiler');
  const style = await site.fetch('/landing.css', {}, 'talos-sv.org');
  assert.equal(style.headers.get('Content-Type'), 'text/css');
  assert.equal(await style.text(), 'styles');
  assert.equal(await (await site.fetch('/diagram.svg', {}, 'talos-sv.org')).text(), '<svg/>');
  const head = await site.fetch('/', { method: 'HEAD' }, 'talos-sv.org');
  assert.equal(head.status, 200);
  assert.equal(await head.text(), '');
  assert.equal((await site.fetch('/missing', {}, 'talos-sv.org')).status, 404);
});

test('large WASM objects stream with the correct MIME and size', async () => {
  const site = fixture();
  site.add('playground', 'talos.wasm', 'wasm', { type: 'application/octet-stream', size: 61119731 });
  const response = await site.fetch('/talos.wasm', {}, 'playground.talos-sv.org');
  assert.equal(response.headers.get('Content-Type'), 'application/wasm');
  assert.equal(response.headers.get('Content-Length'), '61119731');
  assert.equal(response.headers.get('X-Content-Type-Options'), 'nosniff');
  assert.equal(await response.text(), 'wasm');
});

test('HEAD returns metadata without fetching an R2 body', async () => {
  const site = fixture();
  site.add('docs', 'index.html', 'metadata');
  const head = await site.fetch('/', { method: 'HEAD' });
  assert.equal(head.status, 200);
  assert.equal(head.headers.get('Content-Length'), '8');
  assert.equal(await head.text(), '');
  assert.deepEqual(site.calls, [['head', 'releases/release-a/docs/index.html']]);
});

test('conditional GET and HEAD handle weak ETags, lists, and modification dates', async () => {
  const site = fixture();
  site.add('docs', 'index.html', 'version-a');
  await (await site.fetch('/')).text();
  for (const condition of ['"version-a"', 'W/"version-a"', '"other", "version-a"', '*']) {
    const response = await site.fetch('/', { headers: { 'If-None-Match': condition } });
    assert.equal(response.status, 304);
    assert.equal(response.headers.get('Content-Length'), null);
    assert.equal(await response.text(), '');
  }
  const head = await site.fetch('/', { method: 'HEAD', headers: { 'If-None-Match': '"version-a"' } });
  assert.equal(head.status, 304);
  const modified = await site.fetch('/', { headers: { 'If-None-Match': '"other"' } });
  assert.equal(modified.status, 200);
  assert.equal(await modified.text(), 'version-a');
  const date = await site.fetch('/', { headers: { 'If-Modified-Since': 'Mon, 26 Oct 2026 00:00:00 GMT' } });
  assert.equal(date.status, 304);
  assert.equal(site.calls.filter(([method]) => method === 'get').length, 1);
});

test('release changes invalidate edge cache and browsers revalidate stable asset names', async () => {
  const site = fixture();
  site.add('docs', '_static/theme.css', 'old', { type: 'text/css' });
  site.add('docs', '_static/theme.css', 'new', { type: 'text/css' }, 'release-b');
  assert.equal(await (await site.fetch('/_static/theme.css?v=1')).text(), 'old');
  site.env.SITE_RELEASE = 'release-b';
  const fresh = await site.fetch('/_static/theme.css?v=1', { headers: { 'If-None-Match': '"old"' } });
  assert.equal(fresh.status, 200);
  assert.equal(fresh.headers.get('X-Talos-Release'), 'release-b');
  assert.equal(fresh.headers.get('Cache-Control'), 'public, max-age=0, must-revalidate');
  assert.equal(await fresh.text(), 'new');
  assert.equal(await (await site.fetch('/_static/theme.css?v=2')).text(), 'new');
  assert.equal(site.calls.length, 2);
});

test('reject unknown hosts, writes, unconfigured releases, and unsafe paths', async () => {
  const site = fixture();
  assert.equal((await site.fetch('/', {}, 'other.talos-sv.org')).status, 404);
  const post = await site.fetch('/', { method: 'POST' });
  assert.equal(post.status, 405);
  assert.equal(post.headers.get('Allow'), 'GET, HEAD');
  for (const path of ['/%ZZ', '/%2e%2e%2fplayground/index.html', '/.doctrees/index.doctree', '/x%00', '/x%5cy']) {
    assert.equal((await site.fetch(path)).status, 400, path);
  }
  for (const release of ['', 'unpublished', '../other']) {
    site.env.SITE_RELEASE = release;
    assert.equal((await site.fetch('/')).status, 503);
  }
  assert.equal(site.calls.length, 0);
});

test('Wrangler binds the private sites bucket and all three custom domains', async () => {
  const config = JSON.parse(await readFile(new URL('../../utils/cloudflare/wrangler.json', import.meta.url)));
  assert.equal(config.workers_dev, false);
  assert.deepEqual(config.r2_buckets, [{ binding: 'SITES', bucket_name: 'talos-sites' }]);
  assert.deepEqual(config.routes.map((route) => route.pattern).sort(), ['docs.talos-sv.org', 'playground.talos-sv.org', 'talos-sv.org']);
  assert.ok(config.routes.every((route) => route.custom_domain === true));
});
