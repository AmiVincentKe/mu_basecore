/** @file
  LZMA UEFI header file for compression.

  Allows LZMA encoder code to build under UEFI (edk2) build environment.

  Copyright (c) 2025, Microsoft Corporation. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#ifndef __UEFILZMA_H__
#define __UEFILZMA_H__

#include <Uefi.h>
#include <Library/BaseMemoryLib.h>

#ifdef _WIN32
  #undef _WIN32
#endif

#ifndef _SIZE_T_DEFINED
#ifndef __SIZE_T__
typedef UINTN size_t;
#define _SIZE_T_DEFINED
#define __SIZE_T__
#endif
#endif

#ifdef _WIN64
  #undef _WIN64
#endif

#ifndef _PTRDIFF_T_DEFINED
typedef int ptrdiff_t;
#endif

#define memcpy(dst, src, n)   CopyMem ((dst), (src), (n))
#define memmove(dst, src, n)  CopyMem ((dst), (src), (n))
#define memset(dst, val, n)   SetMem  ((dst), (n), (UINT8)(val))

//
// Disable multi-thread support -- UEFI has no threading primitives.
//
#define _7ZIP_ST

#define _LZMA_SIZE_OPT

#endif // __UEFILZMA_H__
