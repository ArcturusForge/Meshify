// Plain executable, no test framework (see CLAUDE.md). Needs a Python interpreter.
#include "document.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <cstdio>

using namespace p5;

static int g_failed = 0;
#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);      \
            ++g_failed;                                                        \
        }                                                                      \
    } while (0)

static const QString kHost = P5_HOST_SCRIPT;

static QJsonObject box(double s) { return {{"sx", s}, {"sy", s}, {"sz", s}}; }
static QJsonObject arr(int n) { return {{"count", n}, {"dx", 25.0}, {"dy", 0.0}, {"dz", 0.0}}; }

// A plugin folder set with array_tools (optionally another version) and faulty.
struct Plugins {
    QTemporaryDir dir;
    PluginManager pm;
    explicit Plugins(const char* version = nullptr, bool withFaulty = true) : pm(kHost) {
        const QString root = dir.path();
        copyPlugin(P5_PLUGINS_DIR "/array_tools", root + "/array_tools", version);
        if (withFaulty) copyPlugin(P5_TEST_PLUGINS_DIR "/faulty", root + "/faulty", nullptr);
        copyPlugin(P5_TEST_PLUGINS_DIR "/bench", root + "/bench", nullptr);
        pm.scan(root);
    }
    static void copyPlugin(const QString& from, const QString& to, const char* version) {
        QDir().mkpath(to);
        for (const char* f : {"plugin.py", "plugin.json"}) QFile::copy(from + "/" + f, to + "/" + f);
        if (!version) return;
        QFile m(to + "/plugin.json");
        m.open(QIODevice::ReadWrite);
        QJsonObject o = QJsonDocument::fromJson(m.readAll()).object();
        o["version"] = version;
        m.resize(0);
        m.write(QJsonDocument(o).toJson());
    }
};

static void testUndoRedo() {
    std::puts("undo/redo across native and plugin steps");
    Plugins pl;
    Document d;
    CHECK(d.applyNative("box", box(10)).ok);
    const Geometry g1 = d.current();
    const Outcome o = d.applyPlugin(pl.pm, "array_tools", "linear_array", arr(3));
    CHECK(o.ok);
    CHECK(d.current().vertexCount() == 24 && d.current().triangleCount() == 36);
    CHECK(d.current().v[3 * 8] == 25.0);  // first vertex of the 2nd copy, 25 mm along x
    const Geometry g2 = d.current();
    CHECK(d.applyNative("translate", {{"dx", 1.0}, {"dy", 2.0}, {"dz", 3.0}}).ok);
    CHECK(d.undo());
    CHECK(d.current() == g2);
    CHECK(d.undo());
    CHECK(d.current() == g1);
    CHECK(d.redo() && d.current() == g2);
    // A new step after an undo drops the redo branch.
    CHECK(d.undo());
    CHECK(d.applyNative("translate", {{"dx", 5.0}, {"dy", 0.0}, {"dz", 0.0}}).ok);
    CHECK(!d.redo());
    CHECK(d.stepCount() == 2);
}

static void testFailuresCommitNothing(Transport tr) {
    std::printf("HIS-01: failed plugin operations leave the committed model untouched (%s)\n",
                tr == Transport::Json ? "json" : "binary");
    Plugins pl;
    pl.pm.setTransport(tr);
    Document d;
    d.applyNative("box", box(10));
    d.applyPlugin(pl.pm, "array_tools", "linear_array", arr(2));
    const Geometry before = d.current();
    const size_t steps = d.stepCount();

    struct Case { const char* plugin; const char* op; QJsonObject params; int timeoutMs; const char* expect; };
    const Case cases[] = {
        {"array_tools", "linear_array", {{"count", 0}, {"dx", 1.0}, {"dy", 0.0}, {"dz", 0.0}}, 10000, "ValueError"},
        {"array_tools", "linear_array", {{"count", 2}}, 10000, "KeyError"},
        {"faulty", "raise_midway", {}, 10000, "boom after partly building"},
        {"faulty", "crash", {}, 10000, "died"},
        {"faulty", "hang", {}, 1500, "did not finish"},
        {"faulty", "bad_index", {}, 10000, "outside"},
        {"faulty", "nan_out", {}, 10000, tr == Transport::Json ? "not valid JSON" : "not a finite number"},
        {"faulty", "no_such_op", {}, 10000, "no operation"},
        {"nobody", "x", {}, 10000, "not installed"},
    };
    for (const Case& c : cases) {
        const Outcome o = d.applyPlugin(pl.pm, c.plugin, c.op, c.params, c.timeoutMs);
        std::printf("  %s.%s -> %s\n", c.plugin, c.op, qPrintable(o.message));
        CHECK(!o.ok);
        CHECK(o.message.contains(c.expect));
        CHECK(d.current() == before);
        CHECK(d.stepCount() == steps && d.cursor() == steps);
    }
    // The hang case was stopped by the timeout, not left running.
    // A plugin that prints to stdout still works: its output goes to stderr.
    CHECK(d.applyPlugin(pl.pm, "faulty", "chatty", {}).ok);
    CHECK(d.current() == before);
}

static void testTransports() {
    std::puts("JSON and binary hand-off give identical results");
    Plugins pl;
    Geometry big;  // 3,001 vertices with awkward doubles
    for (int i = 0; i < 3001; ++i) {
        big.v.push_back(i / 3.0);
        big.v.push_back(-i * 0.1);
        big.v.push_back(1e-7 * i);
    }
    for (uint32_t i = 0; i + 2 < 3001; ++i) { big.t.push_back(i); big.t.push_back(i + 1); big.t.push_back(i + 2); }
    for (const char* mode : {"echo", "array"}) {
        Geometry out[2];
        for (int k = 0; k < 2; ++k) {
            pl.pm.setTransport(k ? Transport::Binary : Transport::Json);
            const CallResult c = mode[0] == 'e' ? pl.pm.call("bench", "echo", {}, big)
                                                : pl.pm.call("array_tools", "linear_array", arr(3), big);
            CHECK(c.ok);
            out[k] = c.geometry;
        }
        CHECK(out[0] == out[1]);
        if (mode[0] == 'e') CHECK(out[1] == big);
        else CHECK(out[1].vertexCount() == 3 * big.vertexCount());
    }
    // An empty mesh survives the binary hand-off too.
    pl.pm.setTransport(Transport::Binary);
    CHECK(pl.pm.call("bench", "echo", {}, Geometry{}).ok);
}

static void testSaveReopen() {
    std::puts("save and reopen with the plugin installed");
    Plugins pl;
    QTemporaryDir tmp;
    const QString path = tmp.filePath("model.json");
    Document d;
    d.applyNative("box", box(12.5));
    d.applyPlugin(pl.pm, "array_tools", "linear_array", arr(4));
    d.applyNative("translate", {{"dx", 0.1}, {"dy", -3.0}, {"dz", 1.0 / 3.0}});
    d.undo();  // cursor is behind the end, so redo must survive the round trip
    CHECK(d.save(path).ok);
    CHECK(!QFile::exists(path + ".tmp"));

    Document r;
    const Outcome o = Document::open(path, r);
    CHECK(o.ok);
    CHECK(r.stepCount() == 3 && r.cursor() == 2);
    CHECK(r.current() == d.current());  // bit for bit, including 1/3 mm
    for (const StepStatus& s : r.status(pl.pm)) CHECK(s.kind == StepStatus::Ok);
    const Document::Replay rp = r.replay(pl.pm);
    CHECK(rp.ok);
    CHECK(r.redo());
    CHECK(r.undo() && r.undo());
    CHECK(r.current().vertexCount() == 8);

    // Saving again over an existing file replaces it completely.
    CHECK(d.save(path).ok);
    CHECK(!QFile::exists(path + ".tmp"));
}

static void testPluginMissing() {
    std::puts("EXT-01: reopen with the required plugin absent");
    QTemporaryDir tmp;
    const QString path = tmp.filePath("model.json");
    Geometry expected;
    {
        Plugins pl;
        Document d;
        d.applyNative("box", box(10));
        d.applyPlugin(pl.pm, "array_tools", "linear_array", arr(3));
        d.applyNative("translate", {{"dx", 0.0}, {"dy", 0.0}, {"dz", 5.0}});
        expected = d.current();
        CHECK(d.save(path).ok);
    }
    PluginManager empty(kHost);  // no plugins installed
    Document r;
    CHECK(Document::open(path, r).ok);
    CHECK(r.current() == expected);  // the saved geometry is all there
    const auto st = r.status(empty);
    CHECK(st[0].kind == StepStatus::Ok);
    CHECK(st[1].kind == StepStatus::PluginMissing);
    CHECK(st[1].message.contains("array_tools") && st[1].message.contains("1.0.0"));
    CHECK(st[2].kind == StepStatus::Ok);
    std::printf("  step 2: %s\n", qPrintable(st[1].message));
    // Undo and redo still cross the unavailable step.
    CHECK(r.undo());
    CHECK(r.current().vertexCount() == 24);   // before the translate
    CHECK(r.undo() && r.current().vertexCount() == 8);
    CHECK(r.undo() && r.current().vertexCount() == 0);
    CHECK(r.redo() && r.redo() && r.redo());
    CHECK(r.current() == expected);
    // Native work continues, and replay says exactly where it cannot go on.
    CHECK(r.applyNative("translate", {{"dx", 1.0}, {"dy", 0.0}, {"dz", 0.0}}).ok);
    const Document::Replay rp = r.replay(empty);
    CHECK(!rp.ok && rp.firstDifferent == 1);
    CHECK(rp.message.contains("not installed"));
    // Applying the missing plugin's operation fails cleanly.
    const Geometry before = r.current();
    CHECK(!r.applyPlugin(empty, "array_tools", "linear_array", arr(2)).ok);
    CHECK(r.current() == before);
    // A saved copy keeps the plugin steps and their records.
    CHECK(r.save(tmp.filePath("again.json")).ok);
    Document r2;
    CHECK(Document::open(tmp.filePath("again.json"), r2).ok);
    CHECK(r2.status(empty)[1].kind == StepStatus::PluginMissing);
    CHECK(r2.current() == r.current());
}

static void testVersionDiffers() {
    std::puts("EXT-01: plugin present but a different version");
    QTemporaryDir tmp;
    const QString path = tmp.filePath("model.json");
    {
        Plugins pl;
        Document d;
        d.applyNative("box", box(10));
        d.applyPlugin(pl.pm, "array_tools", "linear_array", arr(2));
        CHECK(d.save(path).ok);
    }
    Plugins v2("2.0.0", false);
    Document r;
    CHECK(Document::open(path, r).ok);
    const auto st = r.status(v2.pm);
    CHECK(st[1].kind == StepStatus::VersionDiffers);
    std::printf("  step 2: %s\n", qPrintable(st[1].message));
    CHECK(r.replay(v2.pm).ok);  // same code, so same result; the flag is the warning
}

static void testDamagedFiles() {
    std::puts("damaged files are refused whole");
    Plugins pl;
    QTemporaryDir tmp;
    const QString path = tmp.filePath("model.json");
    Document d;
    d.applyNative("box", box(10));
    d.applyPlugin(pl.pm, "array_tools", "linear_array", arr(2));
    CHECK(d.save(path).ok);
    QFile f(path);
    f.open(QIODevice::ReadOnly);
    const QByteArray good = f.readAll();
    f.close();

    auto openBytes = [&](const QByteArray& b) {
        QFile w(tmp.filePath("bad.json"));
        w.open(QIODevice::WriteOnly | QIODevice::Truncate);
        w.write(b);
        w.close();
        Document x;
        return Document::open(tmp.filePath("bad.json"), x);
    };
    CHECK(!openBytes(good.left(good.size() / 2)).ok);           // cut off mid-write
    CHECK(!openBytes("").ok);
    QByteArray badIndex = good;
    badIndex.replace("\"triangles\":[0,2,1", "\"triangles\":[0,2,99");
    const Outcome o = openBytes(badIndex);
    CHECK(!o.ok && o.message.contains("bad geometry"));
    std::printf("  %s\n", qPrintable(o.message));
    Document untouched;
    CHECK(!Document::open(tmp.filePath("missing.json"), untouched).ok);
    CHECK(untouched.stepCount() == 0);
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    PluginManager probe(kHost);
    if (!probe.pythonFound()) {
        std::puts("No Python interpreter found. Install Python 3 or set MESHIFY_PYTHON.");
        return 1;
    }
    std::printf("Python: %s\n", qPrintable(probe.python()));
    testUndoRedo();
    testFailuresCommitNothing(Transport::Binary);
    testFailuresCommitNothing(Transport::Json);
    testTransports();
    testSaveReopen();
    testPluginMissing();
    testVersionDiffers();
    testDamagedFiles();
    std::puts(g_failed ? "FAILED" : "all passed");
    return g_failed ? 1 : 0;
}
