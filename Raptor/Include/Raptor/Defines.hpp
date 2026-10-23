/*
  +------------------------------------------------+
  |  _____             _    ___  _____    01010010 |
  | │  __ ╲           │ │  ╱ _ ╲│  __ ╲   01100001 |
  | │ │__) │__ _ _ __ │ │_│ │ │ │ │__) │  01110000 |
  | │  _  ╱╱ _` │ '_ ╲│ __│ │ │ │  _  ╱   01110100 |
  | │ │ ╲ ╲ (_│ │ │_) │ │_│ │_│ │ │ ╲ ╲   00110000 |
  | │_│  ╲_╲__,_│ .__╱ ╲__│╲___╱│_│  ╲_╲  01010010 |
  |             │ │ A fast and simple C++ 00010100 |
  |             │_│ binary serialization library   |
  +------------------------------------------------+
*/

#pragma once
#include <cstdint>

#define R0R_ND [[nodiscard]]

#if defined(_MSC_VER)
#define R0R_INLINE __forceinline
#define R0R_NOINLINE __declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
#define R0R_INLINE inline __attribute__((always_inline))
#define R0R_NOINLINE __attribute__((noinline))
#else
#define R0R_INLINE inline
#define R0R_NOINLINE
#endif

namespace Rapt0R
{
    using Byte = uint8_t;

    enum class ErrorCode : uint8_t
    {
        SUCCESS = 0,
        FAILED,
        BUFFER_OVERFLOW,
        CORRUPT_DATA,
        INVALID_HEADER
    };

    struct Result
    {
        ErrorCode Error = ErrorCode::FAILED;
        uint64_t BytesProcessed = 0;
        constexpr explicit operator bool() const noexcept { return Error == ErrorCode::SUCCESS; }
    };
}