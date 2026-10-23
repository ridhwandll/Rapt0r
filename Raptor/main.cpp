#define RAPTOR_IMPLEMENTATION 1
#include <Raptor/Raptor.hpp>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

static int gTestsPassed = 0;
static int gTestsFailed = 0;

#define R0R_EXPECT(cond)                                                                         \
    do                                                                                           \
    {                                                                                            \
        if(!(cond))                                                                              \
        {                                                                                        \
            std::cerr << "  [FAIL] Line " << __LINE__ << ": Expected (" << #cond << ")\n";       \
            ++gTestsFailed;                                                                      \
            return false;                                                                        \
        }                                                                                        \
    } while(0)

#define R0R_RUN_TEST(testFunc)                                                                   \
    do                                                                                           \
    {                                                                                            \
        std::cout << "[RUN ] " << #testFunc << "...\n";                                          \
        if(testFunc())                                                                           \
        {                                                                                        \
            std::cout << "[PASS] " << #testFunc << "\n";                                         \
            ++gTestsPassed;                                                                      \
        }                                                                                        \
    } while(0)


// 1. POD Blittable Types
struct Vec3
{
    float x, y, z;

    constexpr bool operator==(const Vec3& other) const noexcept
    {
        return x == other.x && y == other.y && z == other.z;
    }
};

struct alignas(16) Mat4
{
    float m[16];

    bool operator==(const Mat4& other) const noexcept
    {
        return std::memcmp(m, other.m, sizeof(m)) == 0;
    }
};

// 2. Third-Party External Type (Path: HasFreeSerialize / HasFreeDeserialize)
struct ExternalAABB
{
    Vec3 min;
    Vec3 max;
};

namespace Rapt0R
{
    inline void Rapt0RSerialize(Writer& writer, const ExternalAABB& box) noexcept
    {
        writer << box.min << box.max;
    }

    inline void Rapt0RDeserialize(Reader& reader, ExternalAABB& box) noexcept
    {
        reader >> box.min >> box.max;
    }
}

// 3. Complex Game Object (Path: HasMemberSerialize / HasMemberDeserialize)
struct SubMesh
{
    uint32_t materialId = 0;
    std::string name;
    std::vector<Vec3> vertices;

    void Rapt0RSerialize(Rapt0R::Writer& w) const noexcept
    {
        w << materialId << name << vertices;
    }

    void Rapt0RDeserialize(Rapt0R::Reader& r)
    {
        r >> materialId >> name >> vertices;
    }

    bool operator==(const SubMesh& other) const noexcept
    {
        return materialId == other.materialId && name == other.name && vertices == other.vertices;
    }
};

struct SceneNode
{
    uint64_t uuid = 0;
    std::string_view debugTag;      // Zero-copy view
    Mat4 worldTransform {};          // POD struct
    ExternalAABB bounds {};          // Free-function serialized struct
    std::vector<SubMesh> subMeshes; // Nested non-POD vector

    void Rapt0RSerialize(Rapt0R::Writer& w) const noexcept
    {
        w << uuid << debugTag << worldTransform << bounds << subMeshes;
    }

    void Rapt0RDeserialize(Rapt0R::Reader& r)
    {
        r >> uuid >> debugTag >> worldTransform >> bounds >> subMeshes;
    }
};

// ============================================================================
// Unit Tests
// ============================================================================

// Test 1: Primitives and Trivially Copyable POD Structs
bool TestPrimitivesAndPOD()
{
    Rapt0R::Writer writer(256);

    const uint8_t u8 = 250;
    const int32_t i32 = -133742;
    const uint64_t u64 = 0xCAFEBABEDEADBEEFULL;
    const Vec3 pos = { 12.5f, -45.0f, 100.25f };

    writer << u8 << i32 << u64 << pos;
    R0R_EXPECT(writer);
    R0R_EXPECT(writer.Size() == sizeof(u8) + sizeof(i32) + sizeof(u64) + sizeof(Vec3));

    Rapt0R::Reader reader(writer.WrittenSpan());

    uint8_t outU8 = 0;
    int32_t outI32 = 0;
    uint64_t outU64 = 0;
    Vec3 outPos = {};

    reader >> outU8 >> outI32 >> outU64 >> outPos;

    R0R_EXPECT(reader);
    R0R_EXPECT(reader.RemainingBytes() == 0);
    R0R_EXPECT(outU8 == u8);
    R0R_EXPECT(outI32 == i32);
    R0R_EXPECT(outU64 == u64);
    R0R_EXPECT(outPos == pos);

    return true;
}

// Test 2: Zero-Copy StringView & Allocating String
bool TestStringsAndZeroCopy()
{
    Rapt0R::Writer writer(256);

    const std::string_view srcView = "SurgeEngine_Asset_Pak_v1";
    const std::string srcStr = "DynamicShaderName_PBR";
    const std::string_view emptyView = "";

    writer << srcView << srcStr << emptyView;
    R0R_EXPECT(writer);

    Rapt0R::Reader reader(writer.WrittenSpan());

    std::string_view outView;
    std::string outStr;
    std::string_view outEmpty;

    reader >> outView >> outStr >> outEmpty;
    R0R_EXPECT(reader);
    R0R_EXPECT(outView == srcView);
    R0R_EXPECT(outStr == srcStr);
    R0R_EXPECT(outEmpty.empty());

    const auto* bufStart = reinterpret_cast<const char*>(writer.Data());
    const auto* bufEnd = bufStart + writer.Size();
    R0R_EXPECT(outView.data() >= bufStart && outView.data() < bufEnd);

    return true;
}

// Test 3: Contiguous POD Vectors & Empty Vector Stale-Data Clearing
bool TestVectorsAndEmptyClear()
{
    Rapt0R::Writer writer(512);

    const std::vector<Vec3> waypoints = {
        { 1.0f, 2.0f, 3.0f },
        { 4.0f, 5.0f, 6.0f },
        { 7.0f, 8.0f, 9.0f }
    };
    const std::vector<int> emptyVec = {};

    writer << waypoints << emptyVec;
    R0R_EXPECT(writer);

    Rapt0R::Reader reader(writer.WrittenSpan());

    std::vector<Vec3> outWaypoints;
    // Pre-populate with stale items to verify ReadVector clears on count == 0
    std::vector<int> staleVec = { 99, 100, 101 };

    reader >> outWaypoints >> staleVec;

    R0R_EXPECT(reader);
    R0R_EXPECT(outWaypoints == waypoints);
    R0R_EXPECT(staleVec.empty());

    return true;
}

// Test 4: Zero-Copy Aligned Span Extraction (SIMD / GPU Ready)
bool TestAlignedZeroCopySpan()
{
    Rapt0R::Writer writer(1024);

    // 3-byte string to intentionally misalign the stream cursor (4 + 3 = 7 bytes)
    writer << std::string_view("Odd");
    R0R_EXPECT(writer.Size() == 7);

    // Write an array of 16-byte aligned Mat4 structs with alignToType = true
    std::vector<Mat4> matrices(4);
    for(int i = 0; i < 4; ++i)
        matrices[i].m[0] = static_cast<float>(i + 1);

    bool spanWritten = writer.WriteSpan(std::span<const Mat4>(matrices), true);
    R0R_EXPECT(spanWritten);

    Rapt0R::Reader reader(writer.WrittenSpan());

    std::string_view prefix;
    reader >> prefix;
    R0R_EXPECT(prefix == "Odd");

    std::span<const Mat4> zeroCopyMatrices;
    bool spanRead = reader.ReadSpan(zeroCopyMatrices, true);
    R0R_EXPECT(spanRead);
    R0R_EXPECT(zeroCopyMatrices.size() == 4);

    // Verify 16-byte alignment offset and zero-copy pointer match
    R0R_EXPECT((reinterpret_cast<uintptr_t>(zeroCopyMatrices.data()) % alignof(Mat4)) == 0);
    for(int i = 0; i < 4; ++i)
        R0R_EXPECT(zeroCopyMatrices[i] == matrices[i]);

    return true;
}

// Test 5: Deep Composite Hierarchy (Member + Free Function + Nested Non-POD Vector)
bool TestCompositeSceneNode()
{
    SceneNode original;
    original.uuid = 0x1122334455667788ULL;
    original.debugTag = "RootBossNode";
    for(int i = 0; i < 16; ++i) original.worldTransform.m[i] = static_cast<float>(i);
    original.bounds = { { -10.f, -10.f, -10.f }, { 10.f, 10.f, 10.f } };

    original.subMeshes.push_back(SubMesh { 101, "ArmorPlate", { {0.f, 0.f, 0.f}, {1.f, 1.f, 1.f} } });
    original.subMeshes.push_back(SubMesh { 202, "EnergyCore", { {2.f, 3.f, 4.f} } });

    Rapt0R::Writer writer(64); // Small initial capacity to test automatic geometric Grow()
    writer << original;
    R0R_EXPECT(writer);

    Rapt0R::Reader reader(writer.WrittenSpan());
    SceneNode loaded;
    reader >> loaded;

    R0R_EXPECT(reader);
    R0R_EXPECT(reader.RemainingBytes() == 0);
    R0R_EXPECT(loaded.uuid == original.uuid);
    R0R_EXPECT(loaded.debugTag == original.debugTag);
    R0R_EXPECT(loaded.worldTransform == original.worldTransform);
    R0R_EXPECT(loaded.bounds.min == original.bounds.min);
    R0R_EXPECT(loaded.bounds.max == original.bounds.max);
    R0R_EXPECT(loaded.subMeshes == original.subMeshes);

    return true;
}

// Test 6: Unchecked Fast-Path (Batch EnsureCapacity / EnsureRemaining)
bool TestUncheckedFastPath()
{
    Rapt0R::Byte stackBuf[64] = {};
    Rapt0R::Writer writer(stackBuf, sizeof(stackBuf));

    constexpr size_t batchSize = sizeof(uint32_t) + sizeof(float) + sizeof(uint64_t);
    R0R_EXPECT(writer.EnsureCapacity(batchSize));

    writer.WriteUnchecked<uint32_t>(777);
    writer.WriteUnchecked<float>(3.14159f);
    writer.WriteUnchecked<uint64_t>(999999999ULL);

    Rapt0R::Reader reader(writer.WrittenSpan());
    R0R_EXPECT(reader.EnsureRemaining(batchSize));

    const auto a = reader.ReadUnchecked<uint32_t>();
    const auto b = reader.ReadUnchecked<float>();
    const auto c = reader.ReadUnchecked<uint64_t>();

    R0R_EXPECT(a == 777);
    R0R_EXPECT(std::abs(b - 3.14159f) < 1e-5f);
    R0R_EXPECT(c == 999999999ULL);
    R0R_EXPECT(reader.RemainingBytes() == 0);

    return true;
}

// Test 7: Bounds Protection & Fixed-Buffer Overflow Safety
bool TestOverflowProtection()
{
    Rapt0R::Byte tinyStack[8] = {};
    Rapt0R::Writer fixedWriter(tinyStack, sizeof(tinyStack));

    fixedWriter << uint64_t(12345); // Exactly 8 bytes: should succeed
    R0R_EXPECT(fixedWriter);

    fixedWriter << uint8_t(1);      // 9th byte on non-owning fixed buffer: MUST fail!
    R0R_EXPECT(fixedWriter.HasError());
    R0R_EXPECT(fixedWriter.Status() == Rapt0R::ErrorCode::BUFFER_OVERFLOW);

    // Reader overflow check
    Rapt0R::Reader reader(tinyStack, sizeof(tinyStack));
    uint64_t val = 0;
    uint32_t extra = 0;

    reader >> val;
    R0R_EXPECT(reader);
    R0R_EXPECT(val == 12345);

    reader >> extra; // Past end of buffer: MUST fail safely!
    R0R_EXPECT(reader.HasError());
    R0R_EXPECT(reader.Status() == Rapt0R::ErrorCode::BUFFER_OVERFLOW);

    return true;
}

// ============================================================================
// Throughput Benchmark (GB/s)
// ============================================================================

// Prevents MSVC / Clang / GCC from optimizing away benchmark results in Release mode
template <typename T>
R0R_INLINE void DoNotOptimize(T const& value)
{
#if defined(_MSC_VER)
    _ReadWriteBarrier();
    volatile const char* p = reinterpret_cast<volatile const char*>(&value);
    (void)*p;
#else
    asm volatile("" : : "r,m"(value) : "memory");
#endif
}

struct BenchmarkEntity
{
    uint64_t id = 0;
    float pos[3] = { 1.0f, 2.0f, 3.0f };
    float rot[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
    int32_t health = 100;
    int32_t mana = 50;
    bool isAlive = true;
    std::string_view name;
    std::vector<uint32_t> inventory;

    void Rapt0RSerialize(Rapt0R::Writer& w) const noexcept
    {
        w.WritePODs(id, pos, rot, health, mana, isAlive);
        w << name << inventory;
    }

    void Rapt0RDeserialize(Rapt0R::Reader& r)
    {
        r.ReadPODs(id, pos, rot, health, mana, isAlive);
        r >> name >> inventory;
    }
};

void RunThroughputBenchmark()
{
    std::cout << "\n================================================================\n";
    std::cout << " Rapt0R Comprehensive Benchmark Suite (Release Profile)\n";
    std::cout << "================================================================\n";
    std::cout << std::fixed << std::setprecision(2);

    // ------------------------------------------------------------------------
    // WORKLOAD 1: 23 MB Contiguous Vertex Buffer (RAM Bandwidth Limit)
    // ------------------------------------------------------------------------
    {
        constexpr size_t numVertices = 2'000'000;
        std::vector<Vec3> srcMesh(numVertices, Vec3 { 1.0f, 2.0f, 3.0f });
        const size_t payloadBytes = sizeof(uint32_t) + numVertices * sizeof(Vec3);

        Rapt0R::Writer writer(payloadBytes + 64);
        constexpr int iterations = 100;

        auto t0 = std::chrono::high_resolution_clock::now();
        for(int i = 0; i < iterations; ++i)
        {
            writer.Clear();
            writer << srcMesh;
            DoNotOptimize(writer.Data());
        }
        auto t1 = std::chrono::high_resolution_clock::now();

        std::vector<Vec3> dstMesh;
        dstMesh.reserve(numVertices);
        auto t2 = std::chrono::high_resolution_clock::now();
        for(int i = 0; i < iterations; ++i)
        {
            Rapt0R::Reader reader(writer.WrittenSpan());
            reader >> dstMesh;
            DoNotOptimize(dstMesh.data());
        }
        auto t3 = std::chrono::high_resolution_clock::now();

        std::span<const Vec3> zeroCopySpan;
        constexpr int spanIterations = 1'000'000;
        auto t4 = std::chrono::high_resolution_clock::now();
        for(int i = 0; i < spanIterations; ++i)
        {
            Rapt0R::Reader reader(writer.WrittenSpan());
            reader.ReadSpan(zeroCopySpan);
            DoNotOptimize(zeroCopySpan.data());
        }
        auto t5 = std::chrono::high_resolution_clock::now();

        const double totalGB = (static_cast<double>(payloadBytes) * iterations) / (1024.0 * 1024.0 * 1024.0);
        const double writeSec = std::chrono::duration<double>(t1 - t0).count();
        const double readVecSec = std::chrono::duration<double>(t3 - t2).count();
        const double readSpanNs = std::chrono::duration<double, std::nano>(t5 - t4).count() / spanIterations;

        std::cout << "[1] Bulk Vertex Payload (22.89 MB x " << iterations << " passes)\n";
        std::cout << "    Write Throughput        : " << (totalGB / writeSec) << " GB/s\n";
        std::cout << "    Read (std::vector Copy) : " << (totalGB / readVecSec) << " GB/s\n";
        std::cout << "    Read (Zero-Copy Span)   : " << readSpanNs << " ns per call\n\n";
    }

    // ------------------------------------------------------------------------
    // WORKLOAD 2: 50,000 Complex Game Entities (Branching + Strings + Vectors)
    // ------------------------------------------------------------------------
    {
        constexpr size_t numEntities = 50'000;
        std::vector<BenchmarkEntity> srcScene(numEntities);
        for(size_t i = 0; i < numEntities; ++i)
        {
            srcScene[i].id = i;
            srcScene[i].name = "CyberDragon_Boss_Entity";
            srcScene[i].inventory = { 10, 20, 30, 40, 50, 60, 70, 80 };
        }

        Rapt0R::Writer writer(numEntities * 128);
        constexpr int iterations = 50;

        auto t0 = std::chrono::high_resolution_clock::now();
        for(int i = 0; i < iterations; ++i)
        {
            writer.Clear();
            writer << srcScene;
            DoNotOptimize(writer.Data());
        }
        auto t1 = std::chrono::high_resolution_clock::now();

        // Pre-allocate target scene so we measure pure deserialization + vector reuse
        std::vector<BenchmarkEntity> dstScene(numEntities);
        auto t2 = std::chrono::high_resolution_clock::now();
        for(int i = 0; i < iterations; ++i)
        {
            Rapt0R::Reader reader(writer.WrittenSpan());
            reader >> dstScene;
            DoNotOptimize(dstScene.data());
        }
        auto t3 = std::chrono::high_resolution_clock::now();

        const size_t sceneBytes = writer.Size();
        const double totalGB = (static_cast<double>(sceneBytes) * iterations) / (1024.0 * 1024.0 * 1024.0);
        const double writeSec = std::chrono::duration<double>(t1 - t0).count();
        const double readSec = std::chrono::duration<double>(t3 - t2).count();
        const double writeMsPerScene = (writeSec * 1000.0) / iterations;
        const double readMsPerScene = (readSec * 1000.0) / iterations;

        std::cout << "[2] Complex Scene Graph (50,000 Entities = " << (sceneBytes / (1024.0 * 1024.0)) << " MB)\n";
        std::cout << "    Write Scene Throughput  : " << (totalGB / writeSec) << " GB/s (" << writeMsPerScene << " ms / 50k entities)\n";
        std::cout << "    Read Scene Throughput   : " << (totalGB / readSec) << " GB/s (" << readMsPerScene << " ms / 50k entities)\n";
    }
    std::cout << "================================================================\n";
}
int main()
{
    std::cout << "========================================\n";
    std::cout << " Running Rapt0R Test Suite\n";
    std::cout << "========================================\n";

    R0R_RUN_TEST(TestPrimitivesAndPOD);
    R0R_RUN_TEST(TestStringsAndZeroCopy);
    R0R_RUN_TEST(TestVectorsAndEmptyClear);
    R0R_RUN_TEST(TestAlignedZeroCopySpan);
    R0R_RUN_TEST(TestCompositeSceneNode);
    R0R_RUN_TEST(TestUncheckedFastPath);
    R0R_RUN_TEST(TestOverflowProtection);

    std::cout << "----------------------------------------\n";
    std::cout << "Summary: " << gTestsPassed << " passed, " << gTestsFailed << " failed.\n";

    if(gTestsFailed == 0)
        RunThroughputBenchmark();

    return gTestsFailed == 0 ? 0 : 1;
}