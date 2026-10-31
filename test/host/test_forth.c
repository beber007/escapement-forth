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
  CASE("a full period"," ok\nslot 0 slot 1 slot 2 slot 3  every period full\n",": w ;",
       "' w 1 every ' w 1 every ' w 1 every ' w 1 every ' w 1 every");
  CASE("a full period caught"," ok\nslot 0 slot 1 slot 2 slot 3 -257  ok\n",": w ;",
       "' w 1 every ' w 1 every ' w 1 every ' w 1 every ' w 1 ' every catch . 2drop");
  /* Nine on trial take 90 %, and the pool 0.9 %: a tenth is refused, for all the budgets
  ** to fit in the processor (issue #25). */
  CASE("the trials fill the processor"," ok\n"
       "slot 0 slot 1 slot 2 slot 3 slot 4 slot 5 slot 6 slot 7 slot 8  every processor full\n",": w ;",
       "' w 1 every ' w 1 every ' w 1 every ' w 1 every ' w 10 every ' w 10 every ' w 10 every "
       "' w 10 every ' w 100 every ' w 100 every");
  CASE("the processor full caught"," ok\n"
       "slot 0 slot 1 slot 2 slot 3 slot 4 slot 5 slot 6 slot 7 slot 8 -258  ok\n",": w ;",
       "' w 1 every ' w 1 every ' w 1 every ' w 1 every ' w 10 every ' w 10 every ' w 10 every "
       "' w 10 every ' w 100 every ' w 100 ' every catch . 2drop");
  CASE("stop makes room for a trial"," ok\n"
       "slot 0 slot 1 slot 2 slot 3 slot 4 slot 5 slot 6 slot 7 slot 8  ok\n ok\nslot 8  ok\n",": w ;",
       "' w 1 every ' w 1 every ' w 1 every ' w 1 every ' w 10 every ' w 10 every ' w 10 every "
       "' w 10 every ' w 100 every","8 stop","' w 100 every");
  CASE("stop frees the entry"," ok\nslot 0  ok\n ok\nslot 0  ok\n",": w ;","' w 1 every",
       "0 stop","' w 1 every");

  /* A promotion that takes more than the tenth it had on trial, past SLOT_ALL with the
  ** words on trial, is refused; one that takes less makes room (issue #25). */
  CASE("trials and promotions"," ok\n",": big 999 spend ; : small ;");
  Expect("trials and promotions"," ok\n");
  Type("' small 1 every ' small 1 every ' small 1 every ' small 1 every");
  Type("' big 10 every ' big 10 every ' big 10 every ' big 10 every ' big 100 every");
  Expect("nine on trial","slot 0 slot 1 slot 2 slot 3  ok\nslot 4 slot 5 slot 6 slot 7 slot 8  ok\n");
  Run(1,10);
  Run(0,10);
  Type("4 promote");
  Expect("promoted, the trials within the bound","promoted, wcet 1249 us, load 13.4%  ok\n");
  Type("5 promote");
  Expect("past the bound with the trials","refused, with trials 95.9%  ok\n");
  Type("' small 100 every");
  Expect("no room yet"," every processor full\n");
  Type("0 promote");
  Expect("less than its tenth","promoted, wcet 1 us, load 13.5%  ok\n");
  Type("' small 100 every");
  Expect("the room it made","slot 9  ok\n");
  Type("slots");
  Expect("both shown","\n0 1000us small promoted runs 10 max 0 wcet 1 over 0 "
         "\n1 1000us small trial runs 10 max 0 "
         "\n2 1000us small trial runs 10 max 0 "
         "\n3 1000us small trial runs 10 max 0 "
         "\n4 10000us big promoted runs 10 max 999 wcet 1249 over 0 "
         "\n5 10000us big trial runs 10 max 999 "
         "\n6 10000us big trial runs 10 max 999 "
         "\n7 10000us big trial runs 10 max 999 "
         "\n8 100000us big trial runs 0 max 0 "
         "\n9 100000us small trial runs 0 max 0 "
         "\nload 13.5% with trials 93.5%  ok\n");

  /* Exactly the bound passes, for a promotion and for a trial: 90 % on trial, 0.8888 % the
  ** pool, and two words of 1 s that take 24.999 % and 16.113 % more than their tenths. */
  CASE("the bound exactly"," ok\n",": w ; : a 99999 spend ; : b 92890 spend ;");
  Expect("the bound exactly"," ok\n");
  Type("' w 1 every ' w 1 every ' w 1 every ' w 1 every ' w 10 every ' w 10 every ' w 10 every");
  Type("' a 1000 every ' b 1000 every");
  Expect("nine on trial","slot 0 slot 1 slot 2 slot 3 slot 4 slot 5 slot 6  ok\nslot 12 slot 13  ok\n");
  Run(3,10);
  Type("12 promote");
  Expect("a promoted","promoted, wcet 124999 us, load 13.4%  ok\n");
  Type("13 promote");
  Expect("b promoted at the bound","promoted, wcet 116113 us, load 25.0%  ok\n");
  Type("0 stop");
  Expect("a trial stopped"," ok\n");
  Type("' w 1 every");
  Expect("a trial at the bound","slot 0  ok\n");

  /* One part per million past the bound: b here takes 1 us more. */
  CASE("past the bound by one"," ok\n",": w ; : a 99999 spend ; : b 92891 spend ;");
  Expect("past the bound by one"," ok\n");
  Type("' w 1 every ' w 1 every ' w 1 every ' w 1 every ' w 10 every ' w 10 every ' w 10 every");
  Type("' a 1000 every ' b 1000 every");
  Expect("nine on trial, again","slot 0 slot 1 slot 2 slot 3 slot 4 slot 5 slot 6  ok\nslot 12 slot 13  ok\n");
  Run(3,10);
  Type("12 promote");
  Expect("a promoted, again","promoted, wcet 124999 us, load 13.4%  ok\n");
  Type("13 promote");
  Expect("b refused, one past","refused, with trials 95.1%  ok\n");
  Type("0 stop");
  Expect("a trial stopped, again"," ok\n");
  Type("13 promote");
  Expect("b promoted, room made","promoted, wcet 116114 us, load 25.1%  ok\n");
  Type("' w 1 every");
  Expect("a trial refused, one past"," every processor full\n");

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
  Expect("on trial","\n8 100000us w trial runs 10 max 40 \nload 0.9% with trials 10.9%  ok\n");
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
         "\n9 100000us t trial runs 0 max 0 \nload 1.0% with trials 21.0%  ok\n");

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
         "\n9 100000us t trial runs 0 max 0 \nload 1.0% with trials 41.0%  ok\n");

  /* Bound anew, an entry shows nothing of the word before until it runs. */
  Type("0 stop ' t 1 every");
  Type("slots");
  Expect("bound anew","slot 0  ok\n"
         "\n0 1000us t trial runs 0 max 0 "
         "\n4 10000us stuck trial runs 2 max 0 failed: stalled"
         "\n5 10000us fine trial runs 4 max 0 "
         "\n8 100000us w promoted runs 15 max 60 wcet 51 over 2 "
         "\n9 100000us t trial runs 0 max 0 \nload 1.0% with trials 41.0%  ok\n");
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
         "\n8 100000us outer trial runs 1 max 10 \nload 0.9% with trials 20.9%  ok\n");
  Type("0 stop 8 stop");

  /* A preemption between the check's reading of the clock and its reading of the time
  ** left out: not more than the time the word spent. */
  Type(": early 1 preempt 20 0 do loop ; ' early 100 every");
  Type(": inner 50 spend ; ' inner 1 every");
  Run(2,1);
  Type("slots");
  Expect("preempted at the check"," ok\nslot 8  ok\nslot 0  ok\n"
         "\n0 1000us inner trial runs 1 max 50 "
         "\n8 100000us early trial runs 1 max 0 \nload 0.9% with trials 20.9%  ok\n");
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
         "\n11 100000us b trial runs 1 max 0 \nload 0.9% with trials 50.9%  ok\n");
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
         "\n9 100000us outer2 trial runs 1 max 0 \nload 0.9% with trials 30.9%  ok\n");
  Type("0 stop 8 stop 9 stop");

  /* An error at the step of a check, the word over its budget: the error kept. */
  Type(": bad 0 1 20000 spend dup drop dup drop dup drop dup drop dup drop swap / ;");
  Type("' bad 100 every");
  Run(2,3);
  Type("slots");
  Expect("error at a check"," ok\n ok\nslot 8  ok\n"
         "\n8 100000us bad trial runs 1 max 20000 over 1 failed: division by zero"
         "\nload 0.9% with trials 10.9%  ok\n");
  Type("8 stop");

  /* A word on trial gets a tenth of its period: one that loops is cut short there, every
  ** release, counted, and is not promoted. */
  Type(": hog begin 1 spend again ; ' hog 10 every");
  Run(1,3);
  Type("slots");
  Type("4 promote");
  Expect("over budget"," ok\nslot 4  ok\n"
         "\n4 10000us hog trial runs 3 max 1003 over 3 \nload 0.9% with trials 10.9%  ok\n"
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
         "\n12 1000000us c trial runs 1 max 0 \nload 0.9% with trials 10.9%  ok\n");

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
  /* The address and the size wrapping past the top of memory to the Forth's own. */
  Type("-8 @");
  Expect("wrapping"," @ bad address\n");
  Type("-1 c@");
  Expect("wrapping byte"," c@ bad address\n");
  Type("5 -8 !");
  Expect("wrapping store"," ! bad address\n");
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
  /* A slot's word runs on, the break the console's alone. */
  Type("variable k : twenty 20 0 do 1 k +! loop ; ' twenty 1 every");
  Expect("bound","slot 0  ok\n");
  Break = 1;
  Run(0,1);
  Type("k @ . 0 stop");
  Expect("a slot not broken","20  ok\n");
  Cases += 1;
  if (Break != 1) {
     Failures += 1;
     printf("FAIL the break left for the console\n");
  }
  Break = 0;
} /* end of Breaks */

/* Stores: SAVE keeps the dictionary in the platform's store, which a cold start, the
** next FORTH_INIT, loads: the words, their code and their data; UNSAVE empties it. A
** store torn, or stamped by another firmware, is not loaded. */
static void Stores(void)
{
  ForthInit();
  Type("variable v 42 v ! : sq dup * ; defer d ' sq is d");
  Expect("defined"," ok\n");
  Type("save");
  Expect("saved"," ok\n");
  ForthInit();
  Type("5 sq . v @ . 3 d .");
  Expect("loaded","25 42 9  ok\n");
  Type(": cube dup sq * ; 2 cube .");
  Expect("defined after","8  ok\n");
  ForthInit();
  Type("2 cube .");
  Expect("not saved since"," cube ?\n");

  StoredBytes[24] ^= 1;   /* the first byte after the header */
  ForthInit();
  Type("5 sq .");
  Expect("torn"," sq ?\n");
  Type("variable v 42 v ! : sq dup * ; save");
  Expect("saved again"," ok\n");
  StoredBytes[4] ^= 1;    /* the stamp */
  ForthInit();
  Type("5 sq .");
  Expect("another firmware"," sq ?\n");
  Type("variable v 42 v ! : sq dup * ; save");
  Expect("saved a third time"," ok\n");
  HostBuild += 1;         /* a firmware of the same addresses, its code another */
  ForthInit();
  Type("5 sq .");
  Expect("another firmware, the same addresses"," sq ?\n");
  HostBuild -= 1;

  Type(": sq dup * ; save");
  Expect("saved a third time"," ok\n");
  Type("unsave");
  Expect("unsaved"," ok\n");
  ForthInit();
  Type("5 sq .");
  Expect("emptied"," sq ?\n");

  /* IMMEDIATE on the last built-in word, none defined yet: the store still loads. */
  ForthInit();
  Type("immediate : sq dup * ; save");
  Expect("saved after immediate"," ok\n");
  ForthInit();
  Type("5 sq . unsave");
  Expect("loaded after immediate","25  ok\n");
  Type(": s! save ; immediate");
  Expect("immediate save"," ok\n");
  Type(": f s! ;");
  Expect("not inside a definition"," s! inside a definition\n");

  StoredRoom = 16;
  Type("save");
  Expect("no room"," save not stored\n");
  StoredRoom = sizeof StoredBytes;
  Type("' save 1 every");
  Run(0,1);
  Type("slots");
  Expect("not from a slot","slot 0  ok\n\n0 1000us save trial runs 1 max 0 failed: bad argument\n"
         "load 0.9% with trials 10.9%  ok\n");
  Type("0 stop");
  Expect("stopped"," ok\n");
  /* A variable a slot writes as the store is written: the dictionary is loaded, the
  ** variable as it was at some instant of the save. */
  Type("variable n : inc 1 n +! ; ' inc 1 every");
  Expect("bound","slot 0  ok\n");
  StoreRuns = 1;
  Type("save");
  StoreRuns = 0;
  Expect("saved as a slot ran"," ok\n");
  ForthInit();
  Type("n @ 2 < . 0 stop unsave");
  Expect("loaded as a slot ran","-1  ok\n");
  ForthInit();
  Output[0] = 0, OutputLength = 0;
} /* end of Stores */

/* Standard: CATCH and THROW, the strings, the return stack and a few more of the standard
** words; an error of the Forth's own caught as its standard code, but a budget, a break
** and a stall, which must stop the word. */
static void Standard(void)
{
  CASE("catch none","0 1  ok\n",": f 1 ; ' f catch . .");
  CASE("throw caught","7  ok\n",": f 7 throw 99 ; ' f catch .");
  CASE("throw 0"," ok\n0 2  ok\n",": f 0 throw 2 ;","' f catch . .");
  CASE("depth restored","8 1  ok\n",": f 5 6 7 8 throw ; 1 ' f catch . .");
  CASE("own error caught","-10  ok\n",": f 1 0 / ; ' f catch .");
  CASE("underflow caught","-4  ok\n",": f drop ; ' f catch .");
  CASE("standard code thrown"," throw division by zero\n","-10 throw");
  CASE("uncaught"," throw thrown 5\n","5 throw");
  CASE("nested","3 9  ok\n",": g 9 throw ; : f ['] g catch 3 ; ' f catch drop . .");
  CASE("in a loop","1  ok\n",": g 3 = if 1 throw then ; : f 0 10 0 do i ['] g catch if drop 1+ then loop ; f .");
  CASE("rethrow","4  ok\n",": g 4 throw ; : f ['] g catch throw ; ' f catch .");
  CASE("abort"," abort aborted\n","abort");
  CASE("abort caught","-1  ok\n",": f abort ; ' f catch .");
  CASE("catch needs a word"," catch bad argument\n","5 catch");
  CASE("strings","hello, world ok\n",": f s\" hello, world\" type ; f");
  CASE("string at the console","abc3  ok\n","s\" abc\" 2dup type . drop");
  CASE("string length","0  ok\n",": f s\" \" nip ; f .");
  CASE("char","65 66  ok\n","char A . : f [char] B ; f .");
  CASE("return stack","3 3 2 1  ok\n",": f >r 2 r@ r> ; 1 3 f . . . .");
  CASE("left on >r's stack"," ok\n ok\n",": f 1 >r ;","f");
  CASE("more words","0 3 3 3 1 5 2 5 2 0  ok\n",
       "0 ?dup . 3 ?dup . . 7 2 /mod . . 2 5 2dup . . . . 1 2 2drop depth .");
  CASE("0<>","-1 0  ok\n","5 0<> . 0 0<> .");
  CASE("type bad address"," type bad address\n","$C0000000 4 type");
  /* What >R holds is no return, nor a loop's mark; EXIT in a CATCH returns from it. */
  CASE(">r a variable"," ok\n7  ok\n","variable v : t v >r ; t","7 .");
  CASE(">r a zero","1  ok\n",": a 0 >r ; : b a 1 . ; b");
  CASE("r> in a loop"," w return stack empty\n",": w 1 0 do r> drop loop 3 . ; w");
  CASE("exit caught","0 5 6  ok\n",": a ['] exit catch . 5 . ; : b a 6 . ; b");
  CASE("r> caught","-6  ok\n",": f ['] r> catch . ; f");
  CASE("catch gives >r's stack back","5  ok\n",
       ": g r> drop 9 >r 1 throw ; : f 5 >r ['] g catch drop r> ; f .");
  CASE("exit by execute","6  ok\n",": a ['] exit execute 5 . ; : b a 6 . ; b");
  CASE("s\" past Pad"," s\" bad argument\n","s\" xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx\" nip .");
  CASE("a name too long","-19  ok\n","' variable catch nnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnn .");
  CASE("/mod on a full stack","3 1 14  ok\n",
       "1 2 3 4 5 6 7 8 9 10 11 12 13 14 7 2 /mod . . .");
  CASE("a loop's index in a catch","0  ok\n",": f 1 0 do ['] i catch drop . loop ; f");

  /* TYPE, a word of C that may be long, checks the break as NEXT does. */
  ForthInit();
  OutputLength = 0;
  Output[0] = 0;
  Break = 1;
  Type("variable v 2000 allot v 2000 type");
  Cases += 1;
  if (OutputLength > 100 || OutputLength < 17 ||
      strcmp(Output + OutputLength - 17,"type interrupted\n") != 0) {
     Failures += 1;
     printf("FAIL type broken\n  printed %d bytes\n",OutputLength);
  }
  Break = 0;

  /* Not caught: a budget, which only the slot's run stops. */
  OutputLength = 0;
  Output[0] = 0;
  Type("variable z : spin begin 10 spend again ; : f ['] spin catch z ! ; ' f 1 every");
  Expect("bound","slot 0  ok\n");
  Run(0,1);
  Type("z @ . slots 0 stop");
  Expect("budget not caught","0 \n0 1000us f trial runs 1 max 160 over 1 \nload 0.9% with trials 10.9%  ok\n");
  /* A word of the compiler caught as it finds the code full: the structure it closes is
  ** left open, not a branch to 0 that ended the run unseen (a review, 2026-10-24). */
  ForthInit();
  Type("variable v : m ['] else catch . ; immediate defer d : g d 7 . ;");
  Type("v 16384 + here - 40 - allot");
  Type(": f 0 if m ;");
  Expect("else caught, the code full"," ok\n ok\n-8  ; unbalanced control\n");
  ForthInit();
  Type("variable v : m ['] again catch . ; immediate");
  Type("v 16384 + here - 8 - allot");   /* a cell left, for EXIT */
  Type(": f begin m ;");
  Expect("again caught, the code full"," ok\n ok\n-8  ; unbalanced control\n");
  ForthInit();
  Type("variable v : m ['] loop catch . ; immediate");
  Type("v 16384 + here - 48 - allot");
  Type(": f 10 0 do m ;");
  Expect("loop caught, the code full"," ok\n ok\n-8  ; unbalanced control\n");
  ForthInit();
  Type("variable v : m ['] until catch . ; immediate");
  Type("v 16384 + here - 8 - allot");
  Type(": f begin m ;");
  Expect("until caught, the code full"," ok\n ok\n-8  ; unbalanced control\n");
  ForthInit();

  /* Under CATCHes nested, each word of fewer steps than a check's: the steps of all
  ** count, and the budget cuts the run (a review, 2026-10-24). */
  Type(": l4 1 spend 1 spend 1 spend 1 spend 1 spend 1 spend 1 spend ;");
  Type(": l3 ['] l4 catch ['] l4 catch ['] l4 catch ['] l4 catch ['] l4 catch 2drop 2drop drop ;");
  Type(": l2 ['] l3 catch ['] l3 catch ['] l3 catch ['] l3 catch ['] l3 catch 2drop 2drop drop ;");
  Type(": l1 ['] l2 catch ['] l2 catch ['] l2 catch ['] l2 catch ['] l2 catch 2drop 2drop drop ;");
  Type(": top ['] l1 catch ['] l1 catch ['] l1 catch ['] l1 catch ['] l1 catch 2drop 2drop drop ;");
  OutputLength = 0;
  Output[0] = 0;
  Type("' top 1 every");
  Run(0,1);
  Type("slots 0 stop");
  Expect("budget under catches nested","slot 0  ok\n\n0 1000us top trial runs 1 max 105 over 1 "
         "\nload 0.9% with trials 10.9%  ok\n");
  Break = 1;
  Type(": g begin again ; : h ['] g catch 99 ; h");
  Expect("break not caught"," h interrupted\n");
} /* end of Standard */


int main(void)
{
  Interpreter();
  HiddenWords();
  Pool();
  Breaks();
  Addresses();
  Standard();
  Stores();
  printf("%d cases, %d failed\n",Cases,Failures);
  return Failures != 0;
} /* end of main */
