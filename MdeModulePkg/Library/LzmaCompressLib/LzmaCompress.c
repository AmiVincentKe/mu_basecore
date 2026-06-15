/** @file
  LZMA Compress interfaces

  Wraps the LZMA SDK encoder into a UEFI-compatible BASE library.
  Memory allocation is performed via MemoryAllocationLib so that the
  encoder can be used from DXE and MM environments.

  Copyright (c) 2025, Microsoft Corporation. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#include "LzmaCompressLibInternal.h"
#include "Sdk/C/7zTypes.h"
#include "Sdk/C/7zVersion.h"
#include "Sdk/C/LzmaEnc.h"

//
// LZMA header layout:
//   Bytes  0 .. LZMA_PROPS_SIZE-1 : encoder properties (5 bytes)
//   Bytes  LZMA_PROPS_SIZE .. +7  : original size as little-endian UINT64
//
#define LZMA_HEADER_SIZE  (LZMA_PROPS_SIZE + 8)

// ---------------------------------------------------------------------------
// ISzAlloc implementation backed by MemoryAllocationLib
// ---------------------------------------------------------------------------

STATIC
VOID *
EFIAPI
UefiSzAlloc (
  ISzAllocPtr  P,
  size_t       Size
  )
{
  (VOID)P;
  return AllocatePool ((UINTN)Size);
}

STATIC
VOID
EFIAPI
UefiSzFree (
  ISzAllocPtr  P,
  VOID         *Address
  )
{
  (VOID)P;
  if (Address != NULL) {
    FreePool (Address);
  }
}

STATIC ISzAlloc  gUefiAlloc = { UefiSzAlloc, UefiSzFree };

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

/**
  Get the maximum buffer size required to hold the compressed output.

  @param[in]  SourceSize   Size in bytes of the uncompressed source data.

  @return  Maximum number of bytes that the compressed output may occupy.
**/
UINT32
EFIAPI
LzmaCompressGetMaxBufferSize (
  IN UINT32  SourceSize
  )
{
  //
  // Conservative upper bound: 105% of source plus 64 KB plus the header.
  // Use 32-bit arithmetic (SourceSize + SourceSize/20) to avoid generating
  // __aulldiv (64-bit division helper) on IA32 MSVC builds.
  //
  return (UINT32)(SourceSize + SourceSize / 20 + SIZE_64KB + LZMA_HEADER_SIZE);
}

/**
  Compress Source using LZMA and write the result to Destination.

  @param[in]      Source            Pointer to the uncompressed source data.
  @param[in]      SourceSize        Size in bytes of Source.
  @param[out]     Destination       Buffer to receive compressed output.
  @param[in,out]  DestinationSize   On input: size of Destination.
                                    On output: bytes written.

  @retval RETURN_SUCCESS            Compression succeeded.
  @retval RETURN_BUFFER_TOO_SMALL   Destination buffer is too small.
  @retval RETURN_OUT_OF_RESOURCES   Encoder memory allocation failed.
  @retval RETURN_INVALID_PARAMETER  A required pointer is NULL or SourceSize is 0.
**/
RETURN_STATUS
EFIAPI
LzmaUefiCompress (
  IN      CONST VOID  *Source,
  IN      UINT32      SourceSize,
  OUT     VOID        *Destination,
  IN OUT  UINT32      *DestinationSize
  )
{
  SRes             LzmaResult;
  CLzmaEncProps    Props;
  SizeT            OutSizeProcessed;
  SizeT            PropsSize;
  UINT32           RequiredSize;
  UINT8            *OutBuffer;
  UINTN            Index;

  if ((Source == NULL) || (Destination == NULL) || (DestinationSize == NULL)) {
    return RETURN_INVALID_PARAMETER;
  }

  if (SourceSize == 0) {
    return RETURN_INVALID_PARAMETER;
  }

  //
  // Verify destination buffer is large enough.
  //
  RequiredSize = LzmaCompressGetMaxBufferSize (SourceSize);
  if (*DestinationSize < RequiredSize) {
    *DestinationSize = RequiredSize;
    return RETURN_BUFFER_TOO_SMALL;
  }

  OutBuffer = (UINT8 *)Destination;

  //
  // Encode the original size into header bytes [LZMA_PROPS_SIZE .. +7].
  //
  for (Index = 0; Index < 8; Index++) {
    OutBuffer[LZMA_PROPS_SIZE + Index] = (UINT8)((UINT64)SourceSize >> (8 * Index));
  }

  //
  // Set up default compression properties.
  //
  LzmaEncProps_Init (&Props);
  Props.reduceSize = SourceSize;

  //
  // Compress into Destination + LZMA_HEADER_SIZE; write props into bytes [0..4].
  //
  OutSizeProcessed = (SizeT)(*DestinationSize - LZMA_HEADER_SIZE);
  PropsSize        = LZMA_PROPS_SIZE;

  LzmaResult = LzmaEncode (
                 OutBuffer + LZMA_HEADER_SIZE,
                 &OutSizeProcessed,
                 (CONST Byte *)Source,
                 (SizeT)SourceSize,
                 &Props,
                 OutBuffer,          // props encoded into first LZMA_PROPS_SIZE bytes
                 &PropsSize,
                 0,                  // writeEndMark = 0
                 NULL,               // no progress callback
                 &gUefiAlloc,
                 &gUefiAlloc
                 );

  if (LzmaResult == SZ_ERROR_OUTPUT_EOF) {
    *DestinationSize = RequiredSize;
    return RETURN_BUFFER_TOO_SMALL;
  }

  if (LzmaResult != SZ_OK) {
    return RETURN_OUT_OF_RESOURCES;
  }

  *DestinationSize = (UINT32)(LZMA_HEADER_SIZE + OutSizeProcessed);
  return RETURN_SUCCESS;
}

/**
  Constructor for LzmaCompressLib.

  Currently a no-op; reserved for future initialisation.

  @retval RETURN_SUCCESS  Always succeeds.
**/
RETURN_STATUS
EFIAPI
LzmaCompressLibConstructor (
  VOID
  )
{
  return RETURN_SUCCESS;
}
