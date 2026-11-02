// P1 acceptance tests: GEO-01 (edit diameter, thickness and relief; references
// survive or are reported), HIS-01 (a failed step leaves the last good model).
#include "token.hpp"

#include <cmath>
#include <functional>
#include <iostream>
#include <string>
#include <vector>

using namespace meshify::p1;

namespace {

int failures = 0;

#define CHECK(cond)                                                                    \
    do {                                                                               \
        if (!(cond)) {                                                                 \
            ++failures;                                                                \
            std::cerr << "  CHECK failed at line " << __LINE__ << ": " #cond << "\n"; \
        }                                                                              \
    } while (0)

StepStatus statusOf(const BuildResult& r, const std::string& step) {
    const StepReport* s = r.step(step);
    return s ? s->status : StepStatus::Failed;
}

bool allBuilt(const BuildResult& r) {
    for (const auto& s : r.steps) {
        if (s.status != StepStatus::Ok && s.status != StepStatus::Warning) return false;
    }
    return true;
}

void baselineBuild() {
    const BuildResult r = buildToken(TokenParams{});
    CHECK(r.valid);
    CHECK(allBuilt(r));
    CHECK(r.steps.size() == 5);
    CHECK(r.filletEdgesMatched == 3);
    CHECK(r.model.unnamedFaceCount() == 0);
}

void volumeMatchesAnalytic() {
    TokenParams p;
    p.reliefEnabled = false;
    p.filletEnabled = false;
    const BuildResult r = buildToken(p);
    const double pi = std::acos(-1.0);
    const double outer = pi * std::pow(p.diameter / 2, 2) * p.thickness;
    const double recess = pi * std::pow(p.diameter / 2 - p.rimWidth, 2) * p.recessDepth;
    CHECK(r.valid);
    CHECK(std::abs(r.volume - (outer - recess)) < 1e-6 * outer);
}

// Twenty scripted upstream edits. Every reference must resolve to an edge and
// the fillet must rebuild, with the same set of face names each time.
void twentyEditSweep() {
    const std::vector<Name> baselineNames = buildToken(TokenParams{}).model.names();
    int rebuilt = 0;
    for (int i = 0; i < 20; ++i) {
        TokenParams p;
        p.diameter = 25.0 + 1.25 * i;              // 25 .. 48.75 mm
        p.thickness = 2.5 + 0.15 * (i % 10);       // 2.5 .. 3.85 mm
        p.rimWidth = 1.5 + 0.1 * (i % 7);          // 1.5 .. 2.1 mm
        p.recessDepth = 0.5 + 0.05 * (i % 5);      // 0.5 .. 0.7 mm
        p.reliefSize = 0.4 * (p.diameter - 2 * p.rimWidth);
        p.filletRadius = 0.3;
        const BuildResult r = buildToken(p);
        const bool ok = r.valid && allBuilt(r) && r.filletEdgesMatched == 3 &&
                        r.model.names() == baselineNames && r.model.unnamedFaceCount() == 0;
        if (ok) {
            ++rebuilt;
        } else {
            std::cerr << "  edit " << i << " (diameter " << p.diameter << ") did not rebuild cleanly\n";
            for (const auto& s : r.steps) {
                std::cerr << "    " << s.step << ": " << toString(s.status) << " " << s.message << "\n";
            }
        }
    }
    std::cout << "  " << rebuilt << " of 20 edits rebuilt with all references matched\n";
    CHECK(rebuilt == 20);
}

// Removing the recess removes the face one fillet edge was attached to. The
// fillet step is paused with a reason; nothing is silently dropped.
void suppressedRecessBreaksReference() {
    TokenParams p;
    p.recessEnabled = false;
    const BuildResult r = buildToken(p);
    CHECK(statusOf(r, "Rim recess") == StepStatus::Suppressed);
    CHECK(statusOf(r, "Fillet") == StepStatus::Broken);
    const StepReport* fillet = r.step("Fillet");
    CHECK(fillet && fillet->message.find("1 of 3") != std::string::npos);
    CHECK(fillet && fillet->message.find("recess/wall") != std::string::npos);
    CHECK(r.valid);
}

// A radius larger than the recess depth cannot round the inner rim edge.
// The step fails and the model is exactly the pre-fillet result.
void oversizeFilletFailsSafely() {
    TokenParams p;
    p.filletRadius = 3.0;
    const BuildResult failed = buildToken(p);
    TokenParams before = p;
    before.filletEnabled = false;
    const BuildResult reference = buildToken(before);
    CHECK(statusOf(failed, "Fillet") == StepStatus::Failed);
    CHECK(failed.valid);
    CHECK(std::abs(failed.volume - reference.volume) < 1e-9 * reference.volume);
}

void reliefOverlapWarns() {
    TokenParams p;
    p.diameter = 20.0;  // recess radius 7 mm, relief reaches ~8.2 mm
    const BuildResult r = buildToken(p);
    CHECK(statusOf(r, "Relief") == StepStatus::Warning);
    CHECK(r.valid);
}

void earlierVersionRestores() {
    TokenDocument doc(TokenParams{});
    const double original = doc.current().volume;
    TokenParams smaller;
    smaller.diameter = 30.0;
    doc.edit(smaller);
    CHECK(doc.versions().size() == 2);
    CHECK(doc.current().params.diameter == 30.0);
    CHECK(doc.restore(0));
    CHECK(doc.current().params.diameter == 40.0);
    CHECK(doc.current().volume == original);
    CHECK(!doc.restore(5));
}

}  // namespace

int main() {
    const std::vector<std::pair<const char*, std::function<void()>>> tests = {
        {"baseline build", baselineBuild},
        {"volume matches analytic value", volumeMatchesAnalytic},
        {"20 upstream edits rebuild", twentyEditSweep},
        {"suppressed recess breaks one reference", suppressedRecessBreaksReference},
        {"oversize fillet fails safely", oversizeFilletFailsSafely},
        {"relief overlap warns", reliefOverlapWarns},
        {"earlier version restores", earlierVersionRestores},
    };
    for (const auto& [name, fn] : tests) {
        const int before = failures;
        std::cout << "[ RUN  ] " << name << "\n";
        fn();
        std::cout << (failures == before ? "[  OK  ] " : "[ FAIL ] ") << name << "\n";
    }
    std::cout << (failures == 0 ? "All tests passed\n" : "Some tests failed\n");
    return failures == 0 ? 0 : 1;
}
