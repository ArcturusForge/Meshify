// P3 acceptance tests for UX-03: a layout saved with panels on a monitor that is
// no longer connected must open with every panel reachable, and shortcut changes
// from an imported layout must be shown and accepted explicitly.
//
// Screens are simulated through LayoutManager::setScreenProvider, so the tests
// run on one machine (or the offscreen Qt platform in CI). They cannot prove how
// each operating system's window manager places a window; see the README.
#include "shell.hpp"

#include <DockManager.h>
#include <DockWidget.h>
#include <FloatingDockContainer.h>

#include <QApplication>
#include <QGuiApplication>
#include <QScreen>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>

#include <functional>
#include <iostream>
#include <memory>
#include <vector>

using namespace meshify::p3;

namespace {

int failures = 0;

#define CHECK(cond)                                                                    \
    do {                                                                               \
        if (!(cond)) {                                                                 \
            ++failures;                                                                \
            std::cerr << "  CHECK failed at line " << __LINE__ << ": " #cond << "\n"; \
        }                                                                              \
    } while (0)

// Fixed layout for the pure-function tests: two 1920x1040 screens side by side.
const QList<ScreenInfo> kTwoScreens = {{"DP-1", QRect(0, 0, 1920, 1040), true},
                                       {"DP-2", QRect(1920, 0, 1920, 1040), false}};
const QList<ScreenInfo> kOneScreen = {{"DP-1", QRect(0, 0, 1920, 1040), true}};

// Screens for the widget tests. DP-1 is the real (or offscreen) primary screen, so
// Qt's own placement agrees with the simulation; DP-2 is an imaginary monitor to its right.
QRect realScreen() { return QGuiApplication::primaryScreen()->availableGeometry(); }
QList<ScreenInfo> deskScreens() {
    const QRect r = realScreen();
    return {{"DP-1", r, true}, {"DP-2", r.translated(r.width(), 0), false}};
}
QList<ScreenInfo> laptopScreens() { return {{"DP-1", realScreen(), true}}; }

std::unique_ptr<Shell> makeShell(const QList<ScreenInfo>& screens) {
    auto shell = std::make_unique<Shell>();
    shell->layout().setScreenProvider([screens] { return screens; });
    return shell;
}

ads::CFloatingDockContainer* floatPanel(Shell& shell, const QString& id, const QRect& frame) {
    ads::CDockWidget* dock = shell.dock()->findDockWidget(id);
    if (!dock) return nullptr;
    ads::CFloatingDockContainer* floating = shell.dock()->addDockWidgetFloating(dock);
    if (floating) floating->setGeometry(frame);
    return floating;
}

void rescuePlacement() {
    CHECK(titleBarReachable(QRect(100, 100, 600, 400), kOneScreen));
    CHECK(!titleBarReachable(QRect(2400, 200, 600, 400), kOneScreen));
    CHECK(titleBarReachable(QRect(2400, 200, 600, 400), kTwoScreens));
    // Only 30 px of the title bar left on screen: too little to grab.
    CHECK(!titleBarReachable(QRect(1890, 200, 600, 400), kOneScreen));
    // Above the top edge: the title bar is gone.
    CHECK(!titleBarReachable(QRect(100, -300, 600, 400), kOneScreen));

    const QRect keep(100, 100, 600, 400);
    CHECK(rescueRect(keep, kOneScreen, 0) == keep);

    const QRect moved = rescueRect(QRect(2400, 200, 600, 400), kOneScreen, 0);
    CHECK(kOneScreen[0].available.contains(moved));
    CHECK(moved.size() == QSize(600, 400));
    CHECK(rescueRect(QRect(2400, 200, 600, 400), kOneScreen, 1).topLeft() != moved.topLeft());

    const QRect huge = rescueRect(QRect(4000, 0, 5000, 3000), kOneScreen, 0);
    CHECK(kOneScreen[0].available.contains(huge));
    // With no screen information at all, leave the panel alone.
    CHECK(rescueRect(QRect(2400, 200, 600, 400), {}, 0) == QRect(2400, 200, 600, 400));
}

void defaultLayout() {
    auto shell = makeShell(deskScreens());
    const ImportPreview p = shell->layout().preview(shell->layout().save("Default", shell->shortcuts()), shell->shortcuts());
    CHECK(p.valid);
    CHECK(p.missingPanels.isEmpty());
    CHECK(p.missingScreens.isEmpty());

    // Pop two panels out, then reset: nothing floating remains.
    CHECK(floatPanel(*shell, "history", QRect(300, 300, 500, 300)) != nullptr);
    CHECK(floatPanel(*shell, "outliner", QRect(2400, 300, 400, 500)) != nullptr);
    CHECK(shell->dock()->floatingWidgets().size() == 2);
    CHECK(shell->layout().resetToDefault());
    // ADS deletes the emptied floating windows once the event loop runs.
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    CHECK(shell->dock()->floatingWidgets().isEmpty());
    CHECK(shell->dock()->dockWidgetsMap().size() == 6);
}

// UX-03: the layout was saved with panels on DP-2; DP-2 is not connected now.
void layoutFromDisconnectedMonitor() {
    const QRect dp1 = realScreen();
    const QRect dp2 = dp1.translated(dp1.width(), 0);
    QByteArray file;
    {
        auto office = makeShell(deskScreens());
        CHECK(floatPanel(*office, "history", QRect(dp2.left() + 100, 100, 300, 200)) != nullptr);
        CHECK(floatPanel(*office, "outliner", QRect(dp2.left() + 300, 250, 250, 300)) != nullptr);
        CHECK(floatPanel(*office, "tasks", QRect(dp1.left() + 40, 60, 300, 150)) != nullptr);  // stays on DP-1
        file = office->layout().save("Two monitors", office->shortcuts());
    }

    auto laptop = makeShell(laptopScreens());
    const ImportPreview preview = laptop->layout().preview(file, laptop->shortcuts());
    CHECK(preview.valid);
    CHECK(preview.missingScreens == QStringList{"DP-2"});

    const RestoreReport report = laptop->layout().commit(preview, laptop->shortcuts(), {});
    CHECK(report.applied);
    CHECK(!report.fellBackToDefault);
    // The user is told about exactly the two panels that had to move.
    CHECK(report.rescued.size() == 2);

    // Every floating panel, including the one that never moved, can be grabbed.
    const auto floating = laptop->dock()->floatingWidgets();
    CHECK(floating.size() == 3);
    for (ads::CFloatingDockContainer* f : floating) {
        CHECK(titleBarReachable(f->frameGeometry(), laptopScreens()));
    }
    CHECK(laptop->dock()->dockWidgetsMap().size() == 6);

    // The saved file recorded the two panels on DP-2, so the test premise holds.
    const QJsonArray saved = QJsonDocument::fromJson(file).object()["floating"].toArray();
    int onDp2 = 0;
    for (const QJsonValue& v : saved) onDp2 += v.toObject()["x"].toInt() >= dp2.left() ? 1 : 0;
    CHECK(onDp2 == 2);
}

// Qt moves restored windows onto a connected screen on most platforms, but not
// everywhere (for example a window manager may place windows itself). The
// fallback must still catch a panel that is left off screen.
void panelLeftOffScreenByThePlatform() {
    auto shell = makeShell(laptopScreens());
    ads::CFloatingDockContainer* f = floatPanel(*shell, "history", QRect(100, 100, 300, 200));
    CHECK(f != nullptr);
    if (!f) return;
    const QRect far = QRect(realScreen().right() + 600, 120, 300, 200);
    f->setGeometry(far);
    if (f->frameGeometry().topLeft() != far.topLeft()) {
        std::cout << "  (platform clamps window positions itself; fallback not exercised)\n";
        return;
    }
    const auto moved = shell->layout().rescueFloatingPanels();
    CHECK(moved.size() == 1);
    CHECK(titleBarReachable(f->frameGeometry(), laptopScreens()));
}

void missingPluginPanel() {
    QByteArray file;
    {
        auto full = makeShell(laptopScreens());
        auto* dock = new ads::CDockWidget("Brush pack");
        dock->setWidget(new QLabel("brushes"));
        full->layout().registerPanel({"plugin.brushpack", "Brush pack", "Brush Pack"}, dock);
        full->dock()->addDockWidget(ads::RightDockWidgetArea, dock);
        file = full->layout().save("With plugin", full->shortcuts());
    }
    auto bare = makeShell(laptopScreens());
    const ImportPreview preview = bare->layout().preview(file, bare->shortcuts());
    CHECK(preview.valid);
    CHECK(preview.missingPanels == QStringList{"Brush pack"});

    const RestoreReport report = bare->layout().commit(preview, bare->shortcuts(), {});
    CHECK(report.applied);
    CHECK(report.placeholders == QStringList{"Brush pack"});
    ads::CDockWidget* placeholder = bare->dock()->findDockWidget("plugin.brushpack");
    CHECK(placeholder != nullptr);
    CHECK(placeholder && placeholder->windowTitle().startsWith("Unavailable"));
    CHECK(bare->dock()->dockWidgetsMap().size() == 7);

    // Saving again keeps the panel, so installing the plugin later finds its place.
    const ImportPreview again = bare->layout().preview(bare->layout().save("Round trip", bare->shortcuts()), bare->shortcuts());
    CHECK(again.valid);
    CHECK(again.missingPanels.isEmpty());
}

void shortcutPreviewIsExplicit() {
    const QMap<QString, QString> current = {{"view.frameAll", "Home"}, {"view.frameSelection", "F"}, {"edit.undo", "Ctrl+Z"}, {"tool.fillet", "Ctrl+Shift+F"}};
    const QMap<QString, QString> imported = {{"view.frameAll", "Home"},       // same
                                             {"view.frameSelection", "G"},    // changed
                                             {"tool.chamfer", "Ctrl+B"},      // added
                                             {"tool.shell", "Ctrl+Z"}};       // conflicts with edit.undo
    const auto changes = previewShortcuts(current, imported);
    auto find = [&](const QString& cmd) {
        for (const auto& c : changes) {
            if (c.command == cmd) return c;
        }
        return ShortcutChange{};
    };
    CHECK(find("view.frameAll").kind == ShortcutChange::Kind::Same);
    CHECK(find("view.frameSelection").kind == ShortcutChange::Kind::Changed);
    CHECK(find("view.frameSelection").oldKey == "F");
    CHECK(find("tool.chamfer").kind == ShortcutChange::Kind::Added);
    CHECK(find("tool.shell").kind == ShortcutChange::Kind::Conflict);
    CHECK(find("tool.shell").conflictsWith == "edit.undo");

    // Accepting nothing changes nothing.
    CHECK(applyShortcuts(current, changes, {}) == current);

    // Accepting some changes only those; a conflict is reported and never applied.
    QStringList skipped;
    const auto result = applyShortcuts(current, changes, {"view.frameSelection", "tool.shell"}, &skipped);
    CHECK(result["view.frameSelection"] == "G");
    CHECK(!result.contains("tool.chamfer"));
    CHECK(!result.contains("tool.shell"));
    CHECK(skipped == QStringList{"tool.shell"});
    CHECK(result["edit.undo"] == "Ctrl+Z");
}

void importLeavesShortcutsAloneUntilAccepted() {
    QByteArray file;
    {
        auto other = makeShell(laptopScreens());
        other->shortcuts()["view.frameSelection"] = "G";
        file = other->layout().save("Other", other->shortcuts());
    }
    auto me = makeShell(laptopScreens());
    const ImportPreview preview = me->layout().preview(file, me->shortcuts());
    CHECK(preview.valid);
    const RestoreReport none = me->layout().commit(preview, me->shortcuts(), {});
    CHECK(none.applied);
    CHECK(none.shortcuts["view.frameSelection"] == "F");

    const RestoreReport accepted = me->layout().commit(preview, me->shortcuts(), {"view.frameSelection"});
    CHECK(accepted.shortcuts["view.frameSelection"] == "G");
}

void badFilesAreRejected() {
    auto shell = makeShell(laptopScreens());
    CHECK(!shell->layout().preview("not json", shell->shortcuts()).valid);
    CHECK(!shell->layout().preview("{\"format\":\"other\"}", shell->shortcuts()).valid);
    CHECK(!shell->layout().preview("{\"format\":\"meshify-layout\",\"version\":99}", shell->shortcuts()).valid);

    // A damaged layout state falls back to the default arrangement.
    QByteArray file = shell->layout().save("Damaged", shell->shortcuts());
    const int at = file.indexOf("\"state\": \"") + 10;
    file.replace(at, 16, "AAAAAAAAAAAAAAAA");
    const ImportPreview p = shell->layout().preview(file, shell->shortcuts());
    const RestoreReport r = shell->layout().commit(p, shell->shortcuts(), {});
    CHECK(r.fellBackToDefault);
    CHECK(shell->dock()->dockWidgetsMap().size() == 6);
}

}  // namespace

int main(int argc, char** argv) {
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);

    const std::vector<std::pair<const char*, std::function<void()>>> tests = {
        {"rescue placement rules", rescuePlacement},
        {"default layout and reset", defaultLayout},
        {"UX-03 layout from a disconnected monitor", layoutFromDisconnectedMonitor},
        {"panel left off screen by the platform", panelLeftOffScreenByThePlatform},
        {"missing plugin panel keeps its place", missingPluginPanel},
        {"shortcut preview is explicit", shortcutPreviewIsExplicit},
        {"import leaves shortcuts alone until accepted", importLeavesShortcutsAloneUntilAccepted},
        {"bad layout files are rejected", badFilesAreRejected},
    };
    for (const auto& [name, fn] : tests) {
        const int before = failures;
        std::cout << "[ RUN  ] " << name << std::endl;
        fn();
        std::cout << (failures == before ? "[  OK  ] " : "[ FAIL ] ") << name << std::endl;
    }
    std::cout << (failures == 0 ? "All tests passed\n" : "Some tests failed\n");
    return failures == 0 ? 0 : 1;
}
