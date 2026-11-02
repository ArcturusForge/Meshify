#pragma once
// A tiny document: a list of committed operations, each with its result, and a
// cursor for undo/redo. It exists to test two promises from the design brief:
//   HIS-01  a failed operation leaves the last committed model untouched;
//   EXT-01  a file opens without the plugin that made part of it.
#include "geometry.hpp"
#include "plugin_host.hpp"

#include <QJsonObject>
#include <memory>
#include <vector>

namespace p5 {

struct OpRecord {
    QString kind;                      // "native" or "plugin"
    QString plugin, pluginVersion;     // plugin operations only
    QString op;
    QJsonObject params;
    std::shared_ptr<const Geometry> result;  // geometry after this step
};

struct Outcome {
    bool ok = false;
    QString message;  // reason when !ok
    double ms = 0;    // plugin wall time, 0 for native operations
};

// How one history step looks once the installed plugins are known.
struct StepStatus {
    enum Kind { Ok, PluginMissing, VersionDiffers, OperationMissing } kind = Ok;
    QString message;  // empty when Ok
};

class Document {
public:
    const Geometry& current() const;     // geometry at the cursor (empty if none)
    size_t stepCount() const { return ops_.size(); }
    size_t cursor() const { return cursor_; }   // steps 0..cursor-1 are applied
    const OpRecord& step(size_t i) const { return ops_[i]; }

    // Native operations: "box" {sx,sy,sz} (new geometry), "translate" {dx,dy,dz}.
    Outcome applyNative(const QString& op, const QJsonObject& params);
    // Runs the plugin out of process. Commits only if the result validates.
    Outcome applyPlugin(const PluginManager& pm, const QString& plugin, const QString& op,
                        const QJsonObject& params, int timeoutMs = 10000);

    bool undo();
    bool redo();

    // Atomic: the old file stays intact unless the new one was written completely.
    Outcome save(const QString& path) const;
    // All or nothing: a file that does not parse or has bad geometry is refused.
    static Outcome open(const QString& path, Document& out);

    // Compares each step with the plugins installed now.
    std::vector<StepStatus> status(const PluginManager& pm) const;
    // Re-runs every step from its recorded parameters and compares with the saved
    // results. Changes nothing. Stops at the first step that cannot run.
    struct Replay { bool ok = false; size_t firstDifferent = 0; QString message; };
    Replay replay(const PluginManager& pm) const;

private:
    void commit(OpRecord r);
    std::vector<OpRecord> ops_;
    size_t cursor_ = 0;
};

}  // namespace p5
