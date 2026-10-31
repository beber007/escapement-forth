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
  let nested = [], base = 0, lastRaw = -1, line = null;

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
      else if (kind === 3 && m.board) {   // on its own core: nothing interrupts a line
        line = { row: 4, start: t, end: null, over: false, runs: [[t, null]] };
        view.instances.push(line);
      }
      else if (kind === 4 && m.board) {
        if (line !== null) {
          line.runs[0][1] = line.end = t;
          line = null;
        }
      }
      else if (kind === 3)
        begin(4, t);
      else if (kind === 4)
        end(4, t, false);
      else if (kind === 5) {   // events lost: what was open will not end
        while (nested.length > 0)
          end(nested[0].row, t, false);
      }
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
    line = null;
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

// boardFrame: A frame of the board's console, as the page's own worker says it.
function boardFrame(text) {
  const lines = text.replace(/\r/g, "").split("\n");
  const head = lines[0].split(" ");
  if (head[0] === "p")
    return { pins: Number(head[1]) >>> 0 };
  if (head[0] !== "s")
    return null;
  const slots = new Array(16).fill(null).map(() => ({ state: 0, error: "", name: "", runs: 0, max: 0, over: 0, wcet: 0, budget: 0 }));
  for (const line of lines.slice(1)) {
    const f = line.split(" ");
    const n = (k) => Number(f[k]) >>> 0;
    if (n(0) < 16)
      slots[n(0)] = { state: n(1), runs: n(2), max: n(3), over: n(4), wcet: n(5), budget: n(6),
                      name: f[7], error: f.slice(8).join(" ") };
  }
  return { load: Number(head[1]), bound: Number(head[2]), slots };
}

// boardTrace: The board's events (pico/console.c) as the worker's telemetry for timeline.
// Their time is 19 bits of us: read against the last, either way. flush gives what came
// since the last time, or null.
function boardTrace() {
  const KINDS = { 0: 1, 1: 2, 2: 2, 4: 3, 5: 4, 6: 5 };
  let last = null, trace = [];
  return {
    add(event) {
      const raw = event & 0x7FFFF, kind = (event >>> 21) & 7, period = (event >>> 19) & 3;
      last = last === null ? raw : (last + ((raw - last + 0x40000) & 0x7FFFF) - 0x40000) & 0x3FFFFFFF;
      if (KINDS[kind] !== undefined)
        trace.push(last, KINDS[kind] | (kind === 4 || kind === 5 || kind === 6 ? 0 : period) << 8 | (kind === 2 ? 1 : 0) << 16);
    },
    flush() {
      if (last === null)
        return null;
      const m = { trace, clock: last, board: true };
      trace = [];
      return m;
    },
  };
}

// boardLink: A serial port (Web Serial) opened on the board's console: onText gets what it
// prints, onEnd is told if the port ends by itself, the board unplugged.
async function boardLink(port, onText, onEnd, onFrame = () => {}, onEvent = () => {}) {
  await port.open({ baudRate: 115200 });
  const decoder = new TextDecoder(), framed = new TextDecoder(), encoder = new TextEncoder();
  const writer = port.writable.getWriter();
  let reader = null, closing = false;
  // The console's text, with its frames (pico/console.c) taken out. What a word printed
  // that looks like one, cut short or of no kind, stays text.
  let frame = null;
  const say = (bytes) => {
    const text = decoder.decode(bytes, { stream: true }).replace(/\r/g, "");
    if (text !== "")
      onText(text);
  };
  let event = null;
  const onBytes = (bytes) => {
    let text = [];
    const flush = () => {
      if (text.length > 0)
        say(Uint8Array.from(text));
      text = [];
    };
    for (const b of bytes) {
      if (event !== null) {   // anywhere, a frame or a character cut in two
        event.push(b);
        if (event.length === 3) {
          onEvent(event[0] << 16 | event[1] << 8 | event[2]);
          event = null;
        }
      }
      else if (b === 0x1C)
        event = [];
      else if (frame !== null) {
        if (b === 0x1F) {
          const parsed = boardFrame(framed.decode(Uint8Array.from(frame)));
          if (parsed === null)
            say(Uint8Array.from(frame));
          else
            onFrame(parsed);
          frame = null;
        }
        else if (b === 0x1E) {   // the console sends a frame whole: the first was a word's
          say(Uint8Array.from(frame));
          frame = [];
        }
        else if (frame.length >= 2048) {
          say(Uint8Array.from(frame));
          frame = null;
          text.push(b);
        }
        else
          frame.push(b);
      }
      else if (b === 0x1E) {
        flush();
        frame = [];
      }
      else
        text.push(b);
    }
    flush();
  };
  (async () => {
    // An error the port survives, a break or a framing error, gives a new stream; one it
    // does not, the board unplugged, leaves none.
    while (!closing && port.readable !== null) {
      reader = port.readable.getReader();
      try {
        while (true) {
          const { value, done } = await reader.read();
          if (done) {
            closing = closing || "ended";
            break;
          }
          onBytes(value);
        }
      } catch (e) {
        // Round again, if a stream is left.
      }
      reader.releaseLock();
    }
    writer.releaseLock();
    try {
      await port.close();
    } catch (e) {
      // Already gone.
    }
    if (closing !== true)
      onEnd();
  })();
  const write = (bytes) => writer.write(bytes).catch(() => {});
  return {
    send: (line) => write(encoder.encode(line + "\r")),
    interrupt: () => write(Uint8Array.of(3)),
    askPins: () => write(Uint8Array.of(5)),
    askSlots: () => write(Uint8Array.of(6)),
    trace: (on) => write(Uint8Array.of(on ? 14 : 15)),
    close: () => {
      closing = true;
      return reader === null ? undefined : reader.cancel().catch(() => {});
    },
  };
}

// full: What a line's output says full: "dictionary", which a Restart empties, "period",
// whose slots stop frees, "processor", which stop makes room in, or null.
function full(text) {
  return /dictionary full/.test(text) ? "dictionary" : /period full/.test(text) ? "period" :
         /processor full/.test(text) ? "processor" : null;
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
