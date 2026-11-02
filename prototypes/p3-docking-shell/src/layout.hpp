// Workspace layout handling for the docking shell (acceptance scenario UX-03).
//
// Wraps the Qt Advanced Docking System (ADS) manager and adds what the brief
// asks for and ADS does not do: a layout file that records which screens it was
// saved on, recovery of floating panels whose screen is gone, placeholders for
// panels from plugins that are not installed, and an import preview that lists
// every shortcut change before anything is applied.
#pragma once

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QMap>
#include <QRect>
#include <QString>
#include <QStringList>
#include <functional>

class QWidget;
namespace ads {
class CDockManager;
class CDockWidget;
}  // namespace ads

namespace meshify::p3 {

struct ScreenInfo {
    QString name;
    QRect available;  // usable area in virtual desktop coordinates
    bool primary = false;
};

using ScreenProvider = std::function<QList<ScreenInfo>()>;
QList<ScreenInfo> systemScreens();

// True if enough of the title bar is on some screen to grab and drag the panel.
bool titleBarReachable(const QRect& frame, const QList<ScreenInfo>& screens);

// The frame to use for a floating panel: unchanged if reachable, otherwise moved
// onto the primary screen (cascaded by `index`) and shrunk to fit.
QRect rescueRect(const QRect& frame, const QList<ScreenInfo>& screens, int index);

struct PanelSpec {
    QString id;      // stable identifier, also the dock widget's object name
    QString title;
    QString plugin;  // empty for built-in panels
};

struct ShortcutChange {
    enum class Kind { Added, Changed, Same, Conflict };
    Kind kind = Kind::Same;
    QString command;
    QString oldKey;
    QString newKey;
    QString conflictsWith;  // other command that already uses newKey
};

// Compares imported shortcuts with the current ones. Nothing is applied here.
QList<ShortcutChange> previewShortcuts(const QMap<QString, QString>& current,
                                       const QMap<QString, QString>& imported);

// Applies the accepted changes. Conflicts are never applied; they stay listed in
// `skipped` so the caller can show them.
QMap<QString, QString> applyShortcuts(const QMap<QString, QString>& current,
                                      const QList<ShortcutChange>& changes,
                                      const QStringList& accepted, QStringList* skipped = nullptr);

struct ImportPreview {
    bool valid = false;
    QString error;
    QString layoutName;
    QStringList savedScreens;
    QStringList missingScreens;  // saved on screens that are not connected now
    QStringList missingPanels;   // from plugins (or builds) that are not available
    QList<ShortcutChange> shortcuts;
    QByteArray layoutJson;       // kept so commit() needs no second read
};

struct FloatingRescue {
    QString panel;
    QRect from;
    QRect to;
};

struct RestoreReport {
    bool applied = false;
    bool fellBackToDefault = false;
    QStringList placeholders;
    QList<FloatingRescue> rescued;
    QMap<QString, QString> shortcuts;  // shortcuts after the import
    QStringList skippedShortcuts;      // conflicts that were not applied
};

class LayoutManager {
public:
    explicit LayoutManager(ads::CDockManager* manager);

    void setScreenProvider(ScreenProvider provider) { m_screens = std::move(provider); }

    // Panels the application knows how to build, by id.
    void registerPanel(const PanelSpec& spec, ads::CDockWidget* widget);
    // Remembers the current arrangement as the one "Reset layout" returns to.
    void captureDefault();
    bool resetToDefault();

    QByteArray save(const QString& name, const QMap<QString, QString>& shortcuts) const;
    ImportPreview preview(const QByteArray& file, const QMap<QString, QString>& currentShortcuts) const;
    // Applies a previewed layout. Only the shortcut commands in `acceptedShortcuts` change.
    RestoreReport commit(const ImportPreview& preview, const QMap<QString, QString>& currentShortcuts,
                         const QStringList& acceptedShortcuts);

    // Moves any floating panel whose title bar is off every screen back on screen.
    // Returns the panels that ended up somewhere other than `savedFrames` says (or,
    // without it, than they were), whether this code or Qt moved them.
    QList<FloatingRescue> rescueFloatingPanels(const QHash<QString, QRect>& savedFrames = {});

private:
    ads::CDockManager* m_manager;
    ScreenProvider m_screens;
    QHash<QString, PanelSpec> m_panels;
    QByteArray m_default;
};

}  // namespace meshify::p3
