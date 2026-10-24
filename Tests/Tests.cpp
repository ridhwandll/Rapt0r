// Rapt0R regression + round-trip tests. No framework, exits non-zero on any failure.

#include "Raptor/Raptor.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory_resource>
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <variant>
#include <vector>

static int gFailures = 0;

// Variadic so expressions containing braced initializers with commas need no extra parentheses
#define CHECK(...)                                                                \
    do                                                                            \
    {                                                                             \
        if(!(__VA_ARGS__))                                                        \
        {                                                                         \
            std::printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #__VA_ARGS__); \
            ++gFailures;                                                          \
        }                                                                         \
    } while(0)

using Rapt0R::ErrorCode;
using Rapt0R::Reader;
using Rapt0R::Writer;

namespace
{
    // Writes `value`, reads it back into `out`, returns whether both sides succeeded and consumed the same bytes
    template <typename T>
    bool RoundTrip(const T& value, T& out)
    {
        Writer w;
        w.Write(value);
        if(w.HasError())
            return false;

        Reader r(w.WrittenSpan());
        r.Read(out);
        return !r.HasError() && r.RemainingBytes() == 0;
    }

    template <typename T>
    bool RoundTripEquals(const T& value)
    {
        T out {};
        return RoundTrip(value, out) && out == value;
    }

    struct Vec3
    {
        float x, y, z;
        bool operator==(const Vec3&) const = default;
    };

    // Trivially copyable, so before the fix its custom serializer was ignored and the string_view pointer was blitted
    struct NamedId
    {
        uint32_t id = 0;
        std::string_view name;

        void Rapt0RSerialize(Writer& w) const { w.Write(id, name); }
        void Rapt0RDeserialize(Reader& r) { r.Read(id, name); }
        bool operator==(const NamedId&) const = default;
    };

    struct Heavy
    {
        std::string s;
        void Rapt0RSerialize(Writer& w) const { w.Write(s); }
        void Rapt0RDeserialize(Reader& r) { r.Read(s); }
        bool operator==(const Heavy&) const = default;
    };

    struct Entity
    {
        uint64_t id = 0;
        std::string name;
        Vec3 position {};
        std::optional<double> health;
        std::vector<uint32_t> assets;
        std::unordered_map<std::string, uint64_t> attributes;
        std::variant<int32_t, std::string, Vec3> payload;

        // One Write/Read call per field, FusedEntity checks that fusing them changes nothing on the wire
        void Rapt0RSerialize(Writer& w) const
        {
            w.Write(id); w.Write(name); w.Write(position); w.Write(health); w.Write(assets); w.Write(attributes); w.Write(payload);
        }
        void Rapt0RDeserialize(Reader& r)
        {
            r.Read(id); r.Read(name); r.Read(position); r.Read(health); r.Read(assets); r.Read(attributes); r.Read(payload);
        }
        bool operator==(const Entity&) const = default;
    };

    // Same fields as Entity, serialized with the fused Write/Read API. Must produce identical bytes.
    struct FusedEntity
    {
        uint64_t id = 0;
        std::string name;
        Vec3 position {};
        std::optional<double> health;
        std::vector<uint32_t> assets;
        std::unordered_map<std::string, uint64_t> attributes;
        std::variant<int32_t, std::string, Vec3> payload;

        void Rapt0RSerialize(Writer& w) const { w.Write(id, name, position, health, assets, attributes, payload); }
        void Rapt0RDeserialize(Reader& r) { r.Read(id, name, position, health, assets, attributes, payload); }
        bool operator==(const FusedEntity&) const = default;
    };

    // Trivially copyable, but with a custom (padding-free) format: containers must use it instead of blitting
    struct Packed
    {
        uint32_t a = 0;
        uint8_t b = 0; // 3 padding bytes follow in memory

        void Rapt0RSerialize(Writer& w) const { w.Write(a, b); }
        void Rapt0RDeserialize(Reader& r) { r.Read(a, b); }
        bool operator==(const Packed&) const = default;
    };

    Entity MakeEntity()
    {
        Entity e;
        e.id = 42;
        e.name = "RenderNode";
        e.position = { 1.f, 2.f, 3.f };
        e.health = 100.0;
        e.assets = { 1, 2, 3 };
        e.attributes = { { "ShaderID", 0xDEADBEEF }, { "Priority", 7 } };
        e.payload = std::string("variant string");
        return e;
    }

    FusedEntity MakeFusedEntity()
    {
        const Entity e = MakeEntity();
        return { e.id, e.name, e.position, e.health, e.assets, e.attributes, e.payload };
    }

    template <typename... Ts>
    bool FusedBytesMatchSeparate(const Ts&... values)
    {
        Writer separate;
        (separate.Write(values), ...);
        Writer fused;
        fused.Write(values...);
        const auto a = separate.WrittenSpan();
        const auto b = fused.WrittenSpan();
        return !separate.HasError() && !fused.HasError() && std::equal(a.begin(), a.end(), b.begin(), b.end());
    }

    void TestPrimitivesAndStrings()
    {
        CHECK(RoundTripEquals<int32_t>(-12345));
        CHECK(RoundTripEquals<uint64_t>(0xFFFFFFFFFFFFFFFFull));
        CHECK(RoundTripEquals<double>(3.141592653589793));
        CHECK(RoundTripEquals<bool>(true));
        CHECK(RoundTripEquals<bool>(false));
        CHECK(RoundTripEquals(Vec3 { 1.f, -2.f, 3.5f }));
        CHECK(RoundTripEquals(std::string("hello world")));
        CHECK(RoundTripEquals(std::string()));

        // string_view reads point into the source buffer, so keep the writer alive
        Writer w;
        w.Write(std::string_view("zero copy"));
        Reader r(w.WrittenSpan());
        std::string_view view;
        r.Read(view);
        CHECK(!r.HasError() && view == "zero copy");
    }

    void TestContainers()
    {
        CHECK(RoundTripEquals(std::vector<float> { 1.f, 2.f, 3.f }));
        CHECK(RoundTripEquals(std::vector<float> {}));
        CHECK(RoundTripEquals(std::vector<std::string> { "a", "", "ccc" }));
        CHECK(RoundTripEquals(std::vector<bool> { true, false, true, true }));
        CHECK(RoundTripEquals(std::vector<Heavy> { { "x" }, { "yy" } }));
        CHECK(RoundTripEquals(std::array<float, 4> { 1.f, 2.f, 3.f, 4.f }));
        CHECK(RoundTripEquals(std::array<std::string, 2> { "left", "right" }));
        CHECK(RoundTripEquals(std::pair<int32_t, double> { -1, 2.5 }));
        CHECK(RoundTripEquals(std::pair<std::string, int32_t> { "key", 9 }));
        CHECK(RoundTripEquals(std::tuple<char, uint16_t, float> { 'R', uint16_t(1024), 3.14f }));
        CHECK(RoundTripEquals(std::tuple<std::string, int32_t> { "t", 1 }));
        CHECK(RoundTripEquals(std::optional<double> { 1.5 }));
        CHECK(RoundTripEquals(std::optional<double> {}));
        CHECK(RoundTripEquals(std::optional<std::string> { "opt" }));
        CHECK(RoundTripEquals(std::unordered_map<std::string, uint64_t> { { "a", 1 }, { "b", 2 } }));
        CHECK(RoundTripEquals(std::unordered_map<uint32_t, float> { { 1, 1.f }, { 2, 2.f } }));
        CHECK(RoundTripEquals(std::map<std::string, int32_t> { { "x", 1 }, { "y", 2 } }));
        CHECK(RoundTripEquals(std::unordered_set<std::string> { "p", "q" }));
        CHECK(RoundTripEquals(std::set<int32_t> { 5, 1, 3 }));
        CHECK(RoundTripEquals(std::variant<int32_t, std::string, Vec3> { 7 }));
        CHECK(RoundTripEquals(std::variant<int32_t, std::string, Vec3> { std::string("s") }));
        CHECK(RoundTripEquals(std::variant<int32_t, std::string, Vec3> { Vec3 { 1.f, 2.f, 3.f } }));
        CHECK(RoundTripEquals(MakeEntity()));
    }

    void TestCustomSerializerBeatsBlit()
    {
        const NamedId value { 7, "hello" };
        Writer w;
        w.Write(value);
        CHECK(w.Size() == sizeof(uint32_t) + sizeof(uint32_t) + 5);

        Reader r(w.WrittenSpan());
        NamedId out;
        r.Read(out);
        CHECK(!r.HasError() && out == value);
    }

    void TestEmptyAlignedSpanStaysInSync()
    {
        Writer w;
        std::vector<double> empty;
        w.WriteSpan(std::span<const double>(empty), true);
        w.Write(uint32_t(0xAABBCCDD));

        Reader r(w.WrittenSpan());
        std::span<const double> out;
        CHECK(r.ReadSpan(out, true));
        CHECK(out.empty());
        uint32_t tail = 0;
        r.Read(tail);
        CHECK(!r.HasError() && tail == 0xAABBCCDD);
    }

    void TestAlignedSpanRoundTrip()
    {
        const std::vector<double> values { 1.0, 2.0, 3.0 };
        Writer w;
        w.Write(uint8_t(1)); // Force the span payload off its natural alignment
        w.WriteSpan(std::span<const double>(values), true);

        Reader r(w.WrittenSpan());
        uint8_t prefix = 0;
        std::span<const double> out;
        r.Read(prefix);
        CHECK(r.ReadSpan(out, true));
        CHECK(std::equal(out.begin(), out.end(), values.begin(), values.end()));
    }

    void TestMisalignedSpanIsRejected()
    {
        const std::vector<double> values { 1.0, 2.0 };

        // Unaligned stream layout (alignToType = false), data starts at offset 5
        {
            Writer w;
            w.Write(uint8_t(1));
            w.WriteSpan(std::span<const double>(values), false);

            Reader r(w.WrittenSpan());
            uint8_t prefix = 0;
            std::span<const double> out;
            r.Read(prefix);
            CHECK(!r.ReadSpan(out, false));
            CHECK(r.Status() == ErrorCode::MISALIGNED_DATA);
        }

        // Aligned stream layout, but the reader's buffer itself starts at an odd address
        {
            Writer w;
            w.WriteSpan(std::span<const double>(values), true);

            std::vector<Rapt0R::Byte> shifted(w.Size() + 1);
            std::memcpy(shifted.data() + 1, w.Data(), w.Size());

            Reader r(shifted.data() + 1, w.Size());
            std::span<const double> out;
            CHECK(!r.ReadSpan(out, true));
            CHECK(r.Status() == ErrorCode::MISALIGNED_DATA);
        }
    }

    void TestHostileCountsDoNotAllocate()
    {
        const uint32_t hostile = 0x7FFFFFFF;

        {
            Reader r(&hostile, sizeof(hostile));
            std::vector<Heavy> v;
            r.Read(v);
            CHECK(r.Status() == ErrorCode::BUFFER_OVERFLOW && v.empty());
        }
        {
            Reader r(&hostile, sizeof(hostile));
            std::vector<std::string> v;
            r.Read(v);
            CHECK(r.Status() == ErrorCode::BUFFER_OVERFLOW);
        }
        {
            Reader r(&hostile, sizeof(hostile));
            std::unordered_map<std::string, std::string> m;
            r.Read(m);
            CHECK(r.Status() == ErrorCode::BUFFER_OVERFLOW);
        }
        {
            Reader r(&hostile, sizeof(hostile));
            std::unordered_set<std::string> s;
            r.Read(s);
            CHECK(r.Status() == ErrorCode::BUFFER_OVERFLOW);
        }
        {
            Reader r(&hostile, sizeof(hostile));
            std::vector<double> v;
            r.Read(v);
            CHECK(r.Status() == ErrorCode::BUFFER_OVERFLOW);
        }
    }

    void TestCharArraysAndLiterals()
    {
        Writer w;
        w.Write("abc");
        const char* cstr = "pointer";
        w.Write(cstr);
        char fixed[8] = "fixed";
        w.Write(fixed);

        Reader r(w.WrittenSpan());
        std::string a, b;
        char out[8] = {};
        r.Read(a, b, out);
        CHECK(!r.HasError());
        CHECK(a == "abc");
        CHECK(b == "pointer");
        CHECK(std::string_view(out) == "fixed");

        // Destination too small for the stored string
        Writer w2;
        w2.Write("this is too long");
        Reader r2(w2.WrittenSpan());
        char tiny[4] = {};
        r2.Read(tiny);
        CHECK(r2.Status() == ErrorCode::BUFFER_OVERFLOW);
    }

    void TestCorruptValuesRejected()
    {
        {
            const uint8_t badBool = 2;
            Reader r(&badBool, sizeof(badBool));
            bool b = false;
            r.Read(b);
            CHECK(r.Status() == ErrorCode::CORRUPT_DATA);
        }
        {
            const uint8_t badIndex = 9;
            Reader r(&badIndex, sizeof(badIndex));
            std::variant<int32_t, float> v;
            r.Read(v);
            CHECK(r.Status() == ErrorCode::CORRUPT_DATA);
        }
        {
            const uint8_t badFlag = 5;
            Reader r(&badFlag, sizeof(badFlag));
            std::optional<int32_t> o;
            r.Read(o);
            CHECK(r.Status() == ErrorCode::CORRUPT_DATA);
        }
    }

    // Every truncation of a valid stream must fail cleanly (no crash, no exception, error reported)
    void TestTruncatedInputs()
    {
        Writer w;
        w.Write(MakeEntity());
        const auto full = w.WrittenSpan();

        for(size_t len = 0; len < full.size(); ++len)
        {
            Reader r(full.first(len));
            Entity out;
            r.Read(out);
            if(!r.HasError())
            {
                std::printf("  FAIL truncated length %zu decoded without error\n", len);
                ++gFailures;
            }
        }
    }

    struct CountingResource : std::pmr::memory_resource
    {
        size_t allocations = 0;

        void* do_allocate(size_t bytes, size_t alignment) override
        {
            ++allocations;
            return std::pmr::new_delete_resource()->allocate(bytes, alignment);
        }

        void do_deallocate(void* ptr, size_t bytes, size_t alignment) override
        {
            std::pmr::new_delete_resource()->deallocate(ptr, bytes, alignment);
        }

        bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override { return this == &other; }
    };

    // Deserializes `source` into a container that already holds `previous`, exercising node recycling
    template <typename Container>
    bool ReloadEquals(const Container& source, Container previous)
    {
        Writer w;
        w.Write(source);
        Reader r(w.WrittenSpan());
        r.Read(previous);
        return !r.HasError() && r.RemainingBytes() == 0 && previous == source;
    }

    void TestNodeRecyclingReload()
    {
        using StrMap = std::unordered_map<std::string, std::string>;
        const StrMap target { { "alpha", "1" }, { "beta", "2" }, { "gamma", "a value long enough to need a heap allocation" } };
        CHECK(ReloadEquals(target, StrMap {}));                                         // Nothing to recycle
        CHECK(ReloadEquals(target, StrMap { { "x", "y" } }));                           // Fewer nodes than entries
        CHECK(ReloadEquals(target, target));                                            // Same shape
        CHECK(ReloadEquals(StrMap { { "only", "one" } }, target));                      // Surplus nodes released
        CHECK(ReloadEquals(StrMap {}, target));                                         // Reload to empty

        // More than the on-stack node budget, forces the heap path
        std::map<int32_t, std::string> bigOld, bigNew;
        for(int32_t i = 0; i < 100; ++i)
        {
            bigOld[i * 3] = "old";
            bigNew[i * 7 - 50] = std::to_string(i);
        }
        CHECK(ReloadEquals(bigNew, bigOld));
        CHECK(ReloadEquals(std::unordered_map<uint32_t, float> { { 1, 1.f }, { 2, 2.f } }, std::unordered_map<uint32_t, float> { { 9, 9.f } }));
        CHECK(ReloadEquals(std::set<std::string> { "a", "b", "c" }, std::set<std::string> { "z", "y" }));
        CHECK(ReloadEquals(std::unordered_set<int32_t> { 1, 2, 3 }, std::unordered_set<int32_t> { 4, 5, 6, 7 }));

        // Recycled nodes must actually be reused: reloading a same-shape container must not allocate at all.
        // Values share one length, so whichever recycled node an entry lands in already has enough string capacity.
        CountingResource counter;
        const std::string longValue(48, 'v');
        {
            std::pmr::unordered_map<std::pmr::string, std::pmr::string> source(&counter), reload(&counter);
            for(const char* key : { "k0", "k1", "k2", "k3", "k4" })
                source.emplace(key, longValue);
            reload = source;

            Writer w;
            w.Write(source);
            Reader r(w.WrittenSpan());
            const size_t before = counter.allocations;
            r.Read(reload);
            CHECK(!r.HasError() && reload == source);
            CHECK(counter.allocations == before);
        }
        {
            std::pmr::map<std::pmr::string, std::pmr::string> source(&counter), reload(&counter);
            for(const char* key : { "k0", "k1", "k2", "k3", "k4" })
                source.emplace(key, longValue);
            reload = source;

            Writer w;
            w.Write(source);
            Reader r(w.WrittenSpan());
            const size_t before = counter.allocations;
            r.Read(reload);
            CHECK(!r.HasError() && reload == source);
            CHECK(counter.allocations == before);
        }
    }

    void TestAllocatorAwareTypes()
    {
        std::pmr::monotonic_buffer_resource arena;

        std::pmr::string str("an arena backed string that is longer than SSO", &arena);
        Writer w;
        w.Write(str);
        Reader r(w.WrittenSpan());
        std::pmr::string strOut(&arena);
        r.Read(strOut);
        CHECK(!r.HasError() && strOut == str);

        std::pmr::vector<std::pmr::string> vec(&arena);
        vec.emplace_back("first");
        vec.emplace_back("second");
        Writer w2;
        w2.Write(vec);
        Reader r2(w2.WrittenSpan());
        std::pmr::vector<std::pmr::string> vecOut(&arena);
        r2.Read(vecOut);
        CHECK(!r2.HasError() && vecOut == vec);

        std::pmr::unordered_map<std::pmr::string, uint64_t> map(&arena);
        map.emplace("key", 1);
        Writer w3;
        w3.Write(map);
        Reader r3(w3.WrittenSpan());
        std::pmr::unordered_map<std::pmr::string, uint64_t> mapOut(&arena);
        r3.Read(mapOut);
        CHECK(!r3.HasError() && mapOut == map);

        // Default-init allocator: same wire format as std::vector, no zero-fill on read
        std::vector<Vec3> plain { { 1.f, 2.f, 3.f }, { 4.f, 5.f, 6.f } };
        Writer w4;
        w4.Write(plain);
        Reader r4(w4.WrittenSpan());
        std::vector<Vec3, Rapt0R::DefaultInitAllocator<Vec3>> fast;
        r4.Read(fast);
        CHECK(!r4.HasError() && std::equal(fast.begin(), fast.end(), plain.begin(), plain.end()));
    }

    void TestFusedMatchesSeparate()
    {
        const std::optional<double> some = 2.5;
        const std::optional<double> none;
        const std::vector<float> floats { 1.f, 2.f };
        const std::vector<std::string> strings { "a", "bb" };
        const std::unordered_map<std::string, uint64_t> map { { "k", 1 } };

        CHECK(FusedBytesMatchSeparate(uint32_t(1), uint8_t(2), 3.0));
        CHECK(FusedBytesMatchSeparate(std::string("abc"), std::string_view("de"), std::string()));
        CHECK(FusedBytesMatchSeparate(floats, std::vector<float> {}, Vec3 { 1.f, 2.f, 3.f }));
        CHECK(FusedBytesMatchSeparate(some, none, true, false));
        CHECK(FusedBytesMatchSeparate(strings, map, uint16_t(7)));         // Non-flat values in the middle
        CHECK(FusedBytesMatchSeparate(map));                               // Only non-flat values
        CHECK(FusedBytesMatchSeparate(Packed { 1, 2 }, std::array<float, 3> { 1.f, 2.f, 3.f }));
        CHECK(FusedBytesMatchSeparate(MakeEntity()));

        // Containers larger than SMALL_CONTAINER_LIMIT take the element-by-element fallback, same bytes either way
        std::vector<std::string> manyStrings;
        std::unordered_map<std::string, uint64_t> bigMap;
        std::set<int32_t> bigSet;
        for(int32_t i = 0; i < 200; ++i)
        {
            manyStrings.push_back("s" + std::to_string(i));
            bigMap.emplace("k" + std::to_string(i), static_cast<uint64_t>(i));
            bigSet.insert(i * 3);
        }
        CHECK(FusedBytesMatchSeparate(uint8_t(1), manyStrings, bigMap, bigSet, uint8_t(2)));
        CHECK(FusedBytesMatchSeparate(uint8_t(1), strings, map, std::set<int32_t> { 1, 2 }, uint8_t(2)));
        CHECK(RoundTripEquals(manyStrings));
        CHECK(RoundTripEquals(bigMap));

        // Nested flat containers
        const std::map<std::string, std::vector<std::string>> nested { { "a", { "x", "y" } }, { "b", {} } };
        CHECK(FusedBytesMatchSeparate(nested, std::vector<std::vector<float>> { { 1.f }, { 2.f, 3.f } }));
        CHECK(RoundTripEquals(nested));

        // Fused and per-field entity serializers must agree byte for byte
        Writer separate;
        separate.Write(MakeEntity());
        Writer fused;
        fused.Write(MakeFusedEntity());
        const auto a = separate.WrittenSpan();
        const auto b = fused.WrittenSpan();
        CHECK(std::equal(a.begin(), a.end(), b.begin(), b.end()));

        CHECK(RoundTripEquals(MakeFusedEntity()));

        // Fused read of every flat kind
        Writer w;
        w.Write(uint32_t(9), std::string("str"), floats, some, none, true, strings, Vec3 { 4.f, 5.f, 6.f });
        Reader r(w.WrittenSpan());
        uint32_t u = 0;
        std::string s;
        std::vector<float> f;
        std::optional<double> o1, o2 = 1.0;
        bool flag = false;
        std::vector<std::string> strs;
        Vec3 v {};
        CHECK(r.Read(u, s, f, o1, o2, flag, strs, v));
        CHECK(u == 9 && s == "str" && f == floats && o1 == some && !o2 && flag && strs == strings && v == Vec3 { 4.f, 5.f, 6.f });
        CHECK(r.RemainingBytes() == 0);
    }

    void TestCustomSerializersInsideContainers()
    {
        // Packed is 8 bytes in memory but 5 on the wire, so any blit would show up in the size
        const std::vector<Packed> vec { { 1, 2 }, { 3, 4 }, { 5, 6 } };
        Writer w;
        w.Write(vec);
        CHECK(w.Size() == sizeof(uint32_t) + 3 * 5);
        CHECK(RoundTripEquals(vec));

        const std::array<Packed, 2> arr { Packed { 7, 8 }, Packed { 9, 10 } };
        Writer wa;
        wa.Write(arr);
        CHECK(wa.Size() == 2 * 5);
        CHECK(RoundTripEquals(arr));

        CHECK(RoundTripEquals(std::pair<Packed, int32_t> { { 1, 1 }, 2 }));
        CHECK(RoundTripEquals(std::optional<Packed> { Packed { 3, 3 } }));
        CHECK(RoundTripEquals(std::map<int32_t, Packed> { { 1, { 4, 4 } }, { 2, { 5, 5 } } }));
        CHECK(RoundTripEquals(std::tuple<Packed, std::string> { { 6, 6 }, "t" }));
    }

    void TestErrorClosesWindow()
    {
        Rapt0R::Byte buffer[8];
        Writer w(buffer, sizeof(buffer));
        w.Write(uint32_t(1));
        w.Write(uint64_t(2)); // Does not fit
        CHECK(w.Status() == ErrorCode::BUFFER_OVERFLOW);
        const size_t sizeAtError = w.Size();
        w.Write(uint8_t(3));   // Would fit in the remaining 4 bytes, but the writer has failed
        CHECK(!w.Write(uint8_t(4), uint8_t(5)));
        CHECK(w.Size() == sizeAtError);
        CHECK(w.RemainingCapacity() == 0);

        w.Clear();
        CHECK(!w.HasError() && w.RemainingCapacity() == sizeof(buffer));
        w.Write(uint64_t(6));
        CHECK(!w.HasError() && w.Size() == 8);

        const uint8_t bytes[4] = { 1, 2, 3, 4 };
        Reader r(bytes, sizeof(bytes));
        uint64_t big = 0;
        r.Read(big);
        CHECK(r.Status() == ErrorCode::BUFFER_OVERFLOW);
        uint8_t small = 0;
        r.Read(small);        // A byte is available, but the reader has failed
        CHECK(small == 0 && r.RemainingBytes() == 0);
        CHECK(!r.Seek(0)); // Seeking must not reopen a failed reader
    }

    void TestFusedTruncatedInputs()
    {
        Writer w;
        w.Write(MakeFusedEntity());
        const auto full = w.WrittenSpan();

        for(size_t len = 0; len < full.size(); ++len)
        {
            Reader r(full.first(len));
            FusedEntity out;
            r.Read(out);
            if(!r.HasError())
            {
                std::printf("  FAIL fused truncated length %zu decoded without error\n", len);
                ++gFailures;
            }
        }
    }

    void TestWriterLimits()
    {
        // External buffer cannot grow
        Rapt0R::Byte buffer[8];
        Writer w(buffer, sizeof(buffer));
        w.Write(uint64_t(1));
        CHECK(!w.HasError());
        w.Write(uint8_t(1));
        CHECK(w.Status() == ErrorCode::BUFFER_OVERFLOW);

        Writer counts;
        CHECK(counts.ValidateCount(Rapt0R::MAX_ELEMENT_COUNT));
        if constexpr(sizeof(size_t) > sizeof(uint32_t))
        {
            CHECK(!counts.ValidateCount(Rapt0R::MAX_ELEMENT_COUNT + 1));
            CHECK(counts.Status() == ErrorCode::SIZE_OVERFLOW);
        }

        Writer align;
        CHECK(!align.Align(3));
        CHECK(align.Status() == ErrorCode::FAILED);
    }
}

int main()
{
    struct
    {
        const char* name;
        void (*fn)();
    } tests[] = {
        { "PrimitivesAndStrings", TestPrimitivesAndStrings },
        { "Containers", TestContainers },
        { "CustomSerializerBeatsBlit", TestCustomSerializerBeatsBlit },
        { "EmptyAlignedSpanStaysInSync", TestEmptyAlignedSpanStaysInSync },
        { "AlignedSpanRoundTrip", TestAlignedSpanRoundTrip },
        { "MisalignedSpanIsRejected", TestMisalignedSpanIsRejected },
        { "HostileCountsDoNotAllocate", TestHostileCountsDoNotAllocate },
        { "CharArraysAndLiterals", TestCharArraysAndLiterals },
        { "CorruptValuesRejected", TestCorruptValuesRejected },
        { "TruncatedInputs", TestTruncatedInputs },
        { "NodeRecyclingReload", TestNodeRecyclingReload },
        { "AllocatorAwareTypes", TestAllocatorAwareTypes },
        { "FusedMatchesSeparate", TestFusedMatchesSeparate },
        { "CustomSerializersInsideContainers", TestCustomSerializersInsideContainers },
        { "ErrorClosesWindow", TestErrorClosesWindow },
        { "FusedTruncatedInputs", TestFusedTruncatedInputs },
        { "WriterLimits", TestWriterLimits },
    };

    for(const auto& test : tests)
    {
        const int before = gFailures;
        test.fn();
        std::printf("[%s] %s\n", gFailures == before ? "PASS" : "FAIL", test.name);
    }

    std::printf("\n%s (%d failure%s)\n", gFailures == 0 ? "ALL TESTS PASSED" : "TESTS FAILED", gFailures, gFailures == 1 ? "" : "s");
    return gFailures == 0 ? 0 : 1;
}
