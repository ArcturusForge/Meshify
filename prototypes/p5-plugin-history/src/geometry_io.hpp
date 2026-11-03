#pragma once
// Raw binary mesh file used to hand geometry to a plugin process without JSON.
// Layout (native endianness, both ends run on the same machine):
//   bytes 0..3   "MSH1"
//   bytes 4..7   uint32 vertex count n
//   bytes 8..11  uint32 triangle count m
//   bytes 12..15 reserved (0)
//   then 3n doubles (mm), then 3m uint32 indices.
// The doubles start at offset 16, so they are 8-byte aligned in a mapped file.
#include "geometry.hpp"

namespace p5 {

bool writeBinary(const QString& path, const Geometry& g, QString& error);
// Checks the size matches the header, then validates the mesh.
bool readBinary(const QString& path, Geometry& out, QString& error);

}  // namespace p5
