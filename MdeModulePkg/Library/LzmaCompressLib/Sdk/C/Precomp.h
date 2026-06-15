/* Precomp.h -- StdAfx
2026-06-15 : Igor Pavlov : Public domain */

#ifndef __7Z_PRECOMP_H
#define __7Z_PRECOMP_H

/*
 * Pull in the UEFI compatibility shim BEFORE any SDK header so that:
 *   - _7ZIP_ST is defined (disables LzFindMt.h / threading)
 *   - memmove/memcpy/memset are remapped to CopyMem/SetMem
 *   - size_t is defined as UINTN
 * This header is two directory levels above Sdk/C/ (i.e. the library root).
 */
#include "../../UefiLzma.h"

#include "Compiler.h"

#endif
