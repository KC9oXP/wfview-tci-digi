#pragma once

// Software memories window: a list of frequencies (tab, sub-mode, dial, sideband,
// licence classes, comment) kept in memories.csv in the data folder, so it can be
// edited in Excel or a text editor too. The first time, the file is made from the
// built-in list (ARRL band plan digital frequencies). Clicking a memory tunes to it.

#include "memories.h"

#include <QDialog>
#include <QFileSystemWatcher>
#include <QTimer>
#include <functional>
#include <vector>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QSettings;
class QTableWidget;

class MemoriesDialog : public QDialog
{
    Q_OBJECT

public:
    MemoriesDialog(QSettings *settings, const QStringList &tabNames, QWidget *parent = nullptr);
    ~MemoriesDialog() override;

    // What "Add the current frequency" saves: the tab showing, its sub-mode, dial, sideband.
    void setCurrentProvider(std::function<memories::Memory()> f)
    {
        m_current = std::move(f);
    }
    // The tab the main window shows (for the "This tab" filter).
    void setCurrentTab(const QString &tab);

    static QString filePath();
    static std::vector<memories::Memory> builtIn();

signals:
    void tuneRequested(const memories::Memory &m);

protected:
    void hideEvent(QHideEvent *e) override;

private:
    void load();
    bool save();
    void fill();
    bool passes(const memories::Memory &m) const;
    int selectedIndex() const;
    void tuneSelected();
    void addCurrent();
    void editSelected();
    void deleteSelected();
    void addMissingBuiltIn();
    bool editDialog(memories::Memory &m, const QString &title);
    void watch();

    QSettings *m_settings;
    QStringList m_tabNames;
    QString m_mainTab;
    std::vector<memories::Memory> m_list;
    std::function<memories::Memory()> m_current;

    QComboBox *m_tabFilter = nullptr;
    QComboBox *m_classFilter = nullptr;
    QLineEdit *m_search = nullptr;
    QCheckBox *m_singleClick = nullptr;
    QTableWidget *m_table = nullptr;
    QLabel *m_status = nullptr;
    QFileSystemWatcher m_watcher;
    QTimer m_reloadLater;
    bool m_saving = false;
};
