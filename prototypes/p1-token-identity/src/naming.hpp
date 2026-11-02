// Stable face names and edge references carried through OCCT operations.
//
// Faces get logical names from the operation that created them
// ("extrude/top", "recess/wall"). Booleans and fillets pass names on through
// their Modified/Generated/IsDeleted history. An edge is referenced by the
// names of the two faces it separates, so a reference survives any rebuild
// that keeps those two faces adjacent.
#pragma once

#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>

#include <optional>
#include <string>
#include <utility>
#include <vector>

class BRepBuilderAPI_MakeShape;

namespace meshify::p1 {

using Name = std::string;

struct NamedShape {
    TopoDS_Shape shape;
    std::vector<std::pair<TopoDS_Face, Name>> faces;

    std::optional<Name> nameOf(const TopoDS_Shape& face) const;
    std::vector<Name> names() const;
    // Number of faces in the shape that have no name.
    int unnamedFaceCount() const;
};

// Unordered pair of adjacent face names.
struct EdgeRef {
    Name a;
    Name b;
    std::string str() const { return a + " | " + b; }
};

// Names every face of a freshly made primitive by its orientation:
// <prefix>/top, <prefix>/bottom (planar, normal +Z / -Z) or <prefix>/side.
NamedShape nameByOrientation(const TopoDS_Shape& shape, const std::string& prefix);

// Carries input face names onto the result of `op`. Faces the operation
// deletes are dropped; modified faces inherit the name of their source.
NamedShape propagate(BRepBuilderAPI_MakeShape& op,
                     const std::vector<const NamedShape*>& inputs,
                     const TopoDS_Shape& result);

// Names a result face if it has no name yet. Returns false if the face is not
// part of the shape or is already named.
bool assignName(NamedShape& target, const TopoDS_Shape& face, const Name& name);

// All edges whose two adjacent faces carry the referenced names. A face that
// was split by an earlier operation can yield several edges for one reference.
std::vector<TopoDS_Edge> resolve(const NamedShape& model, const EdgeRef& ref);

}  // namespace meshify::p1
