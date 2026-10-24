/* Copyright (c) 2026 Bertrand Hurst. All rights reserved.
** Escapement Forth - a Forth console beside the Escapement real-time OS.
** Distributed under the terms of LICENSE at the root of this repository.
*/
/* File forth.h: A minimal Forth, indirect threaded, written for the pool of slots.
**
** One core interprets and compiles what the console types; the other runs the words
** bound to its slots, from tasks of the kernel. Both run the same inner interpreter, each
** call with a context of its own, its stacks on the caller's: slots preempt one another
** on the one stack of the kernel. The dictionary only grows, so a word, once defined,
** never changes under a core that runs it; only defers do, by one store.
**
** Portable C: the host builds it for the tests, the Pico for the board. What depends on
** the platform, the output and the clock, it reaches through forth_platform.h.
*/

#ifndef FORTH_H
#define FORTH_H

#include <stdint.h>
#include <stddef.h>

typedef intptr_t Cell;

/* FORTH_KEEP: The section of what must outlive a restart of the kernel's core, which
** clears the rest: the dictionary, the console's state, the pool. The board's build puts
** it in .noinit; nothing there has a value until ForthInit gives it one. */
#ifndef FORTH_KEEP
   #define FORTH_KEEP
#endif

#define FORTH_STACK   16     /* cells of each stack of a context */
#define FORTH_WORDS   192    /* words of the dictionary, built in ones included */
#define FORTH_CODE    2048   /* cells of the code space, colon definitions and data */
#define FORTH_NAMES   1536   /* bytes of the names of the words defined at the console */
#define FORTH_LINE    128    /* bytes of a line of input */

#define WORD_IMMEDIATE  0x01
#define WORD_HIDDEN     0x02   /* a definition under way, found only once it ends */

/* Why a call stopped, FORTH_OK when its word returned. */
enum {
   FORTH_OK = 0, FORTH_UNDERFLOW, FORTH_OVERFLOW, FORTH_RETURN_OVERFLOW,
   FORTH_DIVISION, FORTH_FULL, FORTH_UNKNOWN, FORTH_COMPILE_ONLY, FORTH_NAME,
   FORTH_CONTROL, FORTH_ARGUMENT, FORTH_STALLED
};

typedef struct Word Word;
typedef struct Context Context;

struct Word {
   const char *Name;
   uint8_t Length;
   uint8_t Flags;
   void (*Code)(Context *, const Word *);
   Cell *Body;               /* a colon definition's cells, a variable's data */
   volatile Cell Value;      /* a constant's value, the word a defer runs */
};

struct Context {
   const Cell *IP;           /* the next cell of the definition under way, NULL out */
   int Depth, ReturnDepth, Error;
   Cell Data[FORTH_STACK];
   Cell Return[FORTH_STACK];
};

/* ForthInit: Builds the dictionary of the built in words, the platform's included
** (ForthPlatformWords). Called once, before any other. */
void ForthInit(void);

/* ForthAddWord: Adds a word of C to the dictionary, for a platform or the slots; FALSE,
** 0, when it is full. */
int ForthAddWord(const char *name, void (*code)(Context *, const Word *), uint8_t flags);

/* ForthInterpret: Interprets or compiles a line of the console, its text ended by a 0,
** and prints " ok" after it, or the error that stopped it. */
void ForthInterpret(const char *line);

/* ForthExecute: Runs a word in a context of the caller's, from a task or the console,
** until it returns or fails; returns FORTH_OK or the error. The context's stacks are
** left as the word left them. */
int ForthExecute(Context *c, const Word *w);

/* ForthFind: The word of that name, the last defined first, NULL for none. */
const Word *ForthFind(const char *name, int length);

/* ForthIsWord: TRUE, 1, if x is a word of the dictionary that can be found: a cell taken
** for one, run, would fault the core. */
int ForthIsWord(Cell x);

/* ForthOnConsole: TRUE, 1, for the console's context: the words that parse its input
** or write the dictionary, the slots' included, run there alone. */
int ForthOnConsole(const Context *c);

/* ForthErrorText: What an error of ForthExecute says. */
const char *ForthErrorText(int error);

/* ForthPrintNumber, ForthPrintDigits, ForthPrintText: Through the platform's ForthEmit;
** a number with a space after it as . prints it, or without. */
void ForthPrintNumber(Cell n);
void ForthPrintDigits(Cell n);
void ForthPrintText(const char *text);

/* For words of C: the stacks of the context, each checked. A word that finds too few
** cells or no room sets the error and returns; ForthExecute then stops. */
#define FORTH_NEED(c,n)  do { if ((c)->Depth < (n)) { (c)->Error = FORTH_UNDERFLOW; \
                                 return; } } while (0)
#define FORTH_ROOM(c,n)  do { if ((c)->Depth + (n) > FORTH_STACK) { \
                                 (c)->Error = FORTH_OVERFLOW; return; } } while (0)
#define FORTH_TOP(c)     ((c)->Data[(c)->Depth - 1])
#define FORTH_POP(c)     ((c)->Data[--(c)->Depth])
/* The value first: it may read the stack, whose depth the push then moves. */
#define FORTH_PUSH(c,v)  do { Cell pushed_ = (Cell)(v); \
                              (c)->Data[(c)->Depth++] = pushed_; } while (0)

#endif /* FORTH_H */
