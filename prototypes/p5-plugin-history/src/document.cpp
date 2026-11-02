#include "document.hpp"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>

namespace p5 {

namespace {
const Geometry kEmpty;

Geometry makeBox(double sx, double sy, double sz) {
    Geometry g;
    for (int i = 0; i < 8; ++i) {
        g.v.push_back((i & 1) ? sx : 0);
        g.v.push_back((i & 2) ? sy : 0);
        g.v.push_back((i & 4) ? sz : 0);
    }
    g.t = {0, 2, 1, 1, 2, 3, 4, 5, 6, 5, 7, 6, 0, 1, 4, 1, 5, 4,
           2, 6, 3, 3, 6, 7, 0, 4, 2, 2, 4, 6, 1, 3, 5, 3, 7, 5};
    return g;
}
}  // namespace

const Geometry& Document::current() const {
    return cursor_ ? *ops_[cursor_ - 1].result : kEmpty;
}

void Document::commit(OpRecord r) {
    ops_.resize(cursor_);  // a new step discards the redo branch
    ops_.push_back(std::move(r));
    cursor_ = ops_.size();
}

Outcome Document::applyNative(const QString& op, const QJsonObject& p) {
    Geometry g;
    if (op == "box") {
        const double sx = p.value("sx").toDouble(), sy = p.value("sy").toDouble(),
                     sz = p.value("sz").toDouble();
        if (!(sx > 0 && sy > 0 && sz > 0)) return {false, "box sizes must be positive (mm)"};
        g = makeBox(sx, sy, sz);
    } else if (op == "translate") {
        g = current();
        if (g.v.empty()) return {false, "nothing to translate"};
        const double d[3] = {p.value("dx").toDouble(), p.value("dy").toDouble(),
                             p.value("dz").toDouble()};
        for (size_t i = 0; i < g.v.size(); ++i) g.v[i] += d[i % 3];
    } else {
        return {false, "unknown native operation '" + op + "'"};
    }
    if (const QString why = validate(g); !why.isEmpty()) return {false, why};
    OpRecord r;
    r.kind = "native";
    r.op = op;
    r.params = p;
    r.result = std::make_shared<const Geometry>(std::move(g));
    commit(std::move(r));
    return {true, {}, 0};
}

Outcome Document::applyPlugin(const PluginManager& pm, const QString& plugin, const QString& op,
                              const QJsonObject& params, int timeoutMs) {
    const CallResult c = pm.call(plugin, op, params, current(), timeoutMs);
    if (!c.ok) return {false, c.error, c.ms};   // nothing was committed
    OpRecord r;
    r.kind = "plugin";
    r.plugin = plugin;
    r.pluginVersion = pm.find(plugin)->version;
    r.op = op;
    r.params = params;
    r.result = std::make_shared<const Geometry>(c.geometry);
    commit(std::move(r));
    return {true, {}, c.ms};
}

bool Document::undo() {
    if (!cursor_) return false;
    --cursor_;
    return true;
}
bool Document::redo() {
    if (cursor_ >= ops_.size()) return false;
    ++cursor_;
    return true;
}

Outcome Document::save(const QString& path) const {
    QJsonArray ops;
    QJsonObject plugins;
    for (const OpRecord& r : ops_) {
        QJsonObject o{{"kind", r.kind}, {"op", r.op}, {"params", r.params},
                      {"result", toJson(*r.result)}};
        if (r.kind == "plugin") {
            o["plugin"] = r.plugin;
            o["pluginVersion"] = r.pluginVersion;
            plugins[r.plugin] = r.pluginVersion;
        }
        ops.append(o);
    }
    const QJsonObject root{{"format", "meshify-p5"}, {"version", 1}, {"units", "mm"},
                           {"cursor", static_cast<qint64>(cursor_)},
                           {"plugins", plugins}, {"ops", ops}};
    QSaveFile f(path);  // writes a temporary file and renames it on commit()
    if (!f.open(QIODevice::WriteOnly)) return {false, "cannot write " + path};
    f.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
    if (!f.commit()) return {false, "writing " + path + " failed; the old file is unchanged"};
    return {true, {}, 0};
}

Outcome Document::open(const QString& path, Document& out) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {false, "cannot read " + path};
    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &pe);
    if (pe.error != QJsonParseError::NoError || !doc.isObject())
        return {false, "file is damaged (not valid JSON)"};
    const QJsonObject root = doc.object();
    if (root.value("format").toString() != "meshify-p5" || root.value("version").toInt() != 1)
        return {false, "not a version 1 meshify-p5 file"};
    Document d;
    for (const QJsonValue& v : root.value("ops").toArray()) {
        const QJsonObject o = v.toObject();
        OpRecord r;
        r.kind = o.value("kind").toString();
        r.op = o.value("op").toString();
        r.params = o.value("params").toObject();
        r.plugin = o.value("plugin").toString();
        r.pluginVersion = o.value("pluginVersion").toString();
        Geometry g;
        QString why;
        if (!fromJson(o.value("result").toObject(), g, why))
            return {false, QString("step %1 ('%2') has bad geometry: %3")
                               .arg(d.ops_.size() + 1).arg(r.op, why)};
        r.result = std::make_shared<const Geometry>(std::move(g));
        d.ops_.push_back(std::move(r));
    }
    const qint64 c = root.value("cursor").toInteger(-1);
    if (c < 0 || static_cast<size_t>(c) > d.ops_.size()) return {false, "history cursor is out of range"};
    d.cursor_ = static_cast<size_t>(c);
    out = std::move(d);
    return {true, {}, 0};
}

std::vector<StepStatus> Document::status(const PluginManager& pm) const {
    std::vector<StepStatus> s(ops_.size());
    for (size_t i = 0; i < ops_.size(); ++i) {
        const OpRecord& r = ops_[i];
        if (r.kind != "plugin") continue;
        const PluginInfo* p = pm.find(r.plugin);
        if (!p) {
            s[i] = {StepStatus::PluginMissing,
                    QString("plugin '%1' %2 is not installed; this step keeps its saved result "
                            "but cannot be recomputed or edited").arg(r.plugin, r.pluginVersion)};
        } else if (!p->operations.contains(r.op)) {
            s[i] = {StepStatus::OperationMissing,
                    QString("plugin '%1' %2 no longer has operation '%3'")
                        .arg(r.plugin, p->version, r.op)};
        } else if (p->version != r.pluginVersion) {
            s[i] = {StepStatus::VersionDiffers,
                    QString("saved with plugin '%1' %2, installed version is %3")
                        .arg(r.plugin, r.pluginVersion, p->version)};
        }
    }
    return s;
}

Document::Replay Document::replay(const PluginManager& pm) const {
    Document scratch;
    for (size_t i = 0; i < ops_.size(); ++i) {
        const OpRecord& r = ops_[i];
        const Outcome o = r.kind == "native"
                              ? scratch.applyNative(r.op, r.params)
                              : scratch.applyPlugin(pm, r.plugin, r.op, r.params);
        if (!o.ok) return {false, i, QString("step %1 ('%2'): %3").arg(i + 1).arg(r.op, o.message)};
        if (scratch.current() != *r.result)
            return {false, i, QString("step %1 ('%2') gives a different result than the saved one")
                                  .arg(i + 1).arg(r.op)};
    }
    return {true, ops_.size(), {}};
}

}  // namespace p5
