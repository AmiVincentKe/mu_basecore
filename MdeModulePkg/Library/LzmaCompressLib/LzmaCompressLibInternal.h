/** @file
  LZMA Compress Library internal header file.

  Copyright (c) 2025, Microsoft Corporation. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#ifndef __LZMACOMPRESSLIB_INTERNAL_H__
#define __LZMACOMPRESSLIB_INTERNAL_H__

#include <Base.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/LzmaCompressLib.h>

/**
  Constructor for LzmaCompressLib. Called automatically at library load time.

  @retval RETURN_SUCCESS  Always succeeds.
**/
RETURN_STATUS
EFIAPI
LzmaCompressLibConstructor (
  VOID
  );

#endif // __LZMACOMPRESSLIB_INTERNAL_H__
