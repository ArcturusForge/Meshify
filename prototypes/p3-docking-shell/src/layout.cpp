#include "layout.hpp"

#include <DockAreaWidget.h>
#include <DockContainerWidget.h>
#include <DockManager.h>
#include <DockWidget.h>
#include <FloatingDockContainer.h>

#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QScreen>

#include <algorithm>

namespace meshify::p3 {

namespace {
constexpr int kFormatVersion = 1;
constexpr int kTitleStrip = 40;     // height of the part of the frame that counts as the title bar
constexpr int kMinGrabWidth = 80;   // visible width needed to grab it
constexpr int kMinGrabHeight = 20;
}  // namespace

QList<ScreenInfo> systemScreens() {
    QList<ScreenInfo> out;
    const QScreen* primary = QGuiApplication::primaryScreen();
    for (const QScreen* s : QGuiApplication::screens()) {
        out.push_back({s->name(), s->availableGeometry(), s == primary});
    }
    return out;
}

bool titleBarReachable(const QRect& frame, const QList<ScreenInfo>& screens) {
    const QRect strip(frame.left(), frame.top(), frame.width(), kTitleStrip);
    return std::any_of(screens.begin(), screens.end(), [&](const ScreenInfo& s) {
        const QRect visible = strip.intersected(s.available);
        return visible.width() >= kMinGrabWidth && visible.height() >= kMinGrabHeight;
    });
}

QRect rescueRect(const QRect& frame, const QList<ScreenInfo>& screens, int index) {
    if (screens.isEmpty() || titleBarReachable(frame, screens)) return frame;
    auto it = std::find_if(screens.begin(), screens.end(), [](const ScreenInfo& s) { return s.primary; });
    const ScreenInfo& target = it != screens.end() ? *it : screens.first();
    const QSize room(target.available.width() * 4 / 5, target.available.height() * 4 / 5);
    const QSize size = frame.size().boundedTo(room);
    const int step = 28 * (index % 8);
    return QRect(target.available.topLeft() + QPoint(60 + step, 60 + step), size);
}

QList<ShortcutChange> previewShortcuts(const QMap<QString, QString>& current,
                                       const QMap<QString, QString>& imported) {
    QMap<QString, QString> result = current;
    QList<ShortcutChange> out;
    for (auto it = imported.begin(); it != imported.end(); ++it) {
        ShortcutChange c;
        c.command = it.key();
        c.newKey = it.value();
        if (!current.contains(it.key())) {
            c.kind = ShortcutChange::Kind::Added;
        } else if (current[it.key()] == it.value()) {
            c.kind = ShortcutChange::Kind::Same;
            c.oldKey = it.value();
        } else {
            c.kind = ShortcutChange::Kind::Changed;
            c.oldKey = current[it.key()];
        }
        out.push_back(c);
        result[it.key()] = it.value();
    }
    // A key used by two commands after the import is a conflict for the imported one.
    for (ShortcutChange& c : out) {
        if (c.kind == ShortcutChange::Kind::Same || c.newKey.isEmpty()) continue;
        for (auto it = result.begin(); it != result.end(); ++it) {
            if (it.key() != c.command && it.value() == c.newKey) {
                c.kind = ShortcutChange::Kind::Conflict;
                c.conflictsWith = it.key();
                break;
            }
        }
    }
    return out;
}

QMap<QString, QString> applyShortcuts(const QMap<QString, QString>& current,
                                      const QList<ShortcutChange>& changes,
                                      const QStringList& accepted, QStringList* skipped) {
    QMap<QString, QString> out = current;
    for (const ShortcutChange& c : changes) {
        if (!accepted.contains(c.command)) continue;
        if (c.kind == ShortcutChange::Kind::Conflict) {
            if (skipped) skipped->push_back(c.command);
            continue;
        }
        if (c.kind != ShortcutChange::Kind::Same) out[c.command] = c.newKey;
    }
    return out;
}

LayoutManager::LayoutManager(ads::CDockManager* manager) : m_manager(manager), m_screens(systemScreens) {}

void LayoutManager::registerPanel(const PanelSpec& spec, ads::CDockWidget* widget) {
    widget->setObjectName(spec.id);
    m_panels.insert(spec.id, spec);
}

void LayoutManager::captureDefault() { m_default = m_manager->saveState(kFormatVersion); }

bool LayoutManager::resetToDefault() {
    if (m_default.isEmpty()) return false;
    const bool ok = m_manager->restoreState(m_default, kFormatVersion);
    rescueFloatingPanels();
    return ok;
}

QByteArray LayoutManager::save(const QString& name, const QMap<QString, QString>& shortcuts) const {
    QJsonObject root;
    root["format"] = "meshify-layout";
    root["version"] = kFormatVersion;
    root["name"] = name;

    QJsonArray screens;
    for (const ScreenInfo& s : m_screens()) {
        screens.push_back(QJsonObject{{"name", s.name},
                                      {"x", s.available.x()},
                                      {"y", s.available.y()},
                                      {"w", s.available.width()},
                                      {"h", s.available.height()},
                                      {"primary", s.primary}});
    }
    root["screens"] = screens;

    QJsonArray panels;
    for (const PanelSpec& p : m_panels) {
        panels.push_back(QJsonObject{{"id", p.id}, {"title", p.title}, {"plugin", p.plugin}});
    }
    root["panels"] = panels;

    // Where each floating panel was. The ADS state holds this too, but only as an
    // opaque blob, and we need it to tell the user which panels had to move.
    QJsonArray floating;
    for (ads::CFloatingDockContainer* f : m_manager->floatingWidgets()) {
        const QRect r = f->frameGeometry();
        QJsonArray ids;
        for (ads::CDockWidget* d : f->dockContainer()->openedDockWidgets()) ids.push_back(d->objectName());
        floating.push_back(QJsonObject{{"panels", ids}, {"x", r.x()}, {"y", r.y()}, {"w", r.width()}, {"h", r.height()}});
    }
    root["floating"] = floating;
    root["state"] = QString::fromLatin1(m_manager->saveState(kFormatVersion).toBase64());

    QJsonObject keys;
    for (auto it = shortcuts.begin(); it != shortcuts.end(); ++it) keys[it.key()] = it.value();
    root["shortcuts"] = keys;
    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

ImportPreview LayoutManager::preview(const QByteArray& file, const QMap<QString, QString>& currentShortcuts) const {
    ImportPreview p;
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(file, &err);
    if (doc.isNull() || !doc.isObject()) {
        p.error = "Not a layout file: " + err.errorString();
        return p;
    }
    const QJsonObject root = doc.object();
    if (root["format"].toString() != "meshify-layout") {
        p.error = "Not a Meshify layout file";
        return p;
    }
    if (root["version"].toInt() > kFormatVersion) {
        p.error = "This layout was saved by a newer version of Meshify";
        return p;
    }
    p.layoutName = root["name"].toString();

    const QList<ScreenInfo> now = m_screens();
    for (const QJsonValue& v : root["screens"].toArray()) {
        const QJsonObject o = v.toObject();
        const QString name = o["name"].toString();
        p.savedScreens.push_back(name);
        const bool connected = std::any_of(now.begin(), now.end(), [&](const ScreenInfo& s) { return s.name == name; });
        if (!connected) p.missingScreens.push_back(name);
    }
    for (const QJsonValue& v : root["panels"].toArray()) {
        const QJsonObject o = v.toObject();
        if (!m_panels.contains(o["id"].toString())) p.missingPanels.push_back(o["title"].toString());
    }
    QMap<QString, QString> imported;
    const QJsonObject keys = root["shortcuts"].toObject();
    for (auto it = keys.begin(); it != keys.end(); ++it) imported[it.key()] = it.value().toString();
    p.shortcuts = previewShortcuts(currentShortcuts, imported);
    p.layoutJson = file;
    p.valid = true;
    return p;
}

RestoreReport LayoutManager::commit(const ImportPreview& preview, const QMap<QString, QString>& currentShortcuts,
                                    const QStringList& acceptedShortcuts) {
    RestoreReport report;
    report.shortcuts = currentShortcuts;
    if (!preview.valid) return report;
    const QJsonObject root = QJsonDocument::fromJson(preview.layoutJson).object();

    // Panels this build cannot make get a visible placeholder, so the layout keeps
    // their place and saving it again does not lose them.
    for (const QJsonValue& v : root["panels"].toArray()) {
        const QJsonObject o = v.toObject();
        const QString id = o["id"].toString();
        if (m_panels.contains(id) || m_manager->findDockWidget(id)) continue;
        const QString plugin = o["plugin"].toString();
        m_panels.insert(id, PanelSpec{id, o["title"].toString(), plugin});
        auto* dock = new ads::CDockWidget(QString("Unavailable: ") + o["title"].toString());
        dock->setObjectName(id);
        dock->setWidget(new QLabel(plugin.isEmpty() ? "This panel is not available in this version."
                                                    : QString("This panel comes from the plugin \"%1\", which is not installed.\n"
                                                              "Your layout keeps its place.")
                                                          .arg(plugin)));
        m_manager->addDockWidgetTab(ads::CenterDockWidgetArea, dock);
        report.placeholders.push_back(o["title"].toString());
    }

    const QByteArray state = QByteArray::fromBase64(root["state"].toString().toLatin1());
    if (!m_manager->restoreState(state, kFormatVersion)) {
        report.fellBackToDefault = resetToDefault();
        return report;
    }
    report.applied = true;
    QHash<QString, QRect> savedFrames;
    for (const QJsonValue& v : root["floating"].toArray()) {
        const QJsonObject o = v.toObject();
        const QRect r(o["x"].toInt(), o["y"].toInt(), o["w"].toInt(), o["h"].toInt());
        for (const QJsonValue& id : o["panels"].toArray()) savedFrames.insert(id.toString(), r);
    }
    report.rescued = rescueFloatingPanels(savedFrames);
    report.shortcuts = applyShortcuts(currentShortcuts, preview.shortcuts, acceptedShortcuts, &report.skippedShortcuts);
    return report;
}

QList<FloatingRescue> LayoutManager::rescueFloatingPanels(const QHash<QString, QRect>& savedFrames) {
    constexpr int kMovedTolerance = 8;  // window managers shift frames by a few pixels
    QList<FloatingRescue> out;
    const QList<ScreenInfo> screens = m_screens();
    int index = 0;
    for (ads::CFloatingDockContainer* floating : m_manager->floatingWidgets()) {
        QRect frame = floating->frameGeometry();
        const QRect target = rescueRect(frame, screens, index);
        if (target != frame) {
            floating->setGeometry(target);
            ++index;
        }
        const auto opened = floating->dockContainer()->openedDockWidgets();
        const QString id = opened.isEmpty() ? QString() : opened.first()->objectName();
        const QString title = opened.isEmpty() ? QString("Panel") : opened.first()->windowTitle();
        const QRect from = savedFrames.contains(id) ? savedFrames.value(id) : frame;
        const bool moved = (target.topLeft() - from.topLeft()).manhattanLength() > kMovedTolerance;
        if (moved) out.push_back({title, from, target});
    }
    return out;
}

}  // namespace meshify::p3
