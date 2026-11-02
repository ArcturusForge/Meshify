#pragma once
// A triangle mesh in millimetres. Plain data, so it can be shared between
// history steps and sent to a plugin process.
#include <QJsonObject>
#include <QString>
#include <cstdint>
#include <vector>

namespace p5 {

struct Geometry {
    std::vector<double> v;     // x y z per vertex, mm
    std::vector<uint32_t> t;   // three indices per triangle
    bool operator==(const Geometry&) const = default;
    size_t vertexCount() const { return v.size() / 3; }
    size_t triangleCount() const { return t.size() / 3; }
};

// Returns an empty string when the mesh is usable, otherwise the reason it is
// not. Plugin output and file contents must pass this before they are accepted.
QString validate(const Geometry& g);

QJsonObject toJson(const Geometry& g);
// Parses and validates. On failure returns false and sets `error`.
bool fromJson(const QJsonObject& o, Geometry& out, QString& error);

}  // namespace p5
