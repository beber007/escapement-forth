#!/bin/sh
# Copyright (c) 2026 Bertrand Hurst. All rights reserved.
# Escapement Forth - a Forth console beside the Escapement real-time OS.
# Distributed under the terms of LICENSE at the root of this repository.
#
# check_page.sh: The page of build/ loaded in a headless Chrome, in English and in French:
# no error of its scripts, the Forth ready, the tour's first step written: test_web.js
# parses the page's script but does not run it, and a page that parses may still fail
# as it runs. In real time: Chrome's virtual time moves its clock in leaps, which the
# page takes for a clock too coarse. A copy of the page says, after SETTLE s, what it
# shows and the errors it met, to the server, whose log is read; and whether the tour
# could be run before the Forth was ready, its clock not yet measured. Then it runs the
# first step, a line sent and answered, and says what the console and the pool show,
# and where the focus is once Next has gone through the tour, and what the timeline says
# under the mouse; whether it stays held as the page scrolls and over its card, and goes
# on once the mouse has left the card; whether a drag, the arrow keys once it has the
# focus and a sideways wheel scroll it back, and a click alone does not. Each Chrome has
# WAIT s to say it: a cold one on the CI's runner took more than 19 s (2026-10-25).
#   check_page.sh chrome build-directory [port]
chrome=$1
build=$2
port=${3:-8765}
SETTLE=3
WAIT=45
work=$(mktemp -d)
trap 'kill $browser $server 2>/dev/null; wait 2>/dev/null; rm -rf "$work"' EXIT
mkdir "$work/site"
cp -R "$build"/. "$work/site/"
perl -0pi -e 's{<head>}{<head>
<script>
  window.errors = [];
  addEventListener("error", (e) => errors.push(String(e.message)));
  addEventListener("unhandledrejection", (e) => errors.push(String(e.reason)));
  console.error = ((said) => (...a) => { errors.push(a.join(" ")); said(...a); })(console.error);
</script>}; s{</body>}{<script>
  const early = document.getElementById("step-run").disabled;
  setTimeout(() => {
    const shown = { early, run: !document.getElementById("step-run").disabled,
                    status: document.getElementById("status").className,
                    lang: document.documentElement.lang,
                    code: document.getElementById("step-code").innerHTML };
    document.getElementById("step-run").click();
    const next = document.getElementById("step-next");
    next.focus();
    while (!next.disabled)
      next.click();
    shown.focus = document.activeElement.id;
    shown.note = document.querySelector("#output .note").lang;
    const pause = document.getElementById("pause"), banner = document.getElementById("frozen");
    shown.banner = [banner.hidden || banner.textContent];
    pause.click();
    shown.banner.push(banner.hidden || banner.textContent);
    pause.click();
    shown.banner.push(banner.hidden || banner.textContent);
    const plot = document.getElementById("timeline"), box = plot.getBoundingClientRect();
    const tip = document.getElementById("tip");
    plot.dispatchEvent(new PointerEvent("pointermove", { pointerType: "mouse", bubbles: true,
      clientX: box.right - 30, clientY: box.top + (box.height - 18) / 10 }));
    shown.tip = [tip.hidden ? null : tip.textContent, banner.textContent];
    dispatchEvent(new Event("scroll"));
    shown.tip.push(!tip.hidden);
    plot.dispatchEvent(new PointerEvent("pointerleave", { pointerType: "mouse" }));
    shown.tip.push(tip.hidden);
    // The edge drawn, layout.right, against the one held: the page script shares them.
    const card = plot.closest(".card"), scale = document.querySelector("[data-window]").getBoundingClientRect();
    card.dispatchEvent(new PointerEvent("pointerleave", { pointerType: "mouse" }));
    const free = layout.right;
    setTimeout(() => {
      shown.moved = layout.right - free;
      document.querySelector("[data-window]").dispatchEvent(new PointerEvent("pointermove", { pointerType: "mouse",
        bubbles: true, clientX: scale.left + 5, clientY: scale.top + 5 }));
      const held = view.held;
      setTimeout(() => {
        shown.still = held !== null && view.held === held && layout.right === held;
        card.dispatchEvent(new PointerEvent("pointerleave", { pointerType: "mouse" }));
        // A click alone leaves it going; a drag of a few pixels stops it, moved back.
        const press = (type, x) => plot.dispatchEvent(new PointerEvent(type, { pointerType: "mouse", pointerId: 1,
          button: 0, bubbles: true, clientX: x, clientY: box.top + 20 }));
        press("pointerdown", box.right - 100);
        press("pointermove", box.right - 98);
        press("pointerup", box.right - 98);
        shown.dragged = [banner.textContent];
        press("pointerdown", box.right - 100);
        press("pointermove", box.right - 98);
        press("pointermove", box.right - 50);
        press("pointerup", box.right - 50);
        shown.dragged.push(banner.textContent, view.stopped - view.frozen);
        pause.click();
        plot.dispatchEvent(new KeyboardEvent("keydown", { key: "ArrowRight", bubbles: true }));
        shown.dragged.push(banner.textContent);
        // As wide as on a phone, scrolled back 2 s, a label at each edge: none over another.
        plot.style.width = "294px";
        const context = plot.getContext("2d"), fill = context.fillText, labels = [];
        context.fillText = function (text, x, y) {
          if (this.textAlign === "center")
            labels.push([x - this.measureText(text).width / 2, x + this.measureText(text).width / 2]);
          return fill.apply(this, arguments);
        };
        view.window = 1000000;
        view.scroll(-2000000);
        // The labels of one frame: those drawn after the next one begins.
        requestAnimationFrame(() => { labels.length = 0; requestAnimationFrame(() => {
        context.fillText = fill;
        plot.style.width = "";
        const drawn = labels.slice().sort((p, q) => p[0] - q[0]);
        shown.labels = drawn.length > 0 && drawn.every((p, k) => k === 0 || p[0] >= drawn[k - 1][1]) ? true : drawn;
        pause.click();
        plot.focus();
        shown.focus = [shown.focus, document.activeElement === plot];
        plot.dispatchEvent(new KeyboardEvent("keydown", { key: "ArrowLeft", bubbles: true }));
        shown.scrolled = [banner.textContent];
        shown.back = [view.stopped - view.frozen, view.window];
        pause.click();
        plot.dispatchEvent(new WheelEvent("wheel", { deltaX: -40, bubbles: true, cancelable: true }));
        shown.scrolled.push(banner.textContent);
        pause.click();
        plot.dispatchEvent(new WheelEvent("wheel", { deltaY: -40, bubbles: true, cancelable: true }));
        shown.scrolled.push(banner.textContent);
        fetch("/probe?" + encodeURIComponent(JSON.stringify(Object.assign(shown, {
          errors, output: document.getElementById("output").textContent,
          pool: document.querySelector("#pool").textContent }))));
        }); });
      }, 1000);
    }, 1000);
  }, '$SETTLE'000);
</script>
</body>}' "$work/site/index.html"

# The server ours, serving the page: on a port taken, Chrome would load another's.
if curl -s "http://localhost:$port/" >/dev/null; then
  echo "FAILED  port $port taken"
  exit 1
fi
python3 -m http.server "$port" --directory "$work/site" 2>"$work/server.log" >/dev/null &
server=$!
tries=0
until curl -sf "http://localhost:$port/view.js" >/dev/null; do
  tries=$((tries + 1))
  if ! kill -0 $server 2>/dev/null || [ $tries -gt 50 ]; then
    echo "FAILED  no server of the page on port $port"
    exit 1
  fi
  sleep 0.1
done

failed=0
for lang in en fr; do
  : >"$work/server.log"
  "$chrome" --headless --no-sandbox --user-data-dir="$work/$lang" --enable-logging=stderr --v=0 \
    --remote-debugging-port=0 --accept-lang=$lang --lang=$lang "http://localhost:$port/" \
    >/dev/null 2>"$work/$lang.log" &
  browser=$!
  deadline=$(($(date +%s) + WAIT))
  until grep -q "GET /probe" "$work/server.log" || [ "$(date +%s)" -ge $deadline ]; do
    sleep 0.1
  done
  kill $browser 2>/dev/null
  wait $browser 2>/dev/null
  shown=$(python3 -c '
import re, sys, urllib.parse
m = re.search(r"GET /probe\?(\S+)", open(sys.argv[1]).read())
print(urllib.parse.unquote(m.group(1)) if m else "{}")' "$work/server.log")
  errors=$(grep -i "uncaught" "$work/$lang.log")
  check() { python3 -c "import json, re, sys; s = json.loads(sys.argv[1]); sys.exit(0 if $1 else 1)" "$shown"; }
  if [ -n "$errors" ]; then
    echo "FAILED  $lang: $errors"
    failed=1
  elif [ "$shown" = "{}" ]; then
    echo "FAILED  $lang: the page said nothing in $WAIT s; Chrome's log ends:"
    tail -n 20 "$work/$lang.log"
    failed=1
  elif ! check 's["errors"] == []'; then
    echo "FAILED  $lang: errors: $shown"
    failed=1
  elif ! check 's["status"] == "status ready"'; then
    echo "FAILED  $lang: the Forth not ready: $shown"
    failed=1
  elif ! check 's["lang"] == "'$lang'"'; then
    echo "FAILED  $lang: the page not in $lang: $shown"
    failed=1
  elif ! check 're.match(r"<span lang=\"'$lang'\">[^<]+</span>\n: blink ", s["code"])'; then
    echo "FAILED  $lang: the tour's first step not written: $shown"
    failed=1
  elif ! check 's["early"] and s["run"]'; then
    echo "FAILED  $lang: the tour could be run before the Forth was ready, or not after: $shown"
    failed=1
  elif ! check 's["focus"][0] == "step-back" and s["note"] == "'$lang'"'; then
    echo "FAILED  $lang: the focus lost at the tour's end, or the page's notes not in $lang: $shown"
    failed=1
  elif ! check 's["banner"][0] == "" and s["banner"][1] not in ("", True) and s["banner"][2] == ""'; then
    echo "FAILED  $lang: the timeline stopped without saying so: $shown"
    failed=1
  elif ! check 's["tip"][0] is not None and s["tip"][0].startswith({"en": "Task 1 ms", "fr": "Tâche 1 ms"}["'$lang'"]) and s["tip"][1] == "" and s["tip"][3]'; then
    echo "FAILED  $lang: the timeline said nothing under the mouse, or kept it: $shown"
    failed=1
  elif ! check 's["tip"][2]'; then
    echo "FAILED  $lang: the timeline let go as the page scrolled, the mouse still over it: $shown"
    failed=1
  elif ! check 's["moved"] > 500000'; then
    echo "FAILED  $lang: the timeline held, the mouse gone from its card: $shown"
    failed=1
  elif ! check 's["still"]'; then
    echo "FAILED  $lang: the timeline let go, the mouse still over its card: $shown"
    failed=1
  elif ! check 's["scrolled"][0] == s["banner"][1] and s["scrolled"][1] == s["banner"][1] and s["scrolled"][2] == "" and s["back"][0] == s["back"][1] / 10'; then
    echo "FAILED  $lang: the timeline not scrolled back by the arrow keys or a sideways wheel, or by a wheel up: $shown"
    failed=1
  elif ! check 's["labels"] is True'; then
    echo "FAILED  $lang: labels of the axis over one another: $shown"
    failed=1
  elif ! check 's["focus"][1]'; then
    echo "FAILED  $lang: the timeline cannot have the focus: $shown"
    failed=1
  elif ! check 's["dragged"][0] == "" and s["dragged"][1] == s["banner"][1] and s["dragged"][2] > 0 and s["dragged"][3] == ""'; then
    echo "FAILED  $lang: the timeline stopped by a click or a later arrow, or not by a drag: $shown"
    failed=1
  elif ! check '"slot 8  ok" in s["output"] and "blink" in s["pool"]'; then
    echo "FAILED  $lang: the tour's first step not run: $shown"
    failed=1
  else
    echo "ok      the page in $lang"
  fi
done
exit $failed
