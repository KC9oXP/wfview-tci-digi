#include "MemoriesDialog.h"

#include "AppPaths.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QHideEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSaveFile>
#include <QSettings>
#include <QTableWidget>
#include <QUrl>
#include <QVBoxLayout>
#include <algorithm>

namespace
{
QString q(const std::string &s)
{
    return QString::fromStdString(s);
}
std::string s(const QString &q)
{
    return q.toStdString();
}
enum Col
{
    ColName,
    ColTab,
    ColSub,
    ColDial,
    ColSide,
    ColBand,
    ColClasses,
    ColComment,
    ColCount
};
// Sorts the dial column by frequency, not as text.
class DialItem : public QTableWidgetItem
{
public:
    using QTableWidgetItem::QTableWidgetItem;
    bool operator<(const QTableWidgetItem &o) const override
    {
        return data(Qt::UserRole + 1).toLongLong() < o.data(Qt::UserRole + 1).toLongLong();
    }
};
// Classes filter choices (index 0 = any).
const unsigned kClassBits[] = {
    0, memories::Novice, memories::Technician, memories::General, memories::Advanced, memories::Extra, memories::Other};
constexpr int kClassChoices = 7;
} // namespace

QString MemoriesDialog::filePath()
{
    return QDir(AppPaths::base()).filePath(QStringLiteral("memories.csv"));
}

std::vector<memories::Memory> MemoriesDialog::builtIn()
{
    QFile f(QStringLiteral(":/resources/memories-builtin.csv"));
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return memories::fromCsv(f.readAll().toStdString());
}

MemoriesDialog::MemoriesDialog(QSettings *settings, const QStringList &tabNames, QWidget *parent)
    : QDialog(parent)
    , m_settings(settings)
    , m_tabNames(tabNames)
{
    setWindowTitle(tr("Memories"));
    setObjectName(QStringLiteral("memoriesDialog"));
    auto *v = new QVBoxLayout(this);

    auto *top = new QHBoxLayout();
    top->addWidget(new QLabel(tr("Show")));
    m_tabFilter = new QComboBox();
    m_tabFilter->addItem(tr("All tabs"), QString());
    m_tabFilter->addItem(tr("This tab"), QStringLiteral("*"));
    for (const QString &t : m_tabNames)
        m_tabFilter->addItem(t, t);
    m_tabFilter->setToolTip(tr("Which memories to list: all, the tab showing in the main window, or one tab."));
    top->addWidget(m_tabFilter);
    top->addWidget(new QLabel(tr("License")));
    m_classFilter = new QComboBox();
    m_classFilter->addItems(
        {tr("Any class"), tr("Novice"), tr("Technician"), tr("General"), tr("Advanced"), tr("Extra"), tr("Other")});
    m_classFilter->setToolTip(
        tr("Only list memories a US license class may transmit on (FCC Part 97). Receive-only "
           "memories are always listed.\nOther: memories you marked Other (11 m, MARS, other countries ...) and ones "
           "outside the US amateur rules."));
    top->addWidget(m_classFilter);
    m_search = new QLineEdit();
    m_search->setPlaceholderText(tr("Search name or comment"));
    m_search->setClearButtonEnabled(true);
    top->addWidget(m_search, 1);
    v->addLayout(top);

    m_table = new QTableWidget(0, ColCount);
    m_table->setHorizontalHeaderLabels({tr("Name"),
                                        tr("Tab"),
                                        tr("Sub-mode"),
                                        tr("Dial MHz"),
                                        tr("Sideband"),
                                        tr("Band"),
                                        tr("Classes"),
                                        tr("Comment")});
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->verticalHeader()->hide();
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->setSortingEnabled(true);
    m_table->setWordWrap(false);
    v->addWidget(m_table, 1);

    auto *row = new QHBoxLayout();
    auto *go = new QPushButton(tr("Go"));
    go->setToolTip(tr("Tune to the selected memory: tab, sub-mode, dial and sideband."));
    auto *blank = new QPushButton(tr("New..."));
    blank->setToolTip(tr("Add a memory by typing it in (frequency, tab, classes ...)."));
    auto *add = new QPushButton(tr("Add current..."));
    add->setToolTip(tr("Save the frequency, sideband, tab and sub-mode now in use as a memory."));
    auto *edit = new QPushButton(tr("Edit..."));
    auto *del = new QPushButton(tr("Delete"));
    auto *open = new QPushButton(tr("Open file"));
    open->setToolTip(tr("Open memories.csv in the program set for .csv files (e.g. Excel). Changes saved there "
                        "show here straight away.\nCSV columns: Name, Tab, Sub-mode, Dial MHz, Sideband, Band, "
                        "Classes, Comment."));
    auto *builtin = new QPushButton(tr("Add built-in..."));
    builtin->setToolTip(tr("Add any of the built-in memories (ARRL band plan, Feld Hell Club frequencies) that are "
                           "missing from your list. "
                           "Your own memories are not changed."));
    m_singleClick = new QCheckBox(tr("Tune on click"));
    m_singleClick->setToolTip(tr("On: one click on a memory tunes to it. Off: double-click or Go."));
    m_singleClick->setChecked(m_settings->value(QStringLiteral("Memories/SingleClick"), true).toBool());
    row->addWidget(go);
    row->addWidget(m_singleClick);
    row->addStretch(1);
    row->addWidget(blank);
    row->addWidget(add);
    row->addWidget(edit);
    row->addWidget(del);
    row->addWidget(builtin);
    row->addWidget(open);
    v->addLayout(row);

    m_status = new QLabel();
    m_status->setWordWrap(true);
    v->addWidget(m_status);

    connect(m_tabFilter, &QComboBox::currentIndexChanged, this, &MemoriesDialog::fill);
    connect(m_classFilter, &QComboBox::currentIndexChanged, this, &MemoriesDialog::fill);
    connect(m_search, &QLineEdit::textChanged, this, &MemoriesDialog::fill);
    connect(go, &QPushButton::clicked, this, &MemoriesDialog::tuneSelected);
    connect(add, &QPushButton::clicked, this, &MemoriesDialog::addCurrent);
    connect(blank, &QPushButton::clicked, this, &MemoriesDialog::addNew);
    connect(edit, &QPushButton::clicked, this, &MemoriesDialog::editSelected);
    connect(del, &QPushButton::clicked, this, &MemoriesDialog::deleteSelected);
    connect(builtin, &QPushButton::clicked, this, &MemoriesDialog::addMissingBuiltIn);
    connect(open,
            &QPushButton::clicked,
            this,
            []() { QDesktopServices::openUrl(QUrl::fromLocalFile(MemoriesDialog::filePath())); });
    connect(m_table,
            &QTableWidget::cellClicked,
            this,
            [this](int, int)
            {
                if (m_singleClick->isChecked())
                    tuneSelected();
            });
    connect(m_table,
            &QTableWidget::cellDoubleClicked,
            this,
            [this](int, int)
            {
                if (!m_singleClick->isChecked())
                    tuneSelected();
            });
    connect(m_singleClick,
            &QCheckBox::toggled,
            this,
            [this](bool on) { m_settings->setValue(QStringLiteral("Memories/SingleClick"), on); });

    // Reload when the file is changed outside the app (Excel, Notepad). Editors often
    // replace the file, so the watch is set again after each change.
    m_reloadLater.setSingleShot(true);
    m_reloadLater.setInterval(400);
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, [this](const QString &) { m_reloadLater.start(); });
    connect(&m_reloadLater,
            &QTimer::timeout,
            this,
            [this]()
            {
                if (m_saving)
                    return;
                load();
                fill();
                m_status->setText(
                    (tr("Reloaded: memories.csv was changed.") + QLatin1Char(' ') + m_status->text()).trimmed());
            });

    m_settings->beginGroup(QStringLiteral("Memories"));
    m_tabFilter->setCurrentIndex(
        std::max(0, m_tabFilter->findData(m_settings->value(QStringLiteral("Tab")).toString())));
    m_classFilter->setCurrentIndex(m_settings->value(QStringLiteral("Class"), 0).toInt());
    restoreGeometry(m_settings->value(QStringLiteral("Geometry")).toByteArray());
    const QByteArray hs = m_settings->value(QStringLiteral("Header")).toByteArray();
    m_settings->endGroup();
    if (!hs.isEmpty())
        m_table->horizontalHeader()->restoreState(hs);
    else
        resize(900, 480);

    load();
    fill();
    if (hs.isEmpty())
    {
        m_table->resizeColumnsToContents();
        m_table->setColumnWidth(ColClasses, std::min(m_table->columnWidth(ColClasses), 260));
    }
}

MemoriesDialog::~MemoriesDialog() = default;

void MemoriesDialog::hideEvent(QHideEvent *e)
{
    m_settings->beginGroup(QStringLiteral("Memories"));
    m_settings->setValue(QStringLiteral("Tab"), m_tabFilter->currentData().toString());
    m_settings->setValue(QStringLiteral("Class"), m_classFilter->currentIndex());
    m_settings->setValue(QStringLiteral("Geometry"), saveGeometry());
    m_settings->setValue(QStringLiteral("Header"), m_table->horizontalHeader()->saveState());
    m_settings->endGroup();
    QDialog::hideEvent(e);
}

void MemoriesDialog::setCurrentTab(const QString &tab)
{
    m_mainTab = tab;
    // Later, not now: this can come from a click in the table (tuning switched the tab).
    if (m_tabFilter && m_tabFilter->currentData().toString() == QLatin1String("*"))
        QTimer::singleShot(0, this, &MemoriesDialog::fill);
}

void MemoriesDialog::watch()
{
    if (!m_watcher.files().isEmpty())
        m_watcher.removePaths(m_watcher.files());
    if (QFileInfo::exists(filePath()))
        m_watcher.addPath(filePath());
}

void MemoriesDialog::load()
{
    const QString path = filePath();
    if (!QFileInfo::exists(path))
    {
        // First time: start from the built-in list.
        m_list = builtIn();
        if (save())
            m_status->setText(tr("Made %1 from the built-in list (ARRL band plan, Feld Hell Club frequencies).")
                                  .arg(QDir::toNativeSeparators(path)));
        return;
    }
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
    {
        m_status->setText(tr("Cannot read %1: %2").arg(QDir::toNativeSeparators(path), f.errorString()));
        watch();
        return;
    }
    int skipped = 0;
    m_list = memories::fromCsv(f.readAll().toStdString(), &skipped);
    m_status->setText(skipped ? tr("%1 rows in memories.csv were skipped (no name or no frequency).").arg(skipped)
                              : QString());
    watch();
}

bool MemoriesDialog::save()
{
    m_saving = true;
    const QString path = filePath();
    QSaveFile f(path);
    bool ok = f.open(QIODevice::WriteOnly);
    if (ok)
    {
        // A byte order mark so Excel reads the file as UTF-8.
        f.write("\xEF\xBB\xBF");
        f.write(QByteArray::fromStdString(memories::toCsv(m_list)));
        ok = f.commit();
    }
    if (!ok)
        QMessageBox::warning(this,
                             tr("Memories"),
                             tr("Could not save %1:\n%2\n\nIf it is open in Excel, close it there and try again.")
                                 .arg(QDir::toNativeSeparators(path), f.errorString()));
    watch();
    QTimer::singleShot(600, this, [this]() { m_saving = false; });
    return ok;
}

bool MemoriesDialog::passes(const memories::Memory &m) const
{
    QString tab = m_tabFilter->currentData().toString();
    if (tab == QLatin1String("*"))
        tab = m_mainTab;
    if (!tab.isEmpty() && q(m.tab).compare(tab, Qt::CaseInsensitive) != 0)
        return false;
    const unsigned bit = kClassBits[std::clamp(m_classFilter->currentIndex(), 0, kClassChoices - 1)];
    if (bit)
    {
        memories::Emission e;
        const bool rxOnly = !memories::emissionForTab(m.tab, e);
        const unsigned mask = memories::parseClasses(memories::classesText(m));
        const bool other = bit == memories::Other && ((mask & memories::Other) || mask == 0);
        if (!rxOnly && !other && !(mask & bit))
            return false;
    }
    const QString needle = m_search->text().trimmed();
    if (!needle.isEmpty() && !q(m.name).contains(needle, Qt::CaseInsensitive)
        && !q(m.comment).contains(needle, Qt::CaseInsensitive))
        return false;
    return true;
}

void MemoriesDialog::fill()
{
    const int keep = selectedIndex();
    m_table->setSortingEnabled(false);
    m_table->setRowCount(0);
    int shown = 0;
    for (int i = 0; i < int(m_list.size()); ++i)
    {
        const memories::Memory &m = m_list[size_t(i)];
        if (!passes(m))
            continue;
        const int r = m_table->rowCount();
        m_table->insertRow(r);
        const QString band = m.band.empty() ? q(memories::bandName(m.dialHz)) : q(m.band);
        const QString cls = q(memories::classesText(m));
        const QString cells[ColCount] = {q(m.name),
                                         q(m.tab),
                                         q(m.subMode),
                                         q(memories::formatMHz(m.dialHz)),
                                         q(m.sideband),
                                         band,
                                         cls,
                                         q(m.comment)};
        for (int c = 0; c < ColCount; ++c)
        {
            auto *it = c == ColDial ? new DialItem(cells[c]) : new QTableWidgetItem(cells[c]);
            it->setData(Qt::UserRole, i);
            if (c == ColDial)
            {
                it->setData(Qt::UserRole + 1, qlonglong(m.dialHz));
                it->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            }
            if (c == ColComment || c == ColClasses)
                it->setToolTip(cells[c]);
            if (c == ColClasses && m.classes.empty())
                it->setToolTip(tr("%1\n(worked out from FCC Part 97 for the dial and sideband)").arg(cls));
            m_table->setItem(r, c, it);
        }
        if (i == keep)
            m_table->selectRow(r);
        ++shown;
    }
    m_table->setSortingEnabled(true);
    if (shown == 0 && !m_list.empty())
        m_status->setText(tr("No memories match the filter."));
}

int MemoriesDialog::selectedIndex() const
{
    const QList<QTableWidgetItem *> sel = m_table ? m_table->selectedItems() : QList<QTableWidgetItem *>();
    return sel.isEmpty() ? -1 : sel.first()->data(Qt::UserRole).toInt();
}

void MemoriesDialog::tuneSelected()
{
    const int i = selectedIndex();
    if (i < 0 || i >= int(m_list.size()))
        return;
    const memories::Memory &m = m_list[size_t(i)];
    m_status->setText(tr("%1: %2 MHz %3").arg(q(m.name), q(memories::formatMHz(m.dialHz)), q(m.sideband)));
    emit tuneRequested(m);
}

bool MemoriesDialog::editDialog(memories::Memory &m, const QString &title)
{
    QDialog dlg(this);
    dlg.setWindowTitle(title);
    auto *form = new QFormLayout(&dlg);
    auto *name = new QLineEdit(q(m.name));
    auto *tab = new QComboBox();
    tab->setEditable(true);
    tab->addItems(m_tabNames);
    tab->setCurrentText(q(m.tab));
    auto *sub = new QLineEdit(q(m.subMode));
    sub->setPlaceholderText(tr("e.g. BPSK31, Olivia 8/500, 45.45/170 (blank: leave as is)"));
    auto *dial = new QLineEdit(m.dialHz > 0 ? q(memories::formatMHz(m.dialHz)) : QString());
    dial->setPlaceholderText(tr("MHz, e.g. 14.070"));
    auto *side = new QComboBox();
    side->addItem(tr("Keep the radio's mode"), QString());
    side->addItem(QStringLiteral("USB"), QStringLiteral("USB"));
    side->addItem(QStringLiteral("LSB"), QStringLiteral("LSB"));
    side->addItem(QStringLiteral("AM"), QStringLiteral("AM"));
    side->setCurrentIndex(std::max(0, side->findData(q(m.sideband))));
    // License classes: tick boxes; none ticked = worked out from FCC Part 97.
    auto *clsRow = new QWidget();
    auto *clsLay = new QHBoxLayout(clsRow);
    clsLay->setContentsMargins(0, 0, 0, 0);
    struct ClsName
    {
        unsigned bit;
        QString name;
    };
    const ClsName clsNames[] = {{memories::Novice, tr("Novice")},
                                {memories::Technician, tr("Technician")},
                                {memories::General, tr("General")},
                                {memories::Advanced, tr("Advanced")},
                                {memories::Extra, tr("Extra")},
                                {memories::Other, tr("Other")}};
    QList<QPair<QCheckBox *, unsigned>> clsBoxes;
    const unsigned start
        = memories::parseClasses(m.classes.empty() && m.dialHz > 0 ? memories::classesText(m) : m.classes);
    for (const ClsName &c : clsNames)
    {
        auto *cb = new QCheckBox(c.name);
        cb->setChecked(start & c.bit);
        if (c.bit == memories::Other)
            cb->setToolTip(tr("Your own class for things outside the US amateur rules: 11 m, MARS, another "
                              "country's bands ..."));
        clsLay->addWidget(cb);
        clsBoxes << qMakePair(cb, c.bit);
    }
    auto *fromRules = new QPushButton(tr("From the rules"));
    fromRules->setToolTip(tr("Tick the classes FCC Part 97 allows for this tab, frequency and sideband."));
    clsLay->addWidget(fromRules);
    clsLay->addStretch(1);
    auto *comment = new QLineEdit(q(m.comment));
    comment->setMinimumWidth(380);
    form->addRow(tr("Name"), name);
    form->addRow(tr("Tab"), tab);
    form->addRow(tr("Sub-mode"), sub);
    form->addRow(tr("Dial MHz"), dial);
    form->addRow(tr("Sideband"), side);
    form->addRow(tr("License classes"), clsRow);
    form->addRow(tr("Comment"), comment);
    auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    form->addRow(bb);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    connect(fromRules,
            &QPushButton::clicked,
            &dlg,
            [&]()
            {
                memories::Memory t;
                t.tab = s(tab->currentText());
                t.dialHz = memories::parseDial(s(dial->text()));
                t.sideband = s(side->currentData().toString());
                const unsigned mask = memories::parseClasses(memories::classesText(t));
                for (const auto &b : clsBoxes)
                    b.first->setChecked(mask & b.second);
            });
    connect(bb,
            &QDialogButtonBox::accepted,
            &dlg,
            [&]()
            {
                if (name->text().trimmed().isEmpty() || memories::parseDial(s(dial->text())) <= 0)
                {
                    QMessageBox::information(&dlg, title, tr("A memory needs a name and a frequency."));
                    return;
                }
                dlg.accept();
            });
    if (dlg.exec() != QDialog::Accepted)
        return false;
    m.name = s(name->text().trimmed().replace(QLatin1Char('\n'), QLatin1Char(' ')));
    m.tab = s(tab->currentText().trimmed());
    m.subMode = s(sub->text().trimmed());
    m.dialHz = memories::parseDial(s(dial->text()));
    m.sideband = s(side->currentData().toString());
    m.band = memories::bandName(m.dialHz);
    m.comment = s(comment->text().trimmed());
    unsigned mask = 0;
    for (const auto &b : clsBoxes)
        if (b.first->isChecked())
            mask |= b.second;
    // Written out (worked out from the rules when nothing is ticked), so the file shows it too.
    m.classes = mask ? memories::classNames(mask) : memories::classesText(m);
    return true;
}

void MemoriesDialog::addCurrent()
{
    memories::Memory m = m_current ? m_current() : memories::Memory();
    if (m.dialHz <= 0)
    {
        m_status->setText(tr("No frequency yet: connect to wfview first."));
        return;
    }
    m.name = s(QStringLiteral("%1 %2").arg(q(m.tab), q(memories::bandName(m.dialHz))).trimmed());
    if (!editDialog(m, tr("Add memory")))
        return;
    m_list.push_back(m);
    if (save())
        m_status->setText(tr("Added %1.").arg(q(m.name)));
    fill();
}

void MemoriesDialog::addNew()
{
    memories::Memory m;
    m.tab = s(m_mainTab);
    m.sideband = "USB";
    if (!editDialog(m, tr("New memory")))
        return;
    m_list.push_back(m);
    if (save())
        m_status->setText(tr("Added %1.").arg(q(m.name)));
    fill();
}

void MemoriesDialog::editSelected()
{
    const int i = selectedIndex();
    if (i < 0)
    {
        m_status->setText(tr("Select a memory first."));
        return;
    }
    memories::Memory m = m_list[size_t(i)];
    if (!editDialog(m, tr("Edit memory")))
        return;
    m_list[size_t(i)] = m;
    save();
    fill();
}

void MemoriesDialog::deleteSelected()
{
    const int i = selectedIndex();
    if (i < 0)
    {
        m_status->setText(tr("Select a memory first."));
        return;
    }
    if (QMessageBox::question(this, tr("Delete memory"), tr("Delete %1?").arg(q(m_list[size_t(i)].name)))
        != QMessageBox::Yes)
        return;
    m_list.erase(m_list.begin() + i);
    save();
    fill();
}

void MemoriesDialog::addMissingBuiltIn()
{
    std::vector<memories::Memory> missing;
    for (const memories::Memory &b : builtIn())
    {
        bool have = false;
        for (const memories::Memory &m : m_list)
            if (q(m.name).compare(q(b.name), Qt::CaseInsensitive) == 0 || (m.dialHz == b.dialHz && m.tab == b.tab))
                have = true;
        if (!have)
            missing.push_back(b);
    }
    if (missing.empty())
    {
        m_status->setText(tr("All the built-in memories are already in your list."));
        return;
    }
    if (QMessageBox::question(this,
                              tr("Add built-in memories"),
                              tr("Add %n built-in memories that are not in your list?", nullptr, int(missing.size())))
        != QMessageBox::Yes)
        return;
    m_list.insert(m_list.end(), missing.begin(), missing.end());
    if (save())
        m_status->setText(tr("Added %n built-in memories.", nullptr, int(missing.size())));
    fill();
}
