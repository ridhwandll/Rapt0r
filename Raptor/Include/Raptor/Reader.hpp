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

#include "Defines.hpp"
#include "TypeTraits/Vector.hpp"
#include "TypeTraits/Funcs.hpp"

#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Rapt0R
{
   template <typename T>
    inline constexpr bool IsPODBlittable_V =
        std::is_trivially_copyable_v<T> &&
        !std::is_pointer_v<T> &&
        !std::is_same_v<T, std::string_view> &&
        !IsStandardVector_V<T>;

    class Reader
    {
    public:
        constexpr explicit Reader(std::span<const Byte> buffer) noexcept
            : mStart(buffer.data()), mCur(buffer.data()), mEnd(buffer.data() + buffer.size()), mStatus(ErrorCode::SUCCESS) {}

        constexpr Reader(const void* data, size_t sizeBytes) noexcept
            : mStart(static_cast<const Byte*>(data)), mCur(static_cast<const Byte*>(data)), mEnd(static_cast<const Byte*>(data) + sizeBytes), mStatus(ErrorCode::SUCCESS) {}

        R0R_ND R0R_INLINE constexpr size_t Cursor() const noexcept                    { return static_cast<size_t>(mCur - mStart); }
        R0R_ND R0R_INLINE constexpr size_t RemainingBytes() const noexcept            { return static_cast<size_t>(mEnd - mCur); }
        R0R_ND R0R_INLINE constexpr bool HasError() const noexcept                    { return mStatus != ErrorCode::SUCCESS; }
        R0R_ND R0R_INLINE constexpr ErrorCode Status() const noexcept                 { return mStatus; }
        R0R_ND R0R_INLINE constexpr std::span<const Byte> UnreadSpan() const noexcept { return { mCur, RemainingBytes() }; }
        R0R_ND R0R_INLINE constexpr explicit operator bool() const noexcept           { return !HasError(); }
        R0R_ND R0R_INLINE bool EnsureRemaining(size_t bytes) noexcept                 { return !IsOverflowing(bytes); }

        // Fast power-of-two cursor alignment (Crucial for zero-copy ARM64 / SIMD spans)
        R0R_INLINE bool Align(size_t alignment) noexcept
        {
            const size_t offset = Cursor();
            const size_t alignedOffset = (offset + (alignment - 1)) & ~(alignment - 1);
            const size_t padding = alignedOffset - offset;

            if(IsOverflowing(padding)) [[unlikely]]
                return false;

            mCur += padding;
            return true;
        }

        R0R_INLINE bool Skip(size_t byteCount) noexcept
        {
            if(IsOverflowing(byteCount)) [[unlikely]]
                return false;

            mCur += byteCount;
            return true;
        }

        R0R_INLINE bool Seek(size_t absolutePosition) noexcept
        {
            const size_t totalSize = static_cast<size_t>(mEnd - mStart);
            if(absolutePosition > totalSize) [[unlikely]]
            {
                mStatus = ErrorCode::BUFFER_OVERFLOW;
                return false;
            }
            mCur = mStart + absolutePosition;
            return true;
        }

        // Zero-overhead unchecked read (Call ONLY after EnsureRemaining!)
        template <typename T> requires IsPODBlittable_V<T>
        R0R_ND R0R_INLINE T ReadUnchecked() noexcept
        {
            T val;
            std::memcpy(&val, mCur, sizeof(T));
            mCur += sizeof(T);
            return val;
        }

        R0R_INLINE bool ReadRawBytes(void* destination, size_t size) noexcept
        {
            if(IsOverflowing(size)) [[unlikely]]
                return false;

            std::memcpy(destination, mCur, size);
            mCur += size;
            return true;
        }

        R0R_INLINE bool ReadStringView(std::string_view& outView) noexcept
        {
            uint32_t len = 0;
            if(!ReadRawBytes(&len, sizeof(uint32_t))) [[unlikely]]
                return false;

            if(IsOverflowing(len)) [[unlikely]]
                return false;

            outView = std::string_view(reinterpret_cast<const char*>(mCur), len);
            mCur += len;
            return true;
        }

        bool ReadString(std::string& target)
        {
            uint32_t len = 0;
            if(!ReadRawBytes(&len, sizeof(uint32_t))) [[unlikely]]
                return false;

            if(IsOverflowing(len)) [[unlikely]]
                return false;

#if __cpp_lib_string_resize_and_overwrite >= 202110L
// C++23: Avoids double zero-initialization before memcpy
            target.resize_and_overwrite(len, [this](char* buf, size_t n) noexcept { std::memcpy(buf, mCur, n); return n; });
#else
            target.assign(reinterpret_cast<const char*>(mCur), len);
#endif

            mCur += len;
            return true;
        }

        // Zero-copy span extraction with optional hardware alignment
        template <typename T>
        requires IsPODBlittable_V<T>
        R0R_INLINE bool ReadSpan(std::span<const T>& outSpan, bool alignToType = false) noexcept
        {
            uint32_t count = 0;
            if(!ReadRawBytes(&count, sizeof(uint32_t))) [[unlikely]]
                return false;

            if(alignToType && !Align(alignof(T))) [[unlikely]]
                return false;

            const size_t totalBytes = static_cast<size_t>(count) * sizeof(T);
            if(IsOverflowing(totalBytes)) [[unlikely]]
                return false;

            outSpan = std::span<const T>(reinterpret_cast<const T*>(mCur), count);
            mCur += totalBytes;
            return true;
        }

        template <typename T>
        bool ReadVector(std::vector<T>& outVec)
        {
            uint32_t count = 0;
            if(!ReadRawBytes(&count, sizeof(uint32_t))) [[unlikely]]
                return false;

            if(count == 0)
            {
                outVec.clear();
                return true;
            }

            if constexpr(IsPODBlittable_V<T>)
            {
                const size_t totalBytes = static_cast<size_t>(count) * sizeof(T);
                if(IsOverflowing(totalBytes)) [[unlikely]]
                    return false;

                outVec.resize(count);
                std::memcpy(outVec.data(), mCur, totalBytes);
                mCur += totalBytes;
                return true;
            }
            else
            {
                outVec.resize(count); // Reuse existing element capacity
                for(uint32_t i = 0; i < count; ++i)
                {
                    if(!(*this >> outVec[i])) [[unlikely]]
                        return false;
                }
                return true;
            }
        }

        template <typename T>
        R0R_INLINE Reader& operator>>(T& target)
        {
            if(HasError()) [[unlikely]]
                return *this;

            using RawType = std::remove_cvref_t<T>;

            if constexpr(std::is_same_v<RawType, std::string_view>)
                ReadStringView(target);
            else if constexpr(std::is_same_v<RawType, std::string>)
                ReadString(target);
            else if constexpr(IsStandardVector_V<RawType>)
                ReadVector(target);
            else if constexpr(IsPODBlittable_V<RawType>)
                ReadRawBytes(&target, sizeof(RawType));
            else if constexpr(HasMemberDeserialize<RawType, Reader>)
                target.RAPTOR_DESERIALIZE_FUNC(*this);
            else if constexpr(HasFreeDeserialize<RawType, Reader>)
                RAPTOR_DESERIALIZE_FUNC(*this, target);
            else
                static_assert(sizeof(T) == 0, "Type does not support deserialization via Rapt0R::Reader");

            return *this;
        }

        template <typename T> requires IsPODBlittable_V<T>
        R0R_INLINE void ReadUncheckedInto(T& target) noexcept
        {
            std::memcpy(&target, mCur, sizeof(T));
            mCur += sizeof(T);
        }

        template <typename... Ts> requires (sizeof...(Ts) > 0) && (IsPODBlittable_V<std::remove_cvref_t<Ts>> && ...)
        R0R_INLINE bool ReadPODs(Ts&... args) noexcept
        {
            if(HasError()) [[unlikely]]
                return false;

            constexpr size_t totalBytes = (sizeof(Ts) + ... + 0);

            if(IsOverflowing(totalBytes)) [[unlikely]]
                return false;

            (ReadUncheckedInto(args), ...);
            return true;
        }
    private:
        R0R_INLINE bool IsOverflowing(size_t size) noexcept
        {
            if(static_cast<size_t>(mEnd - mCur) < size) [[unlikely]]
            {
                mStatus = ErrorCode::BUFFER_OVERFLOW;
                return true;
            }
            return false;
        }

    private:
        const Byte* mStart;
        const Byte* mCur;
        const Byte* mEnd;
        ErrorCode mStatus;
    };
}