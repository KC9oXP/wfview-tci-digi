#include "TxTools.h"

#include "UiStyle.h"
#include "modemtxsource.h"
#include "txlevel.h"

#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace
{
TxTools *g_instance = nullptr;

// A steady tone with 5 ms ramps (no key clicks).
class TuneTxSource : public ModemTxSource
{
public:
    void fadeOut()
    {
        std::lock_guard<std::mutex> lock(mutex());
        m_on = false;
    }
    // Alternate with a second tone every holdSec (continuous phase), like RTTY's
    // mark and space; hz2 <= 0 = one steady tone.
    void setSecondTone(double hz2, double holdSec)
    {
        std::lock_guard<std::mutex> lock(mutex());
        m_hz2 = hz2;
        m_hold = holdSec;
        m_count = 0.0;
        m_onSecond = false;
    }

protected:
    void modClear() override
    {
        m_on = false;
        m_amp = 0.0;
    }
    void modPreroll(double) override
    {
        m_on = true;
        m_amp = 0.0;
    }
    size_t modQueueChar(char) override
    {
        return 0;
    }
    void modTail(double) override
    {
        m_on = false;
    }
    bool modIdle() const override
    {
        return !m_on && m_amp <= 0.0;
    }
    size_t modUnitsSent() const override
    {
        return 0;
    }
    void modSetCarrier(double hz) override
    {
        m_hz = hz;
    }
    void modRender(float *mono, size_t n, double fs) override
    {
        const double step = 1.0 / std::max(1.0, 0.005 * fs);
        const bool two = m_hz2 > 0.0 && m_hold > 0.0;
        const double holdSamples = std::max(1.0, m_hold * fs);
        for (size_t i = 0; i < n; ++i)
        {
            if (two && (m_count += 1.0) >= holdSamples)
            {
                m_count -= holdSamples;
                m_onSecond = !m_onSecond;
            }
            const double dph = 2.0 * M_PI * (two && m_onSecond ? m_hz2 : m_hz) / fs;
            m_amp = m_on ? std::min(1.0, m_amp + step) : std::max(0.0, m_amp - step);
            m_phase += dph;
            if (m_phase > 2.0 * M_PI)
                m_phase -= 2.0 * M_PI;
            mono[i] = float(m_amp * std::sin(m_phase));
        }
    }

private:
    bool m_on = false;
    double m_amp = 0.0;
    double m_phase = 0.0;
    double m_hz = 1750.0;
    double m_hz2 = 0.0;
    double m_hold = 0.0;
    double m_count = 0.0;
    bool m_onSecond = false;
};
} // namespace

// ---------------------------------------------------------------------------

TuneEngine::TuneEngine(QObject *parent)
    : ModemEngine(QStringLiteral("Tune"), std::make_shared<TuneTxSource>(), parent)
{
    m_carrierHz = 1750.0;
    applyConfig();
}

void TuneEngine::applyConfig()
{
    txSource()->setCarrier(m_carrierHz);
    txSource()->setLevel(float(m_level));
}

void TuneEngine::startTune(double level, double hz, double hz2, double holdSec)
{
    if (transmitting())
        return;
    m_level = std::clamp(level, 0.0, 1.0);
    m_carrierHz = std::clamp(hz, 300.0, 3000.0);
    std::static_pointer_cast<TuneTxSource>(txSource())
        ->setSecondTone(hz2 > 0.0 ? std::clamp(hz2, 300.0, 3000.0) : 0.0, holdSec);
    startTx();
}

void TuneEngine::setTuneLevel(double level)
{
    m_level = std::clamp(level, 0.0, 1.0);
    txSource()->setLevel(float(m_level));
}

void TuneEngine::stopTune()
{
    if (!transmitting())
        return;
    std::static_pointer_cast<TuneTxSource>(txSource())->fadeOut();
    finishTx(); // released once the tone has faded
}

// ---------------------------------------------------------------------------

CwIdEngine::CwIdEngine(QObject *parent)
    : ModemEngine(QStringLiteral("CwId"), std::make_shared<CwTxSource>(), parent)
{
    m_carrierHz = 800.0;
    applyConfig();
}

void CwIdEngine::applyConfig()
{
    auto tx = std::static_pointer_cast<CwTxSource>(txSource());
    tx->setWpm(m_wpm);
    tx->setCarrier(m_carrierHz);
    tx->setLevel(float(m_level));
}

void CwIdEngine::sendId(const QString &text, double level, int wpm, double pitchHz)
{
    if (transmitting())
        return;
    m_level = std::clamp(level, 0.0, 1.0);
    m_wpm = std::clamp(wpm, 5, 60);
    m_carrierHz = std::clamp(pitchHz, 300.0, 3000.0);
    setHoldTx(false);
    send(text.endsWith(QLatin1Char(' ')) ? text : text + QLatin1Char(' '));
}

// ---------------------------------------------------------------------------

TxTools::TxTools(QSettings *settings, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
{
    g_instance = this;
    m_tune = new TuneEngine(this);
    m_cwId = new CwIdEngine(this);
    if (m_settings)
    {
        m_tuneHz = std::clamp(m_settings->value(QStringLiteral("Tune/Hz"), m_tuneHz).toDouble(), 300.0, 3000.0);
        m_cwIdText = m_settings->value(QStringLiteral("CwId/Text"), QStringLiteral("DE {MYCALL}")).toString();
        m_cwIdWpm = std::clamp(m_settings->value(QStringLiteral("CwId/Wpm"), m_cwIdWpm).toInt(), 5, 60);
        m_cwIdPitch
            = std::clamp(m_settings->value(QStringLiteral("CwId/Pitch"), m_cwIdPitch).toDouble(), 300.0, 3000.0);
        m_alTarget
            = std::clamp(m_settings->value(QStringLiteral("Tx/AutoLevelAlc"), m_alTarget).toDouble(), 0.0, 100.0);
    }
    m_alTimer.setInterval(100);
    connect(&m_alTimer, &QTimer::timeout, this, &TxTools::autoLevelTick);
    if (m_cwIdText.trimmed().isEmpty())
        m_cwIdText = QStringLiteral("DE {MYCALL}");
    connect(m_tune, &ModemEngine::transmittingChanged, this, &TxTools::tuningChanged);
    connect(m_tune,
            &ModemEngine::transmittingChanged,
            this,
            [this](bool on)
            {
                // Stopped from elsewhere (Abort, TX time limit, connection lost): keep the old level.
                if (!on && m_al)
                    finishAutoLevel(
                        tr("Auto level stopped before it finished; the TX level was not changed."), false, 0.0);
            });
    connect(m_cwId,
            &ModemEngine::statusChanged,
            this,
            [this](const QString &s)
            {
                if (!m_cwId->transmitting() && s != tr("Off") && s != tr("Listening..."))
                    emit message(tr("CW ID: %1").arg(s));
            });
}

TxTools::~TxTools()
{
    if (g_instance == this)
        g_instance = nullptr;
}

TxTools *TxTools::instance()
{
    return g_instance;
}

bool TxTools::tuning() const
{
    return m_tune->transmitting();
}

void TxTools::startTune(double level)
{
    m_tune->startTune(level, m_tuneHz);
    if (m_tune->transmitting())
        emit message(tr("Tune: %1 Hz tone at %2 dB - click Tune again to stop; the TX level slider changes it at once.")
                         .arg(qRound(m_tuneHz))
                         .arg(txlevel::levelToDb(level), 0, 'f', 1));
    else
        emit message(tr("Tune: %1").arg(m_tune->status()));
}

void TxTools::setTuneLevel(double level)
{
    if (m_tune->transmitting())
        m_tune->setTuneLevel(level);
}

void TxTools::stopTune()
{
    m_tune->stopTune();
}

void TxTools::sendCwId(double level)
{
    if (m_cwIdText.contains(QStringLiteral("{MYCALL}"), Qt::CaseInsensitive) && m_cwId->myCall().isEmpty())
    {
        emit message(tr("CW ID: enter your call in a My call field first."));
        return;
    }
    m_cwId->sendId(m_cwIdText, level, m_cwIdWpm, m_cwIdPitch);
    if (m_cwId->transmitting())
        emit message(
            tr("Sending CW ID: %1")
                .arg(QString(m_cwIdText).replace(QStringLiteral("{MYCALL}"), m_cwId->myCall(), Qt::CaseInsensitive)));
}

void TxTools::setTuneHz(double hz)
{
    m_tuneHz = std::clamp(hz, 300.0, 3000.0);
    if (m_settings)
        m_settings->setValue(QStringLiteral("Tune/Hz"), m_tuneHz);
}

// ---- auto TX level ----------------------------------------------------------

namespace
{
constexpr qint64 kSettleMs = 700; // after a level change: TX audio buffering + the meter's response
constexpr qint64 kKeyTimeoutMs = 5000; // the radio must be transmitting by then
constexpr qint64 kNoReadingsMs = 3000; // settled but no ALC reports
constexpr qint64 kMaxMs = 45000;
constexpr size_t kReadingsPerStep = 2; // the meter peak of two reports
} // namespace

void TxTools::setAutoLevelTarget(double alc)
{
    m_alTarget = std::clamp(alc, 0.0, 100.0);
    if (m_settings)
        m_settings->setValue(QStringLiteral("Tx/AutoLevelAlc"), m_alTarget);
}

void TxTools::startAutoLevel(double level, const TestSignal &sig, std::function<void(double)> apply)
{
    if (m_al)
        return;
    if (m_tune->transmitting())
    {
        emit message(tr("Auto level: stop the Tune tone first."));
        return;
    }
    m_al = std::make_unique<autolevel::Search>(txlevel::levelToDb(level), m_alTarget);
    m_alApply = std::move(apply);
    m_alReadings.clear();
    m_tune->startTune(txlevel::dbToLevel(m_al->currentDb()), sig.hz, sig.hz2, sig.holdSec);
    if (!m_tune->transmitting())
    {
        m_al.reset();
        m_alApply = nullptr;
        emit message(tr("Auto level: %1").arg(m_tune->status()));
        return;
    }
    m_alTotal.start();
    m_alSince.start();
    m_alTimer.start();
    emit autoLevelChanged(true);
    emit message(tr("Auto level step 1: trying %1 dB, looking for ALC %2 (click Auto level again to stop)...")
                     .arg(m_al->currentDb(), 0, 'f', 1)
                     .arg(m_alTarget, 0, 'f', m_alTarget < 1.0 ? 2 : 1));
}

void TxTools::cancelAutoLevel()
{
    if (m_al)
        finishAutoLevel(tr("Auto level cancelled; the TX level was not changed."), false, 0.0);
}

void TxTools::setRadioTransmitting(bool on)
{
    if (on && !m_radioTx)
        m_alTxSince.start();
    m_radioTx = on;
}

void TxTools::onTxSensors(double alc, double, double)
{
    if (!m_al || !m_radioTx)
        return;
    if (m_alSince.elapsed() < kSettleMs || !m_alTxSince.isValid() || m_alTxSince.elapsed() < kSettleMs)
        return;
    m_alReadings.push_back(alc);
}

void TxTools::autoLevelTick()
{
    if (!m_al)
    {
        m_alTimer.stop();
        return;
    }
    if (m_alTotal.elapsed() > kMaxMs)
    {
        finishAutoLevel(
            tr("Auto level took too long (no steady ALC reading); the TX level was not changed."), false, 0.0);
        return;
    }
    if (!m_radioTx)
    {
        if (m_alTotal.elapsed() > kKeyTimeoutMs)
            finishAutoLevel(tr("Auto level: the radio did not go to transmit."), false, 0.0);
        return;
    }
    const qint64 settled = std::min(m_alSince.elapsed(), m_alTxSince.elapsed()) - kSettleMs;
    if (settled > kNoReadingsMs && m_alReadings.empty())
    {
        finishAutoLevel(tr("Auto level: the server sends no ALC readings (tx_sensors), so the level cannot be "
                           "set automatically."),
                        false,
                        0.0);
        return;
    }
    if (m_alReadings.size() < kReadingsPerStep)
        return;

    // The meter's peak over the step.
    const double alc = *std::max_element(m_alReadings.begin(), m_alReadings.end());
    m_alReadings.clear();
    m_alLastAlc = alc;
    using St = autolevel::Search::State;
    const double triedDb = m_al->currentDb();
    const St st = m_al->feed(alc);
    const QString target = QString::number(m_alTarget, 'f', m_alTarget < 1.0 ? 2 : 1);
    if (st == St::Measuring)
    {
        m_tune->setTuneLevel(txlevel::dbToLevel(m_al->currentDb()));
        m_alSince.start();
        emit message(tr("Auto level step %1: ALC %2 at %3 dB - trying %4 dB...")
                         .arg(m_al->steps() + 1)
                         .arg(alc, 0, 'f', 2)
                         .arg(triedDb, 0, 'f', 1)
                         .arg(m_al->currentDb(), 0, 'f', 1));
        return;
    }
    if (st == St::Done)
        finishAutoLevel(tr("Auto level: TX level set to %1 dB (ALC %2, target %3) in %4 steps.")
                            .arg(m_al->resultDb(), 0, 'f', 1)
                            .arg(m_al->resultAlc(), 0, 'f', 2)
                            .arg(target)
                            .arg(m_al->steps()),
                        true,
                        m_al->resultDb());
    else if (st == St::AtMaxBelow)
        finishAutoLevel(tr("Auto level: ALC stays at or below %1 even at full level, so the TX level is set to 0 dB. "
                           "For more drive raise wfview's TX audio gain or the radio's data input (MOD) level.")
                            .arg(target),
                        true,
                        0.0);
    else
        finishAutoLevel(tr("Auto level: ALC is over %1 even at %2 dB. Lower wfview's TX audio gain or the radio's "
                           "data input (MOD) level, then try again. The TX level was not changed.")
                            .arg(target)
                            .arg(txlevel::kMinDb, 0, 'f', 0),
                        false,
                        0.0);
}

void TxTools::finishAutoLevel(const QString &text, bool applyResult, double resultDb)
{
    if (!m_al)
        return;
    m_al.reset(); // first: the transmittingChanged(false) from stopTune() must not finish again
    m_alTimer.stop();
    std::function<void(double)> apply = std::move(m_alApply);
    m_alApply = nullptr;
    m_tune->stopTune();
    if (applyResult && apply)
        apply(txlevel::dbToLevel(resultDb));
    emit autoLevelChanged(false);
    emit message(text);
}

void TxTools::setCwId(const QString &text, int wpm, double pitchHz)
{
    m_cwIdText = text.trimmed().isEmpty() ? QStringLiteral("DE {MYCALL}") : text.trimmed();
    m_cwIdWpm = std::clamp(wpm, 5, 60);
    m_cwIdPitch = std::clamp(pitchHz, 300.0, 3000.0);
    if (m_settings)
    {
        m_settings->setValue(QStringLiteral("CwId/Text"), m_cwIdText);
        m_settings->setValue(QStringLiteral("CwId/Wpm"), m_cwIdWpm);
        m_settings->setValue(QStringLiteral("CwId/Pitch"), m_cwIdPitch);
    }
}

void TxTools::editSettings(QWidget *parent)
{
    QDialog dlg(parent);
    dlg.setWindowTitle(tr("Tune tone, CW ID and Auto level"));
    auto *form = new QFormLayout(&dlg);
    auto *tuneHz = new QSpinBox();
    tuneHz->setRange(300, 3000);
    tuneHz->setSuffix(tr(" Hz"));
    tuneHz->setValue(qRound(m_tuneHz));
    form->addRow(tr("Tune tone"), tuneHz);
    auto *text = new QLineEdit(m_cwIdText);
    text->setToolTip(tr("{MYCALL} is your call. Prosigns as <SK>, <AR>, <KN>."));
    form->addRow(tr("CW ID text"), text);
    auto *wpm = new QSpinBox();
    wpm->setRange(5, 60);
    wpm->setSuffix(tr(" WPM"));
    wpm->setValue(m_cwIdWpm);
    form->addRow(tr("CW ID speed"), wpm);
    auto *pitch = new QSpinBox();
    pitch->setRange(300, 3000);
    pitch->setSuffix(tr(" Hz"));
    pitch->setValue(qRound(m_cwIdPitch));
    form->addRow(tr("CW ID pitch"), pitch);
    auto *alc = new QDoubleSpinBox();
    alc->setRange(0.0, 100.0);
    alc->setDecimals(2);
    alc->setSingleStep(0.1);
    alc->setValue(m_alTarget);
    alc->setToolTip(tr("ALC reading Auto level aims for (the highest TX level at or below it). The scale is "
                       "whatever wfview reports for your radio, as in the TX meter at the top.\n"
                       "0 = no ALC at all: Auto level first raises the level until ALC shows, then backs off "
                       "until it reads 0."));
    form->addRow(tr("Auto level: target ALC"), alc);
    auto *note = new QLabel(tr("The Tune button is in the SSTV tab; Auto level and CW ID are in every transmit "
                               "tab. They use (Auto level: set) the TX level of the tab they are sent from. Put "
                               "{CWID} at the end of a macro to send the CW ID after it."));
    note->setWordWrap(true);
    form->addRow(note);
    auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    form->addRow(bb);
    if (dlg.exec() != QDialog::Accepted)
        return;
    setTuneHz(tuneHz->value());
    setCwId(text->text(), wpm->value(), pitch->value());
    setAutoLevelTarget(alc->value());
}

// ---------------------------------------------------------------------------

TxToolButtons::TxToolButtons(std::function<double()> level,
                             std::function<void(double)> setLevel,
                             std::function<TxTools::TestSignal()> testSignal,
                             bool withTune,
                             QWidget *parent)
    : QWidget(parent)
    , m_level(std::move(level))
    , m_setLevel(std::move(setLevel))
    , m_signal(std::move(testSignal))
{
    auto *h = new QHBoxLayout(this);
    h->setContentsMargins(0, 0, 0, 0);
    m_tune = new QPushButton(tr("Tune"));
    UiStyle::greenWhenOn(m_tune);
    m_tune->setToolTip(tr("Send a steady 1750 Hz tone (SSTV's tune tone; changeable in Options) at this tab's TX "
                          "level, to set the TX level and ALC. Click again to stop. The TX time limit applies."));
    m_cwId = new QPushButton(tr("CW ID"));
    m_cwId->setToolTip(tr("Send your CW ID (\"DE <your call>\") as keyed audio at this tab's TX level.\n"
                          "Text, speed and pitch: Options > Tune tone, CW ID and Auto level."));
    m_auto = new QPushButton(tr("Auto level"));
    UiStyle::greenWhenOn(m_auto);
    m_auto->setToolTip(tr("Set this tab's TX level from the radio's ALC: sends a short test signal shaped like "
                          "this mode and finds the highest level whose ALC is at or below the target (0.1; "
                          "Options > Tune tone, CW ID and Auto level). Usually 5-15 s. Click again to stop.\n"
                          "Use a dummy load or a clear frequency: it transmits."));
    h->addWidget(m_tune);
    h->addWidget(m_auto);
    h->addWidget(m_cwId);
    m_tune->setVisible(withTune);

    TxTools *tools = TxTools::instance();
    if (!tools)
    {
        m_tune->setEnabled(false);
        m_auto->setEnabled(false);
        m_cwId->setEnabled(false);
        return;
    }
    connect(m_auto,
            &QPushButton::clicked,
            this,
            [this, tools]()
            {
                if (tools->autoLeveling())
                    tools->cancelAutoLevel();
                else if (m_setLevel)
                    tools->startAutoLevel(m_level ? m_level() : 0.1,
                                          m_signal ? m_signal() : TxTools::TestSignal {tools->tuneHz()},
                                          m_setLevel);
                sync();
            });
    connect(tools, &TxTools::autoLevelChanged, this, &TxToolButtons::sync);
    connect(m_tune,
            &QPushButton::clicked,
            this,
            [this, tools]()
            {
                if (tools->tuning())
                    tools->stopTune();
                else
                {
                    tools->startTune(m_level ? m_level() : 0.1);
                    m_ownTune = tools->tuning();
                }
                sync();
            });
    connect(m_cwId, &QPushButton::clicked, this, [this, tools]() { tools->sendCwId(m_level ? m_level() : 0.1); });
    connect(tools,
            &TxTools::tuningChanged,
            this,
            [this](bool on)
            {
                if (!on)
                    m_ownTune = false;
                sync();
            });
    sync();
}

void TxToolButtons::levelChanged(double level)
{
    TxTools *tools = TxTools::instance();
    if (m_ownTune && tools && tools->tuning())
        tools->setTuneLevel(level);
}

void TxToolButtons::setTransmitAllowed(bool on)
{
    m_allowed = on;
    sync();
}

void TxToolButtons::sync()
{
    TxTools *tools = TxTools::instance();
    if (!tools)
        return;
    const QSignalBlocker b(m_tune), b2(m_auto);
    const bool al = tools->autoLeveling();
    m_tune->setChecked(tools->tuning() && !al);
    m_tune->setEnabled((m_allowed || tools->tuning()) && !al);
    m_auto->setChecked(al);
    m_auto->setEnabled((m_allowed && m_setLevel && !tools->tuning()) || al);
    m_cwId->setEnabled(m_allowed && !al);
}
