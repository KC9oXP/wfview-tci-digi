#include "CwPanel.h"

#include "CwTxEngine.h"
#include "ModemTxBox.h"

#include <QCheckBox>
#include <QComboBox>
#include <QEvent>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QSettings>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QSplitter>
#include <QTableWidget>
#include <QVBoxLayout>
#include <algorithm>

namespace
{
constexpr int maxDecodeChars = 200000;
}

CwPanel::CwPanel(CwDecoder *decoder, CwTxEngine *tx, QSettings *settings, QWidget *parent)
    : QWidget(parent)
    , m_dec(decoder)
    , m_tx(tx)
    , m_settings(settings)
{
    auto *root = new QVBoxLayout(this);

    // ---- controls: a wrapping two-row grid keeps every control reachable when narrow
    auto *row1 = new QHBoxLayout();
    m_decode = new QPushButton(tr("Decode"));
    m_decode->setCheckable(true);
    m_decode->setToolTip(tr("Decode CW from the receive audio wfview sends over TCI."));
    row1->addWidget(m_decode);

    row1->addWidget(new QLabel(tr("Engine")));
    m_engine = new QComboBox();
    m_engine->addItem(tr("Machine"), CwDecoder::EngineMachine);
    m_engine->addItem(tr("Hand key"), CwDecoder::EngineHandKey);
    m_engine->setToolTip(tr("Machine: keyer or computer-sent CW.\nHand key: learns each sender's own timing."));
    row1->addWidget(m_engine);

    row1->addWidget(new QLabel(tr("Pitch")));
    m_pitchMode = new QComboBox();
    m_pitchMode->addItem(tr("Auto"));
    m_pitchMode->addItem(tr("Fixed"));
    m_pitchMode->setToolTip(
        tr("Auto: follow the strongest steady tone, 200-1200 Hz.\nFixed: decode exactly at the pitch set here."));
    row1->addWidget(m_pitchMode);
    m_pitch = new QSpinBox();
    m_pitch->setRange(CwDecoder::pitchMin(), CwDecoder::pitchMax());
    m_pitch->setSingleStep(10);
    m_pitch->setSuffix(tr(" Hz"));
    row1->addWidget(m_pitch);
    row1->addStretch(1);

    auto *row2 = new QHBoxLayout();
    m_fixedSpeed = new QCheckBox(tr("Fixed speed"));
    row2->addWidget(m_fixedSpeed);
    m_speed = new QSpinBox();
    m_speed->setRange(5, 60);
    m_speed->setSuffix(tr(" WPM"));
    row2->addWidget(m_speed);
    m_squelch = new QCheckBox(tr("Squelch"));
    m_squelch->setToolTip(tr("Hold off decoding noise when no signal is present."));
    row2->addWidget(m_squelch);
    m_multi = new QCheckBox(tr("Multi-signal"));
    m_multi->setToolTip(
        tr("List every CW signal in the passband (200-2000 Hz).\nDouble-click a row to decode that station."));
    row2->addWidget(m_multi);
    auto *resync = new QPushButton(tr("Re-sync"));
    resync->setToolTip(tr("Restart pitch and speed tracking, e.g. after changing stations."));
    row2->addWidget(resync);
    auto *clear = new QPushButton(tr("Clear"));
    row2->addWidget(clear);
    row2->addStretch(1);

    root->addLayout(row1);
    root->addLayout(row2);

    m_status = new QLabel(tr("Off"));
    m_status->setWordWrap(true);
    m_status->setTextInteractionFlags(Qt::TextSelectableByMouse);
    root->addWidget(m_status);

    // ---- decode pane + multi-signal list share the remaining height
    m_split = new QSplitter(Qt::Vertical);
    m_text = new QPlainTextEdit();
    m_text->setReadOnly(true);
    m_text->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    m_text->setToolTip(tr("Double-click a call sign to copy it to the Call field."));
    QFont mono(QStringLiteral("Consolas"));
    mono.setStyleHint(QFont::Monospace);
    mono.setPointSizeF(mono.pointSizeF() * 1.15);
    m_text->setFont(mono);
    m_split->addWidget(m_text);

    m_table = new QTableWidget(0, 4);
    m_table->setHorizontalHeaderLabels({tr("Pitch"), tr("WPM"), tr("SNR"), tr("Text")});
    m_table->verticalHeader()->setVisible(false);
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setFont(mono);
    m_table->setVisible(false);
    m_split->addWidget(m_table);
    m_split->setStretchFactor(0, 3);
    m_split->setStretchFactor(1, 2);

    // ---- transmit: speed and pitch, then the shared macros / type-ahead area
    auto *txw = new QWidget();
    auto *tv = new QVBoxLayout(txw);
    tv->setContentsMargins(0, 0, 0, 0);
    auto *txRow = new QHBoxLayout();
    auto *txLabel = new QLabel(tr("<b>Transmit</b> (audio CW)"));
    txLabel->setToolTip(tr("Sends CW as a keyed audio tone with the radio in DIGU/USB, like fldigi's CW mode.\n"
                           "On the air it is a normal CW signal at the dial frequency plus the pitch."));
    txRow->addWidget(txLabel);
    txRow->addWidget(new QLabel(tr("Speed")));
    m_txWpm = new QSpinBox();
    m_txWpm->setRange(5, 60);
    m_txWpm->setSuffix(tr(" WPM"));
    txRow->addWidget(m_txWpm);
    txRow->addWidget(new QLabel(tr("Pitch")));
    m_txPitch = new QSpinBox();
    m_txPitch->setRange(300, 1500);
    m_txPitch->setSingleStep(10);
    m_txPitch->setSuffix(tr(" Hz"));
    m_txPitch->setToolTip(tr("Tone frequency. Your signal goes out at the dial frequency plus this (USB)."));
    txRow->addWidget(m_txPitch);
    auto *useRx = new QPushButton(tr("= RX pitch"));
    useRx->setToolTip(tr("Transmit on the pitch the decoder is using, i.e. on the other station's frequency."));
    txRow->addWidget(useRx);
    txRow->addStretch(1);
    tv->addLayout(txRow);
    m_txStatus = new QLabel();
    m_txStatus->setWordWrap(true);
    m_txStatus->setVisible(false);
    tv->addWidget(m_txStatus);
    m_txBox = new ModemTxBox(m_tx, QStringLiteral("CW"));
    tv->addWidget(m_txBox, 1);

    m_outer = new QSplitter(Qt::Vertical);
    m_outer->setChildrenCollapsible(false);
    m_outer->addWidget(m_split);
    m_outer->addWidget(txw);
    m_outer->setStretchFactor(0, 1);
    m_outer->setStretchFactor(1, 0);
    root->addWidget(m_outer, 1);
    m_txBox->installShortcuts(this);
    m_text->viewport()->installEventFilter(this);

    connect(m_txWpm, &QSpinBox::valueChanged, m_tx, &CwTxEngine::setWpm);
    connect(m_txPitch, &QSpinBox::valueChanged, this, [this](int hz) { m_tx->setCarrierHz(hz); });
    connect(useRx, &QPushButton::clicked, this, [this]() { m_tx->setCarrierHz(m_dec->currentPitchHz()); });
    connect(m_tx, &ModemEngine::settingsChanged, this, &CwPanel::syncTx);
    connect(m_tx,
            &ModemEngine::statusChanged,
            this,
            [this](const QString &t)
            {
                m_txStatus->setText(t);
                m_txStatus->setVisible(!t.isEmpty() && t != tr("Off") && !t.startsWith(tr("Listening")));
            });
    connect(m_tx, &ModemEngine::textAppended, this, &CwPanel::appendText);

    // ---- wiring
    connect(m_decode, &QPushButton::toggled, m_dec, &CwDecoder::setEnabled);
    connect(m_engine,
            &QComboBox::currentIndexChanged,
            this,
            [this](int i) { m_dec->setEngine(m_engine->itemData(i).toInt()); });
    connect(m_pitchMode, &QComboBox::currentIndexChanged, this, [this](int i) { m_dec->setFixedPitch(i == 1); });
    connect(m_pitch, &QSpinBox::valueChanged, m_dec, &CwDecoder::setPitchHz);
    connect(m_fixedSpeed, &QCheckBox::toggled, m_dec, &CwDecoder::setFixedSpeed);
    connect(m_speed, &QSpinBox::valueChanged, m_dec, &CwDecoder::setSpeedWpm);
    connect(m_squelch, &QCheckBox::toggled, m_dec, &CwDecoder::setSquelch);
    connect(m_multi, &QCheckBox::toggled, m_dec, &CwDecoder::setMultiSignal);
    connect(m_multi, &QCheckBox::toggled, m_table, &QWidget::setVisible);
    connect(resync, &QPushButton::clicked, m_dec, &CwDecoder::resync);
    connect(clear, &QPushButton::clicked, m_text, &QPlainTextEdit::clear);
    connect(m_table,
            &QTableWidget::cellDoubleClicked,
            this,
            [this](int row, int)
            {
                if (auto *it = m_table->item(row, 0))
                    m_dec->decodeChannel(it->data(Qt::UserRole).toInt());
            });

    connect(m_dec, &CwDecoder::settingsChanged, this, &CwPanel::syncFromDecoder);
    connect(m_dec, &CwDecoder::statusChanged, m_status, &QLabel::setText);
    connect(m_dec, &CwDecoder::textAppended, this, &CwPanel::appendText);
    connect(m_dec, &CwDecoder::channelsChanged, this, &CwPanel::showChannels);

    loadState();
    syncFromDecoder();
    syncTx();
}

void CwPanel::syncTx()
{
    const QSignalBlocker b1(m_txWpm), b2(m_txPitch);
    m_txWpm->setValue(m_tx->wpm());
    m_txPitch->setValue(qRound(m_tx->carrierHz()));
}

bool CwPanel::eventFilter(QObject *obj, QEvent *event)
{
    if (obj == m_text->viewport() && event->type() == QEvent::MouseButtonDblClick)
    {
        // Let the text edit select the word first, then offer it as the call.
        const bool handled = QWidget::eventFilter(obj, event);
        QMetaObject::invokeMethod(
            this, [this]() { m_txBox->offerCall(m_text->textCursor().selectedText()); }, Qt::QueuedConnection);
        return handled;
    }
    return QWidget::eventFilter(obj, event);
}

void CwPanel::setRadioState(bool ready, bool canTransmit, const QString &mode)
{
    m_txBox->setRadioState(ready, canTransmit, mode);
}

void CwPanel::loadState()
{
    if (!m_settings)
        return;
    m_settings->beginGroup(QStringLiteral("CW"));
    m_dec->setEngine(m_settings->value(QStringLiteral("Engine"), m_dec->engine()).toInt());
    m_dec->setFixedPitch(m_settings->value(QStringLiteral("FixedPitch"), m_dec->fixedPitch()).toBool());
    m_dec->setPitchHz(m_settings->value(QStringLiteral("Pitch"), m_dec->pitchHz()).toInt());
    m_dec->setFixedSpeed(m_settings->value(QStringLiteral("FixedSpeed"), m_dec->fixedSpeed()).toBool());
    m_dec->setSpeedWpm(m_settings->value(QStringLiteral("Speed"), m_dec->speedWpm()).toInt());
    m_dec->setSquelch(m_settings->value(QStringLiteral("Squelch"), m_dec->squelch()).toBool());
    m_dec->setMultiSignal(m_settings->value(QStringLiteral("Multi"), m_dec->multiSignal()).toBool());
    m_split->restoreState(m_settings->value(QStringLiteral("Split")).toByteArray());
    const QByteArray outer = m_settings->value(QStringLiteral("TxSplit")).toByteArray();
    m_settings->endGroup();
    if (outer.isEmpty() || !m_outer->restoreState(outer))
    {
        const int line = fontMetrics().lineSpacing();
        m_outer->setSizes({24 * line, m_outer->widget(1)->minimumSizeHint().height() + 4 * line});
    }
}

void CwPanel::saveState()
{
    if (!m_settings)
        return;
    m_settings->beginGroup(QStringLiteral("CW"));
    m_settings->setValue(QStringLiteral("Engine"), m_dec->engine());
    m_settings->setValue(QStringLiteral("FixedPitch"), m_dec->fixedPitch());
    m_settings->setValue(QStringLiteral("Pitch"), m_dec->pitchHz());
    m_settings->setValue(QStringLiteral("FixedSpeed"), m_dec->fixedSpeed());
    m_settings->setValue(QStringLiteral("Speed"), m_dec->speedWpm());
    m_settings->setValue(QStringLiteral("Squelch"), m_dec->squelch());
    m_settings->setValue(QStringLiteral("Multi"), m_dec->multiSignal());
    m_settings->setValue(QStringLiteral("Split"), m_split->saveState());
    m_settings->setValue(QStringLiteral("TxSplit"), m_outer->saveState());
    m_settings->endGroup();
}

void CwPanel::syncFromDecoder()
{
    const QSignalBlocker b1(m_decode), b2(m_engine), b3(m_pitchMode), b4(m_pitch), b5(m_fixedSpeed), b6(m_speed),
        b7(m_squelch), b8(m_multi);
    m_decode->setChecked(m_dec->enabled());
    m_engine->setCurrentIndex(m_engine->findData(m_dec->engine()));
    m_pitchMode->setCurrentIndex(m_dec->fixedPitch() ? 1 : 0);
    m_pitch->setValue(m_dec->pitchHz());
    m_pitch->setEnabled(m_dec->fixedPitch());
    m_fixedSpeed->setChecked(m_dec->fixedSpeed());
    m_speed->setValue(m_dec->speedWpm());
    m_speed->setEnabled(m_dec->fixedSpeed());
    m_squelch->setChecked(m_dec->squelch());
    m_multi->setChecked(m_dec->multiSignal());
    m_table->setVisible(m_dec->multiSignal());
}

void CwPanel::appendText(const QString &text)
{
    QScrollBar *sb = m_text->verticalScrollBar();
    const bool atEnd = sb->value() >= sb->maximum() - 2;
    m_text->moveCursor(QTextCursor::End);
    m_text->insertPlainText(text);
    if (m_text->document()->characterCount() > maxDecodeChars)
    {
        QTextCursor c(m_text->document());
        c.movePosition(QTextCursor::Start);
        c.movePosition(QTextCursor::Down, QTextCursor::KeepAnchor, 200);
        c.removeSelectedText();
    }
    if (atEnd)
        sb->setValue(sb->maximum());
}

void CwPanel::showChannels(const QVector<CwChannelInfo> &channels)
{
    m_table->setRowCount(channels.size());
    const QColor dim = palette().color(QPalette::Disabled, QPalette::Text);
    const QColor normal = palette().color(QPalette::Text);
    for (int r = 0; r < channels.size(); ++r)
    {
        const CwChannelInfo &c = channels[r];
        const QStringList cells = {QString::number(qRound(c.pitchHz)),
                                   QString::number(c.wpm, 'f', 0),
                                   QString::number(c.snrDb, 'f', 0),
                                   c.text.right(160)};
        for (int col = 0; col < cells.size(); ++col)
        {
            auto *it = m_table->item(r, col);
            if (!it)
            {
                it = new QTableWidgetItem();
                m_table->setItem(r, col, it);
            }
            it->setText(cells[col]);
            it->setForeground(c.active ? normal : dim);
            if (col == 0)
                it->setData(Qt::UserRole, qRound(c.pitchHz));
        }
    }
}
