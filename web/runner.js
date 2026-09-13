// Makes SystemVerilog code blocks on any website runnable.
//
//   <script src="https://keyizhang.com/obelisk/runner.js" defer></script>
//
// Every `pre > code.language-systemverilog` (or `language-sv`) block gets a
// Run button and an output panel. Script attributes configure the page:
//
//   data-selector  CSS selector for runnable code elements
//   data-args      compiler flags added to every block, e.g. "-O0"
//   data-timeout   seconds a simulation may run before it is stopped (default
//                  60; 0 disables)
//
// A block can add `data-obelisk-args` for its own flags and
// `data-obelisk-timeout` for its own limit, or opt out with
// `data-obelisk="off"`, on either the <code> or its <pre>. Pages that render
// code after load call `ObeliskRunner.scan(root)`.
//
// This is a classic script so a plain <script src> works on any site. The
// compiler runs in a hidden iframe served next to this file (embed.html),
// created on the first click so readers who never press Run download nothing.

(() => {
  if (window.ObeliskRunner) return;

  const script = document.currentScript;
  const base = new URL('.', script?.src ?? location.href);
  const serviceUrl = new URL('embed.html', base);
  const PROTOCOL = 1;
  const SERVICE_LOAD_TIMEOUT_MS = 30000;
  const OUTPUT_LIMIT = 200000;

  const config = {
    selector: script?.dataset.selector ||
      'pre > code.language-systemverilog, pre > code.language-sv',
    args: script?.dataset.args ?? '',
    timeoutMs: 1000 * (script?.dataset.timeout === undefined
      ? 60 : Number(script.dataset.timeout) || 0),
  };

  /* ------------------------------------------------------ compile service */

  const runs = new Map();
  let nextId = 1;
  let service = null;

  function startService() {
    if (service) return service;
    const iframe = document.createElement('iframe');
    iframe.src = serviceUrl.href;
    iframe.title = 'Obelisk compile service';
    iframe.setAttribute('aria-hidden', 'true');
    iframe.tabIndex = -1;
    iframe.style.cssText =
      'position:absolute;width:1px;height:1px;border:0;opacity:0;pointer-events:none;';

    let resolveReady;
    let rejectReady;
    const ready = new Promise((resolve, reject) => {
      resolveReady = resolve;
      rejectReady = reject;
    });
    const timer = setTimeout(() => rejectReady(new Error(
      `could not load ${serviceUrl.origin}. ` +
      'If this site sets a Content-Security-Policy, it must allow ' +
      `frame-src ${serviceUrl.origin}.`,
    )), SERVICE_LOAD_TIMEOUT_MS);

    window.addEventListener('message', (event) => {
      if (event.source !== iframe.contentWindow ||
          event.origin !== serviceUrl.origin) return;
      const message = event.data;
      if (message?.obelisk !== PROTOCOL) return;
      if (message.type === 'ready') {
        clearTimeout(timer);
        resolveReady();
        return;
      }
      runs.get(message.id)?.(message);
    });

    document.body.appendChild(iframe);
    service = {
      ready,
      send: (message) => iframe.contentWindow.postMessage(
        { obelisk: PROTOCOL, ...message }, serviceUrl.origin,
      ),
    };
    return service;
  }

  /* ---------------------------------------------------------------- blocks */

  const STYLE = `
    :host { display: block; margin: 0.5em 0 1.25em; font: 13px/1.4 system-ui, sans-serif; }
    .bar { display: flex; align-items: center; gap: 0.75em; flex-wrap: wrap; }
    button {
      font: inherit; color: inherit; background: transparent; cursor: pointer;
      padding: 0.25em 0.9em; border-radius: 4px;
      border: 1px solid color-mix(in srgb, currentColor 35%, transparent);
    }
    button:hover { background: color-mix(in srgb, currentColor 10%, transparent); }
    .status { opacity: 0.7; }
    pre {
      margin: 0.5em 0 0; padding: 0.6em 0.8em; max-height: 24em; overflow: auto;
      font: 12px/1.45 ui-monospace, SFMono-Regular, Menlo, Consolas, monospace;
      white-space: pre-wrap; overflow-wrap: anywhere; border-radius: 4px;
      background: color-mix(in srgb, currentColor 6%, transparent);
    }
    pre[hidden] { display: none; }
    .stderr { color: #e5534b; }
    .note { opacity: 0.65; }
  `;

  function describe(message, timeoutMs) {
    const seconds = (ms) => `${(ms / 1000).toFixed(ms < 10000 ? 1 : 0)} s`;
    switch (message.result) {
      case 'exited':
        return [`exited with code ${message.code} · compile ${seconds(message.compileMs)}` +
          ` · run ${seconds(message.runMs)}`, message.code === 0 ? 'note' : 'stderr'];
      case 'compile-error': return ['compilation failed', 'stderr'];
      case 'aborted': return [`simulation aborted: ${message.error}`, 'stderr'];
      case 'timeout': return [`stopped after ${seconds(timeoutMs)}`, 'stderr'];
      case 'stopped': return ['stopped', 'stderr'];
      default: return [message.message ?? 'the compiler failed', 'stderr'];
    }
  }

  function attribute(code, name) {
    return code.dataset[name] ?? code.parentElement?.dataset[name];
  }

  function upgrade(code) {
    if (code.dataset.obeliskRunner !== undefined) return;
    if (attribute(code, 'obelisk') === 'off') return;
    code.dataset.obeliskRunner = '';
    const block = code.closest('pre') ?? code;

    const host = document.createElement('div');
    host.className = 'obelisk-runner';
    const root = host.attachShadow({ mode: 'open' });
    root.innerHTML = `<style>${STYLE}</style>
      <div class="bar">
        <button type="button" part="button">Run</button>
        <span class="status" part="status" aria-live="polite"></span>
      </div>
      <pre part="output" hidden></pre>`;
    const button = root.querySelector('button');
    const status = root.querySelector('.status');
    const output = root.querySelector('pre');
    block.after(host);

    let id = null;
    let sent = false;
    let timeoutMs = config.timeoutMs;
    let written = 0;

    // The final summary is always shown, even after truncated output.
    const write = (text, className, { always = false } = {}) => {
      if (!always && written >= OUTPUT_LIMIT) return;
      if (!always && written + text.length > OUTPUT_LIMIT) {
        text = `${text.slice(0, OUTPUT_LIMIT - written)}\n… output truncated\n`;
        className = 'note';
      }
      written += text.length;
      const span = document.createElement('span');
      if (className) span.className = className;
      span.textContent = text;
      const pinned = output.scrollTop + output.clientHeight >= output.scrollHeight - 4;
      output.appendChild(span);
      if (pinned) output.scrollTop = output.scrollHeight;
    };

    const settle = () => {
      runs.delete(id);
      id = null;
      button.textContent = 'Run';
    };

    const onMessage = (message) => {
      switch (message.type) {
        case 'status':
          status.textContent = {
            loading: 'loading compiler…',
            compiling: 'compiling…',
            running: 'running…',
          }[message.phase] ?? '';
          break;
        case 'log':
        case 'output':
          write(message.text, message.stream === 'stderr' ? 'stderr' : '');
          break;
        case 'done': {
          const [text, className] = describe(message, timeoutMs);
          write(`${written ? '\n' : ''}${text}\n`, className, { always: true });
          status.textContent = '';
          settle();
          break;
        }
      }
    };

    button.addEventListener('click', async () => {
      if (id !== null) {
        if (sent) {
          service.send({ type: 'stop', id });
        } else {
          onMessage({ type: 'done', id, result: 'stopped' });
        }
        return;
      }
      id = nextId++;
      const runId = id;
      runs.set(runId, onMessage);
      written = 0;
      output.textContent = '';
      output.hidden = false;
      button.textContent = 'Stop';
      status.textContent = 'starting…';

      sent = false;
      const timeout = attribute(code, 'obeliskTimeout');
      timeoutMs = timeout === undefined ? config.timeoutMs : 1000 * (Number(timeout) || 0);
      const args = [config.args, attribute(code, 'obeliskArgs') ?? ''].join(' ').trim();
      const current = startService();
      try {
        await current.ready;
      } catch (error) {
        if (id !== runId) return;
        write(`${error.message}\n`, 'stderr');
        status.textContent = '';
        settle();
        return;
      }
      if (id !== runId) return;
      sent = true;
      current.send({ type: 'run', id: runId, source: code.textContent, args, timeoutMs });
    });
  }

  function scan(root = document) {
    for (const code of root.querySelectorAll(config.selector)) upgrade(code);
  }

  window.ObeliskRunner = { scan };
  if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', () => scan());
  } else {
    scan();
  }
})();
