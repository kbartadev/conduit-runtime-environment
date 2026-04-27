#include <benchmark/benchmark.h>
#include "conduit/transport/http.hpp"
#include "conduit/core.hpp"

using namespace cre;
using namespace cre::transport;

static const char* RAW_HTTP =
"GET /price?symbol=AAPL HTTP/1.1\r\n"
"Host: example.com\r\n"
"User-Agent: CRE-Bench\r\n"
"Accept: */*\r\n"
"\r\n";

static void BM_HttpParser_Throughput(benchmark::State& state) {
    const char* data = RAW_HTTP;
    const size_t len = strlen(RAW_HTTP);

    for (auto _ : state) {
        http_request_event evt{};
        bool ok = http_parser::parse(data, len, evt);
        benchmark::DoNotOptimize(ok);
        benchmark::DoNotOptimize(evt);
    }

    state.SetItemsProcessed(state.iterations());
}

BENCHMARK(BM_HttpParser_Throughput);
