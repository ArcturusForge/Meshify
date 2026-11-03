// How much does it cost to run an operation in a Python process, and how much
// of that is moving the mesh? Compares the JSON and binary hand-off.
// Usage: p5_plugin_bench [--calls N] [--big]   (--big adds 5M vertices, binary only)
#include "document.hpp"

#include <QCoreApplication>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace p5;

static double pct(std::vector<double> v, double p) {
    std::sort(v.begin(), v.end());
    return v[std::min(v.size() - 1, static_cast<size_t>(p * v.size()))];
}

static Geometry makeMesh(size_t n) {
    Geometry g;
    g.v.resize(n * 3);
    for (size_t i = 0; i < n; ++i) {
        g.v[3 * i] = double(i % 1000) * 0.37;
        g.v[3 * i + 1] = double(i / 1000) * 0.41;
        g.v[3 * i + 2] = double(i % 7) / 3.0;
    }
    for (size_t i = 0; i + 2 < n; i += 1) { g.t.push_back(uint32_t(i)); g.t.push_back(uint32_t(i + 1)); g.t.push_back(uint32_t(i + 2)); }
    return g;
}

// p50 and p95 wall time of one call, or -1 on failure.
static bool measure(PluginManager& pm, Transport tr, const char* plugin, const char* op,
                    const QJsonObject& params, const Geometry& in, int calls, double& p50, double& p95) {
    pm.setTransport(tr);
    std::vector<double> ms;
    for (int i = 0; i < calls; ++i) {
        const CallResult c = pm.call(plugin, op, params, in, 600000);
        if (!c.ok) { std::printf("call failed: %s\n", qPrintable(c.error)); return false; }
        ms.push_back(c.ms);
    }
    p50 = pct(ms, 0.5);
    p95 = pct(ms, 0.95);
    return true;
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    int calls = 7;
    bool big = false;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--calls") && i + 1 < argc) calls = std::atoi(argv[++i]);
        if (!std::strcmp(argv[i], "--big")) big = true;
    }
    PluginManager pm(P5_HOST_SCRIPT);
    if (!pm.pythonFound()) { std::puts("No Python interpreter found."); return 1; }
    pm.scan(P5_PLUGINS_DIR);
    pm.scan(P5_TEST_PLUGINS_DIR);
    std::printf("Python: %s\n\n", qPrintable(pm.python()));

    // "echo" returns the mesh unchanged, so it measures only the transport.
    std::puts("| Vertices | JSON echo p50 (ms) | p95 | Binary echo p50 (ms) | p95 | Speed-up (p50) |");
    std::puts("| --- | --- | --- | --- | --- | --- |");
    std::vector<size_t> sizes = {8, 1000, 10000, 100000, 1000000};
    if (big) sizes.push_back(5000000);
    for (size_t n : sizes) {
        const Geometry g = makeMesh(n);
        double j50 = 0, j95 = 0, b50 = 0, b95 = 0;
        const bool withJson = n <= 1000000;
        if (withJson && !measure(pm, Transport::Json, "bench", "echo", {}, g, n >= 1000000 ? 3 : calls, j50, j95)) return 1;
        if (!measure(pm, Transport::Binary, "bench", "echo", {}, g, calls, b50, b95)) return 1;
        if (withJson)
            std::printf("| %zu | %.0f | %.0f | %.0f | %.0f | %.0fx |\n", n, j50, j95, b50, b95, j50 / b50);
        else
            std::printf("| %zu | not run | | %.0f | %.0f | |\n", n, b50, b95);
    }

    // A real operation: the example plugin doubles the mesh in pure Python, so most
    // of its time is the plugin's own loop, not the hand-off.
    std::puts("\n| Vertices | array_tools JSON p50 (ms) | array_tools binary p50 (ms) |");
    std::puts("| --- | --- | --- |");
    for (size_t n : {size_t(1000), size_t(10000), size_t(100000)}) {
        const Geometry g = makeMesh(n);
        const QJsonObject p{{"count", 2}, {"dx", 0.0}, {"dy", 11.0}, {"dz", 0.0}};
        double j50, j95, b50, b95;
        if (!measure(pm, Transport::Json, "array_tools", "linear_array", p, g, 3, j50, j95)) return 1;
        if (!measure(pm, Transport::Binary, "array_tools", "linear_array", p, g, 3, b50, b95)) return 1;
        std::printf("| %zu | %.0f | %.0f |\n", n, j50, b50);
    }
    return 0;
}
