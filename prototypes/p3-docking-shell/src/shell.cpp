#include "shell.hpp"

#include <DockManager.h>
#include <DockWidget.h>

#include <QAction>
#include <QFile>
#include <QFileDialog>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QTextEdit>

namespace meshify::p3 {

namespace {

ads::CDockWidget* makePanel(const QString& title, const QString& text) {
    auto* dock = new ads::CDockWidget(title);
    auto* label = new QLabel(text);
    label->setAlignment(Qt::AlignCenter);
    label->setWordWrap(true);
    dock->setWidget(label);
    return dock;
}

QString describe(const ImportPreview& p) {
    QStringList lines;
    lines << QString("Layout \"%1\"").arg(p.layoutName);
    if (!p.missingScreens.isEmpty()) {
        lines << QString("Saved on screens that are not connected: %1. Panels there will be moved onto your main screen.")
                     .arg(p.missingScreens.join(", "));
    }
    if (!p.missingPanels.isEmpty()) {
        lines << QString("Panels that are not available here (kept as placeholders): %1").arg(p.missingPanels.join(", "));
    }
    int shown = 0;
    for (const ShortcutChange& c : p.shortcuts) {
        if (c.kind == ShortcutChange::Kind::Same) continue;
        if (shown++ == 0) lines << "Shortcut changes:";
        QString line = QString("  %1: %2 -> %3").arg(c.command, c.oldKey.isEmpty() ? "(none)" : c.oldKey, c.newKey);
        if (c.kind == ShortcutChange::Kind::Conflict) line += QString("  CONFLICT with %1, will not be applied").arg(c.conflictsWith);
        lines << line;
    }
    if (shown == 0) lines << "No shortcut changes.";
    return lines.join("\n");
}

}  // namespace

Shell::Shell(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle("Meshify (Stage 0 docking shell)");
    resize(1280, 800);

    ads::CDockManager::setConfigFlags(ads::CDockManager::DefaultOpaqueConfig);
    m_dock = new ads::CDockManager(this);
    m_layout = new LayoutManager(m_dock);

    struct Def {
        PanelSpec spec;
        const char* text;
        ads::DockWidgetArea area;
    };
    const Def defs[] = {
        {{"viewport", "Viewport", {}}, "3D viewport", ads::CenterDockWidgetArea},
        {{"outliner", "Object browser", {}}, "Objects", ads::LeftDockWidgetArea},
        {{"tools", "Tools", {}}, "Model | Sculpt | Prepare | Analyse", ads::TopDockWidgetArea},
        {{"properties", "Tool settings", {}}, "Active tool settings", ads::RightDockWidgetArea},
        {{"history", "History", {}}, "History", ads::BottomDockWidgetArea},
        {{"tasks", "Task status", {}}, "Background tasks", ads::BottomDockWidgetArea},
    };
    ads::CDockAreaWidget* bottom = nullptr;
    for (const Def& d : defs) {
        auto* panel = makePanel(d.spec.title, d.text);
        m_layout->registerPanel(d.spec, panel);
        if (d.spec.id == "tasks") {
            m_dock->addDockWidgetTabToArea(panel, bottom);
        } else {
            auto* area = m_dock->addDockWidget(d.area, panel);
            if (d.spec.id == "history") bottom = area;
        }
    }
    m_layout->captureDefault();

    m_shortcuts = {{"view.frameAll", "Home"},       {"view.frameSelection", "F"}, {"tool.fillet", "Ctrl+Shift+F"},
                   {"edit.undo", "Ctrl+Z"},        {"edit.redo", "Ctrl+Shift+Z"}, {"layout.reset", ""}};
    buildMenu();
}

void Shell::buildMenu() {
    QMenu* menu = menuBar()->addMenu("&Layout");
    connect(menu->addAction("Reset layout"), &QAction::triggered, this, [this] { m_layout->resetToDefault(); });
    connect(menu->addAction("Save layout..."), &QAction::triggered, this, [this] { saveLayoutDialog(); });
    connect(menu->addAction("Import layout..."), &QAction::triggered, this, [this] { importLayoutDialog(); });
}

void Shell::saveLayoutDialog() {
    const QString path = QFileDialog::getSaveFileName(this, "Save layout", "layout.meshify-layout", "Meshify layout (*.meshify-layout)");
    if (path.isEmpty()) return;
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly) || f.write(m_layout->save("My layout", m_shortcuts)) < 0) {
        QMessageBox::warning(this, "Save layout", "Could not write " + path);
    }
}

void Shell::importLayoutDialog() {
    const QString path = QFileDialog::getOpenFileName(this, "Import layout", {}, "Meshify layout (*.meshify-layout)");
    if (path.isEmpty()) return;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, "Import layout", "Could not read " + path);
        return;
    }
    const ImportPreview preview = m_layout->preview(f.readAll(), m_shortcuts);
    if (!preview.valid) {
        QMessageBox::warning(this, "Import layout", preview.error);
        return;
    }
    if (QMessageBox::question(this, "Import layout", describe(preview) + "\n\nApply?") != QMessageBox::Yes) return;
    QStringList accept;
    for (const ShortcutChange& c : preview.shortcuts) accept << c.command;
    const RestoreReport r = m_layout->commit(preview, m_shortcuts, accept);
    m_shortcuts = r.shortcuts;
    if (!r.rescued.isEmpty()) {
        QMessageBox::information(this, "Import layout", QString("%1 panel(s) moved onto your main screen.").arg(r.rescued.size()));
    }
}

}  // namespace meshify::p3
