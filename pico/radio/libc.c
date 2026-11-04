/* Copyright (c) 2026 Bertrand Hurst. All rights reserved.
** Escapement Forth - a Forth console beside the Escapement real-time OS.
** Distributed under the terms of LICENSE at the root of this repository.
*/
/* File libc.c: The functions of libc/string.h, written byte by byte: the radio's driver
** copies a few hundred bytes at a time, at most. gcc may insert calls to memcpy and memset
** on its own, so these functions hide their own loops from it. */

#include <string.h>
#include <stdio.h>

void *memcpy(void *to, const void *from, size_t n)
{
  volatile unsigned char *t = to;
  const unsigned char *f = from;
  while (n-- > 0)
     *t++ = *f++;
  return to;
} /* end of memcpy */

void *memmove(void *to, const void *from, size_t n)
{
  volatile unsigned char *t = to;
  const unsigned char *f = from;
  if (t <= f || t >= f + n)
     return memcpy(to,from,n);
  while (n-- > 0)
     t[n] = f[n];
  return to;
} /* end of memmove */

void *memset(void *to, int c, size_t n)
{
  volatile unsigned char *t = to;
  while (n-- > 0)
     *t++ = (unsigned char)c;
  return to;
} /* end of memset */

int memcmp(const void *a, const void *b, size_t n)
{
  const unsigned char *x = a, *y = b;
  for (; n > 0; n -= 1, x += 1, y += 1)
     if (*x != *y)
        return *x - *y;
  return 0;
} /* end of memcmp */

size_t strlen(const char *s)
{
  size_t n = 0;
  while (s[n] != 0)
     n += 1;
  return n;
} /* end of strlen */

int strcmp(const char *a, const char *b)
{
  while (*a != 0 && *a == *b)
     a += 1, b += 1;
  return (unsigned char)*a - (unsigned char)*b;
} /* end of strcmp */

int strncmp(const char *a, const char *b, size_t n)
{
  for (; n > 0; n -= 1, a += 1, b += 1)
     if (*a != *b || *a == 0)
        return (unsigned char)*a - (unsigned char)*b;
  return 0;
} /* end of strncmp */

char *strncpy(char *to, const char *from, size_t n)
{
  size_t i = 0;
  for (; i < n && from[i] != 0; i += 1)
     to[i] = from[i];
  for (; i < n; i += 1)
     to[i] = 0;
  return to;
} /* end of strncpy */

int vsnprintf(char *to, size_t size, const char *format, va_list arguments)
{
  (void)format;
  (void)arguments;
  if (size > 0)
     to[0] = 0;
  return 0;
} /* end of vsnprintf */
