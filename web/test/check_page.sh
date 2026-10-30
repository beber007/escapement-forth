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
# focus and a sideways wheel scroll it back, and a click alone does not; whether its
# scale steps by its buttons, the keys and a pinch; and whether each step fits a window
# of 1280 by 680: 823 high for Chrome, of which 143 go to what it draws around the page. Each Chrome has WAIT s to say it: a
# cold one on the CI's runner took more than 19 s (2026-10-25).
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
    // In the window of a small laptop, each step: all it speaks of in sight, without a scroll.
    const out = (selector) => {
      const e = document.querySelector(selector);
      return e.offsetParent !== null && e.getBoundingClientRect().bottom > innerHeight + 1;
    };
    const side = document.querySelector(".side");
    const unfit = () => (document.getElementById("step-watch").hidden = document.getElementById("step-watch").textContent === "", ["#terminal", ".board", "#timeline", "#gauge-text", "#pool", "#step-next", "#restart"].filter(out)
      .concat(side.scrollHeight > side.clientHeight + 1 ? ["tour"] : []));
    shown.size = [innerWidth, innerHeight];
    const runAt = () => Math.round(document.getElementById("step-run").getBoundingClientRect().top);
    shown.runAt = [runAt()];
    shown.unfit = [unfit()];
    while (!next.disabled) {
      next.click();
      shown.unfit.push(unfit());
      shown.runAt.push(runAt());
    }
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
    const card = plot.closest(".card"), scale = document.getElementById("finer").getBoundingClientRect();
    card.dispatchEvent(new PointerEvent("pointerleave", { pointerType: "mouse" }));
    const free = layout.right;
    setTimeout(() => {
      shown.moved = layout.right - free;
      document.getElementById("finer").dispatchEvent(new PointerEvent("pointermove", { pointerType: "mouse",
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
        const context = plot.getContext("2d"), fill = context.fillText, labels = [], column = [];
        context.fillText = function (text, x, y) {
          if (x === 0)
            column.push(y);
          if (this.textAlign === "center")
            labels.push([x - this.measureText(text).width / 2, x + this.measureText(text).width / 2]);
          return fill.apply(this, arguments);
        };
        view.window = 1000000;
        view.scroll(-2000000);
        const names = rowNames;
        rowNames = () => ["a", "b", "c", "d"];
        // The labels of one frame: those drawn after the next one begins.
        requestAnimationFrame(() => { labels.length = column.length = 0; requestAnimationFrame(() => {
        rowNames = names;
        // The names of the rows, each on a line of its own at any height of the timeline.
        shown.names = column.slice().sort((p, q) => p - q).every((y, k, all) => k === 0 || y - all[k - 1] >= 11);
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
        // The scale: a step finer or wider by its buttons, the keys and a pinch, within its views.
        const finer = document.getElementById("finer"), wider = document.getElementById("wider");
        const shownSpan = () => document.getElementById("span").textContent;
        view.window = 200000;
        finer.click();
        shown.scale = [shownSpan()];
        for (let k = 0; k < 12; k += 1)
          wider.click();
        shown.scale.push(shownSpan(), wider.getAttribute("aria-disabled"));
        plot.dispatchEvent(new KeyboardEvent("keydown", { key: "+", bubbles: true }));
        shown.scale.push(shownSpan());
        plot.dispatchEvent(new WheelEvent("wheel", { deltaY: 60, ctrlKey: true, bubbles: true, cancelable: true,
          clientX: box.right - 50, clientY: box.top + 20 }));
        shown.scale.push(shownSpan(), banner.textContent);
        for (let k = 0; k < 12; k += 1)
          plot.dispatchEvent(new KeyboardEvent("keydown", { key: "=", bubbles: true }));
        shown.scale.push(shownSpan(), finer.getAttribute("aria-disabled"));
        plot.dispatchEvent(new KeyboardEvent("keydown", { key: "-", ctrlKey: true, bubbles: true, cancelable: true }));
        shown.scale.push(shownSpan());
        // Stopped, a pinch keeps the time under the pointer where it is drawn; over the
        // column of the rows, that of the left edge.
        view.window = 200000;
        view.pause();
        const under = (x) => view.frozen - view.window + (Math.max(x, box.left + layout.label) - box.left - layout.label) * view.window / (layout.w - layout.label);
        const pinch = (x) => plot.dispatchEvent(new WheelEvent("wheel", { deltaY: -60, ctrlKey: true, bubbles: true,
          cancelable: true, clientX: x, clientY: box.top + 20 }));
        shown.pinched = [under(box.left + 200)];
        pinch(box.left + 200);
        shown.pinched.push(view.window, under(box.left + 200), under(box.left + 10));
        pinch(box.left + 10);
        shown.pinched.push(view.window, under(box.left + 10));
        view.pause();
        for (let k = 0; k < 12; k += 1)
          wider.click();
        // An instance of the console begun before the left edge, a run of it ended there:
        // nothing drawn over the column of the rows.
        view.pause();
        const left = view.frozen - view.window;
        const fake = { row: 4, start: left - 3000, end: null, over: false, runs: [[left - 3000, left - 2000], [left + 1000, null]] };
        view.instances.push(fake);
        const rect = context.fillRect, over = [];
        context.fillRect = function (x, y, w, h) {
          if (Math.min(x, x + w) < layout.label - 0.5)
            over.push([x, w]);
          return rect.apply(this, arguments);
        };
        requestAnimationFrame(() => { over.length = 0; requestAnimationFrame(() => {
        context.fillRect = rect;
        view.instances.splice(view.instances.indexOf(fake), 1);
        view.pause();
        shown.column = over;
        shown.summed = summary.textContent.split(":")[0];
        view.window = 200000;
        // The flow of a step: what to watch said once its lines are typed, what to watch
        // marked, and the one button then going to the next step.
        const run = document.getElementById("step-run"), watch = document.getElementById("step-watch");
        document.getElementById("step-back").click();
        while (step !== 0)
          document.getElementById("step-back").click();
        shown.flow = [watch.hidden, run.textContent];
        run.click();
        // A double click: the lines typed by then, the second must not skip what they show.
        setTimeout(() => run.click(), 100);
        setTimeout(() => {
          shown.flow.push(watch.hidden, run.textContent, document.querySelectorAll(".look").length);
          run.click();
          shown.flow.push(step, watch.hidden, document.querySelectorAll(".look").length);
          fetch("/probe?" + encodeURIComponent(JSON.stringify(Object.assign(shown, {
            errors, output: document.getElementById("output").textContent,
            pool: document.querySelector("#pool").textContent }))));
        }, 1000);
        }); });
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
    --remote-debugging-port=0 --window-size=1280,823 --accept-lang=$lang --lang=$lang "http://localhost:$port/" \
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
  check() { python3 -c "import json, re, sys; s = json.loads(sys.argv[1]); sys.exit(0 if ($1) else 1)" "$shown"; }
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
  elif ! check 's["names"]'; then
    echo "FAILED  $lang: the names of the rows over one another: $shown"
    failed=1
  elif ! check 's["labels"] is True'; then
    echo "FAILED  $lang: labels of the axis over one another: $shown"
    failed=1
  elif ! check 's["scale"][:6] == ["100 ms", "10 s", "true", "5 s", "10 s", ""] and s["scale"][6:] == ["20 ms", "true", "20 ms"] and s["summed"].strip() in ("In the last 10 s", "Sur les 10 dernières secondes")'; then
    echo "FAILED  $lang: the scale not stepped by its buttons, the keys or a pinch: $shown"
    failed=1
  elif ! check 'abs(s["pinched"][2] - s["pinched"][0]) < 1 and s["pinched"][1] == 100000 and abs(s["pinched"][5] - s["pinched"][3]) < 1 and s["pinched"][4] == 50000'; then
    echo "FAILED  $lang: a pinch, stopped, moved the time under the pointer: $shown"
    failed=1
  elif ! check 's["column"] == []'; then
    echo "FAILED  $lang: a run before the left edge drawn over the column of the rows: $shown"
    failed=1
  elif ! check 's["focus"][1]'; then
    echo "FAILED  $lang: the timeline cannot have the focus: $shown"
    failed=1
  elif ! check 's["dragged"][0] == "" and s["dragged"][1] == s["banner"][1] and s["dragged"][2] > 0 and s["dragged"][3] == ""'; then
    echo "FAILED  $lang: the timeline stopped by a click or a later arrow, or not by a drag: $shown"
    failed=1
  elif ! check 's["flow"][0] and not s["flow"][2] and s["flow"][1] != s["flow"][3] and s["flow"][4] >= 2 and s["flow"][5:] == [1, True, 0]'; then
    echo "FAILED  $lang: the steps of the tour do not flow: $shown"
    failed=1
  elif ! check 'len(set(s["runAt"])) == 1'; then
    echo "FAILED  $lang: the button that types the lines moves from step to step: $shown"
    failed=1
  elif ! check 's["size"][1] <= 680 and all(u == [] for u in s["unfit"])'; then
    echo "FAILED  $lang: a step needs a scroll: $shown"
    failed=1
  elif ! check 're.search(r"›\n: blink", s["output"])'; then
    echo "FAILED  $lang: the answer of a line that sets a step up shown: $shown"
    failed=1
  elif ! check '"slot 8  ok" in s["output"] and "blink" in s["pool"]'; then
    echo "FAILED  $lang: the tour's first step not run: $shown"
    failed=1
  else
    echo "ok      the page in $lang"
  fi
done
exit $failed
