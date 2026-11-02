// How much does it cost to run an operation in a Python process?
// Usage: p5_plugin_bench [--calls N]
#include "document.hpp"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QTemporaryDir>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace p5;

static double pct(std::vector<double> v, double p) {
    std::sort(v.begin(), v.end());
    return v[std::min(v.size() - 1, static_cast<size_t>(p * v.size()))];
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    int calls = 15;
    for (int i = 1; i + 1 < argc; ++i)
        if (!std::strcmp(argv[i], "--calls")) calls = std::atoi(argv[i + 1]);

    PluginManager pm(P5_HOST_SCRIPT);
    if (!pm.pythonFound()) { std::puts("No Python interpreter found."); return 1; }
    pm.scan(P5_PLUGINS_DIR);
    std::printf("Python: %s\n\n", qPrintable(pm.python()));

    // Input meshes: a box repeated to reach each size (8 vertices per box).
    std::puts("| Input vertices | Result vertices | Call p50 (ms) | p95 (ms) | Save (ms) | File (MB) | Open (ms) |");
    std::puts("| --- | --- | --- | --- | --- | --- | --- |");
    for (std::vector<int> chain : {std::vector<int>{}, {125}, {125, 10}, {125, 10, 10}}) {
        // 8, 1,000, 10,000 and 100,000 vertices
        Document base;
        base.applyNative("box", {{"sx", 10.0}, {"sy", 10.0}, {"sz", 10.0}});
        for (int copies : chain) {
            const Outcome o = base.applyPlugin(pm, "array_tools", "linear_array",
                             {{"count", copies}, {"dx", 11.0}, {"dy", 0.0}, {"dz", 0.0}}, 120000);
            if (!o.ok) { std::printf("setup failed: %s\n", qPrintable(o.message)); return 1; }
        }
        std::vector<double> ms;
        size_t out = 0;
        for (int i = 0; i < calls; ++i) {
            Document d = base;
            const Outcome o = d.applyPlugin(pm, "array_tools", "linear_array",
                                            {{"count", 2}, {"dx", 0.0}, {"dy", 11.0}, {"dz", 0.0}}, 120000);
            if (!o.ok) { std::printf("call failed: %s\n", qPrintable(o.message)); return 1; }
            ms.push_back(o.ms);
            out = d.current().vertexCount();
        }
        Document d = base;
        d.applyPlugin(pm, "array_tools", "linear_array",
                      {{"count", 2}, {"dx", 0.0}, {"dy", 11.0}, {"dz", 0.0}}, 120000);
        QTemporaryDir tmp;
        const QString path = tmp.filePath("m.json");
        QElapsedTimer t;
        t.start();
        d.save(path);
        const double saveMs = t.nsecsElapsed() / 1e6;
        const double mb = QFileInfo(path).size() / 1048576.0;
        t.restart();
        Document r;
        Document::open(path, r);
        const double openMs = t.nsecsElapsed() / 1e6;
        std::printf("| %zu | %zu | %.0f | %.0f | %.0f | %.1f | %.0f |\n", base.current().vertexCount(),
                    out, pct(ms, 0.5), pct(ms, 0.95), saveMs, mb, openMs);
    }
    return 0;
}
