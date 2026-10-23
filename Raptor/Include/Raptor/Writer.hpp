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
#include <cstdlib>
#include <cstring>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Rapt0R
{
    class Writer
    {
    public:
        explicit Writer(size_t initialCapacity = 4096) noexcept
            : mStart(nullptr), mCur(nullptr), mEnd(nullptr), mStatus(ErrorCode::SUCCESS), mOwnsMemory(true)
        {
            if(initialCapacity > 0)
                Reserve(initialCapacity);
        }

        constexpr explicit Writer(std::span<Byte> externalBuffer) noexcept
            : mStart(externalBuffer.data()), mCur(externalBuffer.data()), mEnd(externalBuffer.data() + externalBuffer.size()), mStatus(ErrorCode::SUCCESS), mOwnsMemory(false) {}

        constexpr Writer(void* data, size_t sizeBytes) noexcept
            : mStart(static_cast<Byte*>(data)), mCur(static_cast<Byte*>(data)), mEnd(static_cast<Byte*>(data) + sizeBytes), mStatus(ErrorCode::SUCCESS), mOwnsMemory(false) {}

        ~Writer() noexcept
        {
            if(mOwnsMemory && mStart)
                std::free(mStart);
        }

        Writer(const Writer&) = delete;
        Writer& operator=(const Writer&) = delete;

        constexpr Writer(Writer&& other) noexcept
            : mStart(other.mStart), mCur(other.mCur), mEnd(other.mEnd), mStatus(other.mStatus), mOwnsMemory(other.mOwnsMemory)
        {
            other.mStart = nullptr;
            other.mCur = nullptr;
            other.mEnd = nullptr;
            other.mOwnsMemory = false;
        }

        constexpr Writer& operator=(Writer&& other) noexcept
        {
            if(this != &other)
            {
                if(mOwnsMemory && mStart)
                    std::free(mStart);

                mStart = other.mStart;
                mCur = other.mCur;
                mEnd = other.mEnd;
                mStatus = other.mStatus;
                mOwnsMemory = other.mOwnsMemory;

                other.mStart = nullptr;
                other.mCur = nullptr;
                other.mEnd = nullptr;
                other.mOwnsMemory = false;
            }
            return *this;
        }

        R0R_ND R0R_INLINE constexpr const Byte* Data() const noexcept                  { return mStart; }
        R0R_ND R0R_INLINE constexpr Byte* Data() noexcept                              { return mStart; }
        R0R_ND R0R_INLINE constexpr size_t Size() const noexcept                       { return static_cast<size_t>(mCur - mStart); }
        R0R_ND R0R_INLINE constexpr size_t Capacity() const noexcept                   { return static_cast<size_t>(mEnd - mStart); }
        R0R_ND R0R_INLINE constexpr size_t RemainingCapacity() const noexcept          { return static_cast<size_t>(mEnd - mCur); }
        R0R_ND R0R_INLINE constexpr bool HasError() const noexcept                     { return mStatus != ErrorCode::SUCCESS; }
        R0R_ND R0R_INLINE constexpr ErrorCode Status() const noexcept                  { return mStatus; }
        R0R_ND R0R_INLINE constexpr std::span<const Byte> WrittenSpan() const noexcept { return { mStart, Size() }; }
        R0R_ND R0R_INLINE constexpr explicit operator bool() const noexcept            { return !HasError(); }

        R0R_INLINE void Clear() noexcept
        {
            mCur = mStart;
            mStatus = ErrorCode::SUCCESS;
        }

        bool Reserve(size_t newCapacity) noexcept
        {
            if(newCapacity <= Capacity())
                return true;

            if(!mOwnsMemory) [[unlikely]]
            {
                mStatus = ErrorCode::BUFFER_OVERFLOW;
                return false;
            }

            const size_t currentOffset = Size();
            Byte* newBlock = static_cast<Byte*>(std::realloc(mStart, newCapacity));
            if(!newBlock) [[unlikely]]
            {
                mStatus = ErrorCode::BUFFER_OVERFLOW;
                return false;
            }

            mStart = newBlock;
            mCur = mStart + currentOffset;
            mEnd = mStart + newCapacity;
            return true;
        }

        R0R_ND R0R_INLINE bool EnsureCapacity(size_t extraBytes) noexcept
        {
            if(RemainingCapacity() < extraBytes) [[unlikely]]
                return Grow(extraBytes);

            return true;
        }

        // Fast power-of-two cursor alignment (Zero-fills padding bytes for deterministic output)
        R0R_INLINE bool Align(size_t alignment) noexcept
        {
            const size_t offset = Size();
            const size_t alignedOffset = (offset + (alignment - 1)) & ~(alignment - 1);
            const size_t padding = alignedOffset - offset;

            if(padding == 0)
                return true;

            if(!EnsureCapacity(padding)) [[unlikely]]
                return false;

            std::memset(mCur, 0, padding);
            mCur += padding;
            return true;
        }

        // Zero-overhead unchecked write (Call ONLY after EnsureCapacity!)
        template <typename T> requires IsPODBlittable_V<T>
        R0R_INLINE void WriteUnchecked(const T& val) noexcept
        {
            std::memcpy(mCur, &val, sizeof(T));
            mCur += sizeof(T);
        }

        R0R_INLINE bool WriteRawBytes(const void* source, size_t size) noexcept
        {
            if(!EnsureCapacity(size)) [[unlikely]]
                return false;

            std::memcpy(mCur, source, size);
            mCur += size;
            return true;
        }

        R0R_INLINE bool WriteString(std::string_view str) noexcept
        {
            const uint32_t len = static_cast<uint32_t>(str.size());
            const size_t totalBytes = sizeof(uint32_t) + len;

            // Single capacity check for both the length prefix and string payload!
            if(!EnsureCapacity(totalBytes)) [[unlikely]]
                return false;

            std::memcpy(mCur, &len, sizeof(uint32_t));
            mCur += sizeof(uint32_t);

            if(len > 0)
            {
                std::memcpy(mCur, str.data(), len);
                mCur += len;
            }
            return true;
        }

        template <typename T> requires IsPODBlittable_V<T>
        R0R_INLINE bool WriteSpan(std::span<const T> span, bool alignToType = false) noexcept
        {
            const uint32_t count = static_cast<uint32_t>(span.size());
            if(!WriteRawBytes(&count, sizeof(uint32_t))) [[unlikely]]
                return false;

            if(count == 0)
                return true;

            if(alignToType && !Align(alignof(T))) [[unlikely]]
                return false;

            const size_t totalBytes = static_cast<size_t>(count) * sizeof(T);
            return WriteRawBytes(span.data(), totalBytes);
        }

        template <typename T>
        bool WriteVector(const std::vector<T>& inVec) noexcept
        {
            const uint32_t count = static_cast<uint32_t>(inVec.size());

            if constexpr(IsPODBlittable_V<T>)
            {
                const size_t payloadBytes = static_cast<size_t>(count) * sizeof(T);
                const size_t totalBytes = sizeof(uint32_t) + payloadBytes;

                // Single bounds check for count + entire contiguous vector array
                if(!EnsureCapacity(totalBytes)) [[unlikely]]
                    return false;

                std::memcpy(mCur, &count, sizeof(uint32_t));
                mCur += sizeof(uint32_t);

                if(payloadBytes > 0)
                {
                    std::memcpy(mCur, inVec.data(), payloadBytes);
                    mCur += payloadBytes;
                }
                return true;
            }
            else
            {
                if(!WriteRawBytes(&count, sizeof(uint32_t))) [[unlikely]]
                    return false;

                for(const auto& item : inVec)
                {
                    if(!(*this << item)) [[unlikely]]
                        return false;
                }
                return true;
            }
        }

        template <typename T>
        R0R_INLINE Writer& operator<<(const T& source) noexcept
        {
            if(HasError()) [[unlikely]]
                return *this;

            using RawType = std::remove_cvref_t<T>;

            if constexpr(std::is_same_v<RawType, std::string_view> || std::is_same_v<RawType, std::string>)
                WriteString(source);
            else if constexpr(IsStandardVector_V<RawType>)
                WriteVector(source);
            else if constexpr(IsPODBlittable_V<RawType>)
                WriteRawBytes(&source, sizeof(RawType));
            else if constexpr(HasMemberSerialize<RawType, Writer>)
                source.RAPTOR_SERIALIZE_FUNC(*this);
            else if constexpr(HasFreeSerialize<RawType, Writer>)
                RAPTOR_SERIALIZE_FUNC(*this, source);
            else
                static_assert(sizeof(T) == 0, "Type does not support serialization via Rapt0R::Writer");

            return *this;
        }

        // Variadic POD Fast-Path: 1 capacity check for N variables!
        template <typename... Ts> requires (sizeof...(Ts) > 0) && (IsPODBlittable_V<std::remove_cvref_t<Ts>> && ...)
        R0R_INLINE bool WritePODs(const Ts&... args) noexcept
        {
            if(HasError()) [[unlikely]]
                return false;

            constexpr size_t totalBytes = (sizeof(Ts) + ... + 0);

            if(!EnsureCapacity(totalBytes)) [[unlikely]]
                return false;

            (WriteUnchecked(args), ...);
            return true;
        }
    private:
        R0R_NOINLINE bool Grow(size_t extraBytes) noexcept
        {
            if(!mOwnsMemory) [[unlikely]]
            {
                mStatus = ErrorCode::BUFFER_OVERFLOW;
                return false;
            }

            const size_t currentCap = Capacity();
            const size_t requiredCap = Size() + extraBytes;

            // Geometric 2x growth to amortize allocations
            size_t newCap = currentCap > 0 ? currentCap * 2 : 4096;
            if(newCap < requiredCap)
                newCap = requiredCap;

            return Reserve(newCap);
        }

    private:
        Byte* mStart;
        Byte* mCur;
        Byte* mEnd;
        ErrorCode mStatus;
        bool mOwnsMemory;
    };
}