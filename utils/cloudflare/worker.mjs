// Both sites live in a private R2 bucket. Deployments select a complete release
// after all objects have uploaded; cache keys include that release as well.
const sites = new Map([
  ['docs.talos-sv.org', 'docs'],
  ['playground.talos-sv.org', 'playground'],
]);
const browserCache = 'public, max-age=0, must-revalidate';

function message(request, status, text, headers = {}) {
  return new Response(request.method === 'HEAD' ? null : text, {
    status,
    headers: { 'Content-Type': 'text/plain; charset=utf-8', 'Cache-Control': 'no-store', ...headers },
  });
}

function pathFor(url) {
  const parts = url.pathname.split('/').slice(1).map(decodeURIComponent);
  if (parts.some((part) => part.startsWith('.') || /[\\/\x00-\x1f\x7f]/.test(part))) {
    return null;
  }
  return parts.join('/');
}

function notModified(request, headers) {
  const condition = request.headers.get('If-None-Match');
  if (condition !== null) {
    return condition.split(',').some((tag) => {
      tag = tag.trim();
      return tag === '*' || tag.replace(/^W\//, '') === headers.get('ETag');
    });
  }
  const since = Date.parse(request.headers.get('If-Modified-Since'));
  return Number.isFinite(since) && since >= Date.parse(headers.get('Last-Modified'));
}

function forBrowser(request, response) {
  const headers = new Headers(response.headers);
  headers.set('Cache-Control', browserCache);
  if (notModified(request, headers)) {
    response.body?.cancel();
    headers.delete('Content-Length');
    return new Response(null, { status: 304, headers });
  }
  if (request.method === 'HEAD') {
    response.body?.cancel();
    return new Response(null, { status: response.status, headers });
  }
  return new Response(response.body, { status: response.status, headers });
}

export default {
  async fetch(request, env, context) {
    const url = new URL(request.url);
    const site = sites.get(url.hostname);
    if (!site) return message(request, 404, 'Not found');
    if (request.method !== 'GET' && request.method !== 'HEAD') {
      return message(request, 405, 'Method not allowed', { Allow: 'GET, HEAD' });
    }
    const release = env.SITE_RELEASE;
    if (!release || release === 'unpublished' || !/^[a-zA-Z0-9][a-zA-Z0-9._-]{0,127}$/.test(release)) {
      return message(request, 503, 'No site release is configured');
    }
    let path;
    try { path = pathFor(url); } catch { return message(request, 400, 'Invalid path'); }
    if (path === null) return message(request, 400, 'Invalid path');
    const prefix = `releases/${release}/${site}/`;
    if (path && !path.endsWith('/') && !path.split('/').at(-1).includes('.')) {
      if (!await env.SITES.head(`${prefix}${path}/index.html`)) return message(request, 404, 'Not found');
      url.pathname += '/';
      return new Response(null, { status: 308, headers: { Location: url.href } });
    }
    if (!path || path.endsWith('/')) path += 'index.html';

    // Ignore query strings in the internal cache. Each Worker deployment has
    // its own object namespace, so a new release never serves an old asset.
    const cacheURL = new URL(url.origin);
    cacheURL.pathname = `/__talos_releases/${release}/${site}/${path}`;
    const cacheKey = new Request(cacheURL, { method: 'GET' });
    const cache = globalThis.caches?.default;
    const cached = cache && await cache.match(cacheKey);
    if (cached) return forBrowser(request, cached);

    const object = request.method === 'HEAD'
      ? await env.SITES.head(prefix + path)
      : await env.SITES.get(prefix + path);
    if (!object) return message(request, 404, 'Not found');
    const headers = new Headers();
    object.writeHttpMetadata(headers);
    if (path.endsWith('.wasm')) headers.set('Content-Type', 'application/wasm');
    if (!headers.has('Content-Type')) headers.set('Content-Type', 'application/octet-stream');
    headers.set('Content-Length', String(object.size));
    headers.set('ETag', object.httpEtag);
    headers.set('Last-Modified', object.uploaded.toUTCString());
    headers.set('X-Content-Type-Options', 'nosniff');
    headers.set('X-Talos-Release', release);
    headers.set('Cache-Control', 'public, max-age=86400');
    // Pass the R2 stream through without buffering large compiler binaries.
    const response = new Response(request.method === 'HEAD' ? null : object.body, { headers });
    if (cache && request.method === 'GET') {
      context.waitUntil(cache.put(cacheKey, response.clone()).catch((error) => console.error('Site cache write failed', error)));
    }
    return forBrowser(request, response);
  },
};
