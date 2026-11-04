#include "ModemPanel.h"

#include "ModemEngine.h"
#include "ModemTxBox.h"
#include "WaterfallWidget.h"

#include <QLabel>
#include <QSettings>
#include <QSplitter>
#include <QVBoxLayout>

ModemPanel::ModemPanel(ModemEngine *engine, QSettings *settings, const QString &name, QWidget *parent)
    : QWidget(parent)
    , m_engine(engine)
    , m_settings(settings)
    , m_name(name)
{
    m_status = new QLabel();
    m_status->setWordWrap(true);
    m_status->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_waterfall = new WaterfallWidget();
    m_txBox = new ModemTxBox(m_engine, m_name);
    connect(m_waterfall, &WaterfallWidget::nudged, m_engine, &ModemEngine::setCarrierHz);
}

void ModemPanel::finishLayout(QWidget *top, QWidget *rx)
{
    auto *root = new QVBoxLayout(this);
    m_split = new QSplitter(Qt::Vertical);
    m_split->setChildrenCollapsible(false);
    m_split->addWidget(top);
    m_split->addWidget(rx);
    m_split->addWidget(m_txBox);
    m_split->setStretchFactor(0, 0);
    m_split->setStretchFactor(1, 1);
    m_split->setStretchFactor(2, 0);
    root->addWidget(m_split);

    connect(m_engine, &ModemEngine::settingsChanged, this, &ModemPanel::syncSettings);
    connect(m_engine, &ModemEngine::statusChanged, m_status, &QLabel::setText);
    connect(m_engine, &ModemEngine::transmittingChanged, this, &ModemPanel::syncTransmitting);
    m_txBox->installShortcuts(this);

    const QByteArray saved
        = m_settings ? m_settings->value(m_name + QStringLiteral("/Split")).toByteArray() : QByteArray();
    if (saved.isEmpty() || !m_split->restoreState(saved))
    {
        const int line = fontMetrics().lineSpacing();
        const int topH = m_split->widget(0)->sizeHint().height();
        const int txArea = m_split->widget(2)->minimumSizeHint().height() + 4 * line;
        m_split->setSizes({topH, 20 * line, txArea});
    }

    syncSettings();
    syncTransmitting(false);
    m_status->setText(m_engine->status());
}

void ModemPanel::saveState()
{
    if (m_settings && m_split)
        m_settings->setValue(m_name + QStringLiteral("/Split"), m_split->saveState());
}

void ModemPanel::addSpectrumLine(const QVector<float> &db, double hzPerBin)
{
    if (isVisible())
        m_waterfall->addLine(db, hzPerBin);
}

void ModemPanel::setRadioState(bool ready, bool canTransmit, const QString &mode)
{
    m_txBox->setRadioState(ready, canTransmit, mode);
}

void ModemPanel::syncSettings()
{
    m_waterfall->setMarker(m_engine->carrierHz(), markerBandwidth());
}

void ModemPanel::syncTransmitting(bool on)
{
    m_waterfall->setTransmitting(on);
}

void ModemPanel::setCallFromReceive(const QString &word)
{
    m_txBox->offerCall(word);
}
