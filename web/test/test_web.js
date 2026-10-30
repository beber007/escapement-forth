// Copyright (c) 2026 Bertrand Hurst. All rights reserved.
// Escapement Forth - a Forth console beside the Escapement real-time OS.
// Distributed under the terms of LICENSE at the root of this repository.
//
// test_web.js: forth.wasm driven as worker.js drives it, without the page, under Node
// (node test_web.js forth.wasm index.html worker.js view.js [step]) or JavaScriptCore
// (jsc test_web.js -- forth.wasm index.html worker.js view.js [step]), step the ms the
// browser's clock is read to, if given: 0.1 in Chrome, 1 in others. The worker and the page's
// script are parsed, not run: what the worker does is copied here; what the page makes of
// it, view.js, is run as the page runs it. The worker is let run every GAP ms, as Chrome lets it, the
// time in between caught up by the kernel: with the page called without a pause, the
// clock ran faster than the wall inside a long slot and went unseen (2026-10-25).

const node = typeof process !== "undefined";
const args = node ? process.argv.slice(2) : arguments;
const now = node ? () => performance.now() : () => preciseTime() * 1000;
const read = node ? (p) => require("fs").readFileSync(p) : (p) => new Uint8Array(readFile(p, "binary"));
const text = node ? (p) => require("fs").readFileSync(p, "utf8") : (p) => readFile(p);
const say = node ? (s) => process.stdout.write(s + "\n") : print;

const GAP = 30;      // ms between two runs of the worker, as in Chrome
const STEP = args[4] === undefined ? 0 : Number(args[4]);
// skips: The releases a pause of the browser may skip. Read to the ms, a pause under a
// step and a half cannot be told from a step of the clock, nor left out: one in a run of
// the task of 1 ms makes its next release come before it ends, as it would in such a
// browser (2026-10-25).
const SKIPS = STEP >= 1 ? 2 : 0;
const LINE = 128;    // FORTH_LINE

let forth, output = "", pins = 0, seen = [], failures = 0;
let jump = 0, stallIn = -1;   // ms the clock jumps once, at the stallIn-th reading from now
// The clock jumps pauseMs at the first reading past pauseAt in a call of the worker, not
// its first. The next jump comes 0.5 to 1.5 pauseEvery later: a pause of the browser
// lands at any time. A jump every ms, with runs of 500 us end to end, landed again and
// again at the end of a wait: a few us that a real pause almost never meets (a review,
// 2026-10-29). paused counts the jumps; seed draws the same intervals each run.
let pauseEvery = 0, pauseMs = 0, pauseAt = 0, paused = 0, callReads = -1, seed = 1;
const t0 = now(), wasm = new WebAssembly.Module(read(args[0]));
// boot: A Forth anew, as the page's Restart starts it.
function boot() {
  forth = new WebAssembly.Instance(wasm, { env: {
    web_now: () => {
      // One reading of the wall: a second, made only while pausing, slowed a loop that
      // reads the clock every few us by a quarter, past the budget it was promoted with.
      const wall = now();
      if (stallIn >= 0 && stallIn-- === 0)
        jump += 50;
      if (pauseEvery > 0 && callReads >= 0 && callReads++ > 0 && wall >= pauseAt) {
        jump += pauseMs;
        paused += 1;
        seed = Math.imul(seed, 1103515245) + 12345 & 0x7fffffff;
        pauseAt = wall + pauseEvery * (0.5 + seed / 0x7fffffff);
      }
      const t = wall - t0 + jump;
      return STEP === 0 ? t : Math.floor(t / STEP) * STEP;
    },
    web_write: (p, n, q) => {
      for (const b of new Uint8Array(forth.memory.buffer, p, n))
        output += String.fromCharCode(b);
      pins = q >>> 0;
      seen.push(pins);
    },
  }}).exports;
}
boot();

function take() {
  const s = output;
  output = "";
  return s;
}

// line: As the worker: what the slots printed before, then the line's own output.
function line(s) {
  const bytes = new Uint8Array(forth.memory.buffer, forth.web_line(), LINE + 1);
  for (let i = 0; i < s.length; i += 1)
    bytes[i] = s.charCodeAt(i);
  bytes[s.length] = 0;
  forth.web_run();
  const before = take();
  const start = now();
  forth.web_interpret();
  return { before, out: take(), ms: now() - start };
}

// idle: The console waiting for a line, the worker let run every GAP ms.
function idle(ms) {
  const end = now() + ms;
  while (now() < end) {
    const next = now() + GAP;
    while (now() < next)
      ;
    callReads = 0;
    forth.web_run();
    callReads = -1;
  }
  return take();
}

function check(what, ok, detail) {
  say((ok ? "ok      " : "FAILED  ") + what + (ok ? "" : ": " + JSON.stringify(detail)));
  if (!ok)
    failures += 1;
}

function number(out) {
  return parseInt(out.trim().split(/\s+/)[0], 10);
}

function near(value, expected, part) {
  return Math.abs(value - expected) <= expected * part + 2;
}

function stopAll() {
  let stops = "";
  for (let i = 0; i < 16; i += 1)
    stops += i + " stop ";
  check("every slot stopped", line(stops).out.endsWith("ok\n"));
  check("  slots lists none", /^\s*load [0-9.]+%  ok\n$/.test(line("slots").out), line("slots").out);
}

// The worker's script and the page's, a fault of syntax in either failing here.
const source = text(args[1]);
for (const [name, script] of [["worker.js", text(args[2])], ["view.js", text(args[3])],
     ["the page's script", source.slice(source.indexOf("<script>") + 8,
                                        source.indexOf("</script>", source.indexOf("<script>")))]]) {
  let fault = null;
  try {
    Function(script);
  } catch (e) {
    fault = String(e);
  }
  check(name + " parses", fault === null, fault);
}

// pend: The line pending() gives, as the page sends it.
const pend = (queue, pool) => {
  const item = V.pending(queue, pool);
  return item === null ? null : item.line;
};
const V = Function(text(args[3]) + " return { timeline, resolve, pending, abandon, answered, full, grainOf, coarse, time, share, rowNames, hit, span, WINDOWS, ZOOM };")();

forth.web_init();
take();

check("5 . prints 5", line("5 .").out === "5  ok\n", line("5 .").out);

// view: The pool as worker.js reads it (platform_web.c, WebView).
function view() {
  const v = new Uint32Array(forth.memory.buffer, forth.web_view(), 2 + 16 * 11);
  const name = (a, n) => String.fromCharCode(...new Uint8Array(forth.memory.buffer, a, n));
  const slots = [];
  for (let i = 0; i < 16; i += 1) {
    const e = 2 + i * 11;
    slots.push({ state: v[e], error: v[e + 1] === 0 ? "" : name(v[e + 9], v[e + 10]),
                 name: v[e + 3] === 0 ? "" : name(v[e + 2], v[e + 3]), runs: v[e + 4], max: v[e + 5],
                 over: v[e + 6], wcet: v[e + 7], budget: v[e + 8] });
  }
  return { load: v[0], bound: v[1], slots };
}

// telemetry: The pairs of the ring written since the last call and the clock, as worker.js
// sends them; trace: the same, read.
let traceTail = 0;
function telemetry() {
  const head = forth.web_trace_head() >>> 0, ring = new Uint32Array(forth.memory.buffer, forth.web_trace(), 8192 * 2);
  const pairs = [];
  for (traceTail = Math.max(traceTail, head - 8192); traceTail < head; traceTail += 1)
    pairs.push(ring[(traceTail % 8192) * 2], ring[(traceTail % 8192) * 2 + 1]);
  return { trace: pairs, clock: forth.web_clock() >>> 0 };
}
function trace() {
  const events = [], pairs = telemetry().trace;
  for (let k = 0; k < pairs.length; k += 2)
    events.push({ t: pairs[k], kind: pairs[k + 1] & 0xff, period: (pairs[k + 1] >> 8) & 0xff,
                  over: (pairs[k + 1] >> 16) & 1 });
  return events;
}

// The tour of the page, each line answered ok, the page's order and pace, and what each
// step must show: an ok follows a word's refusal too ("too few runs  ok").
const block = source.slice(source.indexOf("const STOP_ALL ="), source.indexOf("];", source.indexOf("const TOUR = [")) + 2);
const tour = Function("ZOOM", block + " return TOUR;")(V.ZOOM);
check("the page has a tour", tour.length >= 6, tour.length);
check("each step said in English and in French", tour.every((s) => ["en", "fr"].every((l) =>
      s[l] && s[l].title && s[l].text && typeof s[l].watch === "string")), "");

// Each text of the page has its French, each French its text.
const dictionary = (name) => Function(source.slice(source.indexOf("const " + name + " = {"),
                                      source.indexOf("};", source.indexOf("const " + name + " = {")) + 2) +
                                      " return " + name + ";")();
const keys = (attribute) => [...source.matchAll(new RegExp(attribute + '="(\\w+)"', "g"))].map((m) => m[1]);
for (const [attribute, name] of [["data-i18n", "FR"], ["data-i18n-label", "FR_LABEL"]]) {
  const fr = dictionary(name), html = keys(attribute);
  check("every " + attribute + " in French", html.every((k) => fr[k] !== undefined) &&
        Object.keys(fr).every((k) => html.includes(k)), [html.filter((k) => fr[k] === undefined),
        Object.keys(fr).filter((k) => !html.includes(k))]);
}
// Every text of the page's script said in both languages.
const T = Function("time", "share", source.slice(source.indexOf("const T = {"),
                   source.indexOf("};", source.indexOf("const T = {")) + 2) + " return T;")(V.time, V.share);
check("each text of the script in English and in French", Object.keys(T.en).sort().join() ===
      Object.keys(T.fr).sort().join(), [Object.keys(T.en), Object.keys(T.fr)]);

check("  in French, 0 and 1 singular", T.fr.runs(0) === "0 exécution" && T.fr.runs(1) === "1 exécution" &&
      T.fr.runs(2) === "2 exécutions" && T.en.runs(0) === "0 runs", [T.fr.runs(0), T.en.runs(0)]);
const summed = [200000, 1000000, 10000000].map((us) => T.fr.summary(V.span(us, "fr"), "", "").split(" :")[0]);
check("  in French, the number before dernières, in s from 1 s", summed.join("|") ===
      "Sur les 200 dernières ms|Sur la dernière seconde|Sur les 10 dernières secondes", summed);

// The timeline's rows named by the words bound in each period, the free slots left out;
// a row with none named "".
const bound = Array.from({ length: 16 }, () => ({ state: 0, name: "" }));
bound[4] = { state: 1, name: "work" };
bound[6] = { state: 2, name: "busy" };
bound[13] = { state: 2, name: "long" };
check("each row named by its words", JSON.stringify(V.rowNames(bound)) === '["","work busy","","long"]',
      V.rowNames(bound));

// The views of the timeline, shortest first: the steps of a cost seen at 20 ms, where a run
// of 500 us takes some width.
check("the timeline's views", V.WINDOWS.join() === "20000,50000,100000,200000,500000,1000000,2000000,5000000,10000000" &&
      V.ZOOM === 20000, V.WINDOWS);
check("  the steps of a cost seen at 20 ms", ["cost", "over", "admitted"].every((id) =>
      tour.find((x) => x.id === id).window === V.ZOOM) && tour.every((x) => x.window === undefined ||
      V.WINDOWS.includes(x.window)), tour.map((x) => x.window));

// The timeline (view.js), fed as the worker feeds it: rows 0 and 3, an instance of row 0
// each ms, of row 3 of 30 ms each second, preempted by row 0.
function feed(t, from, to) {
  const pairs = [];
  for (let at = from; at < to; at += 1000) {
    if (at % 1000000 === 0)
      pairs.push(at & 0x3fffffff, 1 | 3 << 8);
    pairs.push(at + 1 & 0x3fffffff, 1, at + 101 & 0x3fffffff, 2);
    if (at % 1000000 === 30000)
      pairs.push(at + 200 & 0x3fffffff, 2 | 3 << 8);
  }
  t.traced({ trace: pairs, clock: to & 0x3fffffff });
}
let t = V.timeline(() => {});
feed(t, 0, 13000000);
check("the timeline keeps the last 10 s", t.instances.length >= 10009 && t.instances.length <= 10020 && t.instances[0].start >= 2999000,
      [t.instances.length, t.instances[0]]);
const long = t.instances.find((i) => i.row === 3);
check("  an instance of 1 s, its runs between those of 1 ms", long.end - long.start === 30200 &&
      long.runs.length === 32, long);
t.pause();
const at = t.frozen;
feed(t, 13000000, 40000000);
check("  bounded while stopped too", t.instances.length <= 20040, t.instances.length);
check("  keeping the 10 s before it stopped", t.instances.filter((i) => i.end <= at && i.end >= at - 10000000).length >= 10009,
      t.instances.filter((i) => i.end <= at).length);
t.scroll(-100000000);
check("  scrolled back to the first of them", t.frozen === at - 10000000 + t.window, [t.frozen, at]);
t.window = 1000000;
t.reset();
check("  started anew, let go, of 200 ms", t.instances.length === 0 && t.frozen === null &&
      t.freezeRow === null && t.window === 200000, t);
let stops = 0;
t = V.timeline(() => { stops += 1; });
t.watch(3);
feed(t, 0, 2000000);
check("  stopped on the next long instance of its row, once", stops === 1 && t.frozen === 50200, [stops, t.frozen]);
t.watch(undefined);
check("  let go by a step that stops on none", t.frozen === null && t.freezeRow === null, t.frozen);
t = V.timeline(() => {});
feed(t, 0x3fff0000 - 0x3fff0000 % 1000000, 0x40100000);
check("  its clock past 2^30", t.clock === 0x40100000 && t.instances.every((i) => i.end === null || i.end > i.start),
      t.clock);

// Held under the pointer: the view stays where it was, what it shows kept, and Pause
// stops it there; let go, it follows the clock again.
t = V.timeline(() => {});
feed(t, 0, 1500000);
t.held = t.clock;
const held = t.held;
feed(t, 1500000, 5000000);
check("the timeline held under the pointer", t.edge() === held && t.frozen === null &&
      t.instances.some((i) => i.row === 3 && i.start === 1000000), [t.edge(), held]);
t.pause();
t.held = null;
check("  Pause there stops it there", t.frozen === held && t.edge() === held, [t.frozen, held]);
t.pause();
check("  let go, it follows the clock", t.edge() === t.clock, [t.edge(), t.clock]);
// Scrolled: the view stops where it was, then moves within what it keeps, Pause back to
// the clock.
t = V.timeline(() => {});
feed(t, 0, 3000000);
t.scroll(-500000);
check("the timeline scrolled stops and moves back", t.frozen === 2500000 && t.edge() === 2500000, t.frozen);
feed(t, 3000000, 4000000);
t.scroll(1000000000);
check("  no later than where it stopped", t.frozen === 3000000, t.frozen);
t.scroll(-1000000000);
check("  no earlier than its first instance", t.frozen === t.instances[0].start + t.window && t.instances[0].start === 0,
      [t.frozen, t.instances[0]]);
t.pause();
check("  Pause back to the clock", t.frozen === null && t.edge() === 4000000, t.edge());
t.held = 3500000;
t.scroll(-1000);
check("  held, stopped where it was held", t.frozen === 3499000, t.frozen);
t = V.timeline(() => {});
feed(t, 0, 12000000);
t.held = t.clock;
feed(t, 12000000, 30000000);
t.scroll(-1000000000);
check("  held long, scrolled back through the 10 s before", t.frozen === 12000000 - 10000000 + t.window, t.frozen);
t.window = 1000000;
t.scroll(0);
check("  a wider scale kept within them", t.frozen === 12000000 - 10000000 + 1000000, t.frozen);

// Zoomed: a scale step by step, kept within the views; stopped, about the time under the
// pointer; held, what a view of 10 s shows kept.
t = V.timeline(() => {});
t.zoom(1);
const finer = t.window;
t.zoom(20);
const widest = t.window;
t.zoom(-20);
check("the timeline zoomed a step, within its views", finer === 500000 && widest === 10000000 && t.window === 20000,
      [finer, widest, t.window]);
t.window = 200000;
feed(t, 0, 12000000);
t.scroll(-1000000);
t.zoom(-1, 10950000);
check("  stopped, about the time under the pointer", t.window === 100000 && t.frozen === 10975000, [t.window, t.frozen]);
t.zoom(1, 10000000);
t.zoom(1, 10000000);
check("  no later than where it stopped", t.window === 500000 && t.frozen === t.stopped, [t.frozen, t.stopped]);
t = V.timeline(() => {});
feed(t, 0, 12000000);
t.window = 10000000;
t.held = t.clock;
feed(t, 12000000, 30000000);
check("  held, the 10 s it shows kept", t.instances.some((i) => i.start === 3000001), t.instances[0]);
check("a span of the axis, in s from 1 s", V.span(2000000, "fr") === "2 s" && V.span(1250000, "fr") === "1,25 s" &&
      V.span(500000, "en") === "500 ms" && V.span(12500, "fr") === "12,5 ms", [V.span(1250000, "fr"), V.span(12500, "fr")]);

// hit: The instance of a row under the pointer, the nearest within the slack, none past it
// nor past the view's edges.
t = V.timeline(() => {});
feed(t, 0, 1000000);
const one = V.hit(t.instances, 0, 999050, 0, 0, 1000000), close = V.hit(t.instances, 0, 999140, 100, 0, 1000000);
const none = V.hit(t.instances, 0, 999500, 100, 0, 1000000);
const drawn = V.hit(t.instances, 0, 998990, 100, 0, 1000000), after = V.hit(t.instances, 0, 998990, 100, 0, 998990);
check("  the instance under the pointer", one !== null && one.start === 999001 && close === one &&
      none === null && drawn === one && after === null && V.hit(t.instances, 3, 15000, 0, 0, 1000000).start === 0,
      [one, close, none, drawn, after]);

// The lines of the tour, a word's slot named; the lines typed, as typed.
const pool = [{ state: 0, name: "" }, { state: 1, name: "work" }];
check("a line of the tour, the slot named", pend([{ line: "{work} stop", tour: true }], pool) === "1 stop");
check("  left out if none of that name", pend([{ line: "{gone} stop", tour: true }, { line: "1 .", tour: false }],
      pool) === "1 .");
check("  a line typed sent as typed", pend([{ line: ".\" {work}\"", tour: false }], pool) === ".\" {work}\"");

// A time written to the us, a run over its budget never written as the budget, nor a
// worst case admitted rounded.
let wrong = [];
for (let us = 0; us <= 1001000; us += us < 20000 ? 1 : 997)
  if (Math.round(parseFloat(V.time(us, "en")) * (/ ms/.test(V.time(us, "en")) ? 1000 : 1)) !== us)
    wrong.push(us);
check("a time written to the us", wrong.length === 0, wrong.slice(0, 5).map((us) => [us, V.time(us, "en")]));
check("  in French, a decimal comma", V.time(1500, "fr") === "1,5 ms" && V.time(1000, "fr") === "1 ms" &&
      V.time(800, "fr") === "800 µs" && V.share(700000, "fr") === "70,0 %", [V.time(1500, "fr"), V.share(700000, "fr")]);

const named = (n) => view().slots.findIndex((e) => e.state !== 0 && e.name === n);
const STEPS = {
  period: (out) => {
    seen = [];
    idle(300);
    check("  GP15 lit and put out", seen.some((q) => q & 1 << 15) && seen.some((q) => !(q & 1 << 15)), seen);
    check("  blink on trial in slot 8", named("blink") === 8 && view().slots[8].state === 1, view().slots[8]);
    for (const l of tour.find((x) => x.id === "period").lines) {
      const r = pend([{ line: l, tour: true }], view().slots);
      if (r !== null)
        line(r);
    }
    seen = [];
    idle(300);
    check("  the step typed again, GP15 still blinks", seen.some((q) => q & 1 << 15) && seen.some((q) => !(q & 1 << 15)) &&
          view().slots.filter((e) => e.state !== 0 && e.name === "blink").length === 1, seen);
  },
  cost: (out) => {
    idle(300);
    const e = view().slots[named("work")];
    check("  work measured within its budget of 1 ms", e.runs > 10 && e.max >= 500 && e.max <= 1000 &&
          e.budget === 1000 && e.over === 0, e);
  },
  over: (out) => {
    const e = view().slots[named("work")];
    check("  work over its budget, counted", e.over > 0, e);
    check("  its runs ended over in the trace", trace().some((x) => x.kind === 2 && x.period === 1 && x.over), "");
  },
  admitted: (out) => {
    check("  work promoted", /promoted, wcet /.test(out), out);
    const v = view(), e = v.slots[named("work")];
    check("  its budget its worst case, the load counted", e.state === 2 && e.budget === e.wcet &&
          v.load > 50000 && v.bound === 700000, [e, v.load]);
  },
  preempted: (out) => {
    let froze = 0;
    const t = V.timeline(() => { froze += 1; });
    t.watch(tour.find((x) => x.id === "preempted").freeze);
    telemetry();
    idle(2200);
    t.traced(telemetry());
    const long = t.instances.find((i) => i.row === 3 && i.end !== null && i.end - i.start >= 20000);
    check("  the 1 s task preempted by a shorter one", long !== undefined && long.runs.length > 1 &&
          t.instances.some((i) => i.row < 3 && i.start > long.start && i.end < long.end), long);
    check("  the timeline stopped on it, once", froze === 1 && t.frozen > long.start &&
          t.frozen - long.start <= 200000, [froze, t.frozen, long]);
    const e = view().slots[named("long")];
    check("  long measured at its own 80 ms", e.runs >= 1 && e.max >= 79000 && e.max <= 90000 && e.over === 0, e);
    const b = view().slots[named("busy")];
    check("  busy on time, never over", b.runs > 100 && b.over === 0, b);
  },
  refused: (out) => {
    check("  promotions until the bound, then refused", (out.match(/promoted, wcet/g) || []).length >= 5 &&
          /refused, load 7/.test(out), out);
    check("  the load under its bound", view().load <= 700000, view().load);
  },
  fails: (out) => {
    idle(100);
    check("  bad failed, its error kept", view().slots[named("bad")].error === "stack empty", view().slots[named("bad")]);
  },
  yours: (out) => check("  every slot free", view().slots.every((e) => e.state === 0), view().slots),
};
// play: A step typed as the page types it, each line answered ok, none left out, then
// what the step must show checked.
function play(s, label) {
  const lost = [];
  let out = "";
  for (const l of s.lines) {
    const r = pend([{ line: l, tour: true }], view().slots);
    const x = r === null ? null : line(r);
    if (x === null || !x.out.endsWith("ok\n") || /full|\?/.test(x.out))
      lost.push([l, x && x.out]);
    out += x === null ? "" : x.out;
  }
  check("step " + s.id + label, lost.length === 0, lost);
  check("  the step has its checks", STEPS[s.id] !== undefined, s.id);
  if (STEPS[s.id])
    STEPS[s.id](out);
}
for (const s of tour) {
  idle(300);
  play(s, "");
}
check("no release skipped by the tour", number(line("overruns .").out) <= SKIPS, line("overruns .").out);

// The steps typed again, out of order, each still showing what it says.
for (const id of ["cost", "admitted", "refused", "period", "fails", "fails", "fails", "fails", "fails", "refused",
                  "admitted", "preempted", "cost", "yours"]) {
  idle(250);
  play(tour.find((x) => x.id === id), " again");
}
stopAll();
traceTail = forth.web_trace_head() >>> 0;

// The catch-up: a slot of 10 ms that runs long, a task of 1 ms beside it, the worker let
// run every GAP ms; the clock passes the backlog at the console, not inside the slot.
line("variable n");
line(": tick  1 n +! ;");
line("' tick 1 every");
line(": slow  0 begin 1+ dup 100000 = until drop ;");
line("' slow 10 every");
let before = number(line("n @ .").out), start = now();
idle(2000);
let runs = number(line("n @ .").out) - before, spent = now() - start;
check("1 ms counted once a ms over 2 s, the worker held", near(runs, spent, 0.05), [runs, spent]);
check("  no release skipped", number(line("overruns .").out) <= SKIPS, line("overruns .").out);
stopAll();

// The slots preempt the console: a word typed that runs long does not hold the tick.
line("' tick 1 every");
line(": spin  0 begin 1+ dup 3000000 = until drop ;");
let r = line("n @ spin n @ swap - .");
check("the console's word preempted, once a ms", near(number(r.out), r.ms, 0.05), [r.out, r.ms]);

// And one another: a word of 1 s on top of which the tick runs.
line(": long  0 begin 1+ dup 3000000 = until drop ;");
line("' long 1000 every");
before = number(line("n @ .").out);
start = now();
idle(2500);
runs = number(line("n @ .").out) - before;
spent = now() - start;
check("1 ms runs on top of a slot of 1 s", near(runs, spent, 0.05), [runs, spent]);
check("  no release skipped", number(line("overruns .").out) <= SKIPS, line("overruns .").out);
check("  the slot of 1 s ran", /long trial runs [1-9]/.test(line("slots").out), line("slots").out);
stopAll();

// What a slot prints waits for the line to end, as on the board.
line(": hi  .\" x\" ;");
line("' hi 10 every");
r = line("1 2 + . spin 4 5 + .");
check("a slot's output not in the line's", r.out.startsWith("3 9  ok\n") && /^x*$/.test(r.out.slice(8)), r.out);
check("  but after it", r.out.length > 8, r.out);
check("  nothing dropped", number(line("dropped .").out) === 0, line("dropped .").out);
stopAll();
idle(100);

// Two SPENDs in one run hold the word's own time of both, 800 us, within its budget of
// 1 ms: read to the ms, the second, counted from its own start, waited a whole step more,
// and the run read 2 ms, over its budget, as a step of the clock before one SPEND did.
line(": twice  300 spend 500 spend ;");
r = line("' twice 10 every");
idle(300);
let e = view().slots[number(r.out.replace("slot", ""))];
check("two spends of a run held as one, within its budget", e.runs > 10 && e.max >= 800 - STEP * 1000 &&
      e.max <= 1000 && e.over === 0, e);
stopAll();
idle(100);

// Short pauses of the browser in the runs of a promoted word are not its own: in Chrome,
// 0.2 to 0.6 ms, under WEB_STALL_US, made a word of 500 us promoted with a budget of
// 626 us run past it (2026-10-29); here 0.15 and 0.3 ms. Read to the ms, such a pause is
// under a step and cannot be seen. churn, a loop of some 400 us, reads the clock only at
// the Forth's checks of its budget, every few us: a jump past a step there is a pause too
// (a review). A word promoted gets the longest run of its trial and a quarter as budget;
// churn's own time varies more than that under jsc as it compiles and collects, and a
// trial of 300 ms let it pass its budget by itself, up to 7 runs in 100: its trial is
// longer (2026-10-30). Under node it is left out: there churn passed its budget by itself
// on the CI's runner, 6 runs in 100, and the code that counted the pauses went over it no
// more often than the code that leaves them out, 0 runs in 100 for both (2026-10-30).
if (STEP < 1) {
  line("variable n  20000 n !");
  line(": churn  0 begin 1+ dup n @ = until drop ;");
  r = line("now churn now swap - .");
  check("churn timed", number(r.out) > 0, r.out);
  line(Math.round(20000 * 400 / number(r.out)) + " n !");
  line(": work  500 spend ;");
}
for (const [word, ms] of STEP < 1 ? [["work", 0.15], ["work", 0.3]].concat(node ? [] : [["churn", 0.3]]) : []) {
  r = line("' " + word + " 10 every");
  const slot = number(r.out.replace("slot", ""));
  idle(word === "churn" ? 1500 : 300);
  line(slot + " promote");
  const before = view().slots[slot].runs, from = paused;
  pauseEvery = 1;
  pauseMs = ms;
  idle(1000);
  pauseEvery = 0;
  e = view().slots[slot];
  check("short pauses of the browser left out of a promoted " + word + ", of " + ms + " ms", e.state === 2 &&
        e.runs - before > 80 && paused - from > 20 && e.over === 0, [e, before, paused - from]);
  stopAll();
  idle(100);
}

// A pause of the browser inside a call is left out of the clock: the browser's clock
// jumping 50 ms while the console waits 300 ms, the wait still takes 300, not 250.
stallIn = 1000;
r = line("now 300 ms now swap - .");
check("a pause of the browser left out of the clock", near(number(r.out), 300000, 0.02) && r.ms >= 295, [r.out, r.ms]);
stallIn = -1;

// A line that does not end is stopped after 10 s, the console answering after it.
line(": forever  begin again ;");
r = line("forever");
check("a line that loops interrupted", r.out.includes("interrupted"), r.out);
check("  after 10 s", r.ms >= 10000 && r.ms < 11000, r.ms);
check("  the console answers after it", line("5 .").out === "5  ok\n");

// A period whose slots are all taken: the page says so apart from a full dictionary, that
// stop frees one, not Restart.
stopAll();
line(": w ;");
let crowded = "";
for (let k = 0; k < 5; k += 1)
  crowded = line("' w 1000 every").out;
check("a period full, the page says so apart", V.full(crowded) === "period", crowded);
stopAll();

// The tour typed again and again fills the dictionary: the page then says so, and that
// Restart empties it.
let filled = null;
for (let k = 0; k < 400 && filled === null; k += 1)
  for (const l of tour.find((x) => x.id === "cost").lines) {
    const out = line(pend([{ line: l, tour: true }], view().slots)).out;
    if (/dictionary full/.test(out))
      filled = out;
  }
check("the tour again and again fills the dictionary, the page says so", filled !== null && V.full(filled) === "dictionary",
      filled);

// A line of the tour that fails leaves the rest of its step out: the words it defines
// would else be bound as they were before, or the slider set a variable not read.
boot();
forth.web_init();
take();
for (const [l, expected] of [["5 .", true], [": w  1 ;", true], ["nothing", false], ["1 0 /", false],
                             ["0 promote", true]]) {
  const out = line(l).out;
  check("a line " + (expected ? "answered: " : "failed: ") + l, V.answered(out) === expected, out);
}
line(": hi  .\" x\" ;");
line("' hi 10 every");
line(": spin  0 begin 1+ dup 3000000 = until drop ;");
const printed = line("spin").out;
check("  a slot's output after the ok, answered", /ok\nx/.test(printed) && V.answered(printed), printed);
const queue = [{ line: "1 .", tour: true }, { line: "2 .", tour: false }, { line: "3 .", tour: true }];
V.abandon(queue);
check("  the rest of the tour's lines left out, not those typed", queue.length === 1 && queue[0].line === "2 .", queue);

// A clock read to 16.7 ms, as Firefox's resistFingerprinting reads it: the page says the
// kernel cannot keep time, not with a clock to the ms or finer.
for (const [to, expected] of [[16.667, true], [1, false], [0.1, false], [0, false]]) {
  const g = V.grainOf(() => to === 0 ? now() : Math.floor(now() / to) * to);
  check("a clock read to " + to + " ms " + (expected ? "" : "not ") + "said too coarse", V.coarse(g) === expected &&
        (to === 0 || Math.abs(g - to * 1000) <= 1), g);
}

// Each step of the tour on a Forth anew, as after a Restart: each stands alone.
for (const s of tour) {
  boot();
  forth.web_init();
  take();
  traceTail = 0;
  play(s, " alone");
}

say(failures === 0 ? "all passed" : failures + " FAILED");
if (failures !== 0) {
  if (node)
    process.exit(1);
  else
    quit(1);
}
