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
        const double dph = 2.0 * M_PI * m_hz / fs;
        for (size_t i = 0; i < n; ++i)
        {
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

void TuneEngine::startTune(double level, double hz)
{
    if (transmitting())
        return;
    m_level = std::clamp(level, 0.0, 1.0);
    m_carrierHz = std::clamp(hz, 300.0, 3000.0);
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
    }
    if (m_cwIdText.trimmed().isEmpty())
        m_cwIdText = QStringLiteral("DE {MYCALL}");
    connect(m_tune, &ModemEngine::transmittingChanged, this, &TxTools::tuningChanged);
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
    dlg.setWindowTitle(tr("Tune tone and CW ID"));
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
    auto *note = new QLabel(tr("The Tune button is in the SSTV tab; CW ID is in every transmit tab. Both use the "
                               "TX level of the tab they are sent from. Put {CWID} at the end of a macro to send "
                               "the CW ID after it."));
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
}

// ---------------------------------------------------------------------------

TxToolButtons::TxToolButtons(std::function<double()> level, bool withTune, QWidget *parent)
    : QWidget(parent)
    , m_level(std::move(level))
{
    auto *h = new QHBoxLayout(this);
    h->setContentsMargins(0, 0, 0, 0);
    m_tune = new QPushButton(tr("Tune"));
    UiStyle::greenWhenOn(m_tune);
    m_tune->setToolTip(tr("Send a steady 1750 Hz tone (SSTV's tune tone; changeable in Options) at this tab's TX "
                          "level, to set the TX level and ALC. Click again to stop. The TX time limit applies."));
    m_cwId = new QPushButton(tr("CW ID"));
    m_cwId->setToolTip(tr("Send your CW ID (\"DE <your call>\") as keyed audio at this tab's TX level.\n"
                          "Text, speed and pitch: Options > Tune tone and CW ID."));
    h->addWidget(m_tune);
    h->addWidget(m_cwId);
    m_tune->setVisible(withTune);

    TxTools *tools = TxTools::instance();
    if (!tools)
    {
        m_tune->setEnabled(false);
        m_cwId->setEnabled(false);
        return;
    }
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
    const QSignalBlocker b(m_tune);
    m_tune->setChecked(tools->tuning());
    m_tune->setEnabled(m_allowed || tools->tuning());
    m_cwId->setEnabled(m_allowed);
}
