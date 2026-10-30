// Copyright (c) 2026 Bertrand Hurst. All rights reserved.
// Escapement Forth - a Forth console beside the Escapement real-time OS.
// Distributed under the terms of LICENSE at the root of this repository.
//
// view.js: What the page makes of what the worker sends, apart from the drawing, so that
// test/test_web.js runs it as the page does: the timeline, the lines of the tour, the
// times and shares as the page writes them.

const ZOOM = 20000, SHORT = 200000, LONG = 1000000;   // us of the timeline's views
const WINDOWS = [ZOOM, 50000, 100000, SHORT, 500000, LONG, 2000000, 5000000, 10000000];
const KEPT = 10 * LONG;                 // us of instances kept behind the clock

// timeline: What the kernel ran, from the trace: each instance of a task, the runs of it
// between the tasks that preempted it, the row 4 the console's lines. frozen, if not null,
// the clock the view stops at, and stopped the clock it stopped at, scrolled back from;
// onFreeze called when the view stops by itself. held, if not null, the clock the view
// stays at while the pointer is over it, the page's Pause and its note left as they are.
function timeline(onFreeze) {
  const view = { window: SHORT, instances: [], clock: 0, frozen: null, stopped: null, freezeRow: null, held: null };
  let nested = [], base = 0, lastRaw = -1;

  // unwrap: The kernel's clock counts modulo 2^30.
  function unwrap(raw) {
    if (lastRaw >= 0 && raw < lastRaw && lastRaw - raw > 0x20000000)
      base += 0x40000000;
    lastRaw = raw;
    return base + raw;
  }

  function begin(row, t) {
    const below = nested[nested.length - 1];
    if (below)
      below.runs[below.runs.length - 1][1] = t;
    const instance = { row, start: t, end: null, over: false, runs: [[t, null]] };
    nested.push(instance);
    view.instances.push(instance);
  }

  // freezeRow: The row whose next instance of 20 ms or more, once ended, the view stops
  // on: the task runs each period, its slots empty or not yet bound too.
  function end(row, t, over) {
    let instance;
    while ((instance = nested.pop()) !== undefined) {
      instance.runs[instance.runs.length - 1][1] = t;
      instance.end = t;
      if (instance.row === row)
        break;
    }
    if (instance === undefined)
      return;
    instance.over = over;
    if (row === view.freezeRow && view.frozen === null && t - instance.start >= 20000) {
      view.freezeRow = null;
      view.window = SHORT;
      view.frozen = view.stopped = t + Math.min(20000, SHORT - (t - instance.start));
      onFreeze();
    }
    const below = nested[nested.length - 1];
    if (below)
      below.runs.push([t, null]);
  }

  // traced: A message of telemetry. Kept: the instances of the last KEPT us, and those of
  // the KEPT us before where the view stopped or is held, to be scrolled back through.
  view.traced = (m) => {
    for (let k = 0; k < m.trace.length; k += 2) {
      const t = unwrap(m.trace[k]), kind = m.trace[k + 1] & 0xff;
      const period = (m.trace[k + 1] >> 8) & 0xff, over = ((m.trace[k + 1] >> 16) & 1) === 1;
      if (kind === 1)
        begin(period, t);
      else if (kind === 2)
        end(period, t, over);
      else if (kind === 3)
        begin(4, t);
      else if (kind === 4)
        end(4, t, false);
    }
    view.clock = Math.max(view.clock, unwrap(m.clock));
    const near = (i, at, behind) => at !== null && i.start <= at && i.end >= at - behind;
    view.instances = view.instances.filter((i) => i.end === null || i.end >= view.clock - KEPT ||
                                                  near(i, view.stopped, KEPT) || near(i, view.held, KEPT));
  };

  // pause: The view stopped where it is, or let go.
  view.pause = () => {
    view.frozen = view.stopped = view.frozen === null ? view.edge() : null;
    view.freezeRow = null;
  };

  // zoom: The view's scale steps of WINDOWS finer or wider, within them; stopped, about
  // the clock at if not undefined, that time staying where it is drawn.
  view.zoom = (steps, at) => {
    const was = view.window, k = Math.max(0, Math.min(WINDOWS.length - 1, WINDOWS.indexOf(was) + steps));
    view.window = WINDOWS[k];
    if (view.frozen !== null) {
      if (at !== undefined)
        view.frozen = at + (view.frozen - at) * view.window / was;
      view.scroll(0);
    }
  };

  // scroll: The view stopped where it is, then moved dt us, within the KEPT us before it
  // stopped and what is kept of them.
  view.scroll = (dt) => {
    if (view.frozen === null)
      view.pause();
    const first = view.instances.length > 0 ? view.instances[0].start : view.stopped;
    const earliest = Math.min(view.stopped, Math.max(view.stopped - KEPT, first) + view.window);
    view.frozen = Math.max(earliest, Math.min(view.stopped, view.frozen + dt));
  };

  // watch: The view let go, to stop on the next long instance of row if not undefined.
  view.watch = (row) => {
    view.frozen = view.stopped = null;
    view.freezeRow = row === undefined ? null : row;
  };

  // reset: The view of a Forth started anew.
  view.reset = () => {
    view.instances = [];
    view.clock = 0;
    view.frozen = view.stopped = view.freezeRow = view.held = null;
    view.window = SHORT;
    nested = [];
    base = 0;
    lastRaw = -1;
  };

  // edge: The clock at the view's right edge.
  view.edge = () => view.frozen !== null ? view.frozen : view.held !== null ? view.held : view.clock;

  return view;
}

// hit: The instance of row drawn between left and right nearest t, running, waiting or
// within slack us of it, null if none: at 200 ms across, a pixel holds 250 us, and a run
// of the 1 ms task a few.
function hit(instances, row, t, slack, left, right) {
  let best = null, gap = slack;
  for (const i of instances) {
    if (i.row !== row || i.start > right || (i.end !== null && i.end < left))
      continue;
    const d = t < i.start ? i.start - t : i.end !== null && t > i.end ? t - i.end : 0;
    if (d <= gap) {
      best = i;
      gap = d;
    }
  }
  return best;
}

// rowNames: For each period, the names of the words bound in its slots, "" for none.
function rowNames(pool) {
  const names = [];
  for (let p = 0; p < 4; p += 1)
    names.push(pool.slice(p * 4, p * 4 + 4).filter((e) => e.state !== 0).map((e) => e.name).join(" "));
  return names;
}

// resolve: A line of the tour, {name} the slot of the word of that name in pool; null if
// no slot has that name, the line then left out.
function resolve(line, pool) {
  let skip = false;
  const resolved = line.replace(/\{(\w+)\}/g, (_, name) => {
    const i = pool.findIndex((e) => e.state !== 0 && e.name === name);
    skip = skip || i < 0;
    return String(i);
  });
  return skip ? null : resolved;
}

// pending: The next line of queue to send, typed as it was or of the tour resolved, and
// whether of the tour and shown or not; null if none.
function pending(queue, pool) {
  while (queue.length > 0) {
    const item = queue.shift(), line = item.tour ? resolve(item.line, pool) : item.line;
    if (line !== null)
      return { line, tour: item.tour, quiet: item.quiet === true };
  }
  return null;
}

// grainOf: The step of the clock now reads, in us: the smallest of a few changes read one
// after another, the first left out, its reading begun in the middle of a step.
function grainOf(now) {
  let last = now(), smallest = Infinity;
  for (let changes = 0, k = 0; changes < 6 && k < 100000000; k += 1) {
    const t = now();
    if (t !== last) {
      if (changes > 0)
        smallest = Math.min(smallest, t - last);
      changes += 1;
      last = t;
    }
  }
  return Math.round(smallest * 1000);
}

// coarse: Whether a clock of that step, in us, is too coarse for the kernel, as where the
// browser guards privacy (Firefox's resistFingerprinting, 16.7 ms): a run of a slot of
// 10 ms then takes a step, past its period, and the console is never run again.
function coarse(grain) {
  return !(grain <= 1000);
}

// full: What a line's output says full: "dictionary", which a Restart empties, "period",
// whose slots stop frees, or null.
function full(text) {
  return /dictionary full/.test(text) ? "dictionary" : /period full/.test(text) ? "period" : null;
}

// answered: Whether a line's output says ok, as the console ends a line it took; what the
// slots printed meanwhile follows it.
function answered(text) {
  return /(^|\s)ok\n/.test(text);
}

// abandon: The lines of the tour left in queue left out, those typed kept.
function abandon(queue) {
  const kept = queue.filter((item) => !item.tour);
  queue.length = 0;
  queue.push(...kept);
}

// time: us as the page writes it, to the us: rounded, a run over its budget was written
// as the budget, and a worst case admitted above it.
function time(us, lang) {
  return us < 1000 ? us + " µs" : decimal(String(us / 1000), lang) + " ms";
}

// count: A count as a slot of the pool writes it, which has room for few digits.
function count(n) {
  return n < 1000 ? String(n) : Math.floor(n / 1000) + "k";
}

// span: us as the axis and the scale write it: from 1 s, in s.
function span(us, lang) {
  return us < 1000000 ? time(us, lang) : decimal(String(us / 1000000), lang) + " s";
}

function share(ppm, lang) {
  return decimal((Math.ceil(ppm / 1000) / 10).toFixed(1), lang) + (lang === "fr" ? " %" : "%");
}

function decimal(text, lang) {
  return lang === "fr" ? text.replace(".", ",") : text;
}
