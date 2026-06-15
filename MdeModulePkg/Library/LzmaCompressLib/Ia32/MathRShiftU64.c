/** @file
  64-bit unsigned right shift helper for MSVC IA32 builds.

  The MSVC 32-bit compiler generates calls to _aullshr when a module
  performs a 64-bit unsigned right shift (>>) on IA32.  UEFI modules
  are built with /NODEFAULTLIB so the CRT-provided version is absent.
  This file supplies the replacement so that LzmaEnc.c (which uses
  UInt64 range-coder variables) links cleanly.

  Derived from CryptoPkg/Library/IntrinsicLib/Ia32/MathRShiftU64.c
  (Intel Corporation, BSD-2-Clause-Patent).

  Copyright (c) Microsoft Corporation.
  Your use of this software is governed by the terms of the Microsoft
  agreement under which you obtained the software.
**/

/*
 * Shifts a 64-bit unsigned value right by a certain number of bits.
 * Input : EDX:EAX  – value to shift; CL – shift count.
 * Output: EDX:EAX  – shifted result.
 */
__declspec (naked) void __cdecl
_aullshr (
  void
  )
{
  _asm {
    ;
    ; Checking: Only handle 64-bit shifting or more
    ;
    cmp     cl, 64
    jae     _Exit

    ;
    ; Handle shifting between 0 and 31 bits
    ;
    cmp     cl, 32
    jae     More32
    shrd    eax, edx, cl
    shr     edx, cl
    ret

    ;
    ; Handle shifting of 32-63 bits
    ;
More32:
    mov     eax, edx
    xor     edx, edx
    and     cl, 31
    shr     eax, cl
    ret

    ;
    ; Shift count >= 64: result is 0
    ;
_Exit:
    xor     eax, eax
    xor     edx, edx
    ret
  }
}
