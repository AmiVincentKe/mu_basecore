/** @file
  LzmaCompressLib produces LZMA custom compression algorithm.

  Copyright (c) 2025, Microsoft Corporation. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#ifndef __LZMA_COMPRESS_LIB_H__
#define __LZMA_COMPRESS_LIB_H__

/**
  Get the maximum buffer size required to hold the compressed output
  for an input of the given size.

  This is a conservative upper bound:
    DestSize <= SourceSize * 21/20 + 65536 + 13 (LZMA header)

  @param[in]  SourceSize   Size in bytes of the source (uncompressed) data.

  @return  Maximum number of bytes that the compressed output may occupy.
**/
UINT32
EFIAPI
LzmaCompressGetMaxBufferSize (
  IN UINT32  SourceSize
  );

/**
  Compress a source buffer using LZMA and write the result to Destination.

  The compressed output begins with a 13-byte LZMA header:
    Bytes  0-4  : LZMA properties (5 bytes)
    Bytes  5-12 : Original (uncompressed) size, stored little-endian UINT64

  This layout is compatible with LzmaUefiDecompress in
  LzmaCustomDecompressLib.

  @param[in]      Source            Pointer to the uncompressed source data.
  @param[in]      SourceSize        Size in bytes of Source.
  @param[out]     Destination       Buffer to receive compressed output.
                                    Must be at least
                                    LzmaCompressGetMaxBufferSize(SourceSize) bytes.
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
LzmaUefiCompress (
  IN      CONST VOID  *Source,
  IN      UINT32      SourceSize,
  OUT     VOID        *Destination,
  IN OUT  UINT32      *DestinationSize
  );

#endif // __LZMA_COMPRESS_LIB_H__
