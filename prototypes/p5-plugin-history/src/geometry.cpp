#include "geometry.hpp"

#include <QJsonArray>
#include <cmath>

namespace p5 {

QString validate(const Geometry& g) {
    if (g.v.size() % 3 != 0) return "vertex array length is not a multiple of 3";
    if (g.t.size() % 3 != 0) return "triangle array length is not a multiple of 3";
    for (double c : g.v) {
        if (!std::isfinite(c)) return "a vertex coordinate is not a finite number";
        if (std::fabs(c) > 1.0e6) return "a vertex coordinate is beyond 1,000 m";
    }
    const size_t n = g.vertexCount();
    for (uint32_t i : g.t)
        if (i >= n) return QString("triangle index %1 is outside the %2 vertices").arg(i).arg(n);
    return {};
}

QJsonObject toJson(const Geometry& g) {
    QJsonArray v, t;
    for (double c : g.v) v.append(c);
    for (uint32_t i : g.t) t.append(static_cast<qint64>(i));
    return {{"vertices", v}, {"triangles", t}};
}

bool fromJson(const QJsonObject& o, Geometry& out, QString& error) {
    const QJsonValue vv = o.value("vertices"), tv = o.value("triangles");
    if (!vv.isArray() || !tv.isArray()) {
        error = "geometry needs 'vertices' and 'triangles' arrays";
        return false;
    }
    Geometry g;
    const QJsonArray va = vv.toArray(), ta = tv.toArray();
    g.v.reserve(va.size());
    g.t.reserve(ta.size());
    for (const QJsonValue& x : va) {
        if (!x.isDouble()) { error = "a vertex coordinate is not a number"; return false; }
        g.v.push_back(x.toDouble());
    }
    for (const QJsonValue& x : ta) {
        const double d = x.toDouble(-1);
        if (!x.isDouble() || d < 0 || d != std::floor(d) || d > 4.0e9) {
            error = "a triangle index is not a non-negative integer";
            return false;
        }
        g.t.push_back(static_cast<uint32_t>(d));
    }
    error = validate(g);
    if (!error.isEmpty()) return false;
    out = std::move(g);
    return true;
}

}  // namespace p5
