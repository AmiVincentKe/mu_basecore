/** @file
  Lz4CompressLib produces an LZ4 block compression algorithm.

  The compressed output format wraps a raw LZ4 block with an 8-byte UEFI
  header:
    Bytes  0-3 : LZ4 UEFI signature ('L','Z','4','U')
    Bytes  4-7 : Original (uncompressed) size stored as little-endian UINT32
    Bytes  8-N : Raw LZ4 block-format compressed data

  Copyright (c) Microsoft Corporation. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#ifndef __LZ4_COMPRESS_LIB_H__
#define __LZ4_COMPRESS_LIB_H__

/**
  Get the maximum buffer size required to hold the compressed output
  for an input of the given size.

  This is a conservative upper bound:
    DestSize <= SourceSize + SourceSize/255 + 16 + 8 (UEFI header)

  @param[in]  SourceSize   Size in bytes of the source (uncompressed) data.

  @return  Maximum number of bytes that the compressed output may occupy.
**/
UINT32
EFIAPI
Lz4CompressGetMaxBufferSize (
  IN UINT32  SourceSize
  );

/**
  Compress a source buffer using LZ4 block format and write the result to
  Destination.

  The compressed output begins with an 8-byte UEFI header:
    Bytes  0-3 : LZ4 UEFI signature ('L','Z','4','U')
    Bytes  4-7 : Original (uncompressed) size, stored little-endian UINT32

  The remainder is a raw LZ4 block stream compatible with the LZ4 block
  decompression specification.

  @param[in]      Source            Pointer to the uncompressed source data.
  @param[in]      SourceSize        Size in bytes of Source.
  @param[out]     Destination       Buffer to receive compressed output.
                                    Must be at least
                                    Lz4CompressGetMaxBufferSize(SourceSize) bytes.
  @param[in,out]  DestinationSize   On input: size in bytes of Destination.
                                    On output: number of bytes written.

  @retval RETURN_SUCCESS            Compression succeeded.
  @retval RETURN_BUFFER_TOO_SMALL   Destination buffer too small; DestinationSize
                                    updated to the required minimum size.
  @retval RETURN_OUT_OF_RESOURCES   Memory allocation for the encoder failed.
  @retval RETURN_INVALID_PARAMETER  Source or Destination is NULL, or SourceSize is 0.
**/
RETURN_STATUS
EFIAPI
Lz4UefiCompress (
  IN      CONST VOID  *Source,
  IN      UINT32       SourceSize,
  OUT     VOID        *Destination,
  IN OUT  UINT32      *DestinationSize
  );

/**
  Decompress a buffer that was previously compressed with Lz4UefiCompress.

  Reads the 8-byte UEFI header to verify the signature and obtain the original
  (uncompressed) size, then decompresses the raw LZ4 block payload.

  @param[in]      Source            Pointer to the compressed source data.
  @param[in]      SourceSize        Size in bytes of Source (including header).
  @param[out]     Destination       Buffer to receive decompressed output.
                                    Must be at least OriginalSize bytes.
  @param[in,out]  DestinationSize   On input: size in bytes of Destination.
                                    On output: number of bytes written.

  @retval RETURN_SUCCESS            Decompression succeeded.
  @retval RETURN_BUFFER_TOO_SMALL   Destination buffer too small; DestinationSize
                                    updated to the required minimum size.
  @retval RETURN_INVALID_PARAMETER  Source or Destination is NULL, SourceSize is
                                    too small, or the header signature is invalid.
**/
RETURN_STATUS
EFIAPI
Lz4UefiDecompress (
  IN      CONST VOID  *Source,
  IN      UINT32       SourceSize,
  OUT     VOID        *Destination,
  IN OUT  UINT32      *DestinationSize
  );

#endif // __LZ4_COMPRESS_LIB_H__
