#include "geometry_io.hpp"

#include <QFile>
#include <cstring>

namespace p5 {

bool writeBinary(const QString& path, const Geometry& g, QString& error) {
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) { error = "cannot write " + path; return false; }
    char head[16] = {'M', 'S', 'H', '1'};
    const uint32_t n = static_cast<uint32_t>(g.vertexCount()), m = static_cast<uint32_t>(g.triangleCount());
    std::memcpy(head + 4, &n, 4);
    std::memcpy(head + 8, &m, 4);
    bool ok = f.write(head, 16) == 16;
    ok = ok && f.write(reinterpret_cast<const char*>(g.v.data()), qint64(g.v.size() * 8)) == qint64(g.v.size() * 8);
    ok = ok && f.write(reinterpret_cast<const char*>(g.t.data()), qint64(g.t.size() * 4)) == qint64(g.t.size() * 4);
    if (!ok) error = "writing " + path + " failed";
    return ok;
}

bool readBinary(const QString& path, Geometry& out, QString& error) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) { error = "plugin wrote no result file"; return false; }
    char head[16];
    if (f.read(head, 16) != 16 || std::memcmp(head, "MSH1", 4) != 0) {
        error = "result file has no valid header";
        return false;
    }
    uint32_t n, m;
    std::memcpy(&n, head + 4, 4);
    std::memcpy(&m, head + 8, 4);
    const qint64 expect = 16 + qint64(n) * 24 + qint64(m) * 12;
    if (f.size() != expect) {
        error = QString("result file is %1 bytes but its header says %2").arg(f.size()).arg(expect);
        return false;
    }
    Geometry g;
    g.v.resize(size_t(n) * 3);
    g.t.resize(size_t(m) * 3);
    if (f.read(reinterpret_cast<char*>(g.v.data()), qint64(g.v.size() * 8)) != qint64(g.v.size() * 8) ||
        f.read(reinterpret_cast<char*>(g.t.data()), qint64(g.t.size() * 4)) != qint64(g.t.size() * 4)) {
        error = "result file is truncated";
        return false;
    }
    error = validate(g);
    if (!error.isEmpty()) { error = "plugin result rejected: " + error; return false; }
    out = std::move(g);
    return true;
}

}  // namespace p5
