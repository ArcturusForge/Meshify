// The Meshify window shell: a central viewport with dockable panels around it,
// following the default layout in the design brief (section 3).
#pragma once

#include "layout.hpp"

#include <QMainWindow>
#include <QMap>

namespace ads {
class CDockManager;
}

namespace meshify::p3 {

class Shell : public QMainWindow {
public:
    explicit Shell(QWidget* parent = nullptr);

    ads::CDockManager* dock() const { return m_dock; }
    LayoutManager& layout() { return *m_layout; }
    QMap<QString, QString>& shortcuts() { return m_shortcuts; }

private:
    void buildMenu();
    void saveLayoutDialog();
    void importLayoutDialog();

    ads::CDockManager* m_dock = nullptr;
    LayoutManager* m_layout = nullptr;
    QMap<QString, QString> m_shortcuts;
};

}  // namespace meshify::p3
