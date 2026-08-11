import assert from 'node:assert/strict';
import { access, readFile } from 'node:fs/promises';

const web = new URL('./', import.meta.url);
const read = (name) => readFile(new URL(name, web), 'utf8');
const [app, html, style, surfer] = await Promise.all([
  read('app.js'), read('index.html'), read('style.css'), read('surfer.html'),
]);

const ids = new Set([...html.matchAll(/\bid="([^"]+)"/g)].map((match) => match[1]));
for (const match of app.matchAll(/\bel\('([^']+)'\)/g)) {
  assert.ok(ids.has(match[1]), `app.js references missing #${match[1]}`);
}

for (const match of app.matchAll(/from\s+'(\.\/[^']+)'/g)) {
  await access(new URL(match[1], web));
}
assert.match(html, /<script\s+type="module"\s+src="\.\/app\.js"><\/script>/);
assert.match(html, /<iframe[^>]+id="surfer"[^>]+sandbox="allow-scripts"/s);
assert.doesNotMatch(html, /sandbox="[^"]*allow-same-origin/);

assert.match(style, /\.panelTools\s*>\s*\[hidden\]\s*{\s*display:\s*none;/);
assert.match(style, /\.irEditor\s+\.cursor\s*{\s*display:\s*none\s*!important;/);
assert.match(style, /\.scheduleCanvas\s*{\s*overflow-x:\s*hidden;/);
assert.match(style, /\.scheduleCanvas\s+svg\s*{[^}]*max-width:\s*600px;/s);

assert.doesNotMatch(surfer, /linear-gradient|body::before/);
assert.match(surfer, /SetMenuVisible:\s*false/);
assert.match(surfer, /SetToolbarGroupEnabled:\s*\['menu',\s*false\]/);
assert.match(surfer, /SetToolbarGroupEnabled:\s*\['files',\s*false\]/);
assert.match(surfer, /event\.source\s*!==\s*parent\s*\|\|\s*event\.origin\s*!==\s*parentOrigin/);
assert.match(surfer, /SelectTheme:\s*'dark\+'/);

assert.match(app, /finishRecording\(`\$\{note\}compile \$\{compileMs\} ms · run \$\{runMs\} ms`/);
assert.match(app, /setStatus\('running',\s*'busy'\)/);
assert.match(app, /finishRecording\([^;]+code === 0 \? 'ok' : 'err'\)/s);
assert.match(app, /saveWaveform\(latestWaveform\)/);
assert.match(app, /showSchedule\(text\)/);
assert.match(app, /setModelLanguage\(irModel,\s*language\)/);
assert.match(html, /id="irEditor"[^>]+aria-label="Read-only compiler output"/);
assert.match(app, /onSourceLocation:\s*revealScheduleSource/);
assert.match(app, /onSourceDeselected:\s*\(\)\s*=>\s*scheduleSourceHighlight\?\.clear\(\)/);
assert.match(app, /editor\.setPosition\(\{[\s\S]*lineNumber:[\s\S]*column:/);
assert.match(app, /createDecorationsCollection\(\)/);
assert.match(app, /isWholeLine:\s*true[\s\S]*className:\s*'scheduleSourceLine'/);
assert.match(style, /\.scheduleSourceLine\s*{[^}]*background:\s*#bd93f959/s);

console.log('web UI module, DOM, timing, schedule, and Surfer contracts OK');
