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

// Include core headers
#include "Raptor/Raptor.hpp"

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
#include <array>
#include <map>
#include <unordered_map>
#include <optional>
#include <tuple>
#include <utility>
#include <algorithm>

namespace Rapt0R::Benchmark
{
    // Compiler optimization blocker
    template <typename T>
    R0R_INLINE void DoNotOptimize(T const& value) noexcept
    {
#if defined(_MSC_VER)
        _ReadWriteBarrier();
        volatile const char* p = reinterpret_cast<volatile const char*>(&value);
        (void)*p;
#else
        asm volatile("" : : "r,m"(value) : "memory");
#endif
    }

    struct BenchmarkVec3
    {
        float x, y, z;
        constexpr bool operator==(const BenchmarkVec3& other) const noexcept
        {
            return x == other.x && y == other.y && z == other.z;
        }
    };

    // Extreme Torture Complex Entity containing nested STL containers, optional flags, maps, and tuples
    struct StressTestEntity
    {
        uint64_t entityId = 0;
        std::string_view nameTag; // Zero-copy view
        BenchmarkVec3 position { 1.f, 2.f, 3.f };
        std::array<float, 4> rotationQuaternion { 0.f, 0.f, 0.f, 1.f };
        std::optional<double> overrideHealth;
        std::vector<uint32_t> assetIndices;
        std::pair<int32_t, int32_t> gridCoordinates { -10, 42 };
        std::tuple<char, uint16_t, float> metadata { 'R', uint16_t(1024), 3.14f };
        std::unordered_map<std::string, uint64_t> attributes;

        // Fused Write/Read: one bounds check per run of flat fields instead of one per field
        void Rapt0RSerialize(Rapt0R::Writer& w) const noexcept
        {
            w.Write(entityId, nameTag, position, rotationQuaternion, overrideHealth, assetIndices, gridCoordinates, metadata, attributes);
        }

        void Rapt0RDeserialize(Rapt0R::Reader& r)
        {
            r.Read(entityId, nameTag, position, rotationQuaternion, overrideHealth, assetIndices, gridCoordinates, metadata, attributes);
        }

        bool operator==(const StressTestEntity&) const = default;
    };

    // Benchmarks are worthless if the data does not round-trip, so every workload is verified
    inline bool gAllPassed = true;

    inline void Verify(bool condition, const char* what)
    {
        if(!condition)
        {
            std::cout << "    !! VERIFICATION FAILED: " << what << "\n";
            gAllPassed = false;
        }
    }

    struct TestStats
    {
        double minGBs;
        double medianGBs;
        double maxGBs;
    };

    template <typename Func>
    inline TestStats MeasureGBs(size_t bytesPerPass, int passes, Func&& fn)
    {
        std::vector<double> timesSec;
        timesSec.reserve(passes);

        // Warmup
        fn();
        fn();

        for(int i = 0; i < passes; ++i)
        {
            auto t0 = std::chrono::high_resolution_clock::now();
            fn();
            auto t1 = std::chrono::high_resolution_clock::now();
            timesSec.push_back(std::chrono::duration<double>(t1 - t0).count());
        }

        std::sort(timesSec.begin(), timesSec.end());
        const double gb = static_cast<double>(bytesPerPass) / (1024.0 * 1024.0 * 1024.0);

        return {
            gb / timesSec.back(),
            gb / timesSec[passes / 2],
            gb / timesSec.front()
        };
    }

    inline void RunIntenseSuite()
    {
        std::cout << "\n================================================================================\n";
        std::cout << " RAPT0R INTENSE C++20 BINARY ENGINE SUITE & BENCHMARK\n";
        std::cout << "================================================================================\n";
        std::cout << std::fixed << std::setprecision(2);

        // --------------------------------------------------------------------
        // WORKLOAD 1: Bulk POD Vertex Blit Test (32 MB)
        // --------------------------------------------------------------------
        {
            constexpr size_t numVertices = 2'800'000;
            std::vector<BenchmarkVec3> srcMesh(numVertices, { 12.34f, 56.78f, 90.12f });
            const size_t payloadBytes = sizeof(uint32_t) + numVertices * sizeof(BenchmarkVec3);

            Rapt0R::Writer writer(payloadBytes + 128);
            writer.Write(srcMesh);

            std::vector<BenchmarkVec3> dstMesh;
            dstMesh.reserve(numVertices);

            auto writeStats = MeasureGBs(payloadBytes, 50, [&]() {
                writer.Clear();
                writer.Write(srcMesh);
                DoNotOptimize(writer.Data());
                                         });

            bool readOk = true;
            auto readStats = MeasureGBs(payloadBytes, 50, [&]() {
                Rapt0R::Reader reader(writer.WrittenSpan());
                reader.Read(dstMesh);
                readOk = readOk && !reader.HasError();
                DoNotOptimize(dstMesh.data());
                                        });

            // Reading into a fresh vector pays for the allocation + zero-fill that the reuse case above hides
            auto coldReadStats = MeasureGBs(payloadBytes, 20, [&]() {
                std::vector<BenchmarkVec3> fresh;
                Rapt0R::Reader reader(writer.WrittenSpan());
                reader.Read(fresh);
                readOk = readOk && !reader.HasError();
                DoNotOptimize(fresh.data());
                                            });

            // Same fresh read, but the allocator skips the zero-fill so every byte is written once
            using FastMesh = std::vector<BenchmarkVec3, Rapt0R::DefaultInitAllocator<BenchmarkVec3>>;
            bool fastOk = true;
            auto coldFastReadStats = MeasureGBs(payloadBytes, 20, [&]() {
                FastMesh fresh;
                Rapt0R::Reader reader(writer.WrittenSpan());
                reader.Read(fresh);
                fastOk = fastOk && !reader.HasError();
                DoNotOptimize(fresh.data());
                                                });
            {
                FastMesh check;
                Rapt0R::Reader reader(writer.WrittenSpan());
                reader.Read(check);
                fastOk = fastOk && !reader.HasError() && std::equal(check.begin(), check.end(), srcMesh.begin(), srcMesh.end());
            }

            Verify(!writer.HasError(), "mesh writer status");
            Verify(readOk, "mesh reader status");
            Verify(fastOk, "mesh default-init read");
            Verify(dstMesh == srcMesh, "mesh round-trip contents");

            std::cout << "[1] Bulk Vertex Mesh (" << (payloadBytes / (1024.0 * 1024.0)) << " MB):\n";
            std::cout << "    Write Throughput : Median " << std::setw(6) << writeStats.medianGBs << " GB/s (Peak " << writeStats.maxGBs << ")\n";
            std::cout << "    Read (reuse)     : Median " << std::setw(6) << readStats.medianGBs << " GB/s (Peak " << readStats.maxGBs << ")\n";
            std::cout << "    Read (fresh vec) : Median " << std::setw(6) << coldReadStats.medianGBs << " GB/s (Peak " << coldReadStats.maxGBs << ")\n";
            std::cout << "    Read (fresh, DefaultInitAllocator) : Median " << std::setw(6) << coldFastReadStats.medianGBs << " GB/s (Peak " << coldFastReadStats.maxGBs << ")\n\n";
        }

        // --------------------------------------------------------------------
        // WORKLOAD 2: Complex Heterogeneous Scene Graph (10,000 Heavy Entities)
        // Tests std::optional, std::array, std::pair, std::tuple, std::unordered_map
        // --------------------------------------------------------------------
        {
            constexpr size_t entityCount = 10'000;
            std::vector<StressTestEntity> srcScene(entityCount);
            for(size_t i = 0; i < entityCount; ++i)
            {
                auto& e = srcScene[i];
                e.entityId = 0x10000000ULL + i;
                e.nameTag = "CyberneticRenderNode_Alpha_X";
                e.overrideHealth = (i % 2 == 0) ? std::optional<double>(100.0) : std::nullopt;
                e.assetIndices = { 101, 202, 303, 404, 505 };
                e.attributes["ShaderID"] = 0xDEADBEEF;
                e.attributes["MaterialPriority"] = i;
            }

            Rapt0R::Writer writer(entityCount * 256);
            writer.Write(srcScene);

            std::vector<StressTestEntity> dstScene(entityCount);

            auto writeStats = MeasureGBs(writer.Size(), 30, [&]() {
                writer.Clear();
                writer.Write(srcScene);
                DoNotOptimize(writer.Data());
                                         });

            bool readOk = true;
            auto readStats = MeasureGBs(writer.Size(), 30, [&]() {
                Rapt0R::Reader reader(writer.WrittenSpan());
                reader.Read(dstScene);
                readOk = readOk && !reader.HasError();
                DoNotOptimize(dstScene.data());
                                        });

            Verify(!writer.HasError(), "scene writer status");
            Verify(readOk, "scene reader status");
            Verify(dstScene == srcScene, "scene round-trip contents");

            std::cout << "[2] Heterogeneous Scene Graph (" << entityCount << " Complex Entities = " << (writer.Size() / (1024.0 * 1024.0)) << " MB):\n";
            std::cout << "    Write Throughput : Median " << std::setw(6) << writeStats.medianGBs << " GB/s (Peak " << writeStats.maxGBs << ")\n";
            std::cout << "    Read Throughput  : Median " << std::setw(6) << readStats.medianGBs << " GB/s (Peak " << readStats.maxGBs << ")\n\n";
        }

        // --------------------------------------------------------------------
        // WORKLOAD 3: Zero-Copy Asset Span Latency (O(1))
        // Note: this measures a 4-byte header read + pointer arithmetic, not data throughput
        // --------------------------------------------------------------------
        {
            constexpr size_t count = 1'000'000;
            std::vector<BenchmarkVec3> verts(count, { 1.f, 2.f, 3.f });
            Rapt0R::Writer w(count * sizeof(BenchmarkVec3) + 64);
            w.WriteSpan(std::span<const BenchmarkVec3>(verts), true);

            std::span<const BenchmarkVec3> outSpan;
            constexpr int spanPasses = 1'000'000;
            bool readOk = true;

            auto t0 = std::chrono::high_resolution_clock::now();
            for(int i = 0; i < spanPasses; ++i)
            {
                Rapt0R::Reader r(w.WrittenSpan());
                readOk = r.ReadSpan(outSpan, true) && readOk;
                DoNotOptimize(outSpan.data());
            }
            auto t1 = std::chrono::high_resolution_clock::now();

            Verify(readOk, "span reader status");
            Verify(std::equal(outSpan.begin(), outSpan.end(), verts.begin(), verts.end()), "span contents");

            const double nsPerCall = std::chrono::duration<double, std::nano>(t1 - t0).count() / spanPasses;
            std::cout << "[3] Zero-Copy Asset Span Extraction (O(1) lookup):\n";
            std::cout << "    Latency          : " << nsPerCall << " ns per 12 MB mesh view\n";
        }

        std::cout << "================================================================================\n";
        if(gAllPassed)
            std::cout << " ALL WORKLOADS ROUND-TRIPPED AND VERIFIED.\n";
        else
            std::cout << " VERIFICATION FAILED, THROUGHPUT NUMBERS ABOVE ARE NOT VALID.\n";
        std::cout << "================================================================================\n";
    }
}

int main()
{
    Rapt0R::Benchmark::RunIntenseSuite();
    return Rapt0R::Benchmark::gAllPassed ? 0 : 1;
}