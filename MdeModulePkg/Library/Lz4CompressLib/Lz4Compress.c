/** @file
  LZ4 Compress interfaces.

  Wraps an LZ4 block encoder into a UEFI-compatible BASE library.
  Memory for the encoder hash table is allocated via MemoryAllocationLib
  (AllocatePages) so the compressor is safe to call from both PEI and DXE
  environments.  LZ4 requires only a 16 KiB hash table (4096 UINT32 entries),
  making it suitable for pre-memory contexts where large allocations would fail.

  Output format (see Lz4CompressLib.h):
    Bytes  0-3 : LZ4 UEFI signature ('L','Z','4','U')
    Bytes  4-7 : Original (uncompressed) size, little-endian UINT32
    Bytes  8-N : Raw LZ4 block-format compressed data

  Copyright (c) Microsoft Corporation. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#include <Base.h>
#include <Uefi/UefiBaseType.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/Lz4CompressLib.h>

// ---------------------------------------------------------------------------
// LZ4 UEFI header
// ---------------------------------------------------------------------------

#define LZ4_UEFI_SIGNATURE  SIGNATURE_32 ('L', 'Z', '4', 'U')
#define LZ4_HEADER_SIZE     8U   // 4-byte signature + 4-byte original size

// ---------------------------------------------------------------------------
// LZ4 block algorithm constants
// (see https://github.com/lz4/lz4/blob/dev/doc/lz4_Block_format.md)
// ---------------------------------------------------------------------------

// Hash table: 4 096 entries * 4 B = 16 KiB.
#define LZ4_HASH_LOG      12U
#define LZ4_HASH_ENTRIES  (1U << LZ4_HASH_LOG)

// Minimum match length in LZ4 is always 4 bytes.
#define LZ4_MINMATCH      4U

// The last LZ4_MFLIMIT bytes of input cannot be a match-finder start.
#define LZ4_MFLIMIT       12U

// The last LZ4_LASTLITERALS bytes of input must always be encoded as literals.
#define LZ4_LASTLITERALS   5U

// Maximum distance (back-reference offset) allowed in LZ4 block format.
#define LZ4_MAX_DISTANCE  65535U

// Token nibble constants for run length and match length encoding.
#define LZ4_ML_BITS        4U
#define LZ4_ML_MASK       ((1U << LZ4_ML_BITS) - 1U)   // 0x0F
#define LZ4_RUN_MASK      LZ4_ML_MASK                   // same mask value

// ---------------------------------------------------------------------------
// Internal byte-order helpers
// ---------------------------------------------------------------------------

/**
  Read 4 bytes from an unaligned address as a little-endian UINT32.
**/
STATIC UINT32
Lz4Read32 (
  IN CONST UINT8  *P
  )
{
  return (UINT32)P[0]
         | ((UINT32)P[1] << 8U)
         | ((UINT32)P[2] << 16U)
         | ((UINT32)P[3] << 24U);
}

/**
  Write a UINT16 to an unaligned address in little-endian byte order.
**/
STATIC VOID
Lz4WriteLE16 (
  OUT UINT8   *P,
  IN  UINT16  V
  )
{
  P[0] = (UINT8)(V & 0xFFU);
  P[1] = (UINT8)((V >> 8U) & 0xFFU);
}

/**
  Read 2 bytes from an unaligned address as a little-endian UINT16.
**/
STATIC UINT16
Lz4ReadLE16 (
  IN CONST UINT8  *P
  )
{
  return (UINT16)((UINT16)P[0] | ((UINT16)P[1] << 8U));
}

/**
  Map a 32-bit value to a LZ4_HASH_LOG-bit hash index.
  Uses the Knuth multiplicative hash (factor = 2654435761).
**/
STATIC UINT32
Lz4Hash32 (
  IN UINT32  Value
  )
{
  return (Value * 2654435761U) >> (32U - LZ4_HASH_LOG);
}

// ---------------------------------------------------------------------------
// LZ4 block compressor (raw LZ4 block, no UEFI header)
// ---------------------------------------------------------------------------

/**
  Compress Src into Dst using the LZ4 block format.

  The hash table (LZ4_HASH_ENTRIES UINT32 entries, zero-filled) must be
  provided by the caller and will be mutated during compression.

  @param[in]  Src           Pointer to uncompressed input data.
  @param[in]  SrcSize       Size in bytes of Src.
  @param[out] Dst           Pointer to output buffer.
  @param[in]  DstCapacity   Size in bytes of Dst.
  @param[in]  HashTable     Caller-provided hash table (LZ4_HASH_ENTRIES
                            UINT32 entries, zero-filled before first call).

  @return  Number of bytes written to Dst on success.
           0 if DstCapacity is too small to hold the compressed output
           (caller should treat this as incompressible and store raw data).
**/
STATIC UINT32
Lz4BlockCompress (
  IN  CONST UINT8  *Src,
  IN  UINT32        SrcSize,
  OUT UINT8        *Dst,
  IN  UINT32        DstCapacity,
  IN  UINT32       *HashTable
  )
{
  CONST UINT8  *Ip;          // Current input read position
  CONST UINT8  *Anchor;      // Start of the current literal run
  CONST UINT8  *IEnd;        // One past the last input byte
  CONST UINT8  *MfLimit;     // Last valid match-finder start position
  CONST UINT8  *MatchLimit;  // Last byte we can extend a match into
  UINT8        *Op;          // Current output write position
  CONST UINT8  *OLimit;      // One past the end of the output buffer
  UINT32        Hash;
  UINT32        RefOff;
  CONST UINT8  *MatchRef;
  UINT32        LitLen;
  UINT32        MatchLen;
  UINT8        *TokenPos;
  UINT16        Offset;
  CONST UINT8  *ForwardIp;
  UINT32        SkipCount;

  if (SrcSize == 0U) {
    return 0U;
  }

  Ip    = Src;
  Anchor = Src;
  IEnd   = Src + SrcSize;

  //
  // Guard against underflow when SrcSize < LZ4_MFLIMIT/LZ4_LASTLITERALS.
  //
  MfLimit   = (SrcSize > LZ4_MFLIMIT)      ? (IEnd - LZ4_MFLIMIT)      : Src;
  MatchLimit = (SrcSize > LZ4_LASTLITERALS) ? (IEnd - LZ4_LASTLITERALS) : Src;

  Op     = Dst;
  OLimit = Dst + DstCapacity;

  //
  // Inputs shorter than LZ4_MINMATCH + LZ4_MFLIMIT bytes cannot contain a
  // compressible match; emit everything as a single literal run.
  //
  if (SrcSize < (LZ4_MINMATCH + LZ4_MFLIMIT)) {
    goto LastLiterals;
  }

  //
  // Start one byte into the input so that any back-reference found on the
  // very first iteration has an offset >= 1 (LZ4 forbids offset 0).
  //
  Ip        = Src + 1U;
  ForwardIp = Ip;
  SkipCount = 0U;

  for (;;) {
    UINT32  Extra;

    // -----------------------------------------------------------------------
    // Find the next 4-byte match using the hash table.
    // The skip count grows after ~64 consecutive misses to speed traversal
    // over incompressible input.
    // -----------------------------------------------------------------------
    do {
      Hash      = Lz4Hash32 (Lz4Read32 (ForwardIp));
      Ip        = ForwardIp;
      ForwardIp = Ip + 1U + (SkipCount++ >> 6U);

      if (ForwardIp > MfLimit) {
        goto LastLiterals;
      }

      RefOff          = HashTable[Hash];
      HashTable[Hash] = (UINT32)(Ip - Src);
      MatchRef        = Src + RefOff;
    } while (
      ((UINT32)(Ip - MatchRef) > LZ4_MAX_DISTANCE) ||
      (Lz4Read32 (MatchRef) != Lz4Read32 (Ip))
      );

    // -----------------------------------------------------------------------
    // Extend the match backwards (but never past Anchor).
    // -----------------------------------------------------------------------
    while ((Ip > Anchor) && (MatchRef > Src) && (Ip[-1] == MatchRef[-1])) {
      --Ip;
      --MatchRef;
    }

    // -----------------------------------------------------------------------
    // Encode the literal run that precedes this match.
    // -----------------------------------------------------------------------
    LitLen   = (UINT32)(Ip - Anchor);
    TokenPos = Op++;

    // Safety: need 1 (token) + LitLen + extra-bytes + 2 (offset) + 1 (ml extra) bytes.
    if ((UINTN)(OLimit - Op) < (UINTN)(LitLen + LitLen / 255U + 4U)) {
      return 0U;
    }

    if (LitLen >= LZ4_RUN_MASK) {
      Extra     = LitLen - LZ4_RUN_MASK;
      *TokenPos = (UINT8)(LZ4_RUN_MASK << LZ4_ML_BITS);
      for (; Extra >= 255U; Extra -= 255U) {
        *Op++ = 255U;
      }

      *Op++ = (UINT8)Extra;
    } else {
      *TokenPos = (UINT8)(LitLen << LZ4_ML_BITS);
    }

    CopyMem (Op, Anchor, LitLen);
    Op += LitLen;

    // Write match offset (little-endian UINT16).
    Offset = (UINT16)(Ip - MatchRef);
    Lz4WriteLE16 (Op, Offset);
    Op += 2U;

    // -----------------------------------------------------------------------
    // Extend the match forward.
    // -----------------------------------------------------------------------
    MatchRef += LZ4_MINMATCH;
    Ip        += LZ4_MINMATCH;
    MatchLen   = 0U;

    while ((Ip < MatchLimit) && (Ip[0] == MatchRef[0])) {
      ++Ip;
      ++MatchRef;
      ++MatchLen;
    }

    // Encode match length.
    if (MatchLen >= LZ4_ML_MASK) {
      Extra      = MatchLen - LZ4_ML_MASK;
      *TokenPos |= (UINT8)LZ4_ML_MASK;

      if ((UINTN)(OLimit - Op) < (UINTN)(Extra / 255U + 2U)) {
        return 0U;
      }

      for (; Extra >= 255U; Extra -= 255U) {
        *Op++ = 255U;
      }

      *Op++ = (UINT8)Extra;
    } else {
      *TokenPos |= (UINT8)MatchLen;
    }

    // -----------------------------------------------------------------------
    // Refresh hash at/near the match end; reset the skip counter.
    // -----------------------------------------------------------------------
    Anchor    = Ip;
    SkipCount = 0U;

    if (Ip >= MfLimit) {
      goto LastLiterals;
    }

    // Keep the two positions just before/at Ip in the hash table so the next
    // search has warm entries to work from.
    {
      UINT32  H0 = Lz4Hash32 (Lz4Read32 (Ip - 2U));
      UINT32  H1 = Lz4Hash32 (Lz4Read32 (Ip));

      HashTable[H0] = (UINT32)(Ip - 2U - Src);
      HashTable[H1] = (UINT32)(Ip - Src);
    }

    ForwardIp = Ip + 1U;
  }

LastLiterals:
  {
    UINT32  LastLitLen = (UINT32)(IEnd - Anchor);
    UINT32  Extra;

    if ((UINTN)(OLimit - Op) < (UINTN)(1U + LastLitLen + LastLitLen / 255U + 1U)) {
      return 0U;
    }

    if (LastLitLen >= LZ4_RUN_MASK) {
      Extra  = LastLitLen - LZ4_RUN_MASK;
      *Op++  = (UINT8)(LZ4_RUN_MASK << LZ4_ML_BITS);

      for (; Extra >= 255U; Extra -= 255U) {
        *Op++ = 255U;
      }

      *Op++ = (UINT8)Extra;
    } else {
      *Op++ = (UINT8)(LastLitLen << LZ4_ML_BITS);
    }

    CopyMem (Op, Anchor, LastLitLen);
    Op += LastLitLen;
  }

  return (UINT32)(Op - Dst);
}

// ---------------------------------------------------------------------------
// LZ4 block decompressor (raw LZ4 block, no UEFI header)
// ---------------------------------------------------------------------------

/**
  Decompress a raw LZ4 block stream.

  @param[in]      Src       Pointer to compressed input (raw LZ4 block, no header).
  @param[in]      SrcSize   Size in bytes of Src.
  @param[out]     Dst       Pointer to decompressed output buffer.
  @param[in,out]  DstSize   On input: size of Dst.  On output: bytes written.

  @retval RETURN_SUCCESS            Decompression succeeded.
  @retval RETURN_BUFFER_TOO_SMALL   Dst is too small for the decompressed data.
  @retval RETURN_INVALID_PARAMETER  Input stream is malformed.
**/
STATIC RETURN_STATUS
Lz4BlockDecompress (
  IN      CONST UINT8  *Src,
  IN      UINT32        SrcSize,
  OUT     UINT8        *Dst,
  IN OUT  UINT32       *DstSize
  )
{
  CONST UINT8  *Ip    = Src;
  CONST UINT8  *IEnd  = Src + SrcSize;
  UINT8        *Op    = Dst;
  CONST UINT8  *OEnd  = Dst + *DstSize;

  while (Ip < IEnd) {
    UINT8   Token = *Ip++;
    UINT32  LitLen;
    UINT32  MatchLen;
    UINT16  MatchOffset;
    UINT8   *MatchRef;

    // ------------------------------------------------------------------
    // Decode literal run length.
    // ------------------------------------------------------------------
    LitLen = (UINT32)(Token >> 4U);

    if (LitLen == LZ4_RUN_MASK) {
      UINT8  S;

      do {
        if (Ip >= IEnd) {
          return RETURN_INVALID_PARAMETER;
        }

        S       = *Ip++;
        LitLen += S;
      } while (S == 255U);
    }

    // ------------------------------------------------------------------
    // Copy literal bytes.
    // ------------------------------------------------------------------
    if ((UINT32)(OEnd - Op) < LitLen) {
      return RETURN_BUFFER_TOO_SMALL;
    }

    if ((UINT32)(IEnd - Ip) < LitLen) {
      return RETURN_INVALID_PARAMETER;
    }

    CopyMem (Op, Ip, LitLen);
    Op += LitLen;
    Ip += LitLen;

    //
    // The last sequence in an LZ4 block has no match section; detect end.
    //
    if (Ip >= IEnd) {
      break;
    }

    // ------------------------------------------------------------------
    // Decode match offset (little-endian UINT16).
    // ------------------------------------------------------------------
    if ((UINT32)(IEnd - Ip) < 2U) {
      return RETURN_INVALID_PARAMETER;
    }

    MatchOffset = Lz4ReadLE16 (Ip);
    Ip         += 2U;

    if (MatchOffset == 0U) {
      return RETURN_INVALID_PARAMETER;
    }

    MatchRef = Op - MatchOffset;

    if (MatchRef < Dst) {
      return RETURN_INVALID_PARAMETER;
    }

    // ------------------------------------------------------------------
    // Decode match length.
    // ------------------------------------------------------------------
    MatchLen = (UINT32)(Token & LZ4_ML_MASK) + LZ4_MINMATCH;

    if ((Token & LZ4_ML_MASK) == LZ4_ML_MASK) {
      UINT8  S;

      do {
        if (Ip >= IEnd) {
          return RETURN_INVALID_PARAMETER;
        }

        S        = *Ip++;
        MatchLen += S;
      } while (S == 255U);
    }

    if ((UINT32)(OEnd - Op) < MatchLen) {
      return RETURN_BUFFER_TOO_SMALL;
    }

    // ------------------------------------------------------------------
    // Copy match bytes.
    // LZ4 allows overlapping copies (MatchOffset < MatchLen), so we copy
    // byte-by-byte to produce the correct result.
    // ------------------------------------------------------------------
    {
      UINT8  *OpEnd = Op + MatchLen;

      while (Op < OpEnd) {
        *Op++ = *MatchRef++;
      }
    }
  }

  *DstSize = (UINT32)(Op - Dst);
  return RETURN_SUCCESS;
}

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
Lz4CompressGetMaxBufferSize (
  IN UINT32  SourceSize
  )
{
  //
  // Conservative upper bound for an LZ4 block: source + source/255 + 16,
  // plus the 8-byte UEFI header.
  //
  return SourceSize + SourceSize / 255U + 16U + LZ4_HEADER_SIZE;
}

/**
  Compress Source using LZ4 and write the result (with UEFI header) to
  Destination.

  @param[in]      Source            Pointer to the uncompressed source data.
  @param[in]      SourceSize        Size in bytes of Source.
  @param[out]     Destination       Buffer to receive compressed output.
  @param[in,out]  DestinationSize   On input: size of Destination.
                                    On output: bytes written.

  @retval RETURN_SUCCESS            Compression succeeded.
  @retval RETURN_BUFFER_TOO_SMALL   Destination buffer is too small.
  @retval RETURN_OUT_OF_RESOURCES   Hash table memory allocation failed.
  @retval RETURN_INVALID_PARAMETER  A required pointer is NULL or SourceSize is 0.
**/
RETURN_STATUS
EFIAPI
Lz4UefiCompress (
  IN      CONST VOID  *Source,
  IN      UINT32       SourceSize,
  OUT     VOID        *Destination,
  IN OUT  UINT32      *DestinationSize
  )
{
  UINT32  *HashTable;
  UINT8   *OutBuffer;
  UINT32   RawSize;
  UINT32   RequiredSize;

  if ((Source == NULL) || (Destination == NULL) || (DestinationSize == NULL)) {
    return RETURN_INVALID_PARAMETER;
  }

  if (SourceSize == 0U) {
    return RETURN_INVALID_PARAMETER;
  }

  RequiredSize = Lz4CompressGetMaxBufferSize (SourceSize);
  if (*DestinationSize < RequiredSize) {
    *DestinationSize = RequiredSize;
    return RETURN_BUFFER_TOO_SMALL;
  }

  //
  // Allocate the encoder hash table via AllocatePages.
  // AllocatePool in PEI is capped at ~64 KiB (UINT16 HOB length); using
  // AllocatePages avoids that restriction while keeping the allocation small
  // (LZ4_HASH_ENTRIES * sizeof(UINT32) = 16 KiB = 4 pages).
  //
  HashTable = (UINT32 *)AllocatePages (
                          EFI_SIZE_TO_PAGES (LZ4_HASH_ENTRIES * sizeof (UINT32))
                          );
  if (HashTable == NULL) {
    return RETURN_OUT_OF_RESOURCES;
  }

  ZeroMem (HashTable, LZ4_HASH_ENTRIES * sizeof (UINT32));

  OutBuffer = (UINT8 *)Destination;

  //
  // Write the 8-byte UEFI header (little-endian).
  //
  {
    UINT32  Sig  = LZ4_UEFI_SIGNATURE;
    UINT32  Size = SourceSize;

    OutBuffer[0] = (UINT8)( Sig         & 0xFFU);
    OutBuffer[1] = (UINT8)((Sig >>  8U) & 0xFFU);
    OutBuffer[2] = (UINT8)((Sig >> 16U) & 0xFFU);
    OutBuffer[3] = (UINT8)((Sig >> 24U) & 0xFFU);
    OutBuffer[4] = (UINT8)( Size         & 0xFFU);
    OutBuffer[5] = (UINT8)((Size >>  8U) & 0xFFU);
    OutBuffer[6] = (UINT8)((Size >> 16U) & 0xFFU);
    OutBuffer[7] = (UINT8)((Size >> 24U) & 0xFFU);
  }

  //
  // Compress the raw LZ4 block into Destination + LZ4_HEADER_SIZE.
  //
  RawSize = Lz4BlockCompress (
              (CONST UINT8 *)Source,
              SourceSize,
              OutBuffer + LZ4_HEADER_SIZE,
              *DestinationSize - LZ4_HEADER_SIZE,
              HashTable
              );

  FreePages (HashTable, EFI_SIZE_TO_PAGES (LZ4_HASH_ENTRIES * sizeof (UINT32)));

  if (RawSize == 0U) {
    return RETURN_BUFFER_TOO_SMALL;
  }

  *DestinationSize = LZ4_HEADER_SIZE + RawSize;
  return RETURN_SUCCESS;
}

/**
  Decompress a buffer previously compressed with Lz4UefiCompress.

  @param[in]      Source            Pointer to the compressed source data.
  @param[in]      SourceSize        Size in bytes of Source (including header).
  @param[out]     Destination       Buffer to receive decompressed output.
  @param[in,out]  DestinationSize   On input: size of Destination.
                                    On output: bytes written.

  @retval RETURN_SUCCESS            Decompression succeeded.
  @retval RETURN_BUFFER_TOO_SMALL   Destination buffer is too small.
  @retval RETURN_INVALID_PARAMETER  Source or Destination is NULL, SourceSize
                                    is too small, or the header is invalid.
**/
RETURN_STATUS
EFIAPI
Lz4UefiDecompress (
  IN      CONST VOID  *Source,
  IN      UINT32       SourceSize,
  OUT     VOID        *Destination,
  IN OUT  UINT32      *DestinationSize
  )
{
  CONST UINT8  *InBuffer;
  UINT32        SigInStream;
  UINT32        OriginalSize;

  if ((Source == NULL) || (Destination == NULL) || (DestinationSize == NULL)) {
    return RETURN_INVALID_PARAMETER;
  }

  if (SourceSize <= LZ4_HEADER_SIZE) {
    return RETURN_INVALID_PARAMETER;
  }

  InBuffer = (CONST UINT8 *)Source;

  //
  // Validate the 4-byte signature (little-endian read).
  //
  SigInStream = Lz4Read32 (InBuffer);
  if (SigInStream != LZ4_UEFI_SIGNATURE) {
    return RETURN_INVALID_PARAMETER;
  }

  OriginalSize = Lz4Read32 (InBuffer + 4U);

  if (*DestinationSize < OriginalSize) {
    *DestinationSize = OriginalSize;
    return RETURN_BUFFER_TOO_SMALL;
  }

  *DestinationSize = OriginalSize;

  return Lz4BlockDecompress (
           InBuffer + LZ4_HEADER_SIZE,
           SourceSize - LZ4_HEADER_SIZE,
           (UINT8 *)Destination,
           DestinationSize
           );
}
