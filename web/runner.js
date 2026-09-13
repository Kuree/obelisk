// Makes SystemVerilog code blocks on any website runnable.
//
//   <script src="https://keyizhang.com/obelisk/runner.js" defer></script>
//
// Every SystemVerilog code block gets a Run button and an output panel. A block
// is a `pre > code` carrying `language-systemverilog` or `language-sv`, either
// on the <code> itself or on a wrapper, which is where Jekyll (Rouge) and
// MkDocs (Pygments) put it. Script attributes configure the page:
//
//   data-selector   CSS selector for runnable code elements
//   data-args       compiler flags added to every block, e.g. "-O0"
//   data-timeout    seconds a simulation may run before it is stopped (default
//                   60; 0 disables)
//   data-highlight  "off" to leave code blocks uncolored
//
// A block can add `data-obelisk-args` for its own flags and
// `data-obelisk-timeout` for its own limit, opt out of highlighting with
// `data-obelisk-highlight="off"`, or opt out entirely with
// `data-obelisk="off"`, on either the <code> or its <pre>. Pages that render
// code after load call `ObeliskRunner.scan(root)`.
//
// Blocks are highlighted only when nothing else has: the pass waits for the
// page's load event, after highlight.js and Prism run their DOMContentLoaded
// passes, and skips any block that already contains markup. The runner reads
// the code's text when Run is pressed, so any highlighter's markup is fine.
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
    selector: script?.dataset.selector || [
      'pre > code.language-systemverilog', 'pre > code.language-sv',
      '.language-systemverilog pre > code', '.language-sv pre > code',
    ].join(', '),
    highlight: script?.dataset.highlight !== 'off',
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

  /* ------------------------------------------------------------ highlight */

  // Everything from here to tokenize() is a copy of web/sv-highlight.js, which
  // this classic script cannot import; a test keeps the two identical.
  const KEYWORD_SET = new Set([
    'module', 'endmodule', 'package', 'endpackage', 'program', 'endprogram',
    'interface', 'endinterface', 'class', 'endclass', 'function', 'endfunction',
    'task', 'endtask', 'begin', 'end', 'generate', 'endgenerate', 'case',
    'casex', 'casez', 'endcase', 'if', 'else', 'for', 'foreach', 'while',
    'do', 'repeat', 'forever', 'break', 'continue', 'return', 'fork', 'join',
    'join_any', 'join_none', 'initial', 'always', 'always_comb', 'always_ff',
    'always_latch', 'assign', 'wait', 'disable', 'default', 'extends',
    'implements', 'virtual', 'pure', 'local', 'protected', 'static',
    'automatic', 'const', 'ref', 'input', 'output', 'inout', 'parameter',
    'localparam', 'typedef', 'enum', 'struct', 'union', 'packed', 'tagged',
    'new', 'this', 'super', 'null', 'extern', 'import', 'export', 'randomize',
    'constraint', 'rand', 'randc', 'covergroup', 'endgroup', 'coverpoint',
    'cross', 'bins', 'property', 'endproperty', 'sequence', 'endsequence',
    'assert', 'assume', 'cover', 'expect', 'posedge', 'negedge', 'edge',
    'timeunit', 'timeprecision', 'unique', 'unique0', 'priority', 'solve',
    'before', 'inside', 'dist', 'with', 'matches', 'iff', 'genvar', 'defparam',
    'modport', 'clocking', 'endclocking', 'randcase', 'randsequence',
  ]);
  const TYPE_SET = new Set([
    'logic', 'bit', 'reg', 'wire', 'byte', 'shortint', 'int', 'longint',
    'integer', 'time', 'real', 'shortreal', 'realtime', 'string', 'chandle',
    'event', 'void', 'signed', 'unsigned', 'tri', 'triand', 'trior', 'tri0',
    'tri1', 'trireg', 'wand', 'wor', 'supply0', 'supply1', 'uwire', 'var',
  ]);

  // One alternative per token kind, tried in order at each position. Anything
  // unmatched (operators, punctuation, whitespace) stays plain text.
  const TOKEN = new RegExp([
    String.raw`(\/\*[\s\S]*?(?:\*\/|$)|\/\/[^\n]*)`,                  // 1 comment
    String.raw`("(?:[^"\\\n]|\\.)*"?)`,                                   // 2 string
    String.raw`(\$[A-Za-z_][\w$]*)`,                                         // 3 system task
    String.raw`(\x60[A-Za-z_]\w*)`,                                          // 4 directive
    String.raw`((?:\d[\d_]*\s*)?'[sS]?[bBoOdDhH]\s*[\da-fA-FxXzZ?_]+` +   // 5 number
      String.raw`|'[01xXzZ](?![\w'])` +
      String.raw`|\d[\d_]*(?:\.\d[\d_]*)?(?:[eE][-+]?\d+)?(?:[munpf]?s\b)?)`,
    String.raw`([A-Za-z_][\w$]*)`,                                            // 6 identifier
    String.raw`(\\\S+)`,                                                    // 7 escaped identifier
  ].join('|'), 'g');

  /** Split source into [kind, text] pairs whose texts concatenate back to it. */
  function tokenize(source) {
    const tokens = [];
    const push = (kind, text) => {
      const last = tokens[tokens.length - 1];
      if (last && last[0] === kind) last[1] += text;
      else tokens.push([kind, text]);
    };
    let position = 0;
    for (const match of source.matchAll(TOKEN)) {
      if (match.index > position) push('', source.slice(position, match.index));
      const [text, comment, string, system, directive, number, word] = match;
      push(comment ? 'comment'
        : string ? 'string'
        : system ? 'system'
        : directive ? 'directive'
        : number ? 'number'
        : word ? (KEYWORD_SET.has(word) ? 'keyword' : TYPE_SET.has(word) ? 'type' : '')
        : '', text);
      position = match.index + text.length;
    }
    if (position < source.length) push('', source.slice(position));
    return tokens;
  }

  // PALETTES from web/sv-highlight.js, for blocks that were not baked. They
  // take the page's own ground, so only the token colors apply. :where() adds
  // no specificity, so a host stylesheet overrides any of them.
  const HIGHLIGHT_STYLE = `
    :where(.obelisk-hl) :where(.obelisk-comment) { color: #6e7781; }
    :where(.obelisk-hl) :where(.obelisk-string) { color: #0a3069; }
    :where(.obelisk-hl) :where(.obelisk-keyword) { color: #cf222e; }
    :where(.obelisk-hl) :where(.obelisk-type) { color: #953800; }
    :where(.obelisk-hl) :where(.obelisk-number) { color: #0550ae; }
    :where(.obelisk-hl) :where(.obelisk-system) { color: #8250df; }
    :where(.obelisk-hl) :where(.obelisk-directive) { color: #116329; }
    :where(.obelisk-hl-dark) :where(.obelisk-comment) { color: #8b949e; }
    :where(.obelisk-hl-dark) :where(.obelisk-string) { color: #a5d6ff; }
    :where(.obelisk-hl-dark) :where(.obelisk-keyword) { color: #ff7b72; }
    :where(.obelisk-hl-dark) :where(.obelisk-type) { color: #ffa657; }
    :where(.obelisk-hl-dark) :where(.obelisk-number) { color: #79c0ff; }
    :where(.obelisk-hl-dark) :where(.obelisk-system) { color: #d2a8ff; }
    :where(.obelisk-hl-dark) :where(.obelisk-directive) { color: #7ee787; }
  `;

  // The page's own text color says whether the block sits on a dark ground,
  // without guessing which ancestor paints the background. Computed colors
  // keep their color space (oklch(), lab(), color(display-p3 ...)), so paint
  // the color onto a pixel to read it back as sRGB.
  let colorProbe = null;

  function isDarkText(element) {
    colorProbe ??= Object.assign(document.createElement('canvas'), { width: 1, height: 1 })
      .getContext('2d', { willReadFrequently: true });
    if (!colorProbe) return false;
    colorProbe.clearRect(0, 0, 1, 1);
    colorProbe.fillStyle = getComputedStyle(element).color;
    colorProbe.fillRect(0, 0, 1, 1);
    const [red, green, blue] = colorProbe.getImageData(0, 0, 1, 1).data;
    return (0.2126 * red + 0.7152 * green + 0.0722 * blue) / 255 > 0.5;
  }

  let highlightStyled = false;

  function highlight(code) {
    if (!config.highlight || attribute(code, 'obeliskHighlight') === 'off') return;
    // Another highlighter got here first, or the author wrote markup.
    if (code.firstElementChild || code.classList.contains('hljs')) return;
    if (!highlightStyled) {
      highlightStyled = true;
      const style = document.createElement('style');
      style.dataset.obeliskRunner = '';
      style.textContent = HIGHLIGHT_STYLE;
      document.head.appendChild(style);
    }
    const fragment = document.createDocumentFragment();
    for (const [kind, text] of tokenize(code.textContent)) {
      if (!kind) {
        fragment.append(text);
        continue;
      }
      const span = document.createElement('span');
      span.className = `obelisk-${kind}`;
      span.textContent = text;
      fragment.append(span);
    }
    code.replaceChildren(fragment);
    code.classList.add(isDarkText(code) ? 'obelisk-hl-dark' : 'obelisk-hl');
  }

  // Deferred to the load event so page highlighters, which run on
  // DOMContentLoaded, claim their blocks first.
  const pendingHighlights = [];
  let pageLoaded = document.readyState === 'complete';
  if (!pageLoaded) {
    window.addEventListener('load', () => {
      pageLoaded = true;
      for (const code of pendingHighlights.splice(0)) highlight(code);
    }, { once: true });
  }

  function scheduleHighlight(code) {
    if (pageLoaded) highlight(code);
    else pendingHighlights.push(code);
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
    scheduleHighlight(code);

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

  // tokenize is exposed for tests; scan is the supported entry point.
  window.ObeliskRunner = { scan, tokenize };
  if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', () => scan());
  } else {
    scan();
  }
})();
