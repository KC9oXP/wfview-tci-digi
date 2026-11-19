#include "WwvPanel.h"

#include "ClockSet.h"
#include "UiStyle.h"
#include "WaterfallWidget.h"
#include "WwvEngine.h"

#include <QDateTime>
#include <QDoubleSpinBox>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTimeZone>
#include <QTimer>
#include <QVBoxLayout>
#include <cmath>

namespace
{
const char *const kLampOff = "QLabel { background-color: #33373c; color: #7c8085; border: 1px solid #4a4f55;"
                             " border-radius: 3px; padding: 2px 8px; }";
const char *const kLampOn = "QLabel { background-color: #1d6a33; color: #eaffea; border: 1px solid #2f9a4c;"
                            " border-radius: 3px; padding: 2px 8px; font-weight: bold; }";

QString signedMs(double ms)
{
    return std::fabs(ms) >= 1000.0 ? QObject::tr("%1 s").arg(std::fabs(ms) / 1000.0, 0, 'f', 2)
                                   : QObject::tr("%1 ms").arg(std::fabs(ms), 0, 'f', 0);
}
} // namespace

WwvPanel::WwvPanel(WwvEngine *engine, QWidget *parent)
    : QWidget(parent)
    , m_wwv(engine)
{
    auto *root = new QVBoxLayout(this);

    // ---- decode + lamps
    auto *r1 = new QHBoxLayout();
    m_decode = new UiStyle::DecodeButton();
    UiStyle::decodeButton(m_decode);
    r1->addWidget(m_decode);
    m_lampTicks = makeLamp(tr("Ticks"),
                           tr("The second ticks (5 ms of 1000 Hz from WWV, 1200 Hz from WWVH) are being "
                              "followed."));
    m_lampTones = makeLamp(tr("Tones"),
                           tr("A 440, 500 or 600 Hz tone is on now (they are silent for part of each "
                              "minute)."));
    m_lampCode = makeLamp(tr("100 Hz code"),
                          tr("The 100 Hz time-code pulses are coming through. If this stays grey while Ticks is lit, "
                             "the radio's audio does not pass 100 Hz: use AM with the widest filter."));
    m_lampTime = makeLamp(tr("Time"),
                          tr("A full minute of time code was decoded. It counts as verified when two "
                             "minutes in a row agree."));
    m_lampCarrier = makeLamp(tr("Carrier"), tr("A carrier measurement found a steady carrier (Measure carrier)."));
    for (QLabel *l : {m_lampTicks, m_lampTones, m_lampCode, m_lampTime, m_lampCarrier})
        r1->addWidget(l);
    m_station = new QLabel();
    r1->addWidget(m_station);
    r1->addStretch(1);
    r1->addWidget(new QLabel(tr("Go to")));
    struct Std
    {
        qint64 hz;
        const char *label;
        const char *who;
    };
    static const Std stds[] = {
        {2500000, "2.5", QT_TR_NOOP("WWV (Colorado) and WWVH (Hawaii)")},
        {5000000, "5", QT_TR_NOOP("WWV (Colorado) and WWVH (Hawaii)")},
        {10000000, "10", QT_TR_NOOP("WWV (Colorado) and WWVH (Hawaii)")},
        {15000000, "15", QT_TR_NOOP("WWV (Colorado) and WWVH (Hawaii)")},
        {20000000, "20", QT_TR_NOOP("WWV (Colorado) only")},
        {25000000, "25", QT_TR_NOOP("WWV (Colorado) only; experimental, may be off the air")},
    };
    for (const Std &f : stds)
    {
        auto *b = new QPushButton(QString::fromLatin1(f.label));
        UiStyle::greenWhenOn(b);
        b->setProperty("hz", f.hz);
        b->setToolTip(
            tr("%1 MHz AM: %2. Green while the radio is on it.").arg(QString::fromLatin1(f.label), tr(f.who)));
        b->setMinimumWidth(QFontMetrics(b->font()).horizontalAdvance(QStringLiteral("2.5")) + 16);
        b->setMaximumWidth(QFontMetrics(b->font()).horizontalAdvance(QStringLiteral("2.5")) + 24);
        r1->addWidget(b);
        m_freqButtons.append(b);
        connect(b,
                &QPushButton::clicked,
                this,
                [this, b]()
                {
                    syncFreqButtons(); // the radio decides when it is green, not the click
                    if (m_wwv->radioReady() && !m_wwv->measuringCarrier())
                        emit tuneRequested(b->property("hz").toLongLong());
                });
    }
    r1->addWidget(new QLabel(tr("MHz")));
    root->addLayout(r1);

    auto *hint = new QLabel(tr("WWV (Colorado) and WWVH (Hawaii): 2.5, 5, 10, 15, 20 MHz (WWV also 25). Use AM with a "
                               "wide filter, so the 100 Hz time code and the tones come through as sent. A time takes "
                               "one to two minutes to decode, and one more to be verified."));
    hint->setWordWrap(true);
    hint->setEnabled(false);
    root->addWidget(hint);

    auto *cols = new QHBoxLayout();

    // ---- time
    auto *timeBox = new QGroupBox(tr("Time"));
    auto *tv = new QVBoxLayout(timeBox);
    m_time = new QLabel(QStringLiteral("--:--:--"));
    QFont big = m_time->font();
    big.setPointSizeF(big.pointSizeF() * 2.4);
    big.setBold(true);
    m_time->setFont(big);
    m_time->setTextInteractionFlags(Qt::TextSelectableByMouse);
    tv->addWidget(m_time);
    m_date = new QLabel();
    tv->addWidget(m_date);
    m_flags = new QLabel();
    m_flags->setWordWrap(true);
    tv->addWidget(m_flags);
    m_pc = new QLabel();
    m_pc->setWordWrap(true);
    QFont mid = m_pc->font();
    mid.setBold(true);
    m_pc->setFont(mid);
    tv->addWidget(m_pc);

    auto *g = new QGridLayout();
    g->addWidget(new QLabel(tr("My grid")), 0, 0);
    m_grid = new QLineEdit();
    m_grid->setMaximumWidth(90);
    m_grid->setPlaceholderText(QStringLiteral("EN71ar"));
    m_grid->setToolTip(tr("Your grid square (4 or 6 characters): the signal's travel time from the station is taken "
                          "off the clock comparison."));
    g->addWidget(m_grid, 0, 1);
    m_path = new QLabel();
    g->addWidget(m_path, 0, 2);
    g->addWidget(new QLabel(tr("Audio delay")), 1, 0);
    m_delay = new QDoubleSpinBox();
    m_delay->setRange(-5000, 5000);
    m_delay->setDecimals(0);
    m_delay->setSuffix(tr(" ms"));
    m_delay->setKeyboardTracking(false);
    m_delay->setToolTip(tr("How long the audio takes from the radio through wfview to this program. Kept for each "
                           "radio. Calibrate delay measures it."));
    g->addWidget(m_delay, 1, 1);
    m_calibrate = new QPushButton(tr("Calibrate delay"));
    m_calibrate->setToolTip(tr("Use while the PC clock is known to be right (just synchronised over the internet): "
                               "sets the audio delay so the PC clock reads 0 off. After that the comparison is good "
                               "to a few hundredths of a second without internet."));
    g->addWidget(m_calibrate, 1, 2);
    g->setColumnStretch(2, 1);
    tv->addLayout(g);
    m_setClock = new QPushButton(tr("Set PC clock to WWV"));
    m_setClock->setToolTip(ClockSet::supported()
                               ? tr("Shifts this computer's clock by the difference shown. Windows asks for "
                                    "administrator rights. On a domain or with internet time on, Windows may move "
                                    "it again later.")
                               : tr("Only available on Windows."));
    tv->addWidget(m_setClock);
    tv->addStretch(1);
    cols->addWidget(timeBox, 1);

    // ---- calibration
    auto *calBox = new QGroupBox(tr("Calibration"));
    auto *cv = new QVBoxLayout(calBox);
    m_clock = new QLabel();
    m_clock->setWordWrap(true);
    m_clock->setTextInteractionFlags(Qt::TextSelectableByMouse);
    cv->addWidget(m_clock);
    auto *trimRow = new QHBoxLayout();
    trimRow->addWidget(new QLabel(tr("Picture trim (SSTV and WEFAX)")));
    m_trim = new QSpinBox();
    m_trim->setRange(-5000, 5000);
    m_trim->setSuffix(tr(" ppm"));
    m_trim->setKeyboardTracking(false);
    m_trim->setToolTip(tr("The sound card clock correction used by the SSTV and WEFAX tabs (their Trim boxes). "
                          "Change it here or there; Copy puts the measured audio clock in."));
    trimRow->addWidget(m_trim);
    m_copyPpm = new QPushButton(tr("Copy audio clock to trim"));
    m_copyPpm->setToolTip(tr("Puts the audio clock error measured above into the Trim boxes of the SSTV and WEFAX "
                             "tabs, so pictures come out straight from the first line."));
    trimRow->addWidget(m_copyPpm);
    m_trimNote = new QLabel();
    trimRow->addWidget(m_trimNote);
    trimRow->addStretch(1);
    cv->addLayout(trimRow);
    m_tone = new QLabel();
    m_tone->setWordWrap(true);
    m_tone->setTextInteractionFlags(Qt::TextSelectableByMouse);
    cv->addWidget(m_tone);
    auto *line = new QFrame();
    line->setFrameShape(QFrame::HLine);
    line->setEnabled(false);
    cv->addWidget(line);
    m_measure = new QPushButton(tr("Measure carrier"));
    m_measure->setToolTip(tr("Finds how far the radio's dial is off: tunes 1 kHz below the WWV frequency you are on "
                             "in USB, then 1 kHz above in LSB, measures where the carrier lands each time (about "
                             "25 s in all) and puts the radio back. A dial error moves the two tones opposite "
                             "ways; anything in the sound path moves both the same way and is left out. "
                             "Click again to stop."));
    cv->addWidget(m_measure, 0, Qt::AlignLeft);
    m_carrier = new QLabel();
    m_carrier->setWordWrap(true);
    m_carrier->setTextInteractionFlags(Qt::TextSelectableByMouse);
    cv->addWidget(m_carrier);
    cv->addStretch(1);
    cols->addWidget(calBox, 1);
    root->addLayout(cols);

    m_symbols = new QLabel();
    QFont mono(QStringLiteral("Consolas"));
    mono.setStyleHint(QFont::Monospace);
    m_symbols->setFont(mono);
    m_symbols->setToolTip(tr("The last 60 seconds of time code: 0, 1, M = position marker, - = no pulse (second 0)."));
    root->addWidget(m_symbols);
    m_message = new QLabel();
    m_message->setWordWrap(true);
    m_message->setTextInteractionFlags(Qt::TextSelectableByMouse);
    root->addWidget(m_message);

    m_wf = new WaterfallWidget();
    m_wf->setMarker(1000.0, 10.0);
    m_wf->setGuides({100.0, 500.0, 600.0, 1200.0},
                    {QStringLiteral("100"), QStringLiteral("500"), QStringLiteral("600"), QStringLiteral("1200")});
    root->addWidget(m_wf, 1);

    connect(m_decode, &QPushButton::toggled, m_wwv, &WwvEngine::setDecoding);
    connect(m_grid, &QLineEdit::editingFinished, this, [this]() { m_wwv->setGrid(m_grid->text()); });
    connect(m_delay, &QDoubleSpinBox::valueChanged, m_wwv, &WwvEngine::setAudioDelayMs);
    connect(m_calibrate, &QPushButton::clicked, m_wwv, &WwvEngine::calibrateDelay);
    connect(m_setClock, &QPushButton::clicked, m_wwv, &WwvEngine::setPcClock);
    connect(m_copyPpm,
            &QPushButton::clicked,
            this,
            [this]()
            {
                emit trimPpmChosen(m_wwv->snap().st.clockPpm);
                syncTrim();
            });
    connect(m_trim,
            &QSpinBox::valueChanged,
            this,
            [this](int v)
            {
                emit trimPpmChosen(v);
                syncTrim();
            });
    connect(m_measure,
            &QPushButton::clicked,
            this,
            [this]()
            {
                if (m_wwv->measuringCarrier())
                    m_wwv->cancelCarrier();
                else
                    m_wwv->measureCarrier();
            });
    connect(m_wwv, &WwvEngine::settingsChanged, this, &WwvPanel::sync);
    connect(m_wwv, &WwvEngine::statsChanged, this, &WwvPanel::syncStats);
    connect(m_wwv, &WwvEngine::messageChanged, m_message, &QLabel::setText);

    // the running clock
    m_tick = new QTimer(this);
    m_tick->setInterval(100);
    connect(m_tick, &QTimer::timeout, this, &WwvPanel::updateClockLine);
    connect(m_tick, &QTimer::timeout, this, &WwvPanel::syncFreqButtons);
    auto *trimTick = new QTimer(this); // picks up changes made in the SSTV / WEFAX tabs
    trimTick->setInterval(1000);
    connect(trimTick, &QTimer::timeout, this, &WwvPanel::syncTrim);
    trimTick->start();
    m_tick->start();
    sync();
    syncStats();
}

QLabel *WwvPanel::makeLamp(const QString &text, const QString &tip)
{
    auto *l = new QLabel(text);
    l->setAlignment(Qt::AlignCenter);
    l->setToolTip(tip);
    l->setStyleSheet(QString::fromLatin1(kLampOff));
    return l;
}

void WwvPanel::setLamp(QLabel *lamp, bool on, const QString &text)
{
    if (!text.isEmpty() && lamp->text() != text)
        lamp->setText(text);
    const bool was = lamp->property("lampOn").toBool();
    if (was == on && lamp->property("lampSet").toBool())
        return;
    lamp->setProperty("lampOn", on);
    lamp->setProperty("lampSet", true);
    lamp->setStyleSheet(QString::fromLatin1(on ? kLampOn : kLampOff));
}

void WwvPanel::addSpectrumLine(const QVector<float> &db, double hzPerBin)
{
    if (isVisible())
        m_wf->addLine(db, hzPerBin);
}

void WwvPanel::sync()
{
    const QSignalBlocker b1(m_decode), b2(m_delay);
    m_decode->setChecked(m_wwv->decoding());
    if (!m_grid->hasFocus())
        m_grid->setText(m_wwv->grid());
    if (!m_delay->hasFocus())
        m_delay->setValue(m_wwv->audioDelayMs());
}

void WwvPanel::syncStats()
{
    const WwvSnapshot &s = m_wwv->snap();
    const auto &st = s.st;
    const bool on = m_wwv->decoding();
    const WwvEngine::CarrierResult car = m_wwv->carrierResult();
    setLamp(m_lampTicks, on && st.ticks);
    setLamp(m_lampTones, on && st.tones, st.tones && st.toneHz > 0 ? tr("Tone %1 Hz").arg(st.toneHz) : tr("Tones"));
    setLamp(m_lampCode, on && st.code);
    setLamp(m_lampTime, on && st.timeValid, st.timeValid && !st.timeVerified ? tr("Time (checking)") : tr("Time"));
    setLamp(m_lampCarrier, car.valid);
    m_station->setText(on && st.ticks ? (st.hawaii ? tr("WWVH (Hawaii)") : tr("WWV (Colorado)")) : QString());

    // time
    if (on && st.timeValid)
    {
        m_flags->setText(tr("UT1 - UTC %1%2 s    Daylight time (US): %3    Leap second: %4")
                             .arg(st.time.dut1 >= 0 ? QStringLiteral("+") : QStringLiteral("-"))
                             .arg(std::fabs(st.time.dut1), 0, 'f', 1)
                             .arg(st.time.dst1 && st.time.dst2
                                      ? tr("on")
                                      : (st.time.dst1 != st.time.dst2 ? tr("changes today") : tr("off")))
                             .arg(st.time.leapWarning ? tr("at the end of this month") : tr("none coming")));
    }
    else
    {
        m_flags->clear();
    }

    // path / delay
    double path = 0, km = 0;
    if (m_wwv->pathDelay(path, km))
        m_path->setText(
            tr("%1 km to %2: %3 ms").arg(qRound(km)).arg(st.hawaii ? tr("WWVH") : tr("WWV")).arg(path, 0, 'f', 1));
    else
        m_path->setText(m_wwv->grid().isEmpty() ? tr("(enter your grid for the path delay)") : tr("(grid not valid)"));

    double off = 0;
    const bool haveOff = on && m_wwv->pcOffsetMs(off);
    if (haveOff)
        m_pc->setText(std::fabs(off) < 5.0 ? tr("PC clock: on time (+/- %1 ms)").arg(s.rawSpreadMs, 0, 'f', 0)
                                           : tr("PC clock is %1 %2 (+/- %3 ms over %4 s)")
                                                 .arg(signedMs(off), off > 0 ? tr("fast") : tr("slow"))
                                                 .arg(s.rawSpreadMs, 0, 'f', 0)
                                                 .arg(s.rawCount));
    else if (on && st.timeValid && !m_wwv->live())
        m_pc->setText(tr("(playing a recording: no comparison with the PC clock)"));
    else if (on && st.timeValid)
        m_pc->setText(tr("Comparing with the PC clock..."));
    else
        m_pc->clear();
    m_calibrate->setEnabled(haveOff);
    m_setClock->setEnabled(haveOff && ClockSet::supported());

    // calibration
    if (on && st.ticks && st.clockPpmSpanSec >= 8.0)
        m_clock->setText(tr("<b>Audio clock: %1%2 ppm</b> (%3 s of ticks, jitter %4 ms)<br>"
                            "The sound path from radio to this program runs that much %5.")
                             .arg(st.clockPpm >= 0 ? QStringLiteral("+") : QString())
                             .arg(st.clockPpm, 0, 'f', 1)
                             .arg(qRound(st.clockPpmSpanSec))
                             .arg(st.tickJitterMs, 0, 'f', 2)
                             .arg(st.clockPpm >= 0 ? tr("fast") : tr("slow")));
    else
        m_clock->setText(on ? tr("Audio clock: waiting for the second ticks...") : tr("Audio clock: Decode is off."));
    m_copyPpm->setEnabled(on && st.ticks && st.clockPpmSpanSec >= 30.0);
    if (on && st.tones && st.toneHz > 0)
        m_tone->setText(tr("<b>Tone %1 Hz reads %2%3 Hz</b> (in AM this is the audio clock again; in USB/LSB it "
                           "is the dial error)")
                            .arg(st.toneHz)
                            .arg(st.toneErrorHz >= 0 ? QStringLiteral("+") : QString())
                            .arg(st.toneErrorHz, 0, 'f', 3));
    else if (on && st.toneHz > 0)
        m_tone->setText(tr("Tone %1 Hz last read %2%3 Hz (silent now)")
                            .arg(st.toneHz)
                            .arg(st.toneErrorHz >= 0 ? QStringLiteral("+") : QString())
                            .arg(st.toneErrorHz, 0, 'f', 3));
    else
        m_tone->setText(tr("Tones: none heard yet."));

    if (m_wwv->measuringCarrier())
    {
        m_measure->setText(tr("Stop (%1 s)").arg(m_wwv->carrierSecondsLeft()));
        m_carrier->setText(st.carrier ? tr("Carrier at %1 Hz so far...").arg(st.carrierHz, 0, 'f', 2)
                                      : tr("Looking for the carrier near 1000 Hz..."));
    }
    else
    {
        m_measure->setText(tr("Measure carrier"));
        if (car.valid)
        {
            const double ppm = car.deltaHz / double(car.nominalHz) * 1e6;
            const auto signedHz
                = [](double v) { return (v >= 0 ? QStringLiteral("+") : QString()) + QString::number(v, 'f', 2); };
            QString text
                = tr("<b>WWV %1 MHz reads %2 Hz %3 on this radio</b> (%4 ppm; %5 UTC, %6 s).<br>"
                     "USB tone %7 Hz, LSB tone %8 Hz from 1000 Hz. The dial error is half their difference; "
                     "both moved %9 Hz the same way, which is not the dial (sound path or a sideband offset in the "
                     "radio).<br>"
                     "To hear a signal exactly on frequency, the dial has to read that much %3. Fading moves the "
                     "carrier a few tenths of a hertz: repeat it a few times.")
                      .arg(car.nominalHz / 1e6, 0, 'f', 1)
                      .arg(signedHz(car.deltaHz))
                      .arg(car.deltaHz >= 0 ? tr("high") : tr("low"))
                      .arg(ppm, 0, 'f', 3)
                      .arg(car.when.toString(QStringLiteral("HH:mm")))
                      .arg(qRound(car.seconds))
                      .arg(signedHz(car.usbHz))
                      .arg(signedHz(car.lsbHz))
                      .arg(signedHz(car.commonHz));
            if (!car.note.isEmpty())
                text += tr("<br><b>Check:</b> %1.").arg(car.note.toHtmlEscaped());
            m_carrier->setText(text);
        }
        else
        {
            m_carrier->setText(tr("Carrier: not measured yet. Tune to a WWV frequency first."));
        }
    }
    m_symbols->setText(on ? QString::fromLatin1(st.symbols.c_str()) : QString());
    updateClockLine();
}

void WwvPanel::updateClockLine()
{
    const WwvSnapshot &s = m_wwv->snap();
    if (!m_wwv->decoding() || !s.st.timeValid)
    {
        m_time->setText(QStringLiteral("--:--:--"));
        m_date->setText(m_wwv->decoding() ? tr("No time decoded yet.") : QString());
        return;
    }
    // WWV's time now: the PC clock less its error when that is known, else the last second decoded.
    qint64 ms = s.utcTickMs;
    double off = 0;
    if (m_wwv->pcOffsetMs(off))
        ms = QDateTime::currentMSecsSinceEpoch() - qint64(std::llround(off));
    const QDateTime t = QDateTime::fromMSecsSinceEpoch(ms, QTimeZone::UTC);
    m_time->setText(t.toString(QStringLiteral("HH:mm:ss")) + QStringLiteral(" UTC"));
    m_date->setText(tr("%1   day %2   %3")
                        .arg(t.toString(QStringLiteral("yyyy-MM-dd")))
                        .arg(t.date().dayOfYear())
                        .arg(s.st.timeVerified ? tr("verified") : tr("not verified yet (one frame)")));
}

void WwvPanel::setTrimSource(std::function<void(int &sstv, int &wefax)> source)
{
    m_trimSource = std::move(source);
    syncTrim();
}

void WwvPanel::syncTrim()
{
    if (!m_trimSource)
        return;
    int sstv = 0, wefax = 0;
    m_trimSource(sstv, wefax);
    if (!m_trim->hasFocus() && m_trim->value() != sstv)
    {
        const QSignalBlocker b(m_trim);
        m_trim->setValue(sstv);
    }
    m_trimNote->setText(wefax == sstv ? QString() : tr("(WEFAX has %1 ppm)").arg(wefax));
}

void WwvPanel::syncFreqButtons()
{
    const bool usable = m_wwv->radioReady() && !m_wwv->measuringCarrier();
    for (QPushButton *b : m_freqButtons)
    {
        const bool on = m_wwv->radioReady() && m_wwv->radioHz() == b->property("hz").toLongLong();
        if (b->isChecked() != on)
        {
            const QSignalBlocker blk(b);
            b->setChecked(on);
        }
        if (b->isEnabled() != usable)
            b->setEnabled(usable);
    }
}
