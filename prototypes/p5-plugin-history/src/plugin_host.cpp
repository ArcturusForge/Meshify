#include "plugin_host.hpp"

#include "geometry_io.hpp"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>

namespace p5 {

PluginManager::PluginManager(QString hostScript, QString python)
    : host_(std::move(hostScript)), python_(std::move(python)) {
    if (python_.isEmpty()) python_ = qEnvironmentVariable("MESHIFY_PYTHON");
    // On Windows "python3" can be a Microsoft Store stub, so look for "python" first.
#ifdef Q_OS_WIN
    for (const char* n : {"python", "python3"})
#else
    for (const char* n : {"python3", "python"})
#endif
        if (python_.isEmpty()) python_ = QStandardPaths::findExecutable(n);
}

int PluginManager::scan(const QString& root) {
    int found = 0;
    for (const QFileInfo& d : QDir(root).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        QFile f(QDir(d.absoluteFilePath()).filePath("plugin.json"));
        if (!f.open(QIODevice::ReadOnly)) continue;
        const QJsonObject m = QJsonDocument::fromJson(f.readAll()).object();
        PluginInfo p;
        p.name = m.value("name").toString();
        p.version = m.value("version").toString();
        p.entry = QDir(d.absoluteFilePath()).filePath(m.value("entry").toString("plugin.py"));
        p.dir = d.absoluteFilePath();
        for (const QJsonValue& o : m.value("operations").toArray()) p.operations << o.toString();
        if (p.name.isEmpty() || p.version.isEmpty() || !QFile::exists(p.entry)) continue;
        plugins_[p.name] = p;
        ++found;
    }
    return found;
}

const PluginInfo* PluginManager::find(const QString& name) const {
    auto it = plugins_.find(name);
    return it == plugins_.end() ? nullptr : &it->second;
}

CallResult PluginManager::call(const QString& plugin, const QString& op, const QJsonObject& params,
                               const Geometry& input, int timeoutMs) const {
    CallResult r;
    QElapsedTimer clock;
    clock.start();
    auto fail = [&](QString why) {
        r.ok = false;
        r.error = std::move(why);
        r.ms = clock.nsecsElapsed() / 1e6;
        return r;
    };
    const PluginInfo* p = find(plugin);
    if (!p) return fail(QString("plugin '%1' is not installed").arg(plugin));
    if (!p->operations.contains(op))
        return fail(QString("plugin '%1' %2 has no operation '%3'").arg(plugin, p->version, op));
    if (python_.isEmpty()) return fail("no Python interpreter found (set MESHIFY_PYTHON)");

    const bool bin = transport_ == Transport::Binary;
    QTemporaryDir tmp;  // holds the binary files; removed when the call ends
    QJsonObject req{{"entry", p->entry}, {"op", op}, {"params", params}};
    const QString inFile = tmp.filePath("in.msh"), outFile = tmp.filePath("out.msh");
    if (bin) {
        QString why;
        if (!tmp.isValid() || !writeBinary(inFile, input, why))
            return fail("could not prepare the geometry for the plugin: " + why);
        req["api"] = 2;
        req["input_file"] = inFile;
        req["output_file"] = outFile;
    } else {
        req["api"] = 1;
        req["geometry"] = toJson(input);
    }
    QProcess proc;
    proc.setProcessChannelMode(QProcess::SeparateChannels);
    proc.start(python_, {host_});
    if (!proc.waitForStarted(5000)) return fail("could not start the Python interpreter");
    proc.write(QJsonDocument(req).toJson(QJsonDocument::Compact));
    proc.closeWriteChannel();
    if (!proc.waitForFinished(timeoutMs)) {
        proc.kill();
        proc.waitForFinished(2000);
        return fail(QString("plugin operation '%1' did not finish within %2 s and was stopped")
                        .arg(op).arg(timeoutMs / 1000.0));
    }
    if (proc.exitStatus() != QProcess::NormalExit || proc.exitCode() != 0)
        return fail(QString("plugin process died (exit code %1)").arg(proc.exitCode()));

    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(proc.readAllStandardOutput(), &pe);
    if (pe.error != QJsonParseError::NoError || !doc.isObject())
        return fail("plugin returned a reply that is not valid JSON");
    const QJsonObject o = doc.object();
    if (!o.value("ok").toBool()) return fail(o.value("error").toString("plugin reported a failure"));
    QString why;
    if (bin) {
        if (!readBinary(outFile, r.geometry, why)) return fail(why);
    } else if (!fromJson(o.value("geometry").toObject(), r.geometry, why)) {
        return fail("plugin result rejected: " + why);
    }
    r.ok = true;
    r.ms = clock.nsecsElapsed() / 1e6;
    return r;
}

}  // namespace p5
