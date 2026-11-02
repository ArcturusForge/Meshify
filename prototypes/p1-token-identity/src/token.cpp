#include "token.hpp"

#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepFilletAPI_MakeFillet.hxx>
#include <BRepGProp.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <GProp_GProps.hxx>
#include <Standard_Failure.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_ListOfShape.hxx>
#include <TopoDS.hxx>
#include <gp_Ax2.hxx>
#include <gp_Circ.hxx>
#include <gp_Vec.hxx>

#include <cmath>
#include <sstream>

namespace meshify::p1 {
namespace {

// Tools reach past the faces they cut so no faces end up coplanar.
constexpr double kOvershoot = 0.05;

bool isValidSolid(const TopoDS_Shape& shape) {
    return !shape.IsNull() && BRepCheck_Analyzer(shape).IsValid();
}

NamedShape sketchAndExtrude(const TokenParams& p) {
    const gp_Circ circle(gp_Ax2(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1)), p.diameter / 2.0);
    const TopoDS_Edge edge = BRepBuilderAPI_MakeEdge(circle);
    const TopoDS_Face profile = BRepBuilderAPI_MakeFace(BRepBuilderAPI_MakeWire(edge));

    BRepPrimAPI_MakePrism prism(profile, gp_Vec(0, 0, p.thickness));
    prism.Build();
    if (!prism.IsDone()) throw Standard_Failure("extrude failed");

    NamedShape out;
    out.shape = prism.Shape();
    assignName(out, prism.FirstShape(), "extrude/bottom");
    assignName(out, prism.LastShape(), "extrude/top");
    for (TopTools_ListOfShape::Iterator it(prism.Generated(edge)); it.More(); it.Next()) {
        assignName(out, it.Value(), "extrude/side");
    }
    return out;
}

double recessRadius(const TokenParams& p) { return p.diameter / 2.0 - p.rimWidth; }

NamedShape cutRecess(const NamedShape& body, const TokenParams& p) {
    const double floorZ = p.thickness - p.recessDepth;
    const TopoDS_Shape cylinder =
        BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(0, 0, floorZ), gp_Dir(0, 0, 1)),
                                 recessRadius(p), p.recessDepth + kOvershoot);
    NamedShape tool = nameByOrientation(cylinder, "recess");
    for (auto& entry : tool.faces) {
        if (entry.second == "recess/bottom") entry.second = "recess/floor";
        else if (entry.second == "recess/side") entry.second = "recess/wall";
    }

    BRepAlgoAPI_Cut cut(body.shape, tool.shape);
    cut.Build();
    if (!cut.IsDone() || cut.HasErrors()) throw Standard_Failure("recess cut failed");
    return propagate(cut, {&body, &tool}, cut.Shape());
}

NamedShape addRelief(const NamedShape& body, const TokenParams& p, bool recessed) {
    const double baseZ = recessed ? p.thickness - p.recessDepth : p.thickness;
    const double length = p.reliefSize;
    const double width = p.reliefSize / 4.0;
    const double z0 = baseZ - kOvershoot;
    const double height = p.reliefHeight + kOvershoot;

    const NamedShape armX = nameByOrientation(
        BRepPrimAPI_MakeBox(gp_Pnt(-length / 2, -width / 2, z0), length, width, height).Shape(),
        "relief");
    const NamedShape armY = nameByOrientation(
        BRepPrimAPI_MakeBox(gp_Pnt(-width / 2, -length / 2, z0), width, length, height).Shape(),
        "relief");

    BRepAlgoAPI_Fuse cross(armX.shape, armY.shape);
    cross.Build();
    if (!cross.IsDone() || cross.HasErrors()) throw Standard_Failure("relief outline failed");
    const NamedShape crossNamed = propagate(cross, {&armX, &armY}, cross.Shape());

    BRepAlgoAPI_Fuse fuse(body.shape, crossNamed.shape);
    fuse.Build();
    if (!fuse.IsDone() || fuse.HasErrors()) throw Standard_Failure("relief join failed");
    return propagate(fuse, {&body, &crossNamed}, fuse.Shape());
}

}  // namespace

const char* toString(StepStatus status) {
    switch (status) {
        case StepStatus::Ok: return "ok";
        case StepStatus::Warning: return "warning";
        case StepStatus::Broken: return "needs input";
        case StepStatus::Failed: return "failed";
        case StepStatus::Suppressed: return "suppressed";
    }
    return "?";
}

std::vector<EdgeRef> filletReferences() {
    return {
        {"extrude/top", "extrude/side"},     // outer top edge
        {"extrude/bottom", "extrude/side"},  // outer bottom edge
        {"extrude/top", "recess/wall"},      // inner rim edge
    };
}

const StepReport* BuildResult::step(const std::string& name) const {
    for (const auto& s : steps) {
        if (s.step == name) return &s;
    }
    return nullptr;
}

BuildResult buildToken(const TokenParams& p) {
    BuildResult r;
    r.params = p;

    // Steps 1-2: sketch and extrude. Nothing can be built without them.
    try {
        r.model = sketchAndExtrude(p);
        r.steps.push_back({"Sketch circle", StepStatus::Ok, ""});
        r.steps.push_back({"Extrude", StepStatus::Ok, ""});
    } catch (const Standard_Failure& e) {
        r.steps.push_back({"Sketch circle", StepStatus::Failed, e.GetMessageString()});
        return r;
    }

    // Step 3: rim recess.
    bool recessed = false;
    if (!p.recessEnabled) {
        r.steps.push_back({"Rim recess", StepStatus::Suppressed, ""});
    } else if (recessRadius(p) <= 0.0 || p.recessDepth >= p.thickness) {
        r.steps.push_back({"Rim recess", StepStatus::Failed,
                           "rim width or depth leaves no material for the recess"});
    } else {
        try {
            NamedShape next = cutRecess(r.model, p);
            if (!isValidSolid(next.shape)) throw Standard_Failure("recess produced an invalid solid");
            r.model = std::move(next);
            recessed = true;
            r.steps.push_back({"Rim recess", StepStatus::Ok, ""});
        } catch (const Standard_Failure& e) {
            r.steps.push_back({"Rim recess", StepStatus::Failed, e.GetMessageString()});
        }
    }

    // Step 4: raised relief. Warn, but still build, when it no longer fits.
    if (!p.reliefEnabled) {
        r.steps.push_back({"Relief", StepStatus::Suppressed, ""});
    } else {
        try {
            NamedShape next = addRelief(r.model, p, recessed);
            if (!isValidSolid(next.shape)) throw Standard_Failure("relief produced an invalid solid");
            r.model = std::move(next);
            const double reach = std::hypot(p.reliefSize / 2.0, p.reliefSize / 8.0);
            const double room = recessed ? recessRadius(p) : p.diameter / 2.0;
            if (reach > room) {
                std::ostringstream msg;
                msg << "relief reaches " << reach << " mm from the centre but the "
                    << (recessed ? "recess" : "token") << " radius is " << room << " mm";
                r.steps.push_back({"Relief", StepStatus::Warning, msg.str()});
            } else {
                r.steps.push_back({"Relief", StepStatus::Ok, ""});
            }
        } catch (const Standard_Failure& e) {
            r.steps.push_back({"Relief", StepStatus::Failed, e.GetMessageString()});
        }
    }

    // Step 5: fillet on referenced edges. Every reference must resolve, or the
    // step is paused and the user is asked for a replacement edge.
    if (!p.filletEnabled) {
        r.steps.push_back({"Fillet", StepStatus::Suppressed, ""});
    } else {
        const auto refs = filletReferences();
        std::vector<TopoDS_Edge> edges;
        std::vector<std::string> missing;
        for (const auto& ref : refs) {
            const auto found = resolve(r.model, ref);
            if (found.empty()) missing.push_back(ref.str());
            edges.insert(edges.end(), found.begin(), found.end());
        }
        if (!missing.empty()) {
            std::ostringstream msg;
            msg << missing.size() << " of " << refs.size() << " edges could not be matched:";
            for (const auto& m : missing) msg << " [" << m << "]";
            r.steps.push_back({"Fillet", StepStatus::Broken, msg.str()});
        } else {
            try {
                BRepFilletAPI_MakeFillet fillet(r.model.shape);
                for (const auto& edge : edges) fillet.Add(p.filletRadius, edge);
                fillet.Build();
                if (!fillet.IsDone()) throw Standard_Failure("fillet could not be computed");
                NamedShape next = propagate(fillet, {&r.model}, fillet.Shape());
                for (std::size_t i = 0; i < refs.size(); ++i) {
                    for (const auto& edge : resolve(r.model, refs[i])) {
                        const TopTools_ListOfShape& made = fillet.Generated(edge);
                        for (TopTools_ListOfShape::Iterator it(made); it.More(); it.Next()) {
                            assignName(next, it.Value(), "fillet/" + refs[i].str());
                        }
                    }
                }
                if (!isValidSolid(next.shape)) throw Standard_Failure("fillet produced an invalid solid");
                r.model = std::move(next);
                r.filletEdgesMatched = static_cast<int>(edges.size());
                r.steps.push_back({"Fillet", StepStatus::Ok, ""});
            } catch (const Standard_Failure& e) {
                std::ostringstream msg;
                msg << e.GetMessageString() << " (radius " << p.filletRadius << " mm)";
                r.steps.push_back({"Fillet", StepStatus::Failed, msg.str()});
            }
        }
    }

    r.valid = isValidSolid(r.model.shape);
    GProp_GProps props;
    BRepGProp::VolumeProperties(r.model.shape, props);
    r.volume = props.Mass();
    return r;
}

TokenDocument::TokenDocument(const TokenParams& params) { versions_.push_back(buildToken(params)); }

const BuildResult& TokenDocument::edit(const TokenParams& params) {
    versions_.push_back(buildToken(params));
    current_ = versions_.size() - 1;
    return versions_.back();
}

bool TokenDocument::restore(std::size_t version) {
    if (version >= versions_.size()) return false;
    current_ = version;
    return true;
}

}  // namespace meshify::p1
