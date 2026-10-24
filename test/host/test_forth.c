/* Copyright (c) 2026 Bertrand Hurst. All rights reserved.
** Escapement Forth - a Forth console beside the Escapement real-time OS.
** Distributed under the terms of LICENSE at the root of this repository.
*/
/* File test_forth.c: The Forth and the pool of slots on the host: each case lines typed
** at a console just started, and what it must print.
*/

#include <stdio.h>
#include <string.h>
#include "forth.h"
#include "slots.h"
#include "platform_host.h"

static int Failures, Cases;

/* Check: Types the lines, NULL ended, at a fresh console; compares what it printed. */
static void Check(const char *name, const char *const lines[], const char *expected)
{
  int i;
  ForthInit();
  OutputLength = 0;
  Output[0] = 0;
  for (i = 0; lines[i] != NULL; i += 1)
     ForthInterpret(lines[i]);
  Cases += 1;
  if (strcmp(Output,expected) != 0) {
     Failures += 1;
     printf("FAIL %s\n  expected: \"%s\"\n  printed:  \"%s\"\n",name,expected,Output);
  }
} /* end of Check */

#define CASE(name,expected,...) \
   do { static const char *const l[] = {__VA_ARGS__, NULL}; Check(name,l,expected); } while (0)

/* Run: The slot task of a period, run n times, the clock moving 1 us between. A word
** that STALLs leaves the run as a restart of the core would, which SlotsRestarted then
** follows. */
static void Run(int period, int n)
{
  while (n-- > 0) {
     if (setjmp(Restart) == 0)
        SlotsRun(period);
     else
        SlotsRestarted();
     Clock += 1;
  }
} /* end of Run */

static void Interpreter(void)
{
  CASE("numbers"," ok\n<4> 1 -2 31 -16  ok\n","1 -2 $1f -$10",".s");
  CASE("arithmetic","3 -2 -3 1 8 7  ok\n",
       "5 2 - . 2 -1 * . -7 2 / . -7 2 mod negate . 3 -2 max 1+ 2 * . 7 abs .");
  CASE("division by zero"," / division by zero\n<0>  ok\n","1 0 /",".s");
  CASE("stack empty"," + stack empty\n","+");
  CASE("stack full"," 17 stack full\n<0>  ok\n",
       "1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17",".s");
  CASE("unknown"," frob ?\n","frob");
  CASE("logic","-1 0 -1 0 -1 8 2  ok\n","1 1 = . 1 2 = . 1 2 < . 1 2 > . 0 0= . 1 3 lshift . 8 2 rshift .");
  CASE("stack words","<3> 2 3 1  ok\n<2> 2 1  ok\n","1 2 3 rot .s","nip over drop .s");
  CASE("variable"," ok\n ok\n42  ok\n","variable v","40 v ! 2 v +!","v @ .");
  CASE("constant"," ok\n10  ok\n","5 2 * constant ten","ten .");
  CASE("colon"," ok\n49  ok\n",": square dup * ;","7 square .");
  CASE("multi-line definition"," compiled\n ok\n6  ok\n",": six","3 2 * ;","six .");
  CASE("if else then"," ok\nyes no  ok\n",": t if .\" yes\" else .\" no\" then space ;","1 t 0 t");
  CASE("begin until"," ok\n3 2 1  ok\n",": down begin dup . 1- dup 0= until drop ;","3 down");
  CASE("begin while repeat"," ok\n1 2 4 8  ok\n",
       ": pow begin dup 10 < while dup . 2 * repeat drop ;","1 pow");
  CASE("do loop"," ok\n0 1 2 3  ok\n",": l 4 0 do i . loop ;","l");
  CASE("+loop down"," ok\n10 7 4 1  ok\n",": l 0 10 do i . -3 +loop ;","l");
  CASE("+loop up"," ok\n0 5  ok\n",": l 10 0 do i . 5 +loop ;","l");
  CASE("nested loops"," ok\n00 01 10 11  ok\n",": l 2 0 do 2 0 do j 48 + emit i 48 + emit space loop loop ;","l");
  CASE("recurse"," ok\n120  ok\n",": f dup 1 > if dup 1- recurse * then ;","5 f .");
  CASE("defer is"," ok\n ok\n ok\n ok\n1  ok\n ok\n2  ok\n","defer d",": one 1 ;",": two 2 ;",
       "' one is d","d .","' two is d","d .");
  CASE("defer unbound"," ok\n d bad argument\n","defer d","d");
  CASE("execute a number"," execute bad argument\n","5 execute");
  CASE("is a number"," ok\n is bad argument\n","defer d","5 is d");
  CASE("tick execute"," ok\n9  ok\n",": n 9 ;","' n execute .");
  CASE("bracket tick"," ok\n ok\n7  ok\n",": n 7 ;",": m ['] n execute ;","m .");
  CASE("comments","3  ok\n","1 2 + ( a comment ) . \\ the rest");
  CASE("dot quote at the console","hi there ok\n",".\" hi there\"");
  CASE("case","1  ok\n","1 DUP .");
  CASE("return stack full"," ok\n r return stack full\n",": r recurse ;","r");
  CASE("if outside a definition"," if compile only\n","if");
  CASE("unbalanced"," ; unbalanced control\n",": x if ;");
  CASE("error drops the definition"," x ?\n d1 ?\n",": d1 x ;","d1");
  CASE("the definition hides until ;"," ok\n compiled\n ok\n2  ok\n",": a 1 ;", ": a","a 1+ ;",
       "a .");
  CASE("undone after an error"," frob ?\n<0>  ok\n","1 2 frob",".s");
  CASE("name expected"," : name expected\n",":");
  CASE("dot quote at the end of the code space"," ok\n .\" dictionary full\n<0>  ok\n",
       "2046 cells allot",": x .\" abc\" ;",".s");
  CASE("exit inside a loop"," ok\n f unbalanced control\n",": f 10 5 do exit loop ;","f");
  CASE("allot inside a definition"," ok\n a inside a definition\n",": a 8 allot ; immediate",
       ": x a ;");
  CASE("variable in a full code space"," ok\n variable dictionary full\n b ?\n",
       "variable a 2047 cells allot","variable b","5 b !");
  CASE("exit inside a loop from 0"," ok\n ok\n g unbalanced control\n",
       ": f 10 0 do exit loop .\" after\" ;",": g f .\" g-after\" ;","g");
  CASE("exit inside a loop over the code space"," ok\n ok\n f unbalanced control\n",
       "variable a 3 cells allot",": f a 4 cells + a do i @ 0= if exit then 1 cells +loop ;","f");
  CASE("a word made inside a definition",
       " ok\n ok\n ok\n mk inside a definition\n<0>  ok\n",
       ": mk ['] variable execute ; immediate",": tk ['] ' execute ; immediate",
       ": nop ;",": x mk v ;",".s");
  CASE("arithmetic wraps","-1 -1  ok\n","-1 1 rshift 1+ 0< . -1 1 rshift negate 2 - 0< 0= .");
  CASE("a defer bound to itself"," ok\n ok\n d bad argument\n","defer d","' d is d","d");
  CASE("allot"," ok\n ok\n7  ok\n","variable a 2 cells allot","7 a 2 cells + !","a 2 cells + @ .");
}

/* Type, Expect: A line typed at the console as it is, and what it printed since the
** last Expect. */
static void Type(const char *line)
{
  ForthInterpret(line);
} /* end of Type */

static void Expect(const char *name, const char *expected)
{
  Cases += 1;
  if (strcmp(Output,expected) != 0) {
     Failures += 1;
     printf("FAIL %s\n  expected: \"%s\"\n  printed:  \"%s\"\n",name,expected,Output);
  }
  OutputLength = 0;
  Output[0] = 0;
} /* end of Expect */

/* What a slot runs here takes the time it SPENDs on the clock. */
/* A hidden word of the inner interpreter, reached by arithmetic on a word's address. */
static void HiddenWords(void)
{
  static char line[64];
  const char *lines[] = {line, NULL};
  snprintf(line,sizeof line,"' exit %d - execute",(int)sizeof(Word));
  Check("a hidden word is no word",lines," execute bad argument\n");
} /* end of HiddenWords */

static void Pool(void)
{
  CASE("every needs a period of the pool"," ok\n every bad argument\n",": w ;","' w 3 every");
  CASE("a comment run by a slot"," ok\nslot 0  ok\n3  ok\n",": w ;","' \\ 1 every","run0 1 2 + .");
  CASE("every a period that wraps"," ok\n every bad argument\n",": w ;","' w 4294967297 every");
  CASE("every a number"," every bad argument\n","5 10 every");
  CASE("every binds on trial"," ok\nslot 4  ok\nslot 5  ok\n",": w ;","' w 10 every",
       "' w 10 every");
  CASE("a full period"," ok\nslot 0 slot 1 slot 2 slot 3  every dictionary full\n",": w ;",
       "' w 1 every ' w 1 every ' w 1 every ' w 1 every ' w 1 every");
  CASE("stop frees the entry"," ok\nslot 0  ok\n ok\nslot 0  ok\n",": w ;","' w 1 every",
       "0 stop","' w 1 every");

  /* A word on trial runs, its longest time kept; promoted with a quarter more and 1 us,
  ** its runs over that counted. */
  CASE("pool"," ok\n ok\n","variable cost 40 cost !",": w cost @ spend ;");
  Expect("pool"," ok\n ok\n");
  Type("' w 100 every");
  Expect("bound","slot 8  ok\n");
  Run(2,5);
  Type("8 promote");
  Expect("too few runs","too few runs  ok\n");
  Run(2,5);
  Type("slots");
  Expect("on trial","\n8 100000us w trial runs 10 max 40 \nload 0.9%  ok\n");
  Type("8 promote");
  Expect("promoted","promoted, wcet 51 us, load 1.0%  ok\n");
  Run(2,3);
  Type("slots");
  Expect("runs promoted","\n8 100000us w promoted runs 13 max 40 wcet 51 over 0 \nload 1.0%  ok\n");

  Type("60 cost !");
  Run(2,2);
  Type("40 cost !");
  Type("slots");
  Expect("over its wcet"," ok\n ok\n\n8 100000us w promoted runs 15 max 60 wcet 51 over 2 "
         "\nload 1.0%  ok\n");

  Type(": t 1 spend ;");
  Type("' t 100 every");
  Expect("bound after"," ok\nslot 9  ok\n");

  /* A word that would write the dictionary fails in a slot. */
  Type(": c 1 , ;");
  Type("' c 1000 every");
  Run(3,1);
  Type("12 stop");
  Expect("console only"," ok\nslot 12  ok\n ok\n");

  /* A word that fails is stopped, its error kept. */
  Type(": bad drop ;");
  Type("' bad 1 every");
  Run(0,3);
  Type("slots");
  Expect("failed"," ok\nslot 0  ok\n\n0 1000us bad trial runs 1 max 0 failed: stack empty"
         "\n8 100000us w promoted runs 15 max 60 wcet 51 over 2 "
         "\n9 100000us t trial runs 0 max 0 \nload 1.0%  ok\n");

  /* A word that held the core when it restarted is stopped; the others run on. */
  Type("variable k : stuck k @ if stall then ; : fine ;");
  Type("' stuck 10 every ' fine 10 every");
  Run(1,2);
  Type("-1 k !");
  Run(1,3);
  Type("slots");
  Expect("stalled"," ok\nslot 4 slot 5  ok\n ok\n"
         "\n0 1000us bad trial runs 1 max 0 failed: stack empty"
         "\n4 10000us stuck trial runs 2 max 0 failed: stalled"
         "\n5 10000us fine trial runs 4 max 0 "
         "\n8 100000us w promoted runs 15 max 60 wcet 51 over 2 "
         "\n9 100000us t trial runs 0 max 0 \nload 1.0%  ok\n");

  /* Bound anew, an entry shows nothing of the word before until it runs. */
  Type("0 stop ' t 1 every");
  Type("slots");
  Expect("bound anew","slot 0  ok\n"
         "\n0 1000us t trial runs 0 max 0 "
         "\n4 10000us stuck trial runs 2 max 0 failed: stalled"
         "\n5 10000us fine trial runs 4 max 0 "
         "\n8 100000us w promoted runs 15 max 60 wcet 51 over 2 "
         "\n9 100000us t trial runs 0 max 0 \nload 1.0%  ok\n");
  Type("0 stop");
  Expect("stopped"," ok\n");

  /* A word's time leaves out the slots that preempted it. */
  Type("4 stop 5 stop 8 stop 9 stop");
  Type(": inner 50 spend ; ' inner 1 every");
  Type(": outer 5 spend run0 5 spend ; ' outer 100 every");
  Run(2,1);
  Type("slots");
  Expect("preemptions left out"," ok\nslot 0  ok\nslot 8  ok\n"
         "\n0 1000us inner trial runs 1 max 50 "
         "\n8 100000us outer trial runs 1 max 10 \nload 0.9%  ok\n");
  Type("0 stop 8 stop");

  /* A preemption between the check's reading of the clock and its reading of the time
  ** left out: not more than the time the word spent. */
  Type(": early 1 preempt 20 0 do loop ; ' early 100 every");
  Type(": inner 50 spend ; ' inner 1 every");
  Run(2,1);
  Type("slots");
  Expect("preempted at the check"," ok\nslot 8  ok\nslot 0  ok\n"
         "\n0 1000us inner trial runs 1 max 50 "
         "\n8 100000us early trial runs 1 max 0 \nload 0.9%  ok\n");
  Type("0 stop 8 stop");

  /* Nor when it comes between the readings of the time left out and of the clock, at the
  ** check, at the end of the run, or between the start and the time left out set to 0. */
  Type(": inner 20000 spend ; ' inner 1 every");
  Type(": late -1 preempt 20 0 do loop ; ' late 100 every");
  Run(2,1);
  Type(": last -1 preempt ; ' last 100 every");
  Run(2,1);
  Type(": a 2 preempt ; ' a 100 every");
  Type(": b 20 0 do loop ; ' b 100 every");
  Run(2,1);
  Type("slots");
  Expect("preempted between readings"," ok\nslot 0  ok\nslot 8  ok\nslot 9  ok\n"
         "slot 10  ok\nslot 11  ok\n"
         "\n0 1000us inner trial runs 6 max 20000 over 6 "
         "\n8 100000us late trial runs 3 max 0 "
         "\n9 100000us last trial runs 2 max 0 "
         "\n10 100000us a trial runs 1 max 0 "
         "\n11 100000us b trial runs 1 max 0 \nload 0.9%  ok\n");
  Type("0 stop 8 stop 9 stop 10 stop 11 stop");

  /* Nor against the word it preempts when it preempts a slot that preempted that word,
  ** as that slot starts. */
  Type(": mid 20000 spend ; ' mid 1 every");
  Type(": outer 1 preempt run0 20 0 do loop ; ' outer 100 every");
  Run(2,1);
  Type(": outer2 -1 preempt run0 ; ' outer2 100 every");
  Run(2,1);
  Type("slots");
  Expect("preempted as a slot starts"," ok\nslot 0  ok\nslot 8  ok\nslot 9  ok\n"
         "\n0 1000us mid trial runs 6 max 20000 over 6 "
         "\n8 100000us outer trial runs 2 max 0 "
         "\n9 100000us outer2 trial runs 1 max 0 \nload 0.9%  ok\n");
  Type("0 stop 8 stop 9 stop");

  /* An error at the step of a check, the word over its budget: the error kept. */
  Type(": bad 0 1 20000 spend dup drop dup drop dup drop dup drop dup drop swap / ;");
  Type("' bad 100 every");
  Run(2,3);
  Type("slots");
  Expect("error at a check"," ok\n ok\nslot 8  ok\n"
         "\n8 100000us bad trial runs 1 max 20000 over 1 failed: division by zero"
         "\nload 0.9%  ok\n");
  Type("8 stop");

  /* A word on trial gets a tenth of its period: one that loops is cut short there, every
  ** release, counted, and is not promoted. */
  Type(": hog begin 1 spend again ; ' hog 10 every");
  Run(1,3);
  Type("slots");
  Type("4 promote");
  Expect("over budget"," ok\nslot 4  ok\n"
         "\n4 10000us hog trial runs 3 max 1003 over 3 \nload 0.9%  ok\n"
         "over budget  ok\n");
  Type("4 stop");
  Expect("stopped hog"," ok\n");

  /* A promoted word that fails past its worst case, before a check: its error kept. */
  Type("variable f : p f @ if 100 spend 1 0 / then ; ' p 100 every");
  Run(2,10);
  Type("8 promote");
  Type("-1 f !");
  Run(2,3);
  Type("slots");
  Expect("error past the worst case","slot 8  ok\npromoted, wcet 1 us, load 0.9%  ok\n ok\n"
         "\n8 100000us p promoted runs 11 max 100 wcet 1 over 1 failed: division by zero"
         "\nload 0.9%  ok\n");
  Type("8 stop");

  /* A word on trial past its budget, ended before a check: counted, not promoted. */
  Type(": big 150000 spend ; ' big 1000 every");
  Run(3,10);
  Type("12 promote");
  Expect("over budget, not cut"," ok\nslot 12  ok\nover budget  ok\n");
  Type("12 stop");

  /* At each release, the promoted words run first, then those on trial. */
  Type(": a 97 emit ;");
  Type(": b 98 emit ;");
  Type("' a 1000 every ' b 1000 every");
  Run(3,10);
  Type("13 promote");
  Expect("promote b"," ok\n ok\n ok\nslot 12 slot 13  ok\n"
         "ababababababababababpromoted, wcet 1 us, load 0.9%  ok\n");
  Run(3,2);
  Expect("b first","baba");

  /* The figures of a promotion go on into it alone: not into a word bound anew in its
  ** entry before the kernel's core ran it. Shown from the promotion on. */
  Type("12 stop 13 stop");
  Type(": c ;");
  Type("' a 1000 every");
  Run(3,10);
  Type("12 promote slots");
  Type("12 stop ' c 1000 every");
  Run(3,1);
  Type("slots");
  Expect("figures carried once",
         " ok\n ok\nslot 12  ok\naaaaaaaaaapromoted, wcet 1 us, load 0.9% "
         "\n12 1000000us a promoted runs 10 max 0 wcet 1 over 0 \nload 0.9%  ok\n"
         "slot 12  ok\n"
         "\n12 1000000us c trial runs 1 max 0 \nload 0.9%  ok\n");

  /* A run going on as its word is promoted is judged against the worst case admitted. */
  Type("12 stop");
  Type("variable g : d g @ if 12 promoting 50 spend then ;");
  Type("' d 1000 every");
  Run(3,10);
  Type("-1 g !");
  Run(3,1);
  Type("0 g !");
  Run(3,2);
  Type("slots");
  Expect("promoted as it ran"," ok\n ok\nslot 12  ok\n ok\npromoted, wcet 1 us, load 0.9%  ok\n ok\n"
         "\n12 1000000us d promoted runs 13 max 50 wcet 1 over 1 \nload 0.9%  ok\n");
}


/* Addresses: @ ! +! c@ c! reach the Forth's memory and what the platform says is there,
** a cell at a cell's boundary; the host says nothing else is. */
static void Addresses(void)
{
  Type("variable v 7 v ! v @ . 1 v +! v @ . 65 v c! v c@ .");
  Expect("own memory","7 8 65  ok\n");
  Type("$C0000000 @");
  Expect("fetch"," @ bad address\n");
  Type("1 $C0000000 !");
  Expect("store"," ! bad address\n");
  Type("1 $C0000000 +!");
  Expect("add"," +! bad address\n");
  Type("$C0000000 c@");
  Expect("byte fetch"," c@ bad address\n");
  Type("1 $C0000000 c!");
  Expect("byte store"," c! bad address\n");
  Type("v 1 + @");
  Expect("unaligned"," @ bad address\n");
  Type("v 1 + c@ .");
  Expect("unaligned byte","0  ok\n");
} /* end of Addresses */


/* Break: A word typed at the console that loops, stopped by a break, Ctrl-C on the board,
** which ForthExecute takes as it takes a budget; the console goes on. */
static void Breaks(void)
{
  Type(": l begin again ;");
  Expect("defined"," ok\n");
  Break = 1;
  Type("l");
  Expect("interrupted"," l interrupted\n");
  Type("1 2 + .");
  Expect("after a break","3  ok\n");
} /* end of Breaks */


int main(void)
{
  Interpreter();
  HiddenWords();
  Pool();
  Breaks();
  Addresses();
  printf("%d cases, %d failed\n",Cases,Failures);
  return Failures != 0;
} /* end of main */
