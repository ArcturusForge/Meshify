#include "naming.hpp"

#include <BRepAdaptor_Surface.hxx>
#include <BRepBuilderAPI_MakeShape.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_IndexedDataMapOfShapeListOfShape.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopTools_ListOfShape.hxx>
#include <TopoDS.hxx>
#include <gp_Dir.hxx>
#include <gp_Pln.hxx>

#include <algorithm>

namespace meshify::p1 {

std::optional<Name> NamedShape::nameOf(const TopoDS_Shape& face) const {
    for (const auto& [f, n] : faces) {
        if (f.IsSame(face)) return n;
    }
    return std::nullopt;
}

std::vector<Name> NamedShape::names() const {
    std::vector<Name> out;
    for (const auto& entry : faces) out.push_back(entry.second);
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

int NamedShape::unnamedFaceCount() const {
    TopTools_IndexedMapOfShape all;
    TopExp::MapShapes(shape, TopAbs_FACE, all);
    int unnamed = 0;
    for (int i = 1; i <= all.Extent(); ++i) {
        if (!nameOf(all(i))) ++unnamed;
    }
    return unnamed;
}

NamedShape nameByOrientation(const TopoDS_Shape& shape, const std::string& prefix) {
    NamedShape out;
    out.shape = shape;
    for (TopExp_Explorer ex(shape, TopAbs_FACE); ex.More(); ex.Next()) {
        const TopoDS_Face face = TopoDS::Face(ex.Current());
        std::string role = "side";
        BRepAdaptor_Surface surface(face);
        if (surface.GetType() == GeomAbs_Plane) {
            gp_Dir normal = surface.Plane().Axis().Direction();
            if (face.Orientation() == TopAbs_REVERSED) normal.Reverse();
            if (normal.Z() > 0.99) role = "top";
            else if (normal.Z() < -0.99) role = "bottom";
        }
        assignName(out, face, prefix + "/" + role);
    }
    return out;
}

bool assignName(NamedShape& target, const TopoDS_Shape& face, const Name& name) {
    TopTools_IndexedMapOfShape all;
    TopExp::MapShapes(target.shape, TopAbs_FACE, all);
    if (!all.Contains(face) || target.nameOf(face)) return false;
    target.faces.emplace_back(TopoDS::Face(face), name);
    return true;
}

NamedShape propagate(BRepBuilderAPI_MakeShape& op,
                     const std::vector<const NamedShape*>& inputs,
                     const TopoDS_Shape& result) {
    NamedShape out;
    out.shape = result;
    for (const NamedShape* input : inputs) {
        for (const auto& [face, name] : input->faces) {
            if (op.IsDeleted(face)) continue;
            const TopTools_ListOfShape& modified = op.Modified(face);
            if (modified.IsEmpty()) {
                assignName(out, face, name);
                continue;
            }
            for (TopTools_ListOfShape::Iterator it(modified); it.More(); it.Next()) {
                assignName(out, it.Value(), name);
            }
        }
    }
    return out;
}

std::vector<TopoDS_Edge> resolve(const NamedShape& model, const EdgeRef& ref) {
    TopTools_IndexedDataMapOfShapeListOfShape edgeFaces;
    TopExp::MapShapesAndAncestors(model.shape, TopAbs_EDGE, TopAbs_FACE, edgeFaces);

    std::vector<TopoDS_Edge> matches;
    for (int i = 1; i <= edgeFaces.Extent(); ++i) {
        std::vector<Name> adjacent;
        std::vector<TopoDS_Shape> seen;
        for (TopTools_ListOfShape::Iterator it(edgeFaces(i)); it.More(); it.Next()) {
            const TopoDS_Shape& face = it.Value();
            // A seam edge lists the same face twice; it separates nothing.
            if (std::any_of(seen.begin(), seen.end(),
                            [&](const TopoDS_Shape& s) { return s.IsSame(face); })) {
                continue;
            }
            seen.push_back(face);
            adjacent.push_back(model.nameOf(face).value_or(""));
        }
        if (adjacent.size() != 2) continue;
        const bool hit = (adjacent[0] == ref.a && adjacent[1] == ref.b) ||
                         (adjacent[0] == ref.b && adjacent[1] == ref.a);
        if (hit) matches.push_back(TopoDS::Edge(edgeFaces.FindKey(i)));
    }
    return matches;
}

}  // namespace meshify::p1
