#include "WsprPanel.h"

#include "TxLevelSlider.h"
#include "UiStyle.h"
#include "WsprNet.h"
#include "wwvcore.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QProgressBar>
#include <QPushButton>
#include <QSettings>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTableWidget>
#include <QTimeZone>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
#include <cstring>
#include <cmath>
#include <functional>

// ---------------------------------------------------------------------------
// The 200 Hz window: last spectrum, busy offsets, free suggestions, TX offset.
// ---------------------------------------------------------------------------

class WsprWindowView : public QWidget
{
public:
    explicit WsprWindowView(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setMinimumHeight(fontMetrics().lineSpacing() * 8);
        setCursor(Qt::CrossCursor);
        setMouseTracking(true);
    }
    std::function<void(double)> onClick;
    wspr::WindowSpectrum spectrum;
    std::vector<wspr::FreeFinder::Busy> busy;
    std::vector<wspr::FreeFinder::Pick> picks;
    double txHz = 1500.0;

    QSize sizeHint() const override
    {
        return QSize(600, fontMetrics().lineSpacing() * 10);
    }

    // A new live line on top of the waterfall (one per second, newest at the top).
    void addLine(const wspr::WindowSpectrum &l)
    {
        if (l.db.empty())
            return;
        const int w = int(l.db.size());
        const int h = 200; // lines kept (about 3 minutes)
        if (m_wf.width() != w || m_wf.height() != h)
        {
            m_wf = QImage(w, h, QImage::Format_RGB32);
            m_wf.fill(QColor(0x14, 0x18, 0x1e));
            m_lines = 0;
        }
        m_wfFirstHz = l.firstHz;
        m_wfBinHz = l.binHz;
        // scroll down one line
        std::memmove(m_wf.scanLine(1), m_wf.scanLine(0), size_t(m_wf.bytesPerLine()) * size_t(h - 1));
        // level: noise floor (median) to 25 dB above it
        std::vector<float> v = l.db;
        std::nth_element(v.begin(), v.begin() + std::ptrdiff_t(v.size() / 2), v.end());
        const float floorDb = v[v.size() / 2];
        auto *row = reinterpret_cast<QRgb *>(m_wf.scanLine(0));
        for (int x = 0; x < w; ++x)
            row[x] = colour((l.db[size_t(x)] - floorDb + 3.0f) / 25.0f);
        ++m_lines;
        update();
    }
    void clearWaterfall()
    {
        m_wf = QImage();
        m_lines = 0;
        update();
    }

private:
    static QRgb colour(float t)
    {
        t = std::clamp(t, 0.0f, 1.0f);
        // dark blue -> blue -> cyan -> yellow -> white
        static const float stops[5][3] = {{0x10, 0x14, 0x30}, {0x20, 0x40, 0xc0}, {0x20, 0xc0, 0xd0}, {0xf0, 0xe0, 0x30}, {0xff, 0xff, 0xff}};
        const float p = t * 4.0f;
        const int i = std::min(3, int(p));
        const float f = p - float(i);
        return qRgb(int(stops[i][0] + (stops[i + 1][0] - stops[i][0]) * f),
                    int(stops[i][1] + (stops[i + 1][1] - stops[i][1]) * f),
                    int(stops[i][2] + (stops[i + 1][2] - stops[i][2]) * f));
    }
    QImage m_wf;
    int m_lines = 0;
    double m_wfFirstHz = 0.0, m_wfBinHz = 0.0;

public:

protected:
    static constexpr double kLo = 1390.0, kHi = 1610.0;
    double xFor(double hz) const
    {
        return (hz - kLo) / (kHi - kLo) * width();
    }
    double hzFor(double x) const
    {
        return kLo + x / std::max(1, width()) * (kHi - kLo);
    }

    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.fillRect(rect(), QColor(0x14, 0x18, 0x1e));
        const int h = height();
        const int axis = fontMetrics().height() + 2;
        const int plotH = h - axis;
        // Window edges (1400 / 1600 Hz) and the 10 Hz grid.
        p.fillRect(QRectF(0, 0, xFor(1400.0), plotH), QColor(0x22, 0x22, 0x22));
        p.fillRect(QRectF(xFor(1600.0), 0, width() - xFor(1600.0), plotH), QColor(0x22, 0x22, 0x22));
        p.setPen(QColor(0x2c, 0x34, 0x40));
        for (int hz = 1400; hz <= 1600; hz += 10)
            p.drawLine(QPointF(xFor(hz), 0), QPointF(xFor(hz), plotH));
        p.setPen(QColor(0x9a, 0xa4, 0xb0));
        for (int hz = 1400; hz <= 1600; hz += 50)
        {
            const QString t = QString::number(hz);
            const int w = fontMetrics().horizontalAdvance(t);
            p.drawText(QPointF(xFor(hz) - w / 2.0, h - 3), t);
        }
        // Live waterfall (newest line at the top), stretched to the window's frequency scale.
        if (!m_wf.isNull() && m_wfBinHz > 0.0)
        {
            const double loHz = m_wfFirstHz - m_wfBinHz / 2.0;
            const double hiHz = m_wfFirstHz + (m_wf.width() - 0.5) * m_wfBinHz;
            const int shown = std::min(m_lines, m_wf.height());
            const double lineH = std::max(1.0, double(plotH) / 120.0); // about 2 minutes fill the strip
            const int rows = std::min(shown, int(plotH / lineH) + 1);
            if (rows > 0)
                p.drawImage(QRectF(xFor(loHz), 0, xFor(hiHz) - xFor(loHz), rows * lineH),
                            m_wf,
                            QRectF(0, 0, m_wf.width(), rows));
            p.setPen(QColor(0x2c, 0x34, 0x40, 120));
            for (int hz = 1400; hz <= 1600; hz += 10)
                p.drawLine(QPointF(xFor(hz), 0), QPointF(xFor(hz), plotH));
        }
        // Busy ranges: a band along the top (orange = decoded, grey = heard only).
        const int band = fontMetrics().height() + 2;
        for (const wspr::FreeFinder::Busy &b : busy)
        {
            const QRectF r(xFor(b.lowHz), 0, std::max(2.0, xFor(b.highHz) - xFor(b.lowHz)), m_wf.isNull() ? plotH : band * 4);
            p.fillRect(r, b.decoded ? QColor(0xc0, 0x70, 0x20, 140) : QColor(0x90, 0x90, 0x90, 110));
            if (b.decoded && !b.call.empty())
            {
                p.save();
                p.setPen(QColor(0xf0, 0xc0, 0x80));
                p.translate(r.center().x() + fontMetrics().ascent() / 2.0, 3);
                p.rotate(90);
                p.drawText(QPointF(0, 0), QString::fromStdString(b.call).left(14));
                p.restore();
            }
        }
        // Spectrum of the last decoded slot (only until the live waterfall has lines).
        if (!spectrum.db.empty() && m_wf.isNull())
        {
            float lo = spectrum.db[0], hi = spectrum.db[0];
            for (float v : spectrum.db)
            {
                lo = std::min(lo, v);
                hi = std::max(hi, v);
            }
            hi = std::max(hi, lo + 6.0f);
            QPainterPath path;
            for (size_t k = 0; k < spectrum.db.size(); ++k)
            {
                const double hz = spectrum.firstHz + double(k) * spectrum.binHz;
                const double y = plotH - 2 - (spectrum.db[k] - lo) / (hi - lo) * (plotH - 6);
                if (k == 0)
                    path.moveTo(xFor(hz), y);
                else
                    path.lineTo(xFor(hz), y);
            }
            p.setPen(QPen(QColor(0x60, 0xc0, 0xff), 1.0));
            p.drawPath(path);
        }
        // Free suggestions (green) and the TX offset (red).
        p.setPen(QPen(QColor(0x40, 0xd0, 0x60), 2.0, Qt::DashLine));
        for (const wspr::FreeFinder::Pick &k : picks)
            p.drawLine(QPointF(xFor(k.hz), 0), QPointF(xFor(k.hz), plotH));
        const double half = wspr::kSignalWidthHz / 2.0;
        // TX offset: an outline over the waterfall (a filled band would hide the traces).
        p.setPen(QPen(QColor(0xff, 0x50, 0x50), 1.0));
        p.setBrush(m_wf.isNull() ? QColor(0xe0, 0x30, 0x30, 120) : QColor(0xe0, 0x30, 0x30, 40));
        p.drawRect(QRectF(xFor(txHz - half), 0, xFor(txHz + half) - xFor(txHz - half), plotH));
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(QColor(0xff, 0x50, 0x50), 2.0));
        p.drawLine(QPointF(xFor(txHz), 0), QPointF(xFor(txHz), plotH));
    }

    void mousePressEvent(QMouseEvent *e) override
    {
        if (e->button() == Qt::LeftButton && onClick)
            onClick(std::clamp(std::round(hzFor(e->position().x())), 1410.0, 1590.0));
    }
    void mouseMoveEvent(QMouseEvent *e) override
    {
        const double hz = hzFor(e->position().x());
        QString tip = tr("%1 Hz").arg(qRound(hz));
        for (const wspr::FreeFinder::Busy &b : busy)
            if (hz >= b.lowHz && hz <= b.highHz)
                tip += b.decoded ? tr(" - in use: %1").arg(QString::fromStdString(b.call))
                                 : tr(" - in use (signal heard, not decoded)");
        setToolTip(tip + tr("\nClick to transmit here."));
    }
};

// ---------------------------------------------------------------------------

namespace
{
QString wattsText(int dbm)
{
    const double w = std::pow(10.0, (dbm - 30) / 10.0);
    if (w >= 1.0)
        return QStringLiteral("%1 W").arg(w, 0, 'g', 3);
    if (w >= 0.001)
        return QStringLiteral("%1 mW").arg(w * 1000.0, 0, 'g', 3);
    return QStringLiteral("%1 uW").arg(w * 1e6, 0, 'g', 3);
}

double distanceKm(const QString &a, const QString &b)
{
    double la1, lo1, la2, lo2;
    if (!wwv::gridToLatLon(a.toStdString(), la1, lo1) || !wwv::gridToLatLon(b.toStdString(), la2, lo2))
        return -1.0;
    const double r = M_PI / 180.0;
    const double d = std::sin(la1 * r) * std::sin(la2 * r) + std::cos(la1 * r) * std::cos(la2 * r) * std::cos((lo2 - lo1) * r);
    return 6371.0 * std::acos(std::clamp(d, -1.0, 1.0));
}
} // namespace

WsprPanel::WsprPanel(WsprEngine *engine, WsprNet *net, QSettings *settings, QWidget *parent)
    : QWidget(parent)
    , m_w(engine)
    , m_net(net)
    , m_settings(settings)
{
    auto *root = new QVBoxLayout(this);
    root->addWidget(buildTop());

    m_table = new QTableWidget(0, 10);
    m_table->setHorizontalHeaderLabels({tr("UTC"),
                                        tr("dB"),
                                        tr("DT"),
                                        tr("MHz"),
                                        tr("Drift"),
                                        tr("Call"),
                                        tr("Grid"),
                                        tr("dBm"),
                                        tr("km"),
                                        tr("Message")});
    m_table->verticalHeader()->hide();
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->setToolTip(tr("Stations decoded, newest first. DT: start time against your clock (s). "
                           "Drift: Hz per minute. Every spot is also written to wspr-spots.txt in the logs folder."));
    root->addWidget(m_table, 1);
    if (m_settings)
        m_table->horizontalHeader()->restoreState(m_settings->value(QStringLiteral("WSPR/TableHeader")).toByteArray());

    connect(m_w, &ModemEngine::settingsChanged, this, &WsprPanel::syncSettings);
    connect(m_w, &ModemEngine::statusChanged, m_status, &QLabel::setText);
    connect(m_w, &WsprEngine::slotChanged, this, &WsprPanel::syncSlot);
    connect(m_w, &WsprEngine::freeChanged, this, &WsprPanel::syncFree);
    connect(m_w, &WsprEngine::slotDecoded, this, &WsprPanel::addSlot);
    connect(m_w, &WsprEngine::liveLine, this, [this](const wspr::WindowSpectrum &l) { m_view->addLine(l); });
    connect(m_w, &ModemEngine::transmittingChanged, this, &WsprPanel::syncSlot);
    connect(m_net, &WsprNet::statusChanged, this, &WsprPanel::syncUpload);
    auto *clock = new QTimer(this);
    clock->setInterval(500);
    connect(clock, &QTimer::timeout, this, &WsprPanel::syncSlot);
    clock->start();

    syncSettings();
    syncFree();
    syncUpload();
    syncSlot();
    m_status->setText(m_w->status());
}

QWidget *WsprPanel::buildTop()
{
    auto *top = new QWidget();
    auto *v = new QVBoxLayout(top);
    v->setContentsMargins(0, 0, 0, 0);

    // ---- receive and the slot clock
    auto *r1 = new QHBoxLayout();
    m_decode = new UiStyle::DecodeButton();
    UiStyle::decodeButton(m_decode);
    m_decode->setToolTip(tr("Decode WSPR in 2-minute slots starting on even UTC minutes (USB dial, signals "
                            "1400-1600 Hz audio). The PC clock must be right to about a second."));
    r1->addWidget(m_decode);
    r1->addWidget(new QLabel(tr("Depth")));
    m_depth = new QComboBox();
    m_depth->addItem(tr("Fast"), 1);
    m_depth->addItem(tr("Normal"), 2);
    m_depth->addItem(tr("Deep"), 3);
    m_depth->setToolTip(tr("Fast: one pass. Normal: a second pass after taking out the strong signals. "
                           "Deep: a third pass without drift search (finds a few more weak ones, takes longer)."));
    r1->addWidget(m_depth);
    auto *clear = new QPushButton(tr("Clear"));
    r1->addWidget(clear);
    r1->addStretch(1);
    m_clock = new QLabel();
    r1->addWidget(m_clock);
    m_slotBar = new QProgressBar();
    m_slotBar->setRange(0, 120);
    m_slotBar->setTextVisible(false);
    m_slotBar->setMaximumWidth(160);
    m_slotBar->setMaximumHeight(fontMetrics().height());
    r1->addWidget(m_slotBar);
    v->addLayout(r1);

    // ---- beacon
    auto *r2 = new QHBoxLayout();
    m_txEnable = new QPushButton(tr("Enable TX"));
    m_txEnable->setCheckable(true);
    UiStyle::greenWhenOn(m_txEnable);
    m_txEnable->setToolTip(tr("Send your WSPR beacon in about the chosen share of slots (never two in a row). "
                              "Always off when the app starts. Transmit and band hopping only run while this "
                              "tab is shown."));
    r2->addWidget(m_txEnable);
    r2->addWidget(new QLabel(tr("TX")));
    m_pct = new QSpinBox();
    m_pct->setRange(0, 100);
    m_pct->setSingleStep(5);
    m_pct->setSuffix(tr(" %"));
    m_pct->setToolTip(tr("Share of slots to transmit in. 20 % (one slot in five) is usual; up to 50 % the "
                         "app never sends two slots in a row, so you also hear who hears you."));
    r2->addWidget(m_pct);
    m_txNext = new QPushButton(tr("TX next slot"));
    m_txNext->setToolTip(tr("Transmit in the next slot once, whatever the percentage (Enable TX must be on)."));
    r2->addWidget(m_txNext);
    r2->addSpacing(12);
    r2->addWidget(new QLabel(tr("My call")));
    m_call = new QLineEdit();
    m_call->setMaximumWidth(fontMetrics().horizontalAdvance(QStringLiteral("MMMMMMMMMMM")));
    m_call->setToolTip(tr("Your call, shared by every tab. Up to 6 characters with a digit in the 2nd or 3rd "
                          "place, or with one / prefix or suffix (sent as two alternating messages)."));
    r2->addWidget(m_call);
    r2->addWidget(new QLabel(tr("Grid")));
    m_grid = new QLineEdit();
    m_grid->setMaxLength(6);
    m_grid->setMaximumWidth(fontMetrics().horizontalAdvance(QStringLiteral("MMMMMMMM")));
    m_grid->setToolTip(tr("Your Maidenhead locator (EN71 or EN71ar), shared with PSK Reporter and GridTracker. "
                          "With 6 characters the beacon alternates \"CALL EN71 30\" and \"<CALL> EN71AR 30\"."));
    r2->addWidget(m_grid);
    r2->addWidget(new QLabel(tr("Power")));
    m_dbm = new QComboBox();
    for (int d = 0; d <= 60; ++d)
        if (wspr::validDbm(d))
            m_dbm->addItem(tr("%1 dBm (%2)").arg(d).arg(wattsText(d)), d);
    m_dbm->setToolTip(tr("The power you report in the message: what reaches the antenna. It does not set or "
                         "measure the radio's power, so set the radio to match (less any feed-line loss)."));
    r2->addWidget(m_dbm);
    m_autoPower = new QCheckBox(tr("From radio"));
    m_autoPower->setToolTip(tr("For radios whose wfview reports output power in watts: after each transmission, "
                               "Power is set from what the radio reported putting out (less the feed-line loss), "
                               "for the next message. The first message uses the Power shown. The app never "
                               "changes the radio's power setting.\nLeave this off if the TX meter in the top bar "
                               "shows percent rather than watts."));
    r2->addWidget(m_autoPower);
    m_loss = new QDoubleSpinBox();
    m_loss->setRange(0.0, 10.0);
    m_loss->setSingleStep(0.5);
    m_loss->setDecimals(1);
    m_loss->setPrefix(tr("loss "));
    m_loss->setSuffix(tr(" dB"));
    m_loss->setKeyboardTracking(false);
    m_loss->setToolTip(tr("Feed-line (and tuner) loss between the radio and the antenna, taken off the measured "
                          "power. 100 ft of RG-8X on 20 m is about 1.5 dB."));
    r2->addWidget(m_loss);
    r2->addSpacing(12);
    r2->addWidget(new QLabel(tr("TX level")));
    m_level = new TxLevelSlider();
    m_level->setSliderToolTip(tr("Audio level of the WSPR tones. Keep ALC at or near zero: WSPR is a single "
                                 "steady tone, so any ALC action only adds distortion."));
    r2->addWidget(m_level, 1);
    v->addLayout(r2);
    auto *rm = new QHBoxLayout();
    m_msgs = new QLabel();
    m_msgs->setTextFormat(Qt::RichText);
    rm->addWidget(m_msgs);
    m_powerNote = new QLabel();
    rm->addWidget(m_powerNote, 1);
    v->addLayout(rm);

    // ---- offset and free suggestions
    auto *r3 = new QHBoxLayout();
    r3->addWidget(new QLabel(tr("TX offset")));
    m_offset = new QSpinBox();
    m_offset->setRange(1410, 1590);
    m_offset->setSuffix(tr(" Hz"));
    m_offset->setKeyboardTracking(false);
    m_offset->setToolTip(tr("Audio frequency of your beacon (the RF is the dial plus this). Click the window "
                            "below or a free offset to change it."));
    r3->addWidget(m_offset);
    m_autoFree = new QCheckBox(tr("Move to a free offset if mine is in use"));
    m_autoFree->setToolTip(tr("Before each transmission: if a station was heard on your offset in the last 20 "
                              "minutes on this band, move to the clearest suggested offset."));
    r3->addWidget(m_autoFree);
    r3->addSpacing(12);
    r3->addWidget(new QLabel(tr("Free:")));
    for (QPushButton *&b : m_pick)
    {
        b = new QPushButton();
        b->setToolTip(tr("Transmit on this offset: clear of every station heard on this band in the last 20 "
                         "minutes."));
        r3->addWidget(b);
        connect(b,
                &QPushButton::clicked,
                this,
                [this, b]()
                {
                    const int hz = b->property("hz").toInt();
                    if (hz > 0)
                        m_w->setTxOffsetHz(hz);
                });
    }
    m_busy = new QLabel();
    r3->addWidget(m_busy, 1);
    v->addLayout(r3);
    m_view = new WsprWindowView();
    m_view->onClick = [this](double hz) { m_w->setTxOffsetHz(hz); };
    m_view->setToolTip(tr("The WSPR window, 1400-1600 Hz: live waterfall (one line a second while Decode is on; a "
                          "WSPR station is a thin line about 6 Hz wide), orange = decoded stations, grey = "
                          "signals heard but not decoded, green = free offsets, red = your TX offset. Busy offsets "
                          "are kept for 20 minutes per band."));
    v->addWidget(m_view);

    // ---- bands, hopping, uploads
    auto *r4 = new QHBoxLayout();
    r4->addWidget(new QLabel(tr("Band")));
    m_band = new QComboBox();
    for (const wspr::Band &b : wspr::bands())
        m_band->addItem(tr("%1  %2 MHz").arg(QLatin1String(b.name)).arg(double(b.dialHz) / 1e6, 0, 'f', 4),
                        QVariant::fromValue<qlonglong>(b.dialHz));
    m_band->setToolTip(tr("The usual WSPR dial frequencies (USB). 60 m: check your licence's 60 m rules "
                          "before transmitting."));
    r4->addWidget(m_band);
    m_go = new QPushButton(tr("Go"));
    m_go->setToolTip(tr("Tune the radio to this band's WSPR dial in USB (a data mode if the radio is in one)."));
    r4->addWidget(m_go);
    r4->addSpacing(12);
    r4->addWidget(new QLabel(tr("Band hopping")));
    m_hop = new QComboBox();
    m_hop->addItem(tr("Off"), int(wspr::HopPlan::Off));
    m_hop->addItem(tr("Coordinated (WSJT-X schedule)"), int(wspr::HopPlan::Coordinated));
    m_hop->addItem(tr("My bands in turn"), int(wspr::HopPlan::Rotate));
    m_hop->setToolTip(tr("Coordinated: the same schedule as WSJT-X, so every hopping station is on the same "
                         "band at once (160, 80, 60, 40, 30, 20, 17, 15, 12, 10 m, one per slot, every 20 "
                         "minutes); bands you did not tick are skipped (the radio stays put).\n"
                         "My bands in turn: the ticked bands one after another, one slot each.\n"
                         "Hopping changes the radio's frequency every 2 minutes while Decode or Enable TX is "
                         "on and this tab is shown."));
    r4->addWidget(m_hop);
    m_hopBands = new QPushButton(tr("Bands..."));
    m_hopBands->setToolTip(tr("Which bands to hop to, and on which of them the beacon may transmit."));
    r4->addWidget(m_hopBands);
    r4->addSpacing(12);
    m_upload = new QCheckBox(tr("Upload spots to WSPRnet"));
    m_upload->setToolTip(tr("Send every decoded spot to wsprnet.org after each slot (with your call and grid), "
                            "and a \"listening\" report when nothing was heard. Off by default.\n"
                            "PSK Reporter and GridTracker also get WSPR spots when they are on "
                            "(Options > PSK Reporter / GridTracker)."));
    r4->addWidget(m_upload);
    m_uploadState = new QLabel();
    r4->addWidget(m_uploadState, 1);
    v->addLayout(r4);

    m_clockHint = new QLabel();
    m_clockHint->setWordWrap(true);
    v->addWidget(m_clockHint);
    m_status = new QLabel();
    m_status->setWordWrap(true);
    v->addWidget(m_status);

    // ---- wiring
    connect(m_decode, &QPushButton::toggled, m_w, &ModemEngine::setDecoding);
    connect(m_depth,
            &QComboBox::currentIndexChanged,
            this,
            [this](int) { m_w->setPasses(m_depth->currentData().toInt()); });
    connect(clear, &QPushButton::clicked, this, [this]() { m_table->setRowCount(0); });
    connect(m_txEnable, &QPushButton::toggled, m_w, &WsprEngine::setTxEnabled);
    connect(m_pct, &QSpinBox::valueChanged, m_w, &WsprEngine::setTxPercent);
    connect(m_txNext, &QPushButton::clicked, m_w, &WsprEngine::txNextSlot);
    connect(m_call, &QLineEdit::editingFinished, this, [this]() { m_w->setMyCall(m_call->text()); });
    connect(m_grid,
            &QLineEdit::editingFinished,
            this,
            [this]()
            {
                const QString g = m_grid->text().trimmed();
                if (g.isEmpty() || wspr::validGrid(g.toStdString()))
                    m_w->setGrid(g.size() == 6 ? g.left(4).toUpper() + g.mid(4).toLower() : g.toUpper());
                else
                    syncSettings(); // put the last good one back
            });
    connect(m_dbm, &QComboBox::currentIndexChanged, this, [this](int) { m_w->setDbm(m_dbm->currentData().toInt()); });
    connect(m_level, &TxLevelSlider::levelChanged, m_w, &ModemEngine::setTxLevel);
    connect(m_autoPower, &QCheckBox::toggled, m_w, &WsprEngine::setAutoPower);
    connect(m_loss, &QDoubleSpinBox::valueChanged, m_w, &WsprEngine::setFeedLossDb);
    connect(m_offset, &QSpinBox::valueChanged, this, [this](int hz) { m_w->setTxOffsetHz(hz); });
    connect(m_autoFree, &QCheckBox::toggled, m_w, &WsprEngine::setAutoFree);
    connect(m_go,
            &QPushButton::clicked,
            this,
            [this]()
            {
                const qint64 hz = m_band->currentData().toLongLong();
                if (hz > 0)
                    emit bandRequested(hz, tr("WSPR %1").arg(m_band->currentText().section(QLatin1Char(' '), 0, 1)));
            });
    connect(m_hop,
            &QComboBox::currentIndexChanged,
            this,
            [this](int)
            {
                wspr::HopPlan p = m_w->hopPlan();
                p.mode = wspr::HopPlan::Mode(m_hop->currentData().toInt());
                bool any = false;
                for (bool b : p.rx)
                    any = any || b;
                m_w->setHopPlan(p);
                if (p.mode != wspr::HopPlan::Off && !any)
                    editHopBands();
            });
    connect(m_hopBands, &QPushButton::clicked, this, &WsprPanel::editHopBands);
    connect(m_upload, &QCheckBox::toggled, m_net, &WsprNet::setEnabled);
    return top;
}

void WsprPanel::editHopBands()
{
    QDialog dlg(this);
    dlg.setWindowTitle(tr("WSPR band hopping"));
    auto *v = new QVBoxLayout(&dlg);
    v->addWidget(new QLabel(tr("Tick the bands to hop to, and where the beacon may transmit.\n"
                               "Coordinated hopping only uses 160-10 m (one band per slot, every 20 minutes).")));
    auto *g = new QGridLayout();
    g->addWidget(new QLabel(tr("<b>Band</b>")), 0, 0);
    g->addWidget(new QLabel(tr("<b>Hop to</b>")), 0, 1);
    g->addWidget(new QLabel(tr("<b>Transmit</b>")), 0, 2);
    const wspr::HopPlan plan = m_w->hopPlan();
    std::vector<QCheckBox *> rx, tx;
    for (size_t k = 0; k < wspr::bands().size(); ++k)
    {
        const int row = int(k) + 1;
        QString note;
        if (wspr::bands()[k].dialHz == 5287200)
            note = tr(" - not a US amateur frequency (receive only in the US)");
        else if (wspr::bands()[k].dialHz == 5364700)
            note = tr(" - US: 9.15 W ERP max (Power 37 dBm or less)");
        g->addWidget(new QLabel(tr("%1 (%2 MHz)%3")
                                    .arg(QLatin1String(wspr::bands()[k].name))
                                    .arg(double(wspr::bands()[k].dialHz) / 1e6, 0, 'f', 4)
                                    .arg(note)),
                     row,
                     0);
        auto *a = new QCheckBox();
        auto *b = new QCheckBox();
        a->setChecked(k < plan.rx.size() && plan.rx[k]);
        b->setChecked(k < plan.tx.size() && plan.tx[k]);
        g->addWidget(a, row, 1, Qt::AlignHCenter);
        g->addWidget(b, row, 2, Qt::AlignHCenter);
        rx.push_back(a);
        tx.push_back(b);
    }
    v->addLayout(g);
    auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    v->addWidget(bb);
    if (dlg.exec() != QDialog::Accepted)
        return;
    wspr::HopPlan p = plan;
    p.rx.assign(wspr::bands().size(), false);
    p.tx.assign(wspr::bands().size(), false);
    for (size_t k = 0; k < wspr::bands().size(); ++k)
    {
        p.rx[k] = rx[k]->isChecked();
        p.tx[k] = tx[k]->isChecked();
    }
    m_w->setHopPlan(p);
}

void WsprPanel::saveState()
{
    if (m_settings)
        m_settings->setValue(QStringLiteral("WSPR/TableHeader"), m_table->horizontalHeader()->saveState());
}

void WsprPanel::setRadioState(bool ready, bool canTransmit, const QString &mode)
{
    Q_UNUSED(mode);
    m_ready = ready;
    m_canTx = ready && canTransmit;
    m_go->setEnabled(ready);
    m_txNext->setEnabled(m_canTx);
    syncSlot();
}

void WsprPanel::syncSettings()
{
    const QSignalBlocker b1(m_decode), b2(m_depth), b3(m_txEnable), b4(m_pct), b5(m_dbm), b6(m_offset),
        b7(m_autoFree), b8(m_hop), b9(m_autoPower), b10(m_loss);
    m_autoPower->setChecked(m_w->autoPower());
    m_loss->setValue(m_w->feedLossDb());
    m_loss->setEnabled(m_w->autoPower());
    m_powerNote->setText(m_w->powerNote());
    m_decode->setChecked(m_w->decoding());
    m_depth->setCurrentIndex(std::max(0, m_depth->findData(m_w->passes())));
    m_txEnable->setChecked(m_w->txEnabled());
    m_pct->setValue(m_w->txPercent());
    m_dbm->setCurrentIndex(std::max(0, m_dbm->findData(m_w->dbm())));
    if (!m_offset->hasFocus())
        m_offset->setValue(qRound(m_w->txOffsetHz()));
    m_autoFree->setChecked(m_w->autoFree());
    m_hop->setCurrentIndex(std::max(0, m_hop->findData(int(m_w->hopPlan().mode))));
    m_hopBands->setEnabled(m_w->hopPlan().mode != wspr::HopPlan::Off);
    if (!m_call->hasFocus())
        m_call->setText(m_w->myCall());
    if (!m_grid->hasFocus())
        m_grid->setText(m_w->grid());
    m_level->setLevel(m_w->txLevel());

    QString err;
    const QStringList msgs = m_w->messages(&err);
    if (msgs.isEmpty())
        m_msgs->setText(tr("<span style='color:#e07070'>Cannot transmit: %1.</span>").arg(err.toHtmlEscaped()));
    else
    {
        QStringList esc;
        for (const QString &m : msgs)
            esc << m.toHtmlEscaped();
        m_msgs->setText(tr("Sends: <b>%1</b>").arg(esc.join(tr("</b>, then <b>"))));
    }
    m_view->txHz = m_w->txOffsetHz();
    m_view->update();
    syncFree();
}

void WsprPanel::syncSlot()
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const qint64 slot = WsprEngine::slotStart(now);
    const int sec = int((now - slot) / 1000);
    m_slotBar->setValue(sec);
    const QString utc = QDateTime::fromMSecsSinceEpoch(now, QTimeZone::utc()).toString(QStringLiteral("HH:mm:ss"));
    QString state = m_w->slotState();
    if (m_w->transmitting())
        state = tr("TX %1 %").arg(qRound(m_w->txProgress() * 100.0));
    else if (m_w->nextIsTx())
        state += tr("  -  next slot: transmit");
    else if (m_w->txNextForced())
        state += tr("  -  next slot: transmit (asked)");
    m_clock->setText(tr("%1 UTC   %2:%3   %4")
                         .arg(utc)
                         .arg(sec / 60)
                         .arg(sec % 60, 2, 10, QLatin1Char('0'))
                         .arg(state));
    m_slotBar->setStyleSheet(m_w->transmitting() ? QStringLiteral("QProgressBar::chunk { background: #b03030; }")
                                                 : QString());
    QString hint;
    if ((m_w->txEnabled() || m_w->hopPlan().mode != wspr::HopPlan::Off) && !m_w->active())
        hint = tr("Transmit and band hopping are paused while another tab is shown.");
    // US 60 m segment 5351.5-5366.5 kHz: 9.15 W ERP; 40 dBm (10 W) and up is over it.
    const qint64 dial = m_w->radioDial();
    if (dial + 1600 > 5351500 && dial + 1400 < 5366500 && m_w->dbm() >= 40)
        hint = (hint.isEmpty() ? QString() : hint + QLatin1Char(' '))
            + tr("60 m: US stations may run at most 9.15 W ERP in 5351.5-5366.5 kHz; Power is %1 dBm (%2). "
                 "Use 37 dBm (5 W) or less, and set the radio's power to match.")
                  .arg(m_w->dbm())
                  .arg(wattsText(m_w->dbm()));
    else if (dial > 5280000 && dial < 5300000 && m_w->txEnabled())
        hint = (hint.isEmpty() ? QString() : hint + QLatin1Char(' '))
            + tr("5.2872 MHz is not a US amateur frequency: receive only in the US.");
    double dt = 0.0;
    int n = 0;
    if (m_w->clockHint(dt, n) && std::fabs(dt) > 1.0)
    {
        if (!hint.isEmpty())
            hint += QLatin1Char(' ');
        hint += tr("The last %1 stations were heard %2 s %3 on average: your PC clock may be %4 by about that "
                   "much (WSPR needs it within about a second; the WWV Time tab can check it).")
                    .arg(n)
                    .arg(std::fabs(dt), 0, 'f', 1)
                    .arg(dt > 0 ? tr("late") : tr("early"))
                    .arg(dt > 0 ? tr("fast") : tr("slow"));
    }
    m_clockHint->setText(hint.isEmpty() ? QString() : QStringLiteral("<span style='color:#e0b040'>%1</span>").arg(hint));
    m_clockHint->setVisible(!hint.isEmpty());
    m_txNext->setEnabled(m_canTx && m_w->txEnabled());
}

void WsprPanel::syncFree()
{
    m_view->busy = m_w->busy();
    m_view->picks = m_w->suggestions();
    m_view->spectrum = m_w->lastSpectrum();
    m_view->txHz = m_w->txOffsetHz();
    m_view->update();
    const int heard = m_w->slotsHeard();
    for (int k = 0; k < 3; ++k)
    {
        QPushButton *b = m_pick[k];
        if (heard > 0 && size_t(k) < m_view->picks.size())
        {
            const wspr::FreeFinder::Pick &pk = m_view->picks[size_t(k)];
            b->setText(pk.clearanceHz > 0.0 ? tr("%1 Hz").arg(pk.hz) : tr("%1 Hz *").arg(pk.hz));
            b->setToolTip(pk.clearanceHz > 0.0
                              ? tr("Clear of every station heard on this band in the last 20 minutes (by %1 Hz).")
                                    .arg(pk.clearanceHz, 0, 'f', 0)
                              : tr("No offset is clear: this is one of the least used (%n signal(s) heard here in "
                                   "the last 20 minutes).",
                                   "",
                                   pk.uses));
            b->setProperty("hz", m_view->picks[size_t(k)].hz);
            b->setEnabled(true);
        }
        else
        {
            b->setText(QStringLiteral("-"));
            b->setProperty("hz", 0);
            b->setEnabled(false);
        }
    }
    QString s;
    if (heard == 0)
        s = tr("(listen for a slot or two to find what is in use)");
    else
    {
        int decoded = 0, energy = 0;
        for (const wspr::FreeFinder::Busy &b : m_view->busy)
            (b.decoded ? decoded : energy) += 1;
        const double cl = m_w->clearance(m_w->txOffsetHz());
        s = tr("%1 decoded, %2 other signal(s) in the last %3 slot(s).").arg(decoded).arg(energy).arg(heard);
        if (!m_view->picks.empty() && m_view->picks.front().clearanceHz <= 0.0)
            s += tr(" Crowded: no clear offset, * = least used.");
        if (cl <= 0.0)
            s += tr(" <span style='color:#e07070'>Your offset is in use (%n signal(s) heard on it).</span>",
                    "",
                    m_w->uses(m_w->txOffsetHz()));
        else if (cl < 1e8)
            s += tr(" Your offset is clear by %1 Hz.").arg(cl, 0, 'f', 0);
    }
    m_busy->setText(s);
}

void WsprPanel::syncUpload()
{
    const QSignalBlocker b(m_upload);
    m_upload->setChecked(m_net->enabled());
    m_uploadState->setText(m_net->enabled() ? m_net->statusText() : QString());
}

void WsprPanel::addSlot(const WsprSlot &s)
{
    const QString utc = QDateTime::fromMSecsSinceEpoch(s.slotMs, QTimeZone::utc()).toString(QStringLiteral("HH:mm"));
    const QString myGrid = m_w->grid();
    // Newest first: this slot's spots go on top, strongest first.
    std::vector<wspr::Spot> spots = s.spots;
    std::sort(spots.begin(), spots.end(), [](const wspr::Spot &a, const wspr::Spot &b) { return a.snr < b.snr; });
    for (const wspr::Spot &p : spots)
    {
        m_table->insertRow(0);
        const QString grid = QString::fromStdString(p.grid);
        const double km = grid.isEmpty() ? -1.0 : distanceKm(myGrid, grid);
        const QStringList cells = {utc,
                                   QString::number(qRound(p.snr)),
                                   QString::number(p.dt, 'f', 1),
                                   QString::number(double(p.rfHz) / 1e6, 'f', 6),
                                   QString::number(qRound(p.drift)),
                                   QString::fromStdString(p.call),
                                   grid,
                                   QString::number(p.dbm),
                                   km >= 0 ? QString::number(qRound(km)) : QString(),
                                   QString::fromStdString(p.message)};
        for (int c = 0; c < cells.size(); ++c)
        {
            auto *it = new QTableWidgetItem(cells.at(c));
            if (c >= 1 && c <= 4)
                it->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            m_table->setItem(0, c, it);
        }
    }
    while (m_table->rowCount() > 1000)
        m_table->removeRow(m_table->rowCount() - 1);
    if (!s.spots.empty())
        m_table->resizeColumnsToContents();
}
