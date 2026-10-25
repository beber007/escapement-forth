// Copyright (c) 2026 Bertrand Hurst. All rights reserved.
// Escapement Forth - a Forth console beside the Escapement real-time OS.
// Distributed under the terms of LICENSE at the root of this repository.
//
// worker.js: The Forth of forth.wasm, in a worker of its own so that a word that runs
// long holds the worker, not the page. The page sends lines; the worker sends back what
// the Forth printed and the pins, as it prints (platform_web.c): start before a line's
// output, done once the line has been interpreted, the page sending the next only then.

importScripts("view.js");   // grainOf

let forth = null;
const decoder = new TextDecoder();
const encoder = new TextEncoder();
const LINE = 128;          // FORTH_LINE
const TRACE = 8192;        // pairs of the ring of the trace (platform_web.c)
const VIEW_ENTRY = 11;     // words of an entry of the view
const SLOTS = 16;
const names = new TextDecoder();   // apart: decoder may hold the first bytes of a character
let traceTail = 0, lastTelemetry = -Infinity;

function text(address, length) {
  return names.decode(new Uint8Array(forth.memory.buffer, address, length));
}

// telemetry: What the kernel ran since the last time, and the pool, for the page to draw;
// at most every 50 ms unless forced, as before a line is said done, the page then taking
// the entries its next line names from it.
function telemetry(force) {
  const now = performance.now();
  if (!force && now - lastTelemetry < 50)
    return;
  lastTelemetry = now;
  const head = forth.web_trace_head() >>> 0;
  if (((head - traceTail) >>> 0) > TRACE)
    traceTail = (head - TRACE) >>> 0;
  const ring = new Uint32Array(forth.memory.buffer, forth.web_trace(), TRACE * 2);
  const trace = new Uint32Array(((head - traceTail) >>> 0) * 2);
  for (let k = 0; traceTail !== head; traceTail = (traceTail + 1) >>> 0, k += 2) {
    trace[k] = ring[(traceTail % TRACE) * 2];
    trace[k + 1] = ring[(traceTail % TRACE) * 2 + 1];
  }
  const v = new Uint32Array(forth.memory.buffer, forth.web_view(), 2 + SLOTS * VIEW_ENTRY);
  const slots = [];
  for (let i = 0; i < SLOTS; i += 1) {
    const e = 2 + i * VIEW_ENTRY;
    slots.push({ state: v[e], error: v[e + 1] === 0 ? "" : text(v[e + 9], v[e + 10]),
                 name: v[e + 3] === 0 ? "" : text(v[e + 2], v[e + 3]), runs: v[e + 4],
                 max: v[e + 5], over: v[e + 6], wcet: v[e + 7], budget: v[e + 8] });
  }
  postMessage({ telemetry: true, trace, clock: forth.web_clock() >>> 0, load: v[0],
                bound: v[1], slots });
}

const imports = {
  env: {
    web_now: () => performance.now(),
    web_write: (text, length, pins) => {
      // A character may fall across two flushes: the decoder keeps its first bytes.
      const bytes = new Uint8Array(forth.memory.buffer, text, length);
      postMessage({ text: decoder.decode(bytes, { stream: true }), pins: pins >>> 0 });
      telemetry(false);   // a line that runs long goes on being drawn
    },
  },
};

WebAssembly.instantiateStreaming(fetch("forth.wasm"), imports).then((result) => {
  forth = result.instance.exports;
  forth.web_init();
  setInterval(() => { forth.web_run(); telemetry(false); }, 1);
  postMessage({ ready: true, grain: grainOf(() => performance.now()) });
}).catch((error) => postMessage({ failed: String(error) }));

onmessage = (event) => {
  if (forth === null)
    return;
  const text = encoder.encode(event.data).slice(0, LINE);
  const line = new Uint8Array(forth.memory.buffer, forth.web_line(), LINE + 1);
  line.set(text);
  line[text.length] = 0;
  forth.web_run();   // the time passed before the line, and what the slots printed in it
  postMessage({ start: true });
  forth.web_interpret();
  telemetry(true);
  postMessage({ done: true });
};
