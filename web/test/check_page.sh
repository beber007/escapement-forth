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
# and where the focus is once Next has gone through the tour.
#   check_page.sh chrome build-directory [port]
chrome=$1
build=$2
port=${3:-8765}
SETTLE=3
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
    shown.banner = banner.hidden;
    pause.click();
    shown.banner = [shown.banner, banner.hidden, banner.textContent];
    pause.click();
    shown.banner.push(banner.hidden);
    setTimeout(() => fetch("/probe?" + encodeURIComponent(JSON.stringify(Object.assign(shown, {
      errors, output: document.getElementById("output").textContent,
      pool: document.querySelector("#pool").textContent })))), 2000);
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
  tries=0
  until grep -q "GET /probe" "$work/server.log" || [ $tries -gt $((SETTLE * 10 + 120)) ]; do
    tries=$((tries + 1))
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
    echo "FAILED  $lang: the page said nothing in $SETTLE s"
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
  elif ! check 's["focus"] == "step-back" and s["note"] == "'$lang'"'; then
    echo "FAILED  $lang: the focus lost at the tour's end, or the page's notes not in $lang: $shown"
    failed=1
  elif ! check 's["banner"][:2] == [True, False] and s["banner"][2] != "" and s["banner"][3]'; then
    echo "FAILED  $lang: the timeline stopped without saying so: $shown"
    failed=1
  elif ! check '"slot 8  ok" in s["output"] and "blink" in s["pool"]'; then
    echo "FAILED  $lang: the tour's first step not run: $shown"
    failed=1
  else
    echo "ok      the page in $lang"
  fi
done
exit $failed
