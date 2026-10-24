/* Copyright (c) 2026 Bertrand Hurst. All rights reserved.
** Escapement Forth - a Forth console beside the Escapement real-time OS.
** Distributed under the terms of LICENSE at the root of this repository.
*/
/* File forth.c: The dictionary, the inner interpreter, and the outer one that interprets
** and compiles the console's lines (forth.h).
**
** Only the console's core writes the dictionary and the code space; a word that would,
** run by a slot, fails instead (ConsoleOnly). The other core reads them, and a defer,
** which it may find changed between two of its runs, never half changed.
*/

#include "forth.h"
#include "forth_platform.h"

static Word Dictionary[FORTH_WORDS] FORTH_KEEP;
static int Words FORTH_KEEP;
static Cell Code[FORTH_CODE] FORTH_KEEP;
static int Here FORTH_KEEP;
static char Names[FORTH_NAMES] FORTH_KEEP;
static int NamesUsed FORTH_KEEP;

/* The console's context, its data stack kept from one line to the next; the state of the
** outer interpreter, and its stack of the control structures being compiled. */
static Context Console FORTH_KEEP;
static const char *In FORTH_KEEP;
static int Compiling FORTH_KEEP;
static Word *Defining FORTH_KEEP;
static int DefinedWords FORTH_KEEP, DefinedHere FORTH_KEEP, DefinedNames FORTH_KEEP;
enum { CONTROL_IF = 1, CONTROL_ELSE, CONTROL_BEGIN, CONTROL_WHILE, CONTROL_DO };
static struct { int Kind, At; } Control[FORTH_STACK] FORTH_KEEP;
static int ControlDepth FORTH_KEEP;

static const Word *Lit FORTH_KEEP, *Branch FORTH_KEEP, *ZeroBranch FORTH_KEEP, *Do FORTH_KEEP,
                  *Loop FORTH_KEEP, *PlusLoop FORTH_KEEP, *DotQuote FORTH_KEEP, *Exit FORTH_KEEP;

static const char *const Errors[] = {
   "", "stack empty", "stack full", "return stack full", "division by zero",
   "dictionary full", "?", "compile only", "name expected", "unbalanced control",
   "bad argument", "stalled", "inside a definition", "over budget"
};


/* ---- The inner interpreter. */

static void DoColon(Context *c, const Word *w)
{
  if (c->ReturnDepth >= FORTH_STACK) {
     c->Error = FORTH_RETURN_OVERFLOW;
     return;
  }
  c->Return[c->ReturnDepth++] = (Cell)c->IP;
  c->IP = w->Body;
} /* end of DoColon */

static void DoConstant(Context *c, const Word *w)
{
  FORTH_ROOM(c,1);
  FORTH_PUSH(c,w->Value);
} /* end of DoConstant */

static void DoVariable(Context *c, const Word *w)
{
  FORTH_ROOM(c,1);
  FORTH_PUSH(c,w->Body);
} /* end of DoVariable */

/* DoDefer: Runs the word last bound to it, each read once: the other core may bind
** another meanwhile. A defer bound to a defer is followed in a loop, not by calling it,
** which on the board recursed on the C stack, and a chain of more than FORTH_STACK is
** taken for a cycle. */
static void DoDefer(Context *c, const Word *w)
{
  const Word *t = (const Word *)w->Value;
  int links = 0;
  while (t != NULL && t->Code == DoDefer && links++ < FORTH_STACK)
     t = (const Word *)t->Value;
  if (t == NULL || t->Code == DoDefer) {
     c->Error = FORTH_ARGUMENT;
     return;
  }
  t->Code(c,t);
} /* end of DoDefer */

/* PExit: Returns to the definition that called, whose cell DoColon pushed: inside a
** DO loop, the top of the return stack is the loop's mark instead (PDo), which no cell
** of the code space is. */
static void PExit(Context *c, const Word *w)
{
  Cell back;
  (void)w;
  if (c->ReturnDepth == 0) {
     c->IP = NULL;
     return;
  }
  back = c->Return[c->ReturnDepth - 1];
  if (back != 0 && ((uintptr_t)back < (uintptr_t)Code ||
                    (uintptr_t)back >= (uintptr_t)&Code[FORTH_CODE] ||
                    ((uintptr_t)back - (uintptr_t)Code) % sizeof(Cell) != 0)) {
     c->Error = FORTH_CONTROL;
     return;
  }
  c->ReturnDepth -= 1;
  c->IP = (const Cell *)back;
} /* end of PExit */

uint32_t ForthOwnTime(uint32_t start, const volatile uint32_t *excluded)
{
  uint32_t left, now;
  /* Read again while a slot preempted between the readings: the time left out read
  ** after the clock may hold more than the word spent, and the difference wraps; read
  ** before, the slot counts against the word (two reviews, 2026-10-24). */
  do {
     left = *excluded;
     now = ForthNow();
  } while (*excluded != left);
  return now - start - left;
} /* end of ForthOwnTime */

/* ForthExecute: The inner interpreter's NEXT, the one place every word of a definition
** goes through: where a budget is checked, words of C being short ones. */
int ForthExecute(Context *c, const Word *w)
{
  const Cell *outer = c->IP;
  int returnDepth = c->ReturnDepth;
  unsigned steps = 0;
  c->Error = FORTH_OK;
  c->IP = NULL;
  w->Code(c,w);
  while (c->IP != NULL && c->Error == FORTH_OK) {
     const Word *next = (const Word *)*c->IP++;
     next->Code(c,next);
     /* Not over an error of the word's, which stops it where the budget would not. */
     if (c->Error == FORTH_OK && c->Budget != 0 && ++steps % FORTH_BUDGET_STEPS == 0 &&
         ForthOwnTime(c->Start,c->Excluded) > c->Budget)
        c->Error = FORTH_BUDGET;
  }
  c->ReturnDepth = returnDepth;
  c->IP = outer;
  return c->Error;
} /* end of ForthExecute */


/* ---- Output. */

void ForthPrintText(const char *text)
{
  while (*text != 0)
     ForthEmit(*text++);
} /* end of ForthPrintText */

void ForthPrintDigits(Cell n)
{
  char digits[24];
  int i = 0;
  uintptr_t u = n < 0 ? -(uintptr_t)n : (uintptr_t)n;
  do {
     digits[i++] = (char)('0' + u % 10);
     u /= 10;
  } while (u != 0);
  if (n < 0)
     ForthEmit('-');
  while (i > 0)
     ForthEmit(digits[--i]);
} /* end of ForthPrintDigits */

void ForthPrintNumber(Cell n)
{
  ForthPrintDigits(n);
  ForthEmit(' ');
} /* end of ForthPrintNumber */


/* ---- The dictionary. */

static int SameName(const char *a, const char *b, int length)
{
  int i;
  for (i = 0; i < length; i += 1) {
     char x = a[i], y = b[i];
     if (x >= 'A' && x <= 'Z') x += 'a' - 'A';
     if (y >= 'A' && y <= 'Z') y += 'a' - 'A';
     if (x != y)
        return 0;
  }
  return 1;
} /* end of SameName */

const Word *ForthFind(const char *name, int length)
{
  int i;
  for (i = Words - 1; i >= 0; i -= 1) {
     const Word *w = &Dictionary[i];
     if (w->Length == length && !(w->Flags & WORD_HIDDEN) && SameName(w->Name,name,length))
        return w;
  }
  return NULL;
} /* end of ForthFind */

static Word *NewWord(const char *name, int length, void (*code)(Context *, const Word *),
                     uint8_t flags)
{
  Word *w;
  if (Words >= FORTH_WORDS)
     return NULL;
  /* Filled before it is counted: the other core may look a word up meanwhile. */
  w = &Dictionary[Words];
  w->Name = name;
  w->Length = (uint8_t)length;
  w->Flags = flags;
  w->Code = code;
  w->Body = &Code[Here];
  w->Value = 0;
  ForthBarrier();
  Words += 1;
  return w;
} /* end of NewWord */

int ForthAddWord(const char *name, void (*code)(Context *, const Word *), uint8_t flags)
{
  int length = 0;
  while (name[length] != 0)
     length += 1;
  return NewWord(name,length,code,flags) != NULL;
} /* end of ForthAddWord */

/* ConsoleOnly: Words that parse the console's input or write the dictionary run in its
** context alone: a slot's task that met one would race the console's core. */
#define CONSOLE_ONLY(c)  do { if (!ForthOnConsole(c)) { (c)->Error = FORTH_ARGUMENT; \
                                 return; } } while (0)

int ForthIsWord(Cell x)
{
  uintptr_t offset = (uintptr_t)x - (uintptr_t)Dictionary;
  /* Nor a hidden one: those of the inner interpreter read the cells after them. */
  return (uintptr_t)x >= (uintptr_t)Dictionary && offset % sizeof(Word) == 0 &&
         offset / sizeof(Word) < (uintptr_t)Words &&
         !(Dictionary[offset / sizeof(Word)].Flags & WORD_HIDDEN);
} /* end of ForthIsWord */

int ForthOnConsole(const Context *c)
{
  return c == &Console;
} /* end of ForthOnConsole */

const char *ForthErrorText(int error)
{
  return error >= 0 && error < (int)(sizeof Errors / sizeof Errors[0]) ? Errors[error] : "?";
} /* end of ForthErrorText */

static void Comma(Context *c, Cell value)
{
  if (Here >= FORTH_CODE) {
     c->Error = FORTH_FULL;
     return;
  }
  Code[Here++] = value;
} /* end of Comma */

/* Token: The next word of the input, skipping blanks; 0 at the end of the line. */
static int Token(const char **name)
{
  int length = 0;
  while (*In == ' ' || *In == '\t' || *In == '\r' || *In == '\n')
     In += 1;
  *name = In;
  while (*In != 0 && *In != ' ' && *In != '\t' && *In != '\r' && *In != '\n') {
     In += 1;
     length += 1;
  }
  return length;
} /* end of Token */

/* Create: A word named by the next of the input, its name kept with the others. */
static Word *Create(Context *c, void (*code)(Context *, const Word *), uint8_t flags)
{
  const char *name;
  int length = Token(&name), i;
  Word *w;
  /* None while a definition is under way, which an immediate word could ask for: an
  ** error then undoes the definition and every word after it, and one of those, bound
  ** meanwhile by IS or EVERY, would be left pointing at what the next one writes (a
  ** review, 2026-10-24). */
  if (Compiling && Defining != NULL) {
     c->Error = FORTH_DEFINING;
     return NULL;
  }
  if (length == 0 || length > 31) {
     c->Error = FORTH_NAME;
     return NULL;
  }
  if (NamesUsed + length > FORTH_NAMES) {
     c->Error = FORTH_FULL;
     return NULL;
  }
  /* The name in place before the word is counted, which WORDS on the other core may
  ** list meanwhile. */
  for (i = 0; i < length; i += 1)
     Names[NamesUsed + i] = name[i];
  if ((w = NewWord(&Names[NamesUsed],length,code,flags)) == NULL) {
     c->Error = FORTH_FULL;
     return NULL;
  }
  NamesUsed += length;
  return w;
} /* end of Create */

static int Number(const char *s, int length, Cell *value)
{
  int i = 0, negative = 0, base = 10;
  uintptr_t n = 0;
  if (i < length && s[i] == '-') { negative = 1; i += 1; }
  if (i < length && s[i] == '$') { base = 16; i += 1; }
  if (i == length)
     return 0;
  for (; i < length; i += 1) {
     int d;
     char ch = s[i];
     if (ch >= '0' && ch <= '9') d = ch - '0';
     else if (ch >= 'a' && ch <= 'f') d = ch - 'a' + 10;
     else if (ch >= 'A' && ch <= 'F') d = ch - 'A' + 10;
     else return 0;
     if (d >= base)
        return 0;
     n = n * base + d;
  }
  *value = negative ? -(Cell)n : (Cell)n;
  return 1;
} /* end of Number */


/* ---- Words of the stacks, of arithmetic and of memory. */

#define BINARY(name,op) \
   static void name(Context *c, const Word *w) \
   { Cell b; (void)w; FORTH_NEED(c,2); b = FORTH_POP(c); FORTH_TOP(c) = op; }
#define UNARY(name,op) \
   static void name(Context *c, const Word *w) \
   { Cell a; (void)w; FORTH_NEED(c,1); a = FORTH_TOP(c); FORTH_TOP(c) = op; }

BINARY(PAdd,FORTH_TOP(c) + b)
BINARY(PSub,FORTH_TOP(c) - b)
BINARY(PMul,FORTH_TOP(c) * b)
BINARY(PAnd,FORTH_TOP(c) & b)
BINARY(POr,FORTH_TOP(c) | b)
BINARY(PXor,FORTH_TOP(c) ^ b)
BINARY(PEqual,FORTH_TOP(c) == b ? -1 : 0)
BINARY(PNotEqual,FORTH_TOP(c) != b ? -1 : 0)
BINARY(PLess,FORTH_TOP(c) < b ? -1 : 0)
BINARY(PGreater,FORTH_TOP(c) > b ? -1 : 0)
BINARY(PMin,FORTH_TOP(c) < b ? FORTH_TOP(c) : b)
BINARY(PMax,FORTH_TOP(c) > b ? FORTH_TOP(c) : b)
BINARY(PLShift,(Cell)((uintptr_t)FORTH_TOP(c) << (b & (8 * sizeof(Cell) - 1))))
BINARY(PRShift,(Cell)((uintptr_t)FORTH_TOP(c) >> (b & (8 * sizeof(Cell) - 1))))
UNARY(PNegate,-a)
UNARY(PAbs,a < 0 ? -a : a)
UNARY(PInvert,~a)
UNARY(POnePlus,a + 1)
UNARY(POneMinus,a - 1)
UNARY(PZeroEqual,a == 0 ? -1 : 0)
UNARY(PZeroLess,a < 0 ? -1 : 0)
UNARY(PFetch,*(const volatile Cell *)a)
UNARY(PCFetch,*(const volatile uint8_t *)a)
UNARY(PCells,a * (Cell)sizeof(Cell))

static void Divide(Context *c, int remainder)
{
  Cell b;
  FORTH_NEED(c,2);
  if ((b = FORTH_POP(c)) == 0) {
     c->Error = FORTH_DIVISION;
     c->Depth += 1;
     return;
  }
  if (b == -1)   /* the one quotient that overflows, taken modulo the cell */
     FORTH_TOP(c) = remainder ? 0 : (Cell)(0 - (uintptr_t)FORTH_TOP(c));
  else
     FORTH_TOP(c) = remainder ? FORTH_TOP(c) % b : FORTH_TOP(c) / b;
} /* end of Divide */

static void PDiv(Context *c, const Word *w) { (void)w; Divide(c,0); }
static void PMod(Context *c, const Word *w) { (void)w; Divide(c,1); }

static void PDup(Context *c, const Word *w)
{
  (void)w;
  FORTH_NEED(c,1);
  FORTH_ROOM(c,1);
  FORTH_PUSH(c,FORTH_TOP(c));
} /* end of PDup */

static void PDrop(Context *c, const Word *w)
{
  (void)w;
  FORTH_NEED(c,1);
  c->Depth -= 1;
} /* end of PDrop */

static void PSwap(Context *c, const Word *w)
{
  Cell a;
  (void)w;
  FORTH_NEED(c,2);
  a = FORTH_TOP(c);
  FORTH_TOP(c) = c->Data[c->Depth - 2];
  c->Data[c->Depth - 2] = a;
} /* end of PSwap */

static void POver(Context *c, const Word *w)
{
  (void)w;
  FORTH_NEED(c,2);
  FORTH_ROOM(c,1);
  FORTH_PUSH(c,c->Data[c->Depth - 2]);
} /* end of POver */

static void PRot(Context *c, const Word *w)
{
  Cell a;
  (void)w;
  FORTH_NEED(c,3);
  a = c->Data[c->Depth - 3];
  c->Data[c->Depth - 3] = c->Data[c->Depth - 2];
  c->Data[c->Depth - 2] = FORTH_TOP(c);
  FORTH_TOP(c) = a;
} /* end of PRot */

static void PNip(Context *c, const Word *w)
{
  (void)w;
  FORTH_NEED(c,2);
  c->Data[c->Depth - 2] = FORTH_TOP(c);
  c->Depth -= 1;
} /* end of PNip */

static void PDepth(Context *c, const Word *w)
{
  (void)w;
  FORTH_ROOM(c,1);
  FORTH_PUSH(c,c->Depth);
} /* end of PDepth */

static void PStore(Context *c, const Word *w)
{
  Cell *a;
  (void)w;
  FORTH_NEED(c,2);
  a = (Cell *)FORTH_POP(c);
  *(volatile Cell *)a = FORTH_POP(c);
} /* end of PStore */

static void PCStore(Context *c, const Word *w)
{
  uint8_t *a;
  (void)w;
  FORTH_NEED(c,2);
  a = (uint8_t *)FORTH_POP(c);
  *(volatile uint8_t *)a = (uint8_t)FORTH_POP(c);
} /* end of PCStore */

static void PPlusStore(Context *c, const Word *w)
{
  Cell *a;
  (void)w;
  FORTH_NEED(c,2);
  a = (Cell *)FORTH_POP(c);
  *(volatile Cell *)a += FORTH_POP(c);
} /* end of PPlusStore */

static void PTrue(Context *c, const Word *w)
{
  (void)w;
  FORTH_ROOM(c,1);
  FORTH_PUSH(c,-1);
} /* end of PTrue */

static void PFalse(Context *c, const Word *w)
{
  (void)w;
  FORTH_ROOM(c,1);
  FORTH_PUSH(c,0);
} /* end of PFalse */

static void PNow(Context *c, const Word *w)
{
  (void)w;
  FORTH_ROOM(c,1);
  FORTH_PUSH(c,ForthNow());
} /* end of PNow */


/* ---- Output words. */

static void PEmit(Context *c, const Word *w)
{
  (void)w;
  FORTH_NEED(c,1);
  ForthEmit((char)FORTH_POP(c));
} /* end of PEmit */

static void PDot(Context *c, const Word *w)
{
  (void)w;
  FORTH_NEED(c,1);
  ForthPrintNumber(FORTH_POP(c));
} /* end of PDot */

static void PCr(Context *c, const Word *w) { (void)c; (void)w; ForthEmit('\n'); }
static void PSpace(Context *c, const Word *w) { (void)c; (void)w; ForthEmit(' '); }

static void PDotS(Context *c, const Word *w)
{
  int i;
  (void)w;
  ForthEmit('<');
  ForthPrintDigits(c->Depth);
  ForthPrintText("> ");
  for (i = 0; i < c->Depth; i += 1)
     ForthPrintNumber(c->Data[i]);
} /* end of PDotS */

static void PWords(Context *c, const Word *w)
{
  int i, j;
  (void)c; (void)w;
  for (i = Words - 1; i >= 0; i -= 1)
     if (!(Dictionary[i].Flags & WORD_HIDDEN)) {
        for (j = 0; j < Dictionary[i].Length; j += 1)
           ForthEmit(Dictionary[i].Name[j]);
        ForthEmit(' ');
     }
} /* end of PWords */

/* PDotQuoteRun: The text compiled after it, its length first, the bytes in the cells
** that follow. */
static void PDotQuoteRun(Context *c, const Word *w)
{
  Cell length = *c->IP++, i;
  const char *text = (const char *)c->IP;
  (void)w;
  for (i = 0; i < length; i += 1)
     ForthEmit(text[i]);
  c->IP += (length + sizeof(Cell) - 1) / sizeof(Cell);
} /* end of PDotQuoteRun */

static void PDotQuote(Context *c, const Word *w)
{
  const char *text;
  Cell length = 0, i;
  (void)w;
  CONSOLE_ONLY(c);
  if (*In == ' ')
     In += 1;
  text = In;
  while (*In != 0 && *In != '"') {
     In += 1;
     length += 1;
  }
  if (*In == '"')
     In += 1;
  if (!Compiling) {
     for (i = 0; i < length; i += 1)
        ForthEmit(text[i]);
     return;
  }
  /* (."), the length, then the text. */
  if (Here + 2 + (length + (Cell)sizeof(Cell) - 1) / (Cell)sizeof(Cell) > FORTH_CODE) {
     c->Error = FORTH_FULL;
     return;
  }
  Comma(c,(Cell)DotQuote);
  Comma(c,length);
  for (i = 0; i < length; i += 1)
     ((char *)&Code[Here])[i] = text[i];
  Here += (length + sizeof(Cell) - 1) / sizeof(Cell);
} /* end of PDotQuote */


/* ---- Definitions. */

static void PColon(Context *c, const Word *w)
{
  int words = Words, here = Here, names = NamesUsed;
  (void)w;
  CONSOLE_ONLY(c);
  if (Compiling) {
     c->Error = FORTH_CONTROL;
     return;
  }
  /* Hidden from the moment it is counted. */
  if ((Defining = Create(c,DoColon,WORD_HIDDEN)) == NULL)
     return;
  DefinedWords = words;
  DefinedHere = here;
  DefinedNames = names;
  ControlDepth = 0;
  Compiling = 1;
} /* end of PColon */

static void PSemicolon(Context *c, const Word *w)
{
  (void)w;
  CONSOLE_ONLY(c);
  if (!Compiling || ControlDepth != 0) {
     c->Error = FORTH_CONTROL;
     return;
  }
  Comma(c,(Cell)Exit);
  if (c->Error != FORTH_OK)
     return;
  /* The body whole in memory before the word can be found, and so bound. */
  ForthBarrier();
  Defining->Flags &= ~WORD_HIDDEN;
  Compiling = 0;
} /* end of PSemicolon */

static void PConstant(Context *c, const Word *w)
{
  Word *n;
  (void)w;
  CONSOLE_ONLY(c);
  FORTH_NEED(c,1);
  if ((n = Create(c,DoConstant,0)) != NULL)
     n->Value = FORTH_POP(c);
} /* end of PConstant */

static void PVariable(Context *c, const Word *w)
{
  (void)w;
  CONSOLE_ONLY(c);
  /* Its cell first: a variable made in a full code space would point past it, kept,
  ** since no definition is under way to undo (a review, 2026-10-24). */
  if (Here >= FORTH_CODE) {
     c->Error = FORTH_FULL;
     return;
  }
  if (Create(c,DoVariable,0) != NULL)
     Comma(c,0);
} /* end of PVariable */

static void PDefer(Context *c, const Word *w)
{
  (void)w;
  CONSOLE_ONLY(c);
  Create(c,DoDefer,0);
} /* end of PDefer */

static const Word *Named(Context *c)
{
  const char *name;
  int length = Token(&name);
  const Word *w;
  if (length == 0) {
     c->Error = FORTH_NAME;
     return NULL;
  }
  if ((w = ForthFind(name,length)) == NULL)
     c->Error = FORTH_UNKNOWN;
  return w;
} /* end of Named */

static void PTick(Context *c, const Word *w)
{
  const Word *n;
  (void)w;
  CONSOLE_ONLY(c);
  FORTH_ROOM(c,1);
  if ((n = Named(c)) != NULL)
     FORTH_PUSH(c,n);
} /* end of PTick */

static void PBracketTick(Context *c, const Word *w)
{
  const Word *n;
  (void)w;
  CONSOLE_ONLY(c);
  if (!Compiling) {
     c->Error = FORTH_COMPILE_ONLY;
     return;
  }
  if ((n = Named(c)) != NULL) {
     Comma(c,(Cell)Lit);
     Comma(c,(Cell)n);
  }
} /* end of PBracketTick */

/* PIs: Binds a defer, by one store of the word: the other core runs the one before or
** this one, whole. */
static void PIs(Context *c, const Word *w)
{
  Word *d;
  (void)w;
  CONSOLE_ONLY(c);
  FORTH_NEED(c,1);
  if ((d = (Word *)Named(c)) == NULL)
     return;
  if (d->Code != DoDefer || !ForthIsWord(FORTH_TOP(c))) {
     c->Error = FORTH_ARGUMENT;
     return;
  }
  ForthBarrier();
  d->Value = FORTH_POP(c);
} /* end of PIs */

static void PExecute(Context *c, const Word *w)
{
  const Word *x;
  (void)w;
  FORTH_NEED(c,1);
  if (!ForthIsWord(FORTH_TOP(c))) {
     c->Error = FORTH_ARGUMENT;
     return;
  }
  x = (const Word *)FORTH_POP(c);
  x->Code(c,x);
} /* end of PExecute */

static void PImmediate(Context *c, const Word *w)
{
  (void)w;
  CONSOLE_ONLY(c);
  if (Words > 0)
     Dictionary[Words - 1].Flags |= WORD_IMMEDIATE;
} /* end of PImmediate */

static void PRecurse(Context *c, const Word *w)
{
  (void)w;
  CONSOLE_ONLY(c);
  if (!Compiling) {
     c->Error = FORTH_COMPILE_ONLY;
     return;
  }
  Comma(c,(Cell)Defining);
} /* end of PRecurse */

static void PComma(Context *c, const Word *w)
{
  (void)w;
  CONSOLE_ONLY(c);
  FORTH_NEED(c,1);
  Comma(c,FORTH_POP(c));
} /* end of PComma */

static void PHere(Context *c, const Word *w)
{
  (void)w;
  FORTH_ROOM(c,1);
  FORTH_PUSH(c,&Code[Here]);
} /* end of PHere */

/* PAllot: Bytes, rounded up to whole cells. */
static void PAllot(Context *c, const Word *w)
{
  Cell n;
  (void)w;
  CONSOLE_ONLY(c);
  FORTH_NEED(c,1);
  /* Not into a definition, which would run the cells left as words. */
  if (Compiling && Defining != NULL) {
     c->Error = FORTH_DEFINING;
     return;
  }
  n = (FORTH_TOP(c) + (Cell)sizeof(Cell) - 1) / (Cell)sizeof(Cell);
  if (n < 0 || n > FORTH_CODE - Here) {
     c->Error = FORTH_FULL;
     return;
  }
  c->Depth -= 1;
  Here += (int)n;
} /* end of PAllot */


/* ---- Comments. */

/* Both move the console's place in its line: run by a slot, they would skip what the
** console had yet to read (a review, 2026-10-24). */
static void PBackslash(Context *c, const Word *w)
{
  (void)w;
  CONSOLE_ONLY(c);
  while (*In != 0)
     In += 1;
} /* end of PBackslash */

static void PParen(Context *c, const Word *w)
{
  (void)w;
  CONSOLE_ONLY(c);
  while (*In != 0 && *In != ')')
     In += 1;
  if (*In == ')')
     In += 1;
} /* end of PParen */


/* ---- Control structures: compiled with branches whose target is the cell after them. */

static void PLit(Context *c, const Word *w)
{
  (void)w;
  FORTH_ROOM(c,1);
  FORTH_PUSH(c,*c->IP++);
} /* end of PLit */

static void PBranch(Context *c, const Word *w)
{
  (void)w;
  c->IP = (const Cell *)*c->IP;
} /* end of PBranch */

static void PZeroBranch(Context *c, const Word *w)
{
  (void)w;
  FORTH_NEED(c,1);
  if (FORTH_POP(c) == 0)
     c->IP = (const Cell *)*c->IP;
  else
     c->IP += 1;
} /* end of PZeroBranch */

/* PDo: The limit, the index, then a mark on top: the address of the dictionary, which is
** neither 0 nor a cell of the code space, for EXIT to refuse. The index alone would not
** do: 0, or an address in the code space as a loop over an array has, looked like a
** cell to return to (a review, 2026-10-24). */
#define LOOP_MARK  ((Cell)Dictionary)

static void PDo(Context *c, const Word *w)
{
  (void)w;
  FORTH_NEED(c,2);
  if (c->ReturnDepth + 3 > FORTH_STACK) {
     c->Error = FORTH_RETURN_OVERFLOW;
     return;
  }
  c->Return[c->ReturnDepth++] = c->Data[c->Depth - 2];   /* the limit */
  c->Return[c->ReturnDepth++] = FORTH_TOP(c);            /* the index */
  c->Return[c->ReturnDepth++] = LOOP_MARK;
  c->Depth -= 2;
} /* end of PDo */

/* LoopBy: Adds n to the index; loops again unless it crossed from limit - 1 to limit
** (the test of Forth 2012 for +LOOP, on the index less the limit). */
static void LoopBy(Context *c, Cell n)
{
  Cell before = c->Return[c->ReturnDepth - 2] - c->Return[c->ReturnDepth - 3];
  Cell after = (Cell)((uintptr_t)before + (uintptr_t)n);
  if (((before ^ after) & (before ^ n)) < 0) {
     c->ReturnDepth -= 3;
     c->IP += 1;
  }
  else {
     c->Return[c->ReturnDepth - 2] += n;
     c->IP = (const Cell *)*c->IP;
  }
} /* end of LoopBy */

static void PLoop(Context *c, const Word *w) { (void)w; LoopBy(c,1); }

static void PPlusLoopRun(Context *c, const Word *w)
{
  (void)w;
  FORTH_NEED(c,1);
  LoopBy(c,FORTH_POP(c));
} /* end of PPlusLoopRun */

static void PI(Context *c, const Word *w)
{
  (void)w;
  FORTH_ROOM(c,1);
  if (c->ReturnDepth < 3 || c->Return[c->ReturnDepth - 1] != LOOP_MARK) {
     c->Error = FORTH_CONTROL;
     return;
  }
  FORTH_PUSH(c,c->Return[c->ReturnDepth - 2]);
} /* end of PI */

static void PJ(Context *c, const Word *w)
{
  (void)w;
  FORTH_ROOM(c,1);
  if (c->ReturnDepth < 6 || c->Return[c->ReturnDepth - 1] != LOOP_MARK ||
      c->Return[c->ReturnDepth - 4] != LOOP_MARK) {
     c->Error = FORTH_CONTROL;
     return;
  }
  FORTH_PUSH(c,c->Return[c->ReturnDepth - 5]);
} /* end of PJ */

static int Opens(Context *c, int kind, int at)
{
  if (!Compiling) {
     c->Error = FORTH_COMPILE_ONLY;
     return 0;
  }
  if (ControlDepth >= FORTH_STACK) {
     c->Error = FORTH_CONTROL;
     return 0;
  }
  Control[ControlDepth].Kind = kind;
  Control[ControlDepth++].At = at;
  return 1;
} /* end of Opens */

/* Closes: The cell the structure of one of the kinds left open, -1 for none. */
static int Closes(Context *c, int kind, int other)
{
  if (!Compiling) {
     c->Error = FORTH_COMPILE_ONLY;
     return -1;
  }
  if (ControlDepth == 0 || (Control[ControlDepth - 1].Kind != kind &&
                            Control[ControlDepth - 1].Kind != other)) {
     c->Error = FORTH_CONTROL;
     return -1;
  }
  return Control[--ControlDepth].At;
} /* end of Closes */

/* Forward: Compiles a branch whose target the structure fills in later. */
static void Forward(Context *c, const Word *branch, int kind)
{
  if (Here + 2 > FORTH_CODE) {
     c->Error = FORTH_FULL;
     return;
  }
  if (Opens(c,kind,Here + 1)) {
     Comma(c,(Cell)branch);
     Comma(c,0);
  }
} /* end of Forward */

static void Backward(Context *c, const Word *branch, int to)
{
  if (Here + 2 > FORTH_CODE) {
     c->Error = FORTH_FULL;
     return;
  }
  Comma(c,(Cell)branch);
  Comma(c,(Cell)&Code[to]);
} /* end of Backward */

static void PIf(Context *c, const Word *w) { (void)w; CONSOLE_ONLY(c); Forward(c,ZeroBranch,CONTROL_IF); }

static void PElse(Context *c, const Word *w)
{
  int at;
  (void)w;
  CONSOLE_ONLY(c);
  if ((at = Closes(c,CONTROL_IF,CONTROL_IF)) < 0)
     return;
  Forward(c,Branch,CONTROL_ELSE);
  if (c->Error == FORTH_OK)
     Code[at] = (Cell)&Code[Here];
} /* end of PElse */

static void PThen(Context *c, const Word *w)
{
  int at;
  (void)w;
  CONSOLE_ONLY(c);
  if ((at = Closes(c,CONTROL_IF,CONTROL_ELSE)) >= 0)
     Code[at] = (Cell)&Code[Here];
} /* end of PThen */

static void PBegin(Context *c, const Word *w) { (void)w; CONSOLE_ONLY(c); Opens(c,CONTROL_BEGIN,Here); }

static void PUntil(Context *c, const Word *w)
{
  int at;
  (void)w;
  CONSOLE_ONLY(c);
  if ((at = Closes(c,CONTROL_BEGIN,CONTROL_BEGIN)) >= 0)
     Backward(c,ZeroBranch,at);
} /* end of PUntil */

static void PAgain(Context *c, const Word *w)
{
  int at;
  (void)w;
  CONSOLE_ONLY(c);
  if ((at = Closes(c,CONTROL_BEGIN,CONTROL_BEGIN)) >= 0)
     Backward(c,Branch,at);
} /* end of PAgain */

static void PWhile(Context *c, const Word *w)
{
  (void)w;
  CONSOLE_ONLY(c);
  if (ControlDepth == 0 || Control[ControlDepth - 1].Kind != CONTROL_BEGIN) {
     c->Error = Compiling ? FORTH_CONTROL : FORTH_COMPILE_ONLY;
     return;
  }
  Forward(c,ZeroBranch,CONTROL_WHILE);
} /* end of PWhile */

static void PRepeat(Context *c, const Word *w)
{
  int exit, at;
  (void)w;
  CONSOLE_ONLY(c);
  if ((exit = Closes(c,CONTROL_WHILE,CONTROL_WHILE)) < 0 ||
      (at = Closes(c,CONTROL_BEGIN,CONTROL_BEGIN)) < 0)
     return;
  Backward(c,Branch,at);
  if (c->Error == FORTH_OK)
     Code[exit] = (Cell)&Code[Here];
} /* end of PRepeat */

static void PDoCompile(Context *c, const Word *w)
{
  (void)w;
  CONSOLE_ONLY(c);
  if (Here + 1 > FORTH_CODE) {
     c->Error = FORTH_FULL;
     return;
  }
  if (Opens(c,CONTROL_DO,Here + 1))
     Comma(c,(Cell)Do);
} /* end of PDoCompile */

static void CloseLoop(Context *c, const Word *loop)
{
  int at;
  CONSOLE_ONLY(c);
  if ((at = Closes(c,CONTROL_DO,CONTROL_DO)) >= 0)
     Backward(c,loop,at);
} /* end of CloseLoop */

static void PLoopCompile(Context *c, const Word *w) { (void)w; CloseLoop(c,Loop); }
static void PPlusLoop(Context *c, const Word *w) { (void)w; CloseLoop(c,PlusLoop); }


/* ---- The outer interpreter. */

static const struct { const char *Name; void (*Code)(Context *, const Word *); uint8_t Flags; }
   BuiltIn[] = {
   {"(lit)", PLit, WORD_HIDDEN}, {"(branch)", PBranch, WORD_HIDDEN},
   {"(0branch)", PZeroBranch, WORD_HIDDEN}, {"(do)", PDo, WORD_HIDDEN},
   {"(loop)", PLoop, WORD_HIDDEN}, {"(+loop)", PPlusLoopRun, WORD_HIDDEN},
   {"(.\")", PDotQuoteRun, WORD_HIDDEN}, {"exit", PExit, 0},
   {"+", PAdd, 0}, {"-", PSub, 0}, {"*", PMul, 0}, {"/", PDiv, 0}, {"mod", PMod, 0},
   {"and", PAnd, 0}, {"or", POr, 0}, {"xor", PXor, 0}, {"invert", PInvert, 0},
   {"negate", PNegate, 0}, {"abs", PAbs, 0}, {"min", PMin, 0}, {"max", PMax, 0},
   {"1+", POnePlus, 0}, {"1-", POneMinus, 0}, {"lshift", PLShift, 0},
   {"rshift", PRShift, 0}, {"=", PEqual, 0}, {"<>", PNotEqual, 0}, {"<", PLess, 0},
   {">", PGreater, 0}, {"0=", PZeroEqual, 0}, {"0<", PZeroLess, 0}, {"true", PTrue, 0},
   {"false", PFalse, 0}, {"dup", PDup, 0}, {"drop", PDrop, 0}, {"swap", PSwap, 0},
   {"over", POver, 0}, {"rot", PRot, 0}, {"nip", PNip, 0}, {"depth", PDepth, 0},
   {"@", PFetch, 0}, {"!", PStore, 0}, {"c@", PCFetch, 0}, {"c!", PCStore, 0},
   {"+!", PPlusStore, 0}, {"cells", PCells, 0}, {",", PComma, 0}, {"here", PHere, 0},
   {"allot", PAllot, 0}, {"emit", PEmit, 0}, {".", PDot, 0}, {"cr", PCr, 0},
   {"space", PSpace, 0}, {".s", PDotS, 0}, {"words", PWords, 0},
   {".\"", PDotQuote, WORD_IMMEDIATE}, {":", PColon, 0},
   {";", PSemicolon, WORD_IMMEDIATE}, {"constant", PConstant, 0},
   {"variable", PVariable, 0}, {"defer", PDefer, 0}, {"is", PIs, 0}, {"'", PTick, 0},
   {"[']", PBracketTick, WORD_IMMEDIATE}, {"execute", PExecute, 0},
   {"immediate", PImmediate, 0}, {"recurse", PRecurse, WORD_IMMEDIATE},
   {"\\", PBackslash, WORD_IMMEDIATE}, {"(", PParen, WORD_IMMEDIATE},
   {"if", PIf, WORD_IMMEDIATE}, {"else", PElse, WORD_IMMEDIATE},
   {"then", PThen, WORD_IMMEDIATE}, {"begin", PBegin, WORD_IMMEDIATE},
   {"until", PUntil, WORD_IMMEDIATE}, {"again", PAgain, WORD_IMMEDIATE},
   {"while", PWhile, WORD_IMMEDIATE}, {"repeat", PRepeat, WORD_IMMEDIATE},
   {"do", PDoCompile, WORD_IMMEDIATE}, {"loop", PLoopCompile, WORD_IMMEDIATE},
   {"+loop", PPlusLoop, WORD_IMMEDIATE}, {"i", PI, 0}, {"j", PJ, 0}, {"now", PNow, 0}
};

void ForthInit(void)
{
  unsigned i;
  Words = Here = NamesUsed = 0;
  Compiling = ControlDepth = 0;
  Console.Depth = Console.ReturnDepth = 0;
  Console.IP = NULL;
  Console.Budget = 0;
  for (i = 0; i < sizeof BuiltIn / sizeof BuiltIn[0]; i += 1)
     ForthAddWord(BuiltIn[i].Name,BuiltIn[i].Code,BuiltIn[i].Flags);
  Lit = &Dictionary[0];
  Branch = &Dictionary[1];
  ZeroBranch = &Dictionary[2];
  Do = &Dictionary[3];
  Loop = &Dictionary[4];
  PlusLoop = &Dictionary[5];
  DotQuote = &Dictionary[6];
  Exit = &Dictionary[7];
  ForthPlatformWords();
} /* end of ForthInit */

/* Abandon: After an error, the stacks emptied and the definition under way undone. */
static void Abandon(void)
{
  Console.Depth = Console.ReturnDepth = 0;
  if (Compiling) {
     Words = DefinedWords;
     Here = DefinedHere;
     NamesUsed = DefinedNames;
     Compiling = 0;
  }
  ControlDepth = 0;
} /* end of Abandon */

void ForthInterpret(const char *line)
{
  const char *name = line;
  int length = 0;
  In = line;
  Console.Error = FORTH_OK;
  while (Console.Error == FORTH_OK && (length = Token(&name)) > 0) {
     const Word *w = ForthFind(name,length);
     Cell n;
     if (w != NULL) {
        if (Compiling && !(w->Flags & WORD_IMMEDIATE))
           Comma(&Console,(Cell)w);
        else
           ForthExecute(&Console,w);
     }
     else if (!Number(name,length,&n))
        Console.Error = FORTH_UNKNOWN;
     else if (Compiling) {
        Comma(&Console,(Cell)Lit);
        Comma(&Console,n);
     }
     else if (Console.Depth >= FORTH_STACK)
        Console.Error = FORTH_OVERFLOW;
     else
        FORTH_PUSH(&Console,n);
  }
  if (Console.Error != FORTH_OK) {
     int i;
     ForthEmit(' ');
     for (i = 0; i < length; i += 1)
        ForthEmit(name[i]);
     ForthEmit(' ');
     ForthPrintText(Errors[Console.Error]);
     ForthEmit('\n');
     Abandon();
  }
  else
     ForthPrintText(Compiling ? " compiled\n" : " ok\n");
} /* end of ForthInterpret */
