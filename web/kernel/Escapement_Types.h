/* Copyright (c) 2026 Bertrand Hurst. All rights reserved.
** Escapement Forth - a Forth console beside the Escapement real-time OS.
** Distributed under the terms of LICENSE at the root of this repository.
*/
/* File Escapement_Types.h: The kernel's types, on wasm32. */

#ifndef _ESCAPEMENT_TYPES_H_
#define _ESCAPEMENT_TYPES_H_

#include <stdint.h>

typedef unsigned char BOOL;
typedef uint8_t  UINT8;
typedef int8_t   INT8;
typedef uint16_t UINT16;
typedef int16_t  INT16;
typedef uint32_t UINT32;
typedef int32_t  INT32;
typedef uintptr_t UINTPTR;

#ifndef NULL
  #define NULL 0
#endif
#ifndef TRUE
  #define TRUE 1
#endif
#ifndef FALSE
  #define FALSE 0
#endif

#endif /* _ESCAPEMENT_TYPES_H_ */
