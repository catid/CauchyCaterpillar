/** \file
    \brief CCatCodec : Tools
    \copyright Copyright (c) 2018 Christopher A. Taylor.  All rights reserved.

    Redistribution and use in source and binary forms, with or without
    modification, are permitted provided that the following conditions are met:

    * Redistributions of source code must retain the above copyright notice,
      this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright notice,
      this list of conditions and the following disclaimer in the documentation
      and/or other materials provided with the distribution.
    * Neither the name of CCat nor the names of its contributors may be
      used to endorse or promote products derived from this software without
      specific prior written permission.

    THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
    AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
    IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
    ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
    LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
    CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
    SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
    INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
    CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
    ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
    POSSIBILITY OF SUCH DAMAGE.
*/

#pragma once

#include "ccat.h"
#include "gf256.h"
#include "Counter.h"
#include "PacketAllocator.h"

#include <stdint.h> // uint32_t
#include <string.h> // memcpy
#include <new> // std::nothrow


//------------------------------------------------------------------------------
// Portability macros

// Compiler-specific debug break
#if defined(_DEBUG) || defined(DEBUG)
    #define CCAT_DEBUG
    #ifdef _WIN32
        #define CCAT_DEBUG_BREAK() __debugbreak()
    #else
        #define CCAT_DEBUG_BREAK() __builtin_trap()
    #endif
    #define CCAT_DEBUG_ASSERT(cond) { if (!(cond)) { CCAT_DEBUG_BREAK(); } }
#else
    #define CCAT_DEBUG_BREAK() do {} while (false);
    #define CCAT_DEBUG_ASSERT(cond) do {} while (false);
#endif

// Compiler-specific force inline keyword
#ifdef _MSC_VER
    #define CCAT_FORCE_INLINE inline __forceinline
#else
    #define CCAT_FORCE_INLINE inline __attribute__((always_inline))
#endif


namespace ccat {


//------------------------------------------------------------------------------
// Constants

/// Max original columns in matrix
/// 1.3333.. * x = 256, x = 192, Enables up to 33% FEC
/// This is also a multiple of 64 which makes the most of bitfields
static const unsigned kMatrixColumnCount = 192;
static_assert(kMatrixColumnCount < 256, "Too large");

/// Max recovery rows in matrix
static const unsigned kMatrixRowCount = 256 - kMatrixColumnCount;

/// Limit the size of a recovery attempt
static const unsigned kMaxRecoveryColumns = 128;

/// Limit the size of involved recovery rows
static const unsigned kMaxRecoveryRows = kMaxRecoveryColumns + 32;
static_assert(kMaxRecoveryRows > kMaxRecoveryColumns, "Update this too");

/// Min encoder window size
static const unsigned kMinEncoderWindowSize = 1;

/// Max encoder window size
static const unsigned kMaxEncoderWindowSize = kMatrixColumnCount;
static_assert(kMaxEncoderWindowSize == CCAT_MAX_WINDOW_PACKETS, "Header mismatch");

/// Max decoder window size
static const unsigned kDecoderWindowSize = 2 * kMatrixColumnCount;

/// Max packet size
static const unsigned kMaxPacketSize = 65536;
static_assert(kMaxPacketSize == CCAT_MAX_BYTES, "Header mismatch");

/// Min window size in msec
static const unsigned kMinWindowMsec = 10;
static_assert(kMinWindowMsec == CCAT_MIN_WINDOW_MSEC, "Header mismatch");

/// Max window size in msec
static const unsigned kMaxWindowMsec = 2000 * 1000; // Int32 limits

/// Encode overhead
static const unsigned kEncodeOverhead = 2;


//------------------------------------------------------------------------------
// Timing

/// Platform independent high-resolution timers
uint64_t GetTimeUsec();
uint64_t GetTimeMsec();


//------------------------------------------------------------------------------
// POD Serialization

CCAT_FORCE_INLINE uint16_t ReadU16_LE(const uint8_t* data)
{
#ifdef GF256_ALIGNED_ACCESSES
    return ((uint16_t)data[1] << 8) | data[0];
#else
    return *(uint16_t*)data;
#endif
}

CCAT_FORCE_INLINE uint32_t ReadU24_LE(const uint8_t* data)
{
    return ((uint32_t)data[2] << 16) | ((uint32_t)data[1] << 8) | data[0];
}

/// This version uses one memory read on Intel but requires at least 4 bytes in the buffer
CCAT_FORCE_INLINE uint32_t ReadU24_LE_Min4Bytes(const uint8_t* data)
{
#ifdef GF256_ALIGNED_ACCESSES
    return ReadU24_LE(data);
#else
    return *(uint32_t*)data & 0xFFFFFF;
#endif
}

CCAT_FORCE_INLINE uint32_t ReadU32_LE(const uint8_t* data)
{
#ifdef GF256_ALIGNED_ACCESSES
    return ((uint32_t)data[3] << 24) | ((uint32_t)data[2] << 16) | ((uint32_t)data[1] << 8) | data[0];
#else
    return *(uint32_t*)data;
#endif
}

CCAT_FORCE_INLINE uint64_t ReadU64_LE(const uint8_t* data)
{
#ifdef GF256_ALIGNED_ACCESSES
    return ((uint64_t)data[7] << 56) | ((uint64_t)data[6] << 48) | ((uint64_t)data[5] << 40) |
        ((uint64_t)data[4] << 32) | ((uint64_t)data[3] << 24) | ((uint64_t)data[2] << 16) |
        ((uint64_t)data[1] << 8) | data[0];
#else
    return *(uint64_t*)data;
#endif
}

CCAT_FORCE_INLINE void WriteU16_LE(uint8_t* data, uint16_t value)
{
#ifdef GF256_ALIGNED_ACCESSES
    data[1] = (uint8_t)(value >> 8);
    data[0] = (uint8_t)value;
#else
    *(uint16_t*)data = value;
#endif
}

CCAT_FORCE_INLINE void WriteU24_LE(uint8_t* data, uint32_t value)
{
    data[2] = (uint8_t)(value >> 16);
    WriteU16_LE(data, (uint16_t)value);
}

CCAT_FORCE_INLINE void WriteU24_LE_Min4Bytes(uint8_t* data, uint32_t value)
{
#ifdef GF256_ALIGNED_ACCESSES
    WriteU24_LE(data, value);
#else
    *(uint32_t*)data = value;
#endif
}

CCAT_FORCE_INLINE void WriteU32_LE(uint8_t* data, uint32_t value)
{
#ifdef GF256_ALIGNED_ACCESSES
    data[3] = (uint8_t)(value >> 24);
    data[2] = (uint8_t)(value >> 16);
    data[1] = (uint8_t)(value >> 8);
    data[0] = (uint8_t)value;
#else
    *(uint32_t*)data = value;
#endif
}

CCAT_FORCE_INLINE void WriteU64_LE(uint8_t* data, uint64_t value)
{
#ifdef GF256_ALIGNED_ACCESSES
    data[7] = (uint8_t)(value >> 56);
    data[6] = (uint8_t)(value >> 48);
    data[5] = (uint8_t)(value >> 40);
    data[4] = (uint8_t)(value >> 32);
    data[3] = (uint8_t)(value >> 24);
    data[2] = (uint8_t)(value >> 16);
    data[1] = (uint8_t)(value >> 8);
    data[0] = (uint8_t)value;
#else
    *(uint64_t*)data = value;
#endif
}


//------------------------------------------------------------------------------
// AlignedLightVector

/**
    Super light-weight replacement for std::vector class

    Features:
    + SIMD aligned buffer.
    + Tuned for Siamese allocation needs.
    + Never shrinks memory usage.
    + Minimal well-defined API: Only functions used several times.
    + Preallocates some elements to improve speed of short runs.
    + Uses normal allocator.
    + Growing the vector does not initialize the new elements for speed.
    + Does not throw on out-of-memory error.
*/
class AlignedLightVector
{
    /// Vector data
    uint8_t* DataPtr = nullptr;

    /// Size of vector: Count of elements in the vector
    unsigned Size = 0;

    /// Number of elements allocated
    unsigned Allocated = 0;

public:
    /// Resize to target size.
    /// Uses the provided allocator and does not automatically free on dtor
    bool Resize(
        pktalloc::Allocator* alloc,
        unsigned elements,
        pktalloc::Realloc behavior);

    /// Set size to zero
    CCAT_FORCE_INLINE void Clear()
    {
        Size = 0;
    }

    /// Get current size (initially 0)
    CCAT_FORCE_INLINE unsigned GetSize() const
    {
        return Size;
    }

    /// Return a pointer to an element
    CCAT_FORCE_INLINE uint8_t* GetPtr(int index = 0) const
    {
        return DataPtr + index;
    }
};


//------------------------------------------------------------------------------
// Cauchy Matrix Math

/*
    GF(256) Cauchy Matrix Overview

    As described on Wikipedia, each element of a normal Cauchy matrix is defined as:

        a_ij = 1 / (x_i - y_j)
        The arrays x_i and y_j are vector parameters of the matrix.
        The values in x_i cannot be reused in y_j.

    Moving beyond the Wikipedia...

    (1) Number of rows (R) is the range of i, and number of columns (C) is the range of j.

    (2) Being able to select x_i and y_j makes Cauchy matrices more flexible in practice
        than Vandermonde matrices, which only have one parameter per row.

    (3) Cauchy matrices are always invertible, AKA always full rank, AKA when treated as
        as linear system y = M*x, the linear system has a single solution.

    (4) A Cauchy matrix concatenated below a square CxC identity matrix always has rank C,
        Meaning that any R rows can be eliminated from the concatenated matrix and the
        matrix will still be invertible.  This is how Reed-Solomon erasure codes work.

    (5) Any row or column can be multiplied by non-zero values, and the resulting matrix
        is still full rank.  This is true for any matrix, since it is effectively the same
        as pre and post multiplying by diagonal matrices, which are always invertible.

    (6) Matrix elements with a value of 1 are much faster to operate on than other values.
        For instance a matrix of [1, 1, 1, 1, 1] is invertible and much faster for various
        purposes than [2, 2, 2, 2, 2].

    (7) For GF(256) matrices, the symbols in x_i and y_j are selected from the numbers
        0...255, and so the number of rows + number of columns may not exceed 256.
        Note that values in x_i and y_j may not be reused as stated above.

    In summary, Cauchy matrices
        are preferred over Vandermonde matrices.  (2)
        are great for MDS erasure codes.  (3) and (4)
        should be optimized to include more 1 elements.  (5) and (6)
        have a limited size in GF(256), rows+cols <= 256.  (7)
*/

/*
    Selected Cauchy Matrix Form

    The matrix consists of elements a_ij, where i = row, j = column.
    a_ij = 1 / (x_i - y_j), where x_i and y_j are sets of GF(256) values
    that do not intersect.

    We select x_i and y_j to just be incrementing numbers for the
    purposes of this library.  Further optimizations may yield matrices
    with more 1 elements, but the benefit seems relatively small.

    The x_i values range from 0...(originalCount - 1).
    The y_j values range from originalCount...(originalCount + recoveryCount - 1).

    We then improve the Cauchy matrix by dividing each column by the
    first row element of that column.  The result is an invertible
    matrix that has all 1 elements in the first row.  This is equivalent
    to a rotated Vandermonde matrix, so we could have used one of those.

    The advantage of doing this is that operations involving the first
    row will be extremely fast (just memory XOR), so the decoder can
    be optimized to take advantage of the shortcut when the first
    recovery row can be used.

    First row element of Cauchy matrix for each column:
    a_0j = 1 / (x_0 - y_j) = 1 / (x_0 - y_j)

    Our Cauchy matrix sets first row to ones, so:
    a_ij = (1 / (x_i - y_j)) / a_0j
    a_ij = (y_j - x_0) / (x_i - y_j)
    a_ij = (y_j + x_0) div (x_i + y_j) in GF(256)
*/

// This function generates each matrix element based on x_i, x_0, y_j
// Note that for x_i == x_0, this will return 1, so it is better to unroll out the first row.
// This is specialized for x_0 = 0.  So x starts at 0 and y starts at x + CountXValues.
static GF256_FORCE_INLINE uint8_t GetMatrixElement(
    uint8_t recoveryRow,
    uint8_t originalColumn)
{
    const uint8_t x_i = recoveryRow;
    const uint8_t y_j = originalColumn + (uint8_t)kMatrixRowCount;
    CCAT_DEBUG_ASSERT(x_i < y_j);
    const uint8_t result = gf256_div(y_j, gf256_add(x_i, y_j));
    CCAT_DEBUG_ASSERT(result != 0);
    return result;
}


} // namespace ccat
