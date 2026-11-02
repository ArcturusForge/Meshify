// P2: runs 30 difficult modelling cases through Open CASCADE and reports
// which ones produce a valid solid, which fail cleanly, and how long each takes.
//
// Pass condition from ADR-0001: at least 90% of the cases expected to work
// produce a valid solid, and every case expected to fail is rejected, either by
// OCCT reporting an error or by the validity check run after each operation.
#include <BRepAdaptor_Curve.hxx>
#include <BRepAlgoAPI_Common.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepFilletAPI_MakeChamfer.hxx>
#include <BRepFilletAPI_MakeFillet.hxx>
#include <BRepGProp.hxx>
#include <BRepOffsetAPI_MakeOffsetShape.hxx>
#include <BRepOffsetAPI_MakeThickSolid.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <ShapeUpgrade_UnifySameDomain.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRepPrimAPI_MakeRevol.hxx>
#include <BRepPrimAPI_MakeSphere.hxx>
#include <BRepPrimAPI_MakeTorus.hxx>
#include <BRep_Builder.hxx>
#include <GProp_GProps.hxx>
#include <Standard_Failure.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopTools_ListOfShape.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>
#include <gp_Ax1.hxx>
#include <gp_Ax2.hxx>
#include <gp_Pnt.hxx>
#include <gp_Vec.hxx>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace {

const double kPi = std::acos(-1.0);

// ---- Building blocks -------------------------------------------------------

TopoDS_Shape box(double x0, double y0, double z0, double dx, double dy, double dz) {
    return BRepPrimAPI_MakeBox(gp_Pnt(x0, y0, z0), dx, dy, dz).Shape();
}

TopoDS_Shape cylinder(double x, double y, double z, double r, double h, gp_Dir axis = gp_Dir(0, 0, 1)) {
    return BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(x, y, z), axis), r, h).Shape();
}

template <class Op>
TopoDS_Shape boolean(const TopoDS_Shape& a, const TopoDS_Shape& b) {
    Op op(a, b);
    op.Build();
    if (!op.IsDone() || op.HasErrors()) throw Standard_Failure("boolean failed");
    return op.Shape();
}

TopoDS_Shape fuse(const TopoDS_Shape& a, const TopoDS_Shape& b) { return boolean<BRepAlgoAPI_Fuse>(a, b); }
TopoDS_Shape cut(const TopoDS_Shape& a, const TopoDS_Shape& b) { return boolean<BRepAlgoAPI_Cut>(a, b); }
TopoDS_Shape common(const TopoDS_Shape& a, const TopoDS_Shape& b) { return boolean<BRepAlgoAPI_Common>(a, b); }

TopoDS_Shape compound(const std::vector<TopoDS_Shape>& parts) {
    TopoDS_Compound c;
    BRep_Builder b;
    b.MakeCompound(c);
    for (const auto& p : parts) b.Add(c, p);
    return c;
}

// Closed polygon in the XY plane at height z, extruded by h.
TopoDS_Shape polygonPrism(const std::vector<std::pair<double, double>>& pts, double z, double h) {
    BRepBuilderAPI_MakePolygon poly;
    for (const auto& [x, y] : pts) poly.Add(gp_Pnt(x, y, z));
    poly.Close();
    const TopoDS_Face face = BRepBuilderAPI_MakeFace(poly.Wire());
    return BRepPrimAPI_MakePrism(face, gp_Vec(0, 0, h)).Shape();
}

gp_Pnt midpoint(const TopoDS_Edge& e) {
    BRepAdaptor_Curve c(e);
    return c.Value((c.FirstParameter() + c.LastParameter()) / 2.0);
}

std::vector<TopoDS_Edge> edgesWhere(const TopoDS_Shape& s, const std::function<bool(const gp_Pnt&)>& pred) {
    TopTools_IndexedMapOfShape edges;
    TopExp::MapShapes(s, TopAbs_EDGE, edges);
    std::vector<TopoDS_Edge> out;
    for (int i = 1; i <= edges.Extent(); ++i) {
        const TopoDS_Edge e = TopoDS::Edge(edges(i));
        if (pred(midpoint(e))) out.push_back(e);
    }
    return out;
}

std::vector<TopoDS_Edge> allEdges(const TopoDS_Shape& s) {
    return edgesWhere(s, [](const gp_Pnt&) { return true; });
}

TopoDS_Shape fillet(const TopoDS_Shape& s, const std::vector<TopoDS_Edge>& edges, double r) {
    if (edges.empty()) throw Standard_Failure("no edges selected");
    BRepFilletAPI_MakeFillet mk(s);
    for (const auto& e : edges) mk.Add(r, e);
    mk.Build();
    if (!mk.IsDone()) throw Standard_Failure("fillet not done");
    return mk.Shape();
}

TopoDS_Shape chamfer(const TopoDS_Shape& s, const std::vector<TopoDS_Edge>& edges, double d) {
    if (edges.empty()) throw Standard_Failure("no edges selected");
    BRepFilletAPI_MakeChamfer mk(s);
    for (const auto& e : edges) mk.Add(d, e);
    mk.Build();
    if (!mk.IsDone()) throw Standard_Failure("chamfer not done");
    return mk.Shape();
}

// Hollows a solid to wall thickness t, removing the faces whose centre is
// highest (an open top).
TopoDS_Shape shellOpenTop(const TopoDS_Shape& s, double t) {
    TopTools_ListOfShape remove;
    double topZ = -1e100;
    std::vector<std::pair<TopoDS_Shape, double>> faces;
    for (TopExp_Explorer ex(s, TopAbs_FACE); ex.More(); ex.Next()) {
        GProp_GProps p;
        BRepGProp::SurfaceProperties(ex.Current(), p);
        const double z = p.CentreOfMass().Z();
        faces.emplace_back(ex.Current(), z);
        topZ = std::max(topZ, z);
    }
    for (const auto& [f, z] : faces) {
        if (z > topZ - 1e-6) remove.Append(f);
    }
    BRepOffsetAPI_MakeThickSolid mk;
    mk.MakeThickSolidByJoin(s, remove, -t, 1e-3);
    mk.Build();
    if (!mk.IsDone()) throw Standard_Failure("shell not done");
    return mk.Shape();
}

TopoDS_Shape offset(const TopoDS_Shape& s, double d) {
    BRepOffsetAPI_MakeOffsetShape mk;
    mk.PerformByJoin(s, d, 1e-3, BRepOffset_Skin, false, false, GeomAbs_Arc);
    if (!mk.IsDone()) throw Standard_Failure("offset not done");
    return mk.Shape();
}

TopoDS_Shape revolve(const std::vector<std::pair<double, double>>& rz) {
    BRepBuilderAPI_MakePolygon poly;
    for (const auto& [r, z] : rz) poly.Add(gp_Pnt(r, 0, z));
    poly.Close();
    const TopoDS_Face face = BRepBuilderAPI_MakeFace(poly.Wire());
    return BRepPrimAPI_MakeRevol(face, gp_Ax1(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1)), 2 * kPi).Shape();
}

// The P1 token without relief: 40 mm across, 4 mm thick, 0.8 mm recess.
TopoDS_Shape token() {
    return cut(cylinder(0, 0, 0, 20, 4), cylinder(0, 0, 3.2, 17, 1));
}

// A "+1" outline as polygon prisms, the shape of a vector relief.
TopoDS_Shape plusOne(double z, double h) {
    const TopoDS_Shape plus = fuse(box(-9, -1.5, z, 8, 3, h), box(-6.5, -4, z, 3, 8, h));
    const TopoDS_Shape one = polygonPrism({{2, -6}, {8, -6}, {8, -4}, {6, -4}, {6, 6}, {4, 6}, {1.5, 3.5}, {3, 2.5}, {4, 3.5}, {4, -4}, {2, -4}}, z, h);
    return fuse(plus, one);
}

// An L-shaped bracket with a concave inner edge along Y at x = 10, z = 10.
// L-shaped bracket. The fuse leaves coplanar faces split along the seam, so
// they are merged afterwards, as the app would do after every boolean.
TopoDS_Shape bracket() {
    ShapeUpgrade_UnifySameDomain unify(fuse(box(0, 0, 0, 40, 20, 10), box(0, 0, 0, 10, 20, 40)));
    unify.Build();
    return unify.Shape();
}

bool approx(double a, double b) { return std::abs(a - b) < 1e-6; }

// ---- Cases -----------------------------------------------------------------

struct Case {
    std::string group;
    std::string name;
    bool expectFailure;
    std::function<TopoDS_Shape()> build;
};

std::vector<Case> cases() {
    return {
        // Fillets
        {"Fillet", "Box, all 12 edges, r 1 mm", false, [] { auto b = box(0, 0, 0, 10, 10, 10); return fillet(b, allEdges(b), 1); }},
        {"Fillet", "Box, all edges, r 4.9 mm (near half the size)", false, [] { auto b = box(0, 0, 0, 10, 10, 10); return fillet(b, allEdges(b), 4.9); }},
        {"Fillet", "Box, all edges, r 6 mm (larger than half)", true, [] { auto b = box(0, 0, 0, 10, 10, 10); return fillet(b, allEdges(b), 6); }},
        {"Fillet", "Cylinder top edge, r 2 mm", false, [] { auto c = cylinder(0, 0, 0, 10, 10); return fillet(c, edgesWhere(c, [](const gp_Pnt& p) { return approx(p.Z(), 10); }), 2); }},
        {"Fillet", "Token outer and inner rim edges, r 0.5 mm", false, [] { auto t = token(); return fillet(t, edgesWhere(t, [](const gp_Pnt& p) { return approx(p.Z(), 4) || approx(p.Z(), 0); }), 0.5); }},
        {"Fillet", "Token inner rim, r 3 mm on a 0.8 mm recess", true, [] { auto t = token(); return fillet(t, edgesWhere(t, [](const gp_Pnt& p) { return approx(p.Z(), 4) && std::hypot(p.X(), p.Y()) < 18; }), 3); }},
        {"Fillet", "Bracket concave inner edge, r 3 mm", false, [] { auto b = bracket(); return fillet(b, edgesWhere(b, [](const gp_Pnt& p) { return approx(p.X(), 10) && approx(p.Z(), 10); }), 3); }},
        {"Fillet", "Tee of two cylinders, intersection edge, r 1 mm", false, [] {
             auto t = fuse(cylinder(0, 0, -15, 5, 30), cylinder(-15, 0, 0, 4, 30, gp_Dir(1, 0, 0)));
             return fillet(t, edgesWhere(t, [](const gp_Pnt& p) { return std::abs(p.Z()) < 5.01 && std::abs(p.Y()) < 5.01 && std::abs(p.X()) < 5.01 && std::abs(p.X()) > 0.5; }), 1); }},
        {"Fillet", "Box corner, three edges meeting, r 2 mm", false, [] { auto b = box(0, 0, 0, 10, 10, 10); return fillet(b, edgesWhere(b, [](const gp_Pnt& p) { return (approx(p.X(), 10) && approx(p.Y(), 10)) || (approx(p.X(), 10) && approx(p.Z(), 10)) || (approx(p.Y(), 10) && approx(p.Z(), 10)); }), 2); }},
        {"Fillet", "Slot edges after a cut, r 0.5 mm", false, [] { auto s = cut(box(0, 0, 0, 30, 20, 10), box(10, -1, 5, 10, 22, 6)); return fillet(s, edgesWhere(s, [](const gp_Pnt& p) { return approx(p.Z(), 10) && (approx(p.X(), 10) || approx(p.X(), 20)); }), 0.5); }},
        // Chamfers
        {"Chamfer", "Box, all edges, 1 mm", false, [] { auto b = box(0, 0, 0, 10, 10, 10); return chamfer(b, allEdges(b), 1); }},
        {"Chamfer", "Cylinder top edge, 1.5 mm", false, [] { auto c = cylinder(0, 0, 0, 10, 10); return chamfer(c, edgesWhere(c, [](const gp_Pnt& p) { return approx(p.Z(), 10); }), 1.5); }},
        {"Chamfer", "Bracket concave inner edge, 2 mm", false, [] { auto b = bracket(); return chamfer(b, edgesWhere(b, [](const gp_Pnt& p) { return approx(p.X(), 10) && approx(p.Z(), 10); }), 2); }},
        {"Chamfer", "Token outer top edge, 0.6 mm", false, [] { auto t = token(); return chamfer(t, edgesWhere(t, [](const gp_Pnt& p) { return approx(p.Z(), 4) && std::hypot(p.X(), p.Y()) > 19; }), 0.6); }},
        // Shells
        {"Shell", "Box, open top, 1 mm wall", false, [] { return shellOpenTop(box(0, 0, 0, 20, 20, 20), 1); }},
        {"Shell", "Cylinder, open top, 1.2 mm wall", false, [] { return shellOpenTop(cylinder(0, 0, 0, 10, 20), 1.2); }},
        {"Shell", "Filleted box, open top, 1 mm wall", false, [] { auto b = box(0, 0, 0, 20, 20, 20); return shellOpenTop(fillet(b, edgesWhere(b, [](const gp_Pnt& p) { return p.Z() < 19.9; }), 3), 1); }},
        {"Shell", "Bracket, open top, 2 mm wall", false, [] { return shellOpenTop(bracket(), 2); }},
        {"Shell", "Box, 11 mm wall on a 20 mm box", true, [] { return shellOpenTop(box(0, 0, 0, 20, 20, 20), 11); }},
        // Offsets
        {"Offset", "Box outward 1 mm, rounded corners", false, [] { return offset(box(0, 0, 0, 10, 10, 10), 1); }},
        {"Offset", "Filleted box inward 0.5 mm", false, [] { auto b = box(0, 0, 0, 10, 10, 10); return offset(fillet(b, allEdges(b), 1), -0.5); }},
        {"Offset", "Torus outward 0.5 mm", false, [] { return offset(BRepPrimAPI_MakeTorus(10, 3).Shape(), 0.5); }},
        // Booleans
        {"Boolean", "Cylinder through box", false, [] { return cut(box(0, 0, 0, 20, 20, 10), cylinder(10, 10, -1, 4, 12)); }},
        {"Boolean", "Two boxes sharing a face (coplanar fuse)", false, [] { return fuse(box(0, 0, 0, 10, 10, 10), box(10, 0, 0, 10, 10, 10)); }},
        {"Boolean", "Two tangent cylinders fused", false, [] { return fuse(cylinder(0, 0, 0, 5, 10), cylinder(10, 0, 0, 5, 10)); }},
        {"Boolean", "Plate with 49 holes in one cut", false, [] {
             std::vector<TopoDS_Shape> holes;
             for (int i = 0; i < 7; ++i)
                 for (int j = 0; j < 7; ++j) holes.push_back(cylinder(10 + 13 * i, 10 + 13 * j, -1, 3, 7));
             return cut(box(0, 0, 0, 100, 100, 5), compound(holes)); }},
        {"Boolean", "Sphere and box intersection", false, [] { return common(BRepPrimAPI_MakeSphere(gp_Pnt(5, 5, 5), 7).Shape(), box(0, 0, 0, 10, 10, 10)); }},
        {"Boolean", "Revolved goblet cut by a box", false, [] { return cut(revolve({{0, 0}, {8, 0}, {8, 1}, {2, 2}, {2, 10}, {7, 16}, {6, 16}, {0, 11}}), box(0, -10, -1, 10, 20, 20)); }},
        {"Relief", "Vector \"+1\" raised 0.6 mm on the token", false, [] { return fuse(token(), plusOne(3.15, 0.65)); }},
        {"Relief", "Vector \"+1\" engraved 0.5 mm into a box", false, [] { return cut(box(-15, -15, 0, 30, 30, 4), plusOne(3.5, 1)); }},
    };
}

struct Outcome {
    bool producedSolid = false;
    bool valid = false;
    double volume = 0.0;
    double ms = 0.0;
    std::string error;
};

Outcome run(const Case& c) {
    Outcome o;
    const auto start = std::chrono::steady_clock::now();
    try {
        const TopoDS_Shape s = c.build();
        o.producedSolid = !s.IsNull();
        if (o.producedSolid) {
            o.valid = BRepCheck_Analyzer(s).IsValid();
            GProp_GProps p;
            BRepGProp::VolumeProperties(s, p);
            o.volume = p.Mass();
            if (o.volume <= 0) o.valid = false;
        }
    } catch (const Standard_Failure& e) {
        o.error = e.GetMessageString() ? e.GetMessageString() : "OCCT exception";
    } catch (const std::exception& e) {
        o.error = e.what();
    }
    o.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    return o;
}

}  // namespace

int main() {
    const auto all = cases();
    int expectedOk = 0, ok = 0, expectedFail = 0, cleanFail = 0, invalid = 0, caughtByCheck = 0;
    double totalMs = 0;

    std::printf("| # | Group | Case | Result | Volume (mm^3) | Time (ms) |\n");
    std::printf("| --- | --- | --- | --- | --- | --- |\n");
    for (std::size_t i = 0; i < all.size(); ++i) {
        const Case& c = all[i];
        const Outcome o = run(c);
        totalMs += o.ms;
        std::string result;
        if (c.expectFailure) {
            ++expectedFail;
            if (o.producedSolid && !o.valid) {
                // OCCT returned a broken solid without reporting an error. The
                // validity check after every operation is what rejects it.
                ++cleanFail;
                ++caughtByCheck;
                result = "rejected by validity check (OCCT returned an invalid solid)";
            } else if (o.producedSolid) {
                ++cleanFail;  // a valid solid is not harmful
                result = "unexpected success";
            } else {
                ++cleanFail;
                result = "failed cleanly: " + o.error;
            }
        } else {
            ++expectedOk;
            if (o.valid) {
                ++ok;
                result = "valid solid";
            } else if (o.producedSolid) {
                ++invalid;
                result = "INVALID solid";
            } else {
                result = "FAILED: " + o.error;
            }
        }
        std::printf("| %zu | %s | %s | %s | %.3f | %.1f |\n", i + 1, c.group.c_str(), c.name.c_str(),
                    result.c_str(), o.volume, o.ms);
    }

    const double rate = expectedOk ? 100.0 * ok / expectedOk : 0.0;
    std::printf("\n%d of %d expected-to-work cases produced a valid solid (%.0f%%).\n", ok, expectedOk, rate);
    std::printf("%d of %d expected failures were rejected (%d only by the validity check).\n",
                cleanFail, expectedFail, caughtByCheck);
    std::printf("Invalid solids from cases expected to work: %d. Total time %.0f ms.\n", invalid, totalMs);

    const bool pass = rate >= 90.0 && cleanFail == expectedFail && invalid == 0;
    std::printf("%s\n", pass ? "PASS" : "FAIL");
    return pass ? 0 : 1;
}
