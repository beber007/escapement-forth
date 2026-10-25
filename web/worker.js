// Copyright (c) 2026 Bertrand Hurst. All rights reserved.
// Escapement Forth - a Forth console beside the Escapement real-time OS.
// Distributed under the terms of LICENSE at the root of this repository.
//
// worker.js: The Forth of forth.wasm, in a worker of its own so that a word that runs
// long holds the worker, not the page. The page sends lines; the worker sends back what
// the Forth printed and the pins, as it prints (platform_web.c): start before a line's
// output, done once the line has been interpreted, the page sending the next only then.

let forth = null;
const decoder = new TextDecoder();
const encoder = new TextEncoder();
const LINE = 128;   // FORTH_LINE

const imports = {
  env: {
    web_now: () => performance.now(),
    web_write: (text, length, pins) => {
      // A character may fall across two flushes: the decoder keeps its first bytes.
      const bytes = new Uint8Array(forth.memory.buffer, text, length);
      postMessage({ text: decoder.decode(bytes, { stream: true }), pins: pins >>> 0 });
    },
  },
};

WebAssembly.instantiateStreaming(fetch("forth.wasm"), imports).then((result) => {
  forth = result.instance.exports;
  forth.web_init();
  setInterval(() => forth.web_run(), 1);
  postMessage({ ready: true });
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
  postMessage({ done: true });
};
