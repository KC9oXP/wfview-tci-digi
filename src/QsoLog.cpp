#include "QsoLog.h"

#include "AppPaths.h"

#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QTableWidget>
#include <QTimeZone>
#include <QUrl>
#include <QVBoxLayout>

namespace
{
QString field(const char *name, const QString &v)
{
    if (v.isEmpty())
        return QString();
    const QByteArray u = v.toUtf8();
    return QStringLiteral("<%1:%2>%3 ").arg(QLatin1String(name)).arg(u.size()).arg(v);
}
} // namespace

namespace Adif
{
QString band(qint64 hz)
{
    struct B
    {
        double lo, hi;
        const char *name;
    };
    static const B bands[] = {{0.1357, 0.1378, "2190m"},
                              {0.472, 0.479, "630m"},
                              {1.8, 2.0, "160m"},
                              {3.5, 4.0, "80m"},
                              {5.06, 5.45, "60m"},
                              {7.0, 7.3, "40m"},
                              {10.1, 10.15, "30m"},
                              {14.0, 14.35, "20m"},
                              {18.068, 18.168, "17m"},
                              {21.0, 21.45, "15m"},
                              {24.89, 24.99, "12m"},
                              {28.0, 29.7, "10m"},
                              {50.0, 54.0, "6m"},
                              {144.0, 148.0, "2m"},
                              {222.0, 225.0, "1.25m"},
                              {420.0, 450.0, "70cm"}};
    const double mhz = hz / 1e6;
    for (const B &b : bands)
        if (mhz >= b.lo && mhz <= b.hi)
            return QLatin1String(b.name);
    return QString();
}

QString header()
{
    return QStringLiteral("wfview TCI Digi QSO log\n<ADIF_VER:5>3.1.4 <PROGRAMID:15>wfview TCI Digi <EOH>\n");
}

QString record(const Qso &q)
{
    QString r;
    r += field("CALL", q.call.toUpper());
    r += field("QSO_DATE", q.on.toString(QStringLiteral("yyyyMMdd")));
    r += field("TIME_ON", q.on.toString(QStringLiteral("HHmmss")));
    r += field("QSO_DATE_OFF", q.off.toString(QStringLiteral("yyyyMMdd")));
    r += field("TIME_OFF", q.off.toString(QStringLiteral("HHmmss")));
    if (q.freqHz > 0)
    {
        r += field("FREQ", QString::number(q.freqHz / 1e6, 'f', 6));
        r += field("BAND", band(q.freqHz));
    }
    r += field("MODE", q.mode);
    r += field("SUBMODE", q.submode);
    r += field("RST_SENT", q.rstSent);
    r += field("RST_RCVD", q.rstRcvd);
    r += field("NAME", q.name);
    r += field("QTH", q.qth);
    r += field("GRIDSQUARE", q.grid.toUpper());
    r += field("COMMENT", q.comment);
    r += field("STATION_CALLSIGN", q.myCall.toUpper());
    r += field("MY_GRIDSQUARE", q.myGrid.toUpper());
    return r.trimmed() + QStringLiteral(" <EOR>\n");
}

QList<Qso> parse(const QString &text)
{
    QList<Qso> out;
    int pos = text.indexOf(QStringLiteral("<EOH>"), 0, Qt::CaseInsensitive);
    pos = pos < 0 ? 0 : pos + 5;
    static const QRegularExpression tag(QStringLiteral("<([A-Za-z_]+)(?::(\\d+))?(?::[A-Za-z])?>"));
    Qso q;
    QString date, time, dateOff, timeOff;
    while (true)
    {
        const QRegularExpressionMatch m = tag.match(text, pos);
        if (!m.hasMatch())
            break;
        const QString name = m.captured(1).toUpper();
        const int len = m.captured(2).toInt();
        const QString v = text.mid(m.capturedEnd(), len);
        pos = m.capturedEnd() + len;
        if (name == QLatin1String("EOR"))
        {
            q.on = QDateTime::fromString(date + time.left(6).leftJustified(6, '0'), QStringLiteral("yyyyMMddHHmmss"));
            q.on.setTimeZone(QTimeZone::utc());
            q.off = QDateTime::fromString(dateOff + timeOff.left(6).leftJustified(6, '0'),
                                          QStringLiteral("yyyyMMddHHmmss"));
            q.off.setTimeZone(QTimeZone::utc());
            out.append(q);
            q = Qso();
            date.clear();
            time.clear();
            dateOff.clear();
            timeOff.clear();
            continue;
        }
        if (name == QLatin1String("CALL"))
            q.call = v;
        else if (name == QLatin1String("QSO_DATE"))
            date = v;
        else if (name == QLatin1String("TIME_ON"))
            time = v;
        else if (name == QLatin1String("QSO_DATE_OFF"))
            dateOff = v;
        else if (name == QLatin1String("TIME_OFF"))
            timeOff = v;
        else if (name == QLatin1String("FREQ"))
            q.freqHz = qRound64(v.toDouble() * 1e6);
        else if (name == QLatin1String("MODE"))
            q.mode = v;
        else if (name == QLatin1String("SUBMODE"))
            q.submode = v;
        else if (name == QLatin1String("RST_SENT"))
            q.rstSent = v;
        else if (name == QLatin1String("RST_RCVD"))
            q.rstRcvd = v;
        else if (name == QLatin1String("NAME"))
            q.name = v;
        else if (name == QLatin1String("QTH"))
            q.qth = v;
        else if (name == QLatin1String("GRIDSQUARE"))
            q.grid = v;
        else if (name == QLatin1String("COMMENT"))
            q.comment = v;
        else if (name == QLatin1String("STATION_CALLSIGN"))
            q.myCall = v;
        else if (name == QLatin1String("MY_GRIDSQUARE"))
            q.myGrid = v;
    }
    return out;
}
} // namespace Adif

QsoLogDock::QsoLogDock(QSettings *settings, QWidget *parent)
    : QDockWidget(tr("QSO log"), parent)
    , m_settings(settings)
{
    setObjectName(QStringLiteral("qsoLog"));
    auto *w = new QWidget();
    auto *v = new QVBoxLayout(w);

    auto *g = new QGridLayout();
    const auto edit = [](int chars)
    {
        auto *e = new QLineEdit();
        e->setMinimumWidth(QFontMetrics(e->font()).horizontalAdvance(QString(chars, QLatin1Char('M'))));
        return e;
    };
    m_call = edit(7);
    m_rstS = edit(3);
    m_rstR = edit(3);
    m_name = edit(6);
    m_qth = edit(8);
    m_grid = edit(4);
    m_comment = edit(10);
    m_myGrid = edit(4);
    m_myGrid->setText(m_settings->value(QStringLiteral("Station/MyGrid")).toString());
    m_myGrid->setToolTip(tr("Your Maidenhead grid, written as MY_GRIDSQUARE."));
    int r = 0;
    g->addWidget(new QLabel(tr("Call")), r, 0);
    g->addWidget(m_call, r, 1);
    g->addWidget(new QLabel(tr("RST sent")), r, 2);
    g->addWidget(m_rstS, r, 3);
    g->addWidget(new QLabel(tr("rcvd")), r, 4);
    g->addWidget(m_rstR, r, 5);
    ++r;
    g->addWidget(new QLabel(tr("Name")), r, 0);
    g->addWidget(m_name, r, 1);
    g->addWidget(new QLabel(tr("QTH")), r, 2);
    g->addWidget(m_qth, r, 3, 1, 3);
    ++r;
    g->addWidget(new QLabel(tr("Grid")), r, 0);
    g->addWidget(m_grid, r, 1);
    g->addWidget(new QLabel(tr("My grid")), r, 2);
    g->addWidget(m_myGrid, r, 3);
    ++r;
    g->addWidget(new QLabel(tr("Comment")), r, 0);
    g->addWidget(m_comment, r, 1, 1, 5);
    v->addLayout(g);

    m_info = new QLabel();
    m_info->setWordWrap(true);
    v->addWidget(m_info);

    auto *row = new QHBoxLayout();
    m_log = new QPushButton(tr("Log QSO"));
    m_log->setToolTip(tr("Append this QSO to the ADIF log (Ctrl+L)."));
    m_log->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_L));
    auto *clear = new QPushButton(tr("Clear"));
    auto *open = new QPushButton(tr("Open folder"));
    open->setToolTip(tr("Open the folder with the ADIF log and the session logs."));
    row->addWidget(m_log);
    row->addWidget(clear);
    row->addStretch(1);
    row->addWidget(open);
    v->addLayout(row);

    m_table = new QTableWidget(0, 7);
    m_table->setHorizontalHeaderLabels(
        {tr("UTC"), tr("Call"), tr("Freq"), tr("Mode"), tr("Sent"), tr("Rcvd"), tr("Name")});
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->verticalHeader()->hide();
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    v->addWidget(m_table, 1);
    setWidget(w);

    connect(m_log, &QPushButton::clicked, this, &QsoLogDock::logQso);
    connect(clear, &QPushButton::clicked, this, &QsoLogDock::clearForm);
    connect(
        open, &QPushButton::clicked, this, []() { QDesktopServices::openUrl(QUrl::fromLocalFile(AppPaths::logs())); });
    connect(m_call,
            &QLineEdit::textEdited,
            this,
            [this](const QString &t)
            {
                if (!t.isEmpty() && !m_start.isValid())
                    m_start = QDateTime::currentDateTimeUtc();
            });
    connect(m_myGrid,
            &QLineEdit::editingFinished,
            this,
            [this]()
            {
                m_settings->setValue(QStringLiteral("Station/MyGrid"), m_myGrid->text().trimmed());
                emit myGridChanged(m_myGrid->text().trimmed());
            });

    QFile f(logPath());
    if (f.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        const QList<Qso> all = Adif::parse(QString::fromUtf8(f.readAll()));
        for (int i = std::max(0, int(all.size()) - 500); i < all.size(); ++i)
            addRow(all.at(i), true);
    }
    clearForm();
}

QString QsoLogDock::logPath() const
{
    return QDir(AppPaths::logs()).filePath(QStringLiteral("wfview-tci-digi.adi"));
}

QString QsoLogDock::defaultRst() const
{
    if (m_mode == QLatin1String("CW") || m_mode == QLatin1String("RTTY") || m_mode == QLatin1String("PSK"))
        return QStringLiteral("599");
    if (m_mode == QLatin1String("SSTV"))
        return QStringLiteral("595");
    return QStringLiteral("59");
}

void QsoLogDock::setRadio(qint64 hz, const QString &modulation)
{
    Q_UNUSED(modulation);
    m_hz = hz;
    QString f = QString::number(hz / 1e6, 'f', 4);
    m_info->setText(tr("%1 MHz  %2  %3").arg(f, Adif::band(hz), m_submode.isEmpty() ? m_mode : m_submode));
}

void QsoLogDock::setMode(const QString &mode, const QString &submode, const QString &shown)
{
    Q_UNUSED(shown);
    const bool changed = mode != m_mode;
    m_mode = mode;
    m_submode = submode;
    if (changed || m_rstS->text().isEmpty())
    {
        m_rstS->setText(defaultRst());
        m_rstR->setText(defaultRst());
    }
    setRadio(m_hz, QString());
    m_log->setEnabled(!m_mode.isEmpty());
    m_log->setToolTip(m_mode.isEmpty() ? tr("This tab's mode is receive-only (no QSOs).")
                                       : tr("Append this QSO to the ADIF log (Ctrl+L)."));
}

void QsoLogDock::setCall(const QString &call)
{
    if (call.isEmpty() || call.compare(m_call->text(), Qt::CaseInsensitive) == 0)
        return;
    m_call->setText(call.toUpper());
    m_start = QDateTime::currentDateTimeUtc();
}

void QsoLogDock::setMyCall(const QString &call)
{
    m_myCall = call;
}

void QsoLogDock::clearForm()
{
    m_call->clear();
    m_name->clear();
    m_qth->clear();
    m_grid->clear();
    m_comment->clear();
    m_rstS->setText(defaultRst());
    m_rstR->setText(defaultRst());
    m_start = QDateTime();
}

void QsoLogDock::logQso()
{
    const QString call = m_call->text().trimmed().toUpper();
    if (call.isEmpty())
    {
        m_info->setText(tr("Enter the other station's call first."));
        return;
    }
    Qso q;
    q.off = QDateTime::currentDateTimeUtc();
    q.on = m_start.isValid() ? m_start : q.off;
    q.call = call;
    q.mode = m_mode;
    q.submode = m_submode;
    q.rstSent = m_rstS->text().trimmed();
    q.rstRcvd = m_rstR->text().trimmed();
    q.name = m_name->text().trimmed();
    q.qth = m_qth->text().trimmed();
    q.grid = m_grid->text().trimmed();
    q.comment = m_comment->text().trimmed();
    q.freqHz = m_hz;
    q.myCall = m_myCall;
    q.myGrid = m_myGrid->text().trimmed();

    QFile f(logPath());
    const bool fresh = !f.exists() || f.size() == 0;
    if (!f.open(QIODevice::Append | QIODevice::Text))
    {
        m_info->setText(tr("Cannot write %1").arg(logPath()));
        return;
    }
    if (fresh)
        f.write(Adif::header().toUtf8());
    f.write(Adif::record(q).toUtf8());
    f.close();
    addRow(q, true);
    emit logged(tr("Logged %1 %2 %3").arg(q.call, q.submode.isEmpty() ? q.mode : q.submode, Adif::band(q.freqHz)));
    emit qsoLogged(q);
    m_info->setText(tr("Logged %1 at %2Z.").arg(q.call, q.off.toString(QStringLiteral("HH:mm"))));
    clearForm();
}

void QsoLogDock::addRow(const Qso &q, bool top)
{
    const int row = top ? 0 : m_table->rowCount();
    m_table->insertRow(row);
    const QStringList cells = {q.on.toString(QStringLiteral("yyyy-MM-dd HH:mm")),
                               q.call,
                               q.freqHz > 0 ? QString::number(q.freqHz / 1e6, 'f', 4) : QString(),
                               q.submode.isEmpty() ? q.mode : q.submode,
                               q.rstSent,
                               q.rstRcvd,
                               q.name};
    for (int c = 0; c < cells.size(); ++c)
        m_table->setItem(row, c, new QTableWidgetItem(cells.at(c)));
}

void QsoLogDock::setMyGrid(const QString &grid)
{
    if (m_myGrid && m_myGrid->text().trimmed() != grid.trimmed())
        m_myGrid->setText(grid.trimmed());
}
