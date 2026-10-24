/*
  Rapt0R head-to-head benchmark against zpp_bits, bitsery, alpaca, cereal and glaze (BEVE).

  Every library serializes the same data model, with its own idiomatic glue code, into a reused buffer.
  Each (library, workload) pair is verified by a full round-trip comparison before anything is timed; a library that
  fails is skipped with an error.

  Measurements run on Google Benchmark. Benchmarks are named <Workload>/<Operation>/<Library>:
    Write     : serialize into the adapter's reused buffer
    ReadReuse : deserialize into an object that already holds the previous result (asset hot-reload, network ticks)
    ReadFresh : deserialize into a default-constructed object (first load); construction and destruction are not timed

  Defaults: 10 repetitions, randomly interleaved, median/mean/stddev/cv aggregates. Useful flags:
    --benchmark_filter=Scene10k/Write/        one workload and operation
    --benchmark_out=results.json              JSON with every repetition, for compare.py
    --benchmark_repetitions=20                more samples
  After the run, a summary table compares each library's median with Rapt0R.

  A/B testing a Rapt0R change (Mann-Whitney U test per benchmark, needs numpy + scipy):
    RaptorCompetitorBench --benchmark_filter=/Rapt0R --benchmark_out=before.json
    ... rebuild with the change ...
    RaptorCompetitorBench --benchmark_filter=/Rapt0R --benchmark_out=after.json
    python <build>/_deps/googlebenchmark-src/tools/compare.py benchmarks before.json after.json

  The 10k-entity reads are dominated by heap allocation and hashing and vary 10-30% between repetitions; trust the
  medians across many repetitions (or compare.py), not a single run.
*/

#include "Raptor/Raptor.hpp"

#include <zpp_bits.h>

#include <bitsery/adapter/buffer.h>
#include <bitsery/bitsery.h>
#include <bitsery/ext/std_map.h>
#include <bitsery/ext/std_optional.h>
#include <bitsery/traits/array.h>
#include <bitsery/traits/string.h>
#include <bitsery/traits/vector.h>

#include <alpaca/alpaca.h>

#include <cereal/archives/binary.hpp>
#include <cereal/types/array.hpp>
#include <cereal/types/optional.hpp>
#include <cereal/types/string.hpp>
#include <cereal/types/unordered_map.hpp>
#include <cereal/types/vector.hpp>

#include <glaze/beve.hpp>

#include <benchmark/benchmark.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <istream>
#include <map>
#include <optional>
#include <ostream>
#include <streambuf>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

// Identical data model stamped into one namespace per library, so each library's glue code stays isolated
#define RAPT0R_BENCH_MODEL                                                  \
    struct Vec3                                                             \
    {                                                                       \
        float x, y, z;                                                      \
        bool operator==(const Vec3&) const = default;                       \
    };                                                                      \
                                                                            \
    struct Entity                                                           \
    {                                                                       \
        uint64_t id;                                                        \
        std::string name;                                                   \
        Vec3 position;                                                      \
        std::array<float, 4> rotation;                                      \
        std::optional<double> health;                                       \
        std::vector<uint32_t> assets;                                       \
        std::unordered_map<std::string, uint64_t> attributes;               \
        bool operator==(const Entity&) const = default;                     \
    };                                                                      \
                                                                            \
    struct Scene                                                            \
    {                                                                       \
        std::vector<Entity> entities;                                       \
        bool operator==(const Scene&) const = default;                      \
    };                                                                      \
                                                                            \
    struct Mesh                                                             \
    {                                                                       \
        std::vector<Vec3> vertices;                                         \
        bool operator==(const Mesh&) const = default;                       \
    };

// ============================================================================
// Rapt0R
// ============================================================================
namespace RaptorModel
{
    RAPT0R_BENCH_MODEL

    // One Write/Read call per field: one bounds check per field
    inline void Rapt0RSerialize(Rapt0R::Writer& w, const Entity& e) noexcept
    {
        w.Write(e.id); w.Write(e.name); w.Write(e.position); w.Write(e.rotation); w.Write(e.health); w.Write(e.assets); w.Write(e.attributes);
    }
    inline void Rapt0RDeserialize(Rapt0R::Reader& r, Entity& e)
    {
        r.Read(e.id); r.Read(e.name); r.Read(e.position); r.Read(e.rotation); r.Read(e.health); r.Read(e.assets); r.Read(e.attributes);
    }
    inline void Rapt0RSerialize(Rapt0R::Writer& w, const Scene& s) noexcept { w.Write(s.entities); }
    inline void Rapt0RDeserialize(Rapt0R::Reader& r, Scene& s) { r.Read(s.entities); }
    inline void Rapt0RSerialize(Rapt0R::Writer& w, const Mesh& m) noexcept { w.Write(m.vertices); }
    inline void Rapt0RDeserialize(Rapt0R::Reader& r, Mesh& m) { r.Read(m.vertices); }
}

// Same library, fused Write/Read API: one bounds check per run of flat fields, register-resident cursor
namespace RaptorFusedModel
{
    RAPT0R_BENCH_MODEL

    inline void Rapt0RSerialize(Rapt0R::Writer& w, const Entity& e) noexcept { w.Write(e.id, e.name, e.position, e.rotation, e.health, e.assets, e.attributes); }
    inline void Rapt0RDeserialize(Rapt0R::Reader& r, Entity& e) { r.Read(e.id, e.name, e.position, e.rotation, e.health, e.assets, e.attributes); }
    inline void Rapt0RSerialize(Rapt0R::Writer& w, const Scene& s) noexcept { w.Write(s.entities); }
    inline void Rapt0RDeserialize(Rapt0R::Reader& r, Scene& s) { r.Read(s.entities); }
    inline void Rapt0RSerialize(Rapt0R::Writer& w, const Mesh& m) noexcept { w.Write(m.vertices); }
    inline void Rapt0RDeserialize(Rapt0R::Reader& r, Mesh& m) { r.Read(m.vertices); }
}

// NAME is the display name in the summary table, ID the filter-friendly part of the benchmark name
template <typename SceneT, typename MeshT, const char* Name, const char* Id>
struct RaptorAdapterBase
{
    static constexpr const char* NAME = Name;
    static constexpr const char* ID = Id;
    using Scene = SceneT;
    using Mesh = MeshT;

    Rapt0R::Writer writer;

    template <typename T>
    size_t Write(const T& value)
    {
        writer.Clear();
        writer.Write(value);
        return writer.HasError() ? 0 : writer.Size();
    }

    template <typename T>
    bool Read(T& value)
    {
        Rapt0R::Reader reader(writer.WrittenSpan());
        reader.Read(value);
        return !reader.HasError();
    }
};

inline constexpr char RAPTOR_FUSED_NAME[] = "Rapt0R";
inline constexpr char RAPTOR_FUSED_ID[] = "Rapt0R";
inline constexpr char RAPTOR_PER_FIELD_NAME[] = "Rapt0R per-field";
inline constexpr char RAPTOR_PER_FIELD_ID[] = "Rapt0R_per_field";
using RaptorAdapter = RaptorAdapterBase<RaptorFusedModel::Scene, RaptorFusedModel::Mesh, RAPTOR_FUSED_NAME, RAPTOR_FUSED_ID>;
using RaptorPerFieldAdapter = RaptorAdapterBase<RaptorModel::Scene, RaptorModel::Mesh, RAPTOR_PER_FIELD_NAME, RAPTOR_PER_FIELD_ID>;

// ============================================================================
// zpp_bits (aggregates are reflected automatically)
// ============================================================================
namespace ZppModel
{
    RAPT0R_BENCH_MODEL

    // zpp_bits' automatic member counting fails on MSVC for these structs, so declare the counts explicitly
    auto serialize(const Vec3&) -> zpp::bits::members<3>;
    auto serialize(const Entity&) -> zpp::bits::members<7>;
    auto serialize(const Scene&) -> zpp::bits::members<1>;
    auto serialize(const Mesh&) -> zpp::bits::members<1>;
}

struct ZppAdapter
{
    static constexpr const char* NAME = "zpp_bits";
    static constexpr const char* ID = "zpp_bits";
    using Scene = ZppModel::Scene;
    using Mesh = ZppModel::Mesh;

    std::vector<std::byte> buffer;
    size_t size = 0;

    template <typename T>
    size_t Write(const T& value)
    {
        zpp::bits::out out(buffer);
        if(zpp::bits::failure(out(value)))
            return 0;
        size = out.position();
        return size;
    }

    template <typename T>
    bool Read(T& value)
    {
        zpp::bits::in in(std::span<const std::byte>(buffer.data(), size));
        return !zpp::bits::failure(in(value));
    }
};

// ============================================================================
// bitsery
// ============================================================================
namespace BitseryModel
{
    RAPT0R_BENCH_MODEL

    template <typename S>
    void serialize(S& s, Vec3& v)
    {
        s.value4b(v.x);
        s.value4b(v.y);
        s.value4b(v.z);
    }

    template <typename S>
    void serialize(S& s, Entity& e)
    {
        s.value8b(e.id);
        s.text1b(e.name, 1024);
        s.object(e.position);
        s.container4b(e.rotation);
        s.ext8b(e.health, bitsery::ext::StdOptional {});
        s.container4b(e.assets, 1024);
        s.ext(e.attributes, bitsery::ext::StdMap { 1024 }, [](S& s2, std::string& key, uint64_t& value) {
            s2.text1b(key, 1024);
            s2.value8b(value);
        });
    }

    template <typename S>
    void serialize(S& s, Scene& scene) { s.container(scene.entities, 100'000'000); }

    template <typename S>
    void serialize(S& s, Mesh& mesh) { s.container(mesh.vertices, 100'000'000); }
}

struct BitseryAdapter
{
    static constexpr const char* NAME = "bitsery";
    static constexpr const char* ID = "bitsery";
    using Scene = BitseryModel::Scene;
    using Mesh = BitseryModel::Mesh;
    using Buffer = std::vector<uint8_t>;

    Buffer buffer;
    size_t size = 0;

    template <typename T>
    size_t Write(const T& value)
    {
        size = bitsery::quickSerialization<bitsery::OutputBufferAdapter<Buffer>>(buffer, value);
        return size;
    }

    template <typename T>
    bool Read(T& value)
    {
        auto state = bitsery::quickDeserialization<bitsery::InputBufferAdapter<Buffer>>({ buffer.begin(), size }, value);
        return state.first == bitsery::ReaderError::NoError && state.second;
    }
};

// ============================================================================
// alpaca (aggregates are reflected automatically, fixed-length ints for a fair comparison)
// ============================================================================
namespace AlpacaModel
{
    RAPT0R_BENCH_MODEL
}

struct AlpacaAdapter
{
    static constexpr const char* NAME = "alpaca";
    static constexpr const char* ID = "alpaca";
    static constexpr auto OPTIONS = alpaca::options::fixed_length_encoding;
    using Scene = AlpacaModel::Scene;
    using Mesh = AlpacaModel::Mesh;

    std::vector<uint8_t> buffer;

    template <typename T>
    size_t Write(const T& value)
    {
        buffer.clear(); // alpaca appends to the container
        return alpaca::serialize<OPTIONS>(value, buffer);
    }

    template <typename T>
    bool Read(T& value)
    {
        std::error_code ec;
        size_t index = 0;
        size_t end = buffer.size();
        alpaca::deserialize<OPTIONS>(value, buffer, index, end, ec);
        return !ec;
    }
};

// ============================================================================
// cereal (stream based, the de-facto standard baseline)
// ============================================================================
namespace CerealModel
{
    RAPT0R_BENCH_MODEL

    template <typename A>
    void serialize(A& a, Vec3& v) { a(v.x, v.y, v.z); }

    template <typename A>
    void serialize(A& a, Entity& e) { a(e.id, e.name, e.position, e.rotation, e.health, e.assets, e.attributes); }

    template <typename A>
    void serialize(A& a, Scene& s) { a(s.entities); }

    template <typename A>
    void serialize(A& a, Mesh& m) { a(m.vertices); }
}

struct CerealAdapter
{
    static constexpr const char* NAME = "cereal";
    static constexpr const char* ID = "cereal";
    using Scene = CerealModel::Scene;
    using Mesh = CerealModel::Mesh;

    // Minimal streambufs over a reused vector, so cereal is not penalized by std::stringstream copies
    struct VectorOutBuf : std::streambuf
    {
        explicit VectorOutBuf(std::vector<char>& target) : out(target) {}
        int_type overflow(int_type c) override
        {
            if(c != traits_type::eof())
                out.push_back(static_cast<char>(c));
            return c;
        }
        std::streamsize xsputn(const char* s, std::streamsize n) override
        {
            out.insert(out.end(), s, s + n);
            return n;
        }
        std::vector<char>& out;
    };

    struct MemoryInBuf : std::streambuf
    {
        MemoryInBuf(char* data, size_t size) { setg(data, data, data + size); }
    };

    std::vector<char> buffer;

    template <typename T>
    size_t Write(const T& value)
    {
        buffer.clear();
        VectorOutBuf sb(buffer);
        std::ostream os(&sb);
        {
            cereal::BinaryOutputArchive archive(os);
            archive(value);
        }
        return buffer.size();
    }

    template <typename T>
    bool Read(T& value)
    {
        MemoryInBuf sb(buffer.data(), buffer.size());
        std::istream is(&sb);
        try
        {
            cereal::BinaryInputArchive archive(is);
            archive(value);
            return true;
        }
        catch(...)
        {
            return false;
        }
    }
};

// ============================================================================
// glaze BEVE (aggregates are reflected automatically, default options: structs are written with field names)
// ============================================================================
namespace GlazeModel
{
    RAPT0R_BENCH_MODEL
}

struct GlazeAdapter
{
    static constexpr const char* NAME = "glaze BEVE";
    static constexpr const char* ID = "glaze_beve";
    using Scene = GlazeModel::Scene;
    using Mesh = GlazeModel::Mesh;

    std::string buffer;

    template <typename T>
    size_t Write(const T& value)
    {
        if(glz::write_beve(value, buffer))
            return 0;
        return buffer.size();
    }

    template <typename T>
    bool Read(T& value)
    {
        return !glz::read_beve(value, buffer);
    }
};

// ============================================================================
// Harness (Google Benchmark)
// ============================================================================
namespace
{
    using Clock = std::chrono::steady_clock;

    template <typename Scene>
    Scene MakeScene(size_t count)
    {
        Scene scene;
        scene.entities.resize(count);
        for(size_t i = 0; i < count; ++i)
        {
            auto& e = scene.entities[i];
            e.id = 0x10000000ULL + i;
            e.name = "CyberneticRenderNode_" + std::to_string(i % 1000);
            e.position = { static_cast<float>(i), 2.f, 3.f };
            e.rotation = { 0.f, 0.f, 0.f, 1.f };
            if(i % 2 == 0)
                e.health = 100.0 + static_cast<double>(i);
            e.assets = { 101, 202, 303, 404, 505 };
            e.attributes["ShaderID"] = 0xDEADBEEF;
            e.attributes["MaterialPriority"] = i;
        }
        return scene;
    }

    template <typename Mesh>
    Mesh MakeMesh(size_t count)
    {
        Mesh mesh;
        mesh.vertices.resize(count);
        for(size_t i = 0; i < count; ++i)
            mesh.vertices[i] = { static_cast<float>(i), 56.78f, 90.12f };
        return mesh;
    }

    // Workloads: each names its data type in a library's model and knows how to build it
    struct Scene10k
    {
        static constexpr const char* ID = "Scene10k";
        static constexpr const char* TITLE = "Scene: 10000 entities (strings, optional, vector, unordered_map)";
        template <typename A> using Data = typename A::Scene;
        template <typename A> static Data<A> Make() { return MakeScene<Data<A>>(10'000); }
    };

    struct Scene100
    {
        static constexpr const char* ID = "Scene100";
        static constexpr const char* TITLE = "Scene: 100 entities (strings, optional, vector, unordered_map)";
        template <typename A> using Data = typename A::Scene;
        template <typename A> static Data<A> Make() { return MakeScene<Data<A>>(100); }
    };

    struct Mesh1M
    {
        static constexpr const char* ID = "Mesh1M";
        static constexpr const char* TITLE = "Mesh: 1000000 vertices (bulk POD array)";
        template <typename A> using Data = typename A::Mesh;
        template <typename A> static Data<A> Make() { return MakeMesh<Data<A>>(1'000'000); }
    };

    // Source data is built once per (library, workload) and shared by every benchmark and repetition
    template <typename A, typename W>
    const typename W::template Data<A>& Source()
    {
        static const auto data = W::template Make<A>();
        return data;
    }

    struct Verification
    {
        bool ok = false;
        size_t wireBytes = 0;
    };

    // Google Benchmark only times code, it never checks results. Before anything is timed, each (library, workload) pair
    // is round-tripped: read into a fresh object, then again into that now-filled object, comparing both with the source.
    // A library that fails is skipped with an error instead of reporting the speed of wrong output.
    template <typename A, typename W>
    const Verification& Verified()
    {
        static const Verification result = [] {
            Verification v;
            const auto& source = Source<A, W>();
            A adapter;
            v.wireBytes = adapter.Write(source);
            typename W::template Data<A> target {};
            v.ok = v.wireBytes > 0 && adapter.Read(target) && target == source && adapter.Read(target) && target == source;
            return v;
        }();
        return result;
    }

    template <typename A, typename W>
    bool Prepare(benchmark::State& state)
    {
        const Verification& v = Verified<A, W>();
        if(!v.ok)
        {
            state.SkipWithError("round-trip verification failed");
            return false;
        }
        state.counters["wire_bytes"] = static_cast<double>(v.wireBytes);
        return true;
    }

    template <typename A, typename W>
    void BM_Write(benchmark::State& state)
    {
        if(!Prepare<A, W>(state))
            return;

        const auto& source = Source<A, W>();
        A adapter;
        const size_t bytes = adapter.Write(source); // Grows the adapter's buffer before timing starts
        for(auto _ : state)
        {
            size_t written = adapter.Write(source);
            benchmark::DoNotOptimize(written);
            benchmark::ClobberMemory();
        }
        state.SetBytesProcessed(static_cast<int64_t>(state.iterations()) * static_cast<int64_t>(bytes));
    }

    // Read into an object that already holds the previous result (asset hot-reload, network ticks)
    template <typename A, typename W>
    void BM_ReadReuse(benchmark::State& state)
    {
        if(!Prepare<A, W>(state))
            return;

        A adapter;
        const size_t bytes = adapter.Write(Source<A, W>());
        typename W::template Data<A> target {};
        adapter.Read(target);
        for(auto _ : state)
        {
            bool ok = adapter.Read(target);
            benchmark::DoNotOptimize(ok);
            benchmark::ClobberMemory();
        }
        state.SetBytesProcessed(static_cast<int64_t>(state.iterations()) * static_cast<int64_t>(bytes));
    }

    // Read into a default-constructed object (first load). Timed manually around the Read call only: constructing the
    // target and destroying the previous one stay outside the measurement. PauseTiming/ResumeTiming would do the same
    // but cost hundreds of nanoseconds per call, which is noise on reads of a few microseconds.
    template <typename A, typename W>
    void BM_ReadFresh(benchmark::State& state)
    {
        if(!Prepare<A, W>(state))
            return;

        A adapter;
        const size_t bytes = adapter.Write(Source<A, W>());
        for(auto _ : state)
        {
            typename W::template Data<A> target {};
            const auto t0 = Clock::now();
            bool ok = adapter.Read(target);
            const auto t1 = Clock::now();
            benchmark::DoNotOptimize(ok);
            benchmark::DoNotOptimize(target);
            state.SetIterationTime(std::chrono::duration<double>(t1 - t0).count());
        }
        state.SetBytesProcessed(static_cast<int64_t>(state.iterations()) * static_cast<int64_t>(bytes));
    }

    struct WorkloadInfo
    {
        std::string id;
        std::string title;
        std::vector<std::pair<std::string, std::string>> libraries; // (ID, display name), Rapt0R first
    };

    std::vector<WorkloadInfo>& Workloads()
    {
        static std::vector<WorkloadInfo> workloads;
        return workloads;
    }

    // Benchmarks are named <Workload>/<Operation>/<Library>, e.g. Scene10k/Write/Rapt0R, so they can be filtered with
    // --benchmark_filter=Scene10k/Write/ or --benchmark_filter=/Rapt0R$
    template <typename W, typename... Adapters>
    void RegisterWorkload()
    {
        const auto name = [](const char* operation, const char* library) { return std::string(W::ID) + "/" + operation + "/" + library; };

        (benchmark::RegisterBenchmark(name("Write", Adapters::ID), BM_Write<Adapters, W>)->Unit(benchmark::kMicrosecond), ...);
        (benchmark::RegisterBenchmark(name("ReadReuse", Adapters::ID), BM_ReadReuse<Adapters, W>)->Unit(benchmark::kMicrosecond), ...);
        (benchmark::RegisterBenchmark(name("ReadFresh", Adapters::ID), BM_ReadFresh<Adapters, W>)->Unit(benchmark::kMicrosecond)->UseManualTime(), ...);

        Workloads().push_back({ W::ID, W::TITLE, { { Adapters::ID, Adapters::NAME }... } });
    }

    // Google Benchmark's usual console output, followed by a comparison table: median time per benchmark relative to
    // Rapt0R. The table keeps the format of the previous harness, so existing tooling (the report charts) still parses it.
    class SummaryReporter : public benchmark::ConsoleReporter
    {
    public:
        void ReportRuns(const std::vector<Run>& runs) override
        {
            ConsoleReporter::ReportRuns(runs);
            for(const Run& run : runs)
            {
                Entry& entry = mEntries[run.run_name.function_name];
                if(run.skipped != benchmark::internal::NotSkipped)
                {
                    entry.failed = true;
                    continue;
                }
                // The stddev/cv aggregates report every counter as 0, so keep the largest value seen
                if(const auto it = run.counters.find("wire_bytes"); it != run.counters.end())
                    entry.wireBytes = std::max(entry.wireBytes, static_cast<size_t>(it->second.value));
                if(run.run_type == Run::RT_Aggregate)
                {
                    if(run.aggregate_name == "median")
                        entry.median = run.GetAdjustedRealTime();
                }
                else
                    entry.samples.push_back(run.GetAdjustedRealTime());
            }
        }

        void Finalize() override
        {
            ConsoleReporter::Finalize();
            PrintSummary();
        }

        bool AllPassed() const { return mAllPassed; }

    private:
        struct Entry
        {
            double median = -1.0;            // From the "median" aggregate when repetitions > 1
            std::vector<double> samples;     // Otherwise from the individual runs
            size_t wireBytes = 0;
            bool failed = false;
        };

        double MedianOf(const std::string& name)
        {
            const auto it = mEntries.find(name);
            if(it == mEntries.end() || it->second.failed)
                return -1.0;
            Entry& entry = it->second;
            if(entry.median >= 0.0)
                return entry.median;
            if(entry.samples.empty())
                return -1.0;
            std::sort(entry.samples.begin(), entry.samples.end());
            return entry.samples[entry.samples.size() / 2];
        }

        void PrintSummary()
        {
            std::ostream& out = GetOutputStream();
            char line[256];

            out << "\n";
#if defined(_MSC_VER) && !defined(__clang__)
            out << "Compiler: MSVC " << _MSC_VER << "\n";
#elif defined(__clang__)
            out << "Compiler: Clang " << __clang_major__ << "." << __clang_minor__ << "\n";
#elif defined(__GNUC__)
            out << "Compiler: GCC " << __GNUC__ << "." << __GNUC_MINOR__ << "\n";
#endif
            out << "Times are medians in us, lower is better. 'vs R0R' > 1 means slower than Rapt0R.\n";
#if defined(_MSC_VER) && !defined(__clang__)
            out << "(alpaca skipped for Scene on MSVC: its member counting breaks on std::optional fields)\n";
#endif

            for(const WorkloadInfo& workload : Workloads())
            {
                const auto metric = [&](const std::string& operation, const std::string& library) {
                    return MedianOf(workload.id + "/" + operation + "/" + library);
                };

                // Skip workloads the filter excluded entirely
                bool any = false;
                for(const auto& [id, display] : workload.libraries)
                    any = any || mEntries.contains(workload.id + "/Write/" + id) || mEntries.contains(workload.id + "/ReadReuse/" + id)
                          || mEntries.contains(workload.id + "/ReadFresh/" + id);
                if(!any)
                    continue;

                const std::string& baseline = workload.libraries.front().first;
                const double baseWrite = metric("Write", baseline);
                const double baseReuse = metric("ReadReuse", baseline);
                const double baseFresh = metric("ReadFresh", baseline);
                const auto ratio = [](double value, double base) { return (value > 0.0 && base > 0.0) ? value / base : 0.0; };

                out << "\n" << workload.title << "\n";
                std::snprintf(line, sizeof(line), "  %-16s %11s %12s %8s %14s %8s %14s %8s  %s\n",
                              "Library", "Bytes", "Write us", "vs R0R", "Read reuse us", "vs R0R", "Read fresh us", "vs R0R", "Round-trip");
                out << line;

                for(const auto& [id, display] : workload.libraries)
                {
                    size_t bytes = 0;
                    bool failed = false;
                    bool ran = false;
                    for(const char* operation : { "Write", "ReadReuse", "ReadFresh" })
                    {
                        if(const auto it = mEntries.find(workload.id + "/" + operation + "/" + id); it != mEntries.end())
                        {
                            ran = true;
                            bytes = std::max(bytes, it->second.wireBytes);
                            failed = failed || it->second.failed;
                        }
                    }
                    if(!ran) // Excluded by --benchmark_filter
                        continue;
                    mAllPassed = mAllPassed && !failed;

                    // "value  ratio" columns, or dashes for an operation the filter excluded
                    char cells[3][48];
                    const std::pair<double, double> values[3] = {
                        { metric("Write", id), baseWrite }, { metric("ReadReuse", id), baseReuse }, { metric("ReadFresh", id), baseFresh } };
                    const int widths[3] = { 12, 14, 14 };
                    for(int c = 0; c < 3; ++c)
                    {
                        const auto [value, base] = values[c];
                        const double r = ratio(value, base);
                        if(value <= 0.0)
                            std::snprintf(cells[c], sizeof(cells[c]), "%*s %8s", widths[c], "-", "-");
                        else if(r <= 0.0)
                            std::snprintf(cells[c], sizeof(cells[c]), "%*.2f %8s", widths[c], value, "-");
                        else
                            std::snprintf(cells[c], sizeof(cells[c]), "%*.2f %7.2fx", widths[c], value, r);
                    }

                    std::snprintf(line, sizeof(line), "  %-16s %11zu %s %s %s  %s\n", display.c_str(), bytes,
                                  cells[0], cells[1], cells[2], failed ? "FAILED" : "OK");
                    out << line;
                }
            }

            out << "\n" << (mAllPassed ? "All libraries round-tripped correctly." : "Some libraries FAILED round-trip verification, see table.") << "\n";
        }

        std::map<std::string, Entry> mEntries;
        bool mAllPassed = true;
    };
}

int main(int argc, char** argv)
{
    // Defaults tuned for a fair head-to-head; listed before the user's arguments so the command line overrides them.
    //  - repetitions + median aggregates: a robust central value and a visible spread (stddev, cv)
    //  - random interleaving: repetitions of all benchmarks run in random order, so no library always runs first on a
    //    cold heap or right after the same neighbour (the old harness measured up to 35% bias from run order)
    //  - display (not report) aggregates only: the console stays readable while --benchmark_out JSON keeps every
    //    repetition, which tools/compare.py needs for its significance test
    static char defaults[][64] = {
        "--benchmark_repetitions=10",
        "--benchmark_enable_random_interleaving=true",
        "--benchmark_display_aggregates_only=true",
        "--benchmark_min_time=0.1s",
    };
    std::vector<char*> args { argv[0] };
    for(auto& flag : defaults)
        args.push_back(flag);
    for(int i = 1; i < argc; ++i)
        args.push_back(argv[i]);
    int count = static_cast<int>(args.size());
    args.push_back(nullptr);

    benchmark::Initialize(&count, args.data());
    if(benchmark::ReportUnrecognizedArguments(count, args.data()))
        return 1;

    // alpaca is left out of the Scene workloads on MSVC: its aggregate member counting miscounts structs that contain a
    // std::optional member (reports 1 member for { uint64_t, std::optional<double> }), so Entity cannot compile
#if defined(_MSC_VER) && !defined(__clang__)
    RegisterWorkload<Scene10k, RaptorAdapter, RaptorPerFieldAdapter, ZppAdapter, BitseryAdapter, CerealAdapter, GlazeAdapter>();
    RegisterWorkload<Scene100, RaptorAdapter, RaptorPerFieldAdapter, ZppAdapter, BitseryAdapter, CerealAdapter, GlazeAdapter>();
#else
    RegisterWorkload<Scene10k, RaptorAdapter, RaptorPerFieldAdapter, ZppAdapter, BitseryAdapter, AlpacaAdapter, CerealAdapter, GlazeAdapter>();
    RegisterWorkload<Scene100, RaptorAdapter, RaptorPerFieldAdapter, ZppAdapter, BitseryAdapter, AlpacaAdapter, CerealAdapter, GlazeAdapter>();
#endif
    RegisterWorkload<Mesh1M, RaptorAdapter, ZppAdapter, BitseryAdapter, AlpacaAdapter, CerealAdapter, GlazeAdapter>();

    SummaryReporter reporter;
    benchmark::RunSpecifiedBenchmarks(&reporter);
    benchmark::Shutdown();
    return reporter.AllPassed() ? 0 : 1;
}
