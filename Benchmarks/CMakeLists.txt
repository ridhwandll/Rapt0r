# Only built with -DRAPTOR_BUILD_COMPETITOR_BENCHMARKS=ON, since it downloads the competing libraries at configure time.

include(FetchContent)

# All competitors are header-only
macro(rapt0r_fetch_headers name url)
    FetchContent_Declare(${name}
        URL ${url}
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        SOURCE_SUBDIR _rapt0r_headers_only
    )
    FetchContent_MakeAvailable(${name})
endmacro()

rapt0r_fetch_headers(zpp_bits https://github.com/eyalz800/zpp_bits/archive/refs/tags/v4.7.6.tar.gz)
rapt0r_fetch_headers(bitsery  https://github.com/fraillt/bitsery/archive/refs/tags/v5.2.5.tar.gz)
rapt0r_fetch_headers(alpaca   https://github.com/p-ranav/alpaca/archive/refs/tags/v0.3.0.tar.gz)
rapt0r_fetch_headers(cereal   https://github.com/USCiLab/cereal/archive/refs/tags/v1.3.2.tar.gz)
rapt0r_fetch_headers(glaze    https://github.com/stephenberry/glaze/archive/refs/tags/v9.0.0.tar.gz)

set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_GTEST_TESTS OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
set(BENCHMARK_INSTALL_DOCS OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_WERROR OFF CACHE BOOL "" FORCE)
FetchContent_Declare(googlebenchmark
    URL https://github.com/google/benchmark/archive/refs/tags/v1.9.5.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(googlebenchmark)

add_executable(RaptorCompetitorBench CompetitorBench.cpp)
target_link_libraries(RaptorCompetitorBench PRIVATE Rapt0R benchmark::benchmark)
target_include_directories(RaptorCompetitorBench SYSTEM PRIVATE
    ${zpp_bits_SOURCE_DIR}
    ${bitsery_SOURCE_DIR}/include
    ${alpaca_SOURCE_DIR}/include
    ${cereal_SOURCE_DIR}/include
    ${glaze_SOURCE_DIR}/include
)

# glaze requires C++23
target_compile_features(RaptorCompetitorBench PRIVATE cxx_std_23)
if(MSVC)
    target_compile_options(RaptorCompetitorBench PRIVATE /bigobj /utf-8 /permissive- /Zc:preprocessor /Zc:__cplusplus)
endif()
