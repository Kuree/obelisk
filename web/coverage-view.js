// The Coverage view: the self-contained HTML report obelisk-cov wrote for the
// latest run with coverage, shown as is. The page runs in the sandboxed frame
// from a blob: URL, so it can run its own scripts but never reach this page.

/** "line 95.2% · toggle 71.6%" from a report's embedded metrics. */
export function coverageSummary(html) {
  try {
    const payload = html.match(/<script\b[^>]*\bid="coverage-data"[^>]*>([\s\S]*?)<\/script>/);
    const metrics = JSON.parse(payload[1]).metrics;
    return Object.entries(metrics)
      .filter(([, metric]) => metric.available)
      .map(([name, metric]) => `${name} ${Number(metric.percent).toFixed(1)}%`)
      .join(' · ') || 'no coverage collected';
  } catch {
    return 'coverage report';
  }
}

export class CoverageView {
  #frame;
  #empty;
  #download;
  #urls;
  #report = null;
  #failure = null;
  #generation = 0;
  #shown = null;

  /**
   * @param {{frame: HTMLIFrameElement, empty: HTMLElement, download: HTMLElement}} elements
   * @param {{createObjectURL: Function, revokeObjectURL: Function}} urls
   */
  constructor({ frame, empty, download }, urls = URL) {
    this.#frame = frame;
    this.#empty = empty;
    this.#download = download;
    this.#urls = urls;
  }

  /** Keep a run's report; the next show() displays it. */
  update(html) {
    this.#report = { html, generation: ++this.#generation };
    this.#failure = null;
  }

  /**
   * A run with coverage produced no report: drop the previous one, which no
   * longer describes the latest run, and say why instead.
   */
  fail(reason) {
    this.#report = null;
    this.#failure = reason;
    if (this.#shown) this.#urls.revokeObjectURL(this.#shown.href);
    this.#shown = null;
  }

  /**
   * Show the latest report, or say why there is none. Returns the status line
   * for the page, as {text, kind}.
   */
  show(coverageOn) {
    this.#download.hidden = !this.#report;
    if (!this.#report) {
      this.#frame.hidden = true;
      this.#empty.hidden = false;
      if (this.#failure) {
        this.#empty.textContent = `The latest run produced no coverage report: ${this.#failure}.`;
        return { text: 'no coverage report', kind: 'err' };
      }
      this.#empty.textContent = coverageOn
        ? 'No coverage yet. Run the design to collect it.'
        : 'Coverage is off. Turn it on under Options, then run the design.';
      return { text: 'no coverage', kind: '' };
    }
    // A new URL only for a new report, so switching tabs keeps the frame's
    // place in the report.
    if (this.#shown?.generation !== this.#report.generation) {
      if (this.#shown) this.#urls.revokeObjectURL(this.#shown.href);
      this.#shown = {
        href: this.#urls.createObjectURL(new Blob([this.#report.html], { type: 'text/html' })),
        generation: this.#report.generation,
      };
      this.#frame.src = this.#shown.href;
    }
    this.#empty.hidden = true;
    this.#frame.hidden = false;
    return { text: coverageSummary(this.#report.html), kind: 'ok' };
  }

  /** Save the latest report as coverage.html through a temporary link. */
  save(document) {
    if (!this.#report) return;
    const href = this.#urls.createObjectURL(new Blob([this.#report.html], { type: 'text/html' }));
    const anchor = document.createElement('a');
    anchor.href = href;
    anchor.download = 'coverage.html';
    anchor.click();
    setTimeout(() => this.#urls.revokeObjectURL(href));
  }
}
