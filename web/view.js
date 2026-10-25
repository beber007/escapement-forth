// Copyright (c) 2026 Bertrand Hurst. All rights reserved.
// Escapement Forth - a Forth console beside the Escapement real-time OS.
// Distributed under the terms of LICENSE at the root of this repository.
//
// view.js: What the page makes of what the worker sends, apart from the drawing, so that
// test/test_web.js runs it as the page does: the timeline, the lines of the tour, the
// times and shares as the page writes them.

const SHORT = 200000, LONG = 1000000;   // us of the timeline's two views
const KEPT = 2 * LONG;                  // us of instances kept behind the clock

// timeline: What the kernel ran, from the trace: each instance of a task, the runs of it
// between the tasks that preempted it, the row 4 the console's lines. frozen, if not null,
// the clock the view stops at; onFreeze called when the view stops by itself.
function timeline(onFreeze) {
  const view = { window: SHORT, instances: [], clock: 0, frozen: null, freezeRow: null };
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
      view.frozen = t + Math.min(20000, SHORT - (t - instance.start));
      onFreeze();
    }
    const below = nested[nested.length - 1];
    if (below)
      below.runs.push([t, null]);
  }

  // traced: A message of telemetry. Kept: the instances of the last KEPT us, and while
  // frozen those the view shows.
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
    const shown = (i) => view.frozen !== null && i.start <= view.frozen && i.end >= view.frozen - LONG;
    view.instances = view.instances.filter((i) => i.end === null || i.end >= view.clock - KEPT || shown(i));
  };

  // pause: The view stopped where it is, or let go.
  view.pause = () => {
    view.frozen = view.frozen === null ? view.clock : null;
    view.freezeRow = null;
  };

  // watch: The view let go, to stop on the next long instance of row if not undefined.
  view.watch = (row) => {
    view.frozen = null;
    view.freezeRow = row === undefined ? null : row;
  };

  // reset: The view of a Forth started anew.
  view.reset = () => {
    view.instances = [];
    view.clock = 0;
    view.frozen = view.freezeRow = null;
    view.window = SHORT;
    nested = [];
    base = 0;
    lastRaw = -1;
  };

  return view;
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
// whether of the tour; null if none.
function pending(queue, pool) {
  while (queue.length > 0) {
    const item = queue.shift(), line = item.tour ? resolve(item.line, pool) : item.line;
    if (line !== null)
      return { line, tour: item.tour };
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

function share(ppm, lang) {
  return decimal((Math.ceil(ppm / 1000) / 10).toFixed(1), lang) + " %";
}

function decimal(text, lang) {
  return lang === "fr" ? text.replace(".", ",") : text;
}
