// Copyright (c) 2026 Bertrand Hurst. All rights reserved.
// Escapement Forth - a Forth console beside the Escapement real-time OS.
// Distributed under the terms of LICENSE at the root of this repository.
//
// test_web.js: forth.wasm driven as worker.js drives it, without the page, under Node
// (node test_web.js forth.wasm index.html worker.js) or JavaScriptCore (jsc test_web.js
// -- forth.wasm index.html worker.js). The worker and the page's script are parsed, not
// run: what they do is copied here. The worker is let run every GAP ms, as Chrome lets it, the
// time in between caught up by the kernel: with the page called without a pause, the
// clock ran faster than the wall inside a long slot and went unseen (2026-10-25).

const node = typeof process !== "undefined";
const args = node ? process.argv.slice(2) : arguments;
const now = node ? () => performance.now() : () => preciseTime() * 1000;
const read = node ? (p) => require("fs").readFileSync(p) : (p) => new Uint8Array(readFile(p, "binary"));
const text = node ? (p) => require("fs").readFileSync(p, "utf8") : (p) => readFile(p);
const say = node ? (s) => process.stdout.write(s + "\n") : print;

const GAP = 30;      // ms between two runs of the worker, as in Chrome
const LINE = 128;    // FORTH_LINE

let forth, output = "", pins = 0, seen = [], failures = 0;
const t0 = now();
forth = new WebAssembly.Instance(new WebAssembly.Module(read(args[0])), { env: {
  web_now: () => now() - t0,
  web_write: (p, n, q) => {
    for (const b of new Uint8Array(forth.memory.buffer, p, n))
      output += String.fromCharCode(b);
    pins = q >>> 0;
    seen.push(pins);
  },
}}).exports;

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
    forth.web_run();
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
for (const [name, script] of [["worker.js", text(args[2])],
     ["the page's script", source.slice(source.indexOf("<script>") + 8, source.indexOf("</script>"))]]) {
  let fault = null;
  try {
    Function(script);
  } catch (e) {
    fault = String(e);
  }
  check(name + " parses", fault === null, fault);
}

forth.web_init();
take();

check("5 . prints 5", line("5 .").out === "5  ok\n", line("5 .").out);

// The examples of the page, each line answered ok, the page's order and pace, and what
// some print: an ok follows a word's refusal too ("too few runs  ok").
const EXPECT = {
  "1000 ms 8 promote": /^promoted, wcet /,
  "0 ' risky catch .": /^42 /,
  "1 ' risky catch .": /^0 /,
  "200 ms slots": /slow trial runs [1-9][0-9]* max [0-9]+ over [1-9]/,
};
const block = source.slice(source.indexOf("const EXAMPLES = [") + "const EXAMPLES = ".length);
const examples = Function("return " + block.slice(0, block.indexOf("];") + 1))();
check("the page has examples", examples.length >= 6, examples.length);
for (const [label, lines] of examples) {
  idle(300);
  for (const l of lines) {
    const r = line(l);
    check("example \"" + label + "\": " + l, r.out.endsWith("ok\n"), r.out);
    if (EXPECT[l])
      check("  as expected", EXPECT[l].test(r.out), r.out);
    if (l === "1000 ms n @ .")
      check("  10 ms counted 100 times in 1 s", near(number(r.out), 100, 0.03), r.out);
  }
  if (label.startsWith("blink GP15")) {
    seen = [];
    idle(300);
    check("  GP15 lit and put out", seen.some((q) => q & 1 << 15) && seen.some((q) => !(q & 1 << 15)), seen);
  }
}
check("no release skipped by the examples", number(line("overruns .").out) === 0, line("overruns .").out);

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
check("  no release skipped", number(line("overruns .").out) === 0, line("overruns .").out);
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
check("  no release skipped", number(line("overruns .").out) === 0, line("overruns .").out);
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

// A line that does not end is stopped after 10 s, the console answering after it.
line(": forever  begin again ;");
r = line("forever");
check("a line that loops interrupted", r.out.includes("interrupted"), r.out);
check("  after 10 s", r.ms >= 10000 && r.ms < 11000, r.ms);
check("  the console answers after it", line("5 .").out === "5  ok\n");

say(failures === 0 ? "all passed" : failures + " FAILED");
if (failures !== 0) {
  if (node)
    process.exit(1);
  else
    quit(1);
}
