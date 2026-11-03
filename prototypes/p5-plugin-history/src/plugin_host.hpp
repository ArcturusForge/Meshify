#pragma once
// Runs Python plugin operations in a separate process. The app sends one JSON
// request on stdin and reads one JSON reply from stdout. A plugin that raises,
// crashes, hangs or returns rubbish therefore cannot touch the document: the
// caller only ever receives a validated Geometry or an error message.
#include "geometry.hpp"

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <map>

namespace p5 {

struct PluginInfo {
    QString name, version, dir, entry;
    QStringList operations;
};

struct CallResult {
    bool ok = false;
    Geometry geometry;
    QString error;   // what went wrong, in words a user could act on
    double ms = 0;   // wall time of the whole call
};

// How geometry travels between the app and the plugin process.
enum class Transport {
    Json,    // API 1: vertices and triangles as JSON arrays on the pipe
    Binary,  // API 2: raw binary files; the plugin maps the input, nothing is parsed
};

class PluginManager {
public:
    // `hostScript` is meshify_host.py. An empty `python` searches PATH
    // (MESHIFY_PYTHON overrides).
    explicit PluginManager(QString hostScript, QString python = {});

    // Reads <root>/<plugin>/plugin.json for every plugin folder.
    // Returns the number of plugins found.
    int scan(const QString& root);

    const PluginInfo* find(const QString& name) const;
    void setTransport(Transport t) { transport_ = t; }
    Transport transport() const { return transport_; }
    bool pythonFound() const { return !python_.isEmpty(); }
    QString python() const { return python_; }

    CallResult call(const QString& plugin, const QString& op, const QJsonObject& params,
                    const Geometry& input, int timeoutMs = 10000) const;

private:
    QString host_, python_;
    Transport transport_ = Transport::Binary;
    std::map<QString, PluginInfo> plugins_;
};

}  // namespace p5
