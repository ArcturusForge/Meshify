// The card-game token from the design brief, rebuilt from its parameters as a
// short operation history: sketch circle, extrude, rim recess, relief, fillet.
// All lengths are millimetres.
#pragma once

#include "naming.hpp"

#include <string>
#include <vector>

namespace meshify::p1 {

struct TokenParams {
    double diameter = 40.0;
    double thickness = 4.0;
    double rimWidth = 3.0;
    double recessDepth = 0.8;
    double reliefSize = 16.0;    // length of each arm of the raised cross
    double reliefHeight = 0.6;
    double filletRadius = 0.5;
    bool recessEnabled = true;
    bool reliefEnabled = true;
    bool filletEnabled = true;
};

enum class StepStatus {
    Ok,          // rebuilt as recorded
    Warning,     // rebuilt, but the result needs the user's attention
    Broken,      // a reference could not be matched; step paused, not applied
    Failed,      // the kernel could not compute the step; previous result kept
    Suppressed,  // turned off by the user
};

const char* toString(StepStatus status);

struct StepReport {
    std::string step;
    StepStatus status = StepStatus::Ok;
    std::string message;
};

// The fillet step's edge references, as recorded when the user picked them.
std::vector<EdgeRef> filletReferences();

struct BuildResult {
    TokenParams params;
    NamedShape model;
    std::vector<StepReport> steps;
    bool valid = false;
    double volume = 0.0;  // mm^3
    int filletEdgesMatched = 0;

    const StepReport* step(const std::string& name) const;
};

// Replays the whole history. A failed or broken step never leaves a partial
// result: the model is always the output of the last step that succeeded.
BuildResult buildToken(const TokenParams& params);

// Keeps every rebuilt version, so a result from before an upstream edit can
// be restored.
class TokenDocument {
public:
    explicit TokenDocument(const TokenParams& params);

    const BuildResult& current() const { return versions_.at(current_); }
    const std::vector<BuildResult>& versions() const { return versions_; }

    // Rebuilds with new parameters and makes the result current.
    const BuildResult& edit(const TokenParams& params);
    // Makes an earlier version current again without rebuilding it.
    bool restore(std::size_t version);

private:
    std::vector<BuildResult> versions_;
    std::size_t current_ = 0;
};

}  // namespace meshify::p1
