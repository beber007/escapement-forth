# Review instructions

The reviews of this repository follow the plugin
[`review-method`](https://github.com/beber007/review-method), at the commit that
`.claude/settings.json` pins. `CLAUDE.md` brings this file in, so that `/code-review`
reads it. The first four sections are for each review. The last three are for the
plugin.

## What Important means here

Important means a wrong behaviour, or a false statement. Here, that is above all:

- A word reads or writes outside the memory that is there, or past the code space
  (9e60635, a2d2c9d).
- A slot that preempts another finds a state half written. The host runs the slots on
  one thread, so no host test reaches that window (a2d2c9d).
- Core 0 and core 1 share data with no order between them: a reader takes two samples
  mixed (#53).
- A word on trial makes a promoted word miss its deadline (#25).
- A word holds its budget on one board, and not on another (#52).
- A restart loses what it must keep, or runs over core 1's stack (a2d2c9d).
- The web page and a Pico answer differently, and no text says so (#46).
- A text lies: a figure not measured, a measurement without its date or its board.
- A test or a CI job now skips, without a word, what it ran before.

Style, naming and rewording are Nit at most.

## Cap the nits

Report at most five Nits per review, except for a change of documentation alone:
there, report every one, since its plain style is part of what it is reviewed for.

## Do not report

- The code of the submodules: `escapement/`, `lib/cyw43-driver/` and `lib/btstack/`.
  Escapement has its own reviews.

## Always check

- Each fault fixed: a host test seen to fail on it first, or, where the host cannot
  reach it, the reason written in the commit.
- Each change on the side of the board: the Pico, the Pico 2 and the Pico W. The
  RP2040 and the RP2350 differ in their memory, their timer and their cores.
- Each change of what a Pico does: does the web page's model (`web/chip_web.c`,
  `web/platform_web.c`, `web/kernel/`, and the clock of `web/worker.js`) do the same,
  or does a text say how it differs?
- Each figure in a text: is it in the code, or in a measurement with its date and its
  board?

## How deep, by risk

- **Deep**, three runs: the Forth and its slots (`src/`); the port on the boards
  (`pico/`); a move of the submodule `escapement`.
- **Standard**, one run: anything else, such as the web page (`web/`), the examples,
  the tools, the tests and the CI.
- **Documentation alone**, a change of Markdown files only: one run, then one run on
  its fixes.

## Commands a review may run

- `"Bash(git submodule update --init)"`: fetch the submodules, which the builds need.
- `"Bash(make:*)"`: run the host tests, build the three images and the web page, and
  run its tests. Do not run `make -C web serve`: it does not return.
- `"Bash(arm-none-eabi-size:*)"`: read the size of an image.

A review runs no tool that reaches a board.

## Checks before the reviews

- `git fetch origin`, so that `origin/main` is the `main` of now.
- `git submodule update --init`, since a branch may move a submodule.
- `make -C test/host run`.
- `make -C pico`, `make -C pico BOARD=pico2` and `make -C pico BOARD=picow`.
- `make -C web` and `make -C web test`, then `make -C web page` where Chrome is.
  These three take another clang in `CLANG`. `make -C web test` takes another
  JavaScript engine in `JS` and `JS_ARGS` (`JS=.../jsc JS_ARGS=--`). `make -C web page`
  takes Chrome's path in `CHROME`, on a Mac:
  `CHROME="/Applications/Google Chrome.app/Contents/MacOS/Google Chrome"`.
