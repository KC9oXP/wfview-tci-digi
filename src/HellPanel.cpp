#include "HellPanel.h"

#include "AppPaths.h"
#include "HellEngine.h"
#include "HellWidget.h"
#include "UiStyle.h"
#include "WaterfallWidget.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QSplitter>
#include <QTextCursor>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>

HellPanel::HellPanel(HellEngine *engine, QSettings *settings, QWidget *parent)
    : ModemPanel(engine, settings, QStringLiteral("Hell"), parent)
    , m_hell(engine)
{
    QWidget *top = buildTop();

    // Receive picture, with a one-line strip below for what we transmitted.
    auto *rxBox = new QWidget();
    auto *rv = new QVBoxLayout(rxBox);
    rv->setContentsMargins(0, 0, 0, 0);
    m_rx = new HellWidget();
    m_rx->setToolTip(tr("Received Feld Hell, each column drawn twice so drifting text stays readable. "
                        "Slanted text: adjust RX trim. Letters cut in half: Align (Auto, or Shift).\n"
                        "Mouse wheel / PgUp: back through the last 30 minutes; Live or End: newest."));
    rv->addWidget(m_rx, 1);

    // alignment, picture saving
    auto *r2 = new QHBoxLayout();
    m_autoAlign = new QCheckBox(tr("Auto align"));
    m_autoAlign->setToolTip(tr("Find where the columns of the letters begin, so each letter sits whole in its "
                               "column instead of being split top/bottom (the receiver starts its columns at an "
                               "arbitrary moment; fldigi shows the same split). Applies to everything kept."));
    r2->addWidget(m_autoAlign);
    r2->addWidget(new QLabel(tr("Shift")));
    m_shift = new QSpinBox();
    m_shift->setRange(-7, 7);
    m_shift->setToolTip(tr("Move the letters up or down by half-pixels, on top of the automatic alignment."));
    r2->addWidget(m_shift);
    m_shiftInfo = new QLabel();
    r2->addWidget(m_shiftInfo);
    r2->addStretch(1);
    auto *save = new QPushButton(tr("Save picture"));
    save->setToolTip(tr("Save everything received (up to 30 minutes) as a PNG in the logs folder."));
    r2->addWidget(save);
    m_autoSave = new QCheckBox(tr("Auto-save"));
    m_autoSave->setToolTip(tr("Save what was received as a PNG in the logs folder every 30 minutes while decoding, "
                              "when Decode is switched off, on Clear and when the program closes (at least 20 s "
                              "of reception)."));
    r2->addWidget(m_autoSave);
    rv->addLayout(r2);
    m_echo = new QPlainTextEdit();
    m_echo->setReadOnly(true);
    m_echo->setMaximumBlockCount(200);
    m_echo->setFixedHeight(QFontMetrics(font()).lineSpacing() * 2 + 10);
    m_echo->setPlaceholderText(tr("Transmitted text appears here."));
    rv->addWidget(m_echo);

    if (m_settings)
    {
        m_rx->setZoom(m_settings->value(QStringLiteral("Hell/Zoom"), 2).toInt());
        m_rx->setGain(m_settings->value(QStringLiteral("Hell/Contrast"), 1.5).toDouble());
    }
    m_zoom->setCurrentIndex(m_zoom->findData(m_rx->zoom()));
    m_contrast->setValue(qRound(m_rx->gain() * 10));
    if (m_settings)
    {
        m_rx->setAutoAlign(m_settings->value(QStringLiteral("Hell/AutoAlign"), true).toBool());
        m_rx->setManualShift(m_settings->value(QStringLiteral("Hell/Shift"), 0).toInt());
        m_autoSave->setChecked(m_settings->value(QStringLiteral("Hell/AutoSave"), true).toBool());
    }
    else
        m_autoSave->setChecked(true);
    syncAlign();

    connect(m_autoAlign,
            &QCheckBox::toggled,
            this,
            [this](bool on)
            {
                m_rx->setAutoAlign(on);
                if (m_settings)
                    m_settings->setValue(QStringLiteral("Hell/AutoAlign"), on);
            });
    connect(m_shift,
            &QSpinBox::valueChanged,
            this,
            [this](int v)
            {
                m_rx->setManualShift(v);
                if (m_settings)
                    m_settings->setValue(QStringLiteral("Hell/Shift"), v);
            });
    connect(m_rx, &HellWidget::alignmentChanged, this, &HellPanel::syncAlign);
    connect(m_autoSave,
            &QCheckBox::toggled,
            this,
            [this](bool on)
            {
                if (m_settings)
                    m_settings->setValue(QStringLiteral("Hell/AutoSave"), on);
            });
    connect(save,
            &QPushButton::clicked,
            this,
            [this]()
            {
                const QString p = savePicture(false);
                statusLabel()->setText(p.isEmpty() ? tr("Nothing received to save.")
                                                   : tr("Saved %1").arg(QDir::toNativeSeparators(p)));
            });
    // Auto-save every 30 minutes while decoding, and when Decode goes off.
    auto *autoTimer = new QTimer(this);
    autoTimer->setInterval(30 * 60 * 1000);
    connect(autoTimer,
            &QTimer::timeout,
            this,
            [this]()
            {
                if (m_hell->decoding())
                    savePicture(true);
            });
    autoTimer->start();
    connect(m_hell,
            &ModemEngine::settingsChanged,
            this,
            [this]()
            {
                if (m_wasDecoding && !m_hell->decoding())
                    savePicture(true);
                m_wasDecoding = m_hell->decoding();
            });

    connect(m_hell, &HellEngine::columns, m_rx, &HellWidget::addColumns);
    connect(m_hell,
            &ModemEngine::textAppended,
            this,
            [this](const QString &t)
            {
                m_echo->moveCursor(QTextCursor::End);
                m_echo->insertPlainText(t);
                m_echo->ensureCursorVisible();
            });
    connect(m_hell,
            &ModemEngine::statsChanged,
            this,
            [this]()
            { m_level->setText(m_hell->decoding() ? tr("Level %1 dB").arg(qRound(m_hell->levelDb())) : QString()); });
    connect(waterfall(), &WaterfallWidget::clicked, m_hell, &ModemEngine::setCarrierHz);
    finishLayout(top, rxBox);
}

QWidget *HellPanel::buildTop()
{
    auto *top = new QWidget();
    auto *v = new QVBoxLayout(top);
    v->setContentsMargins(0, 0, 0, 0);

    auto *r1 = new QHBoxLayout();
    m_decode = new QPushButton(tr("Decode"));
    UiStyle::decodeButton(m_decode);
    r1->addWidget(m_decode);
    r1->addWidget(new QLabel(tr("Freq")));
    m_freq = new QDoubleSpinBox();
    m_freq->setRange(ModemEngine::kMinHz, ModemEngine::kMaxHz);
    m_freq->setDecimals(0);
    m_freq->setSuffix(tr(" Hz"));
    m_freq->setKeyboardTracking(false);
    m_freq->setToolTip(tr("Audio frequency of the signal (receive and transmit). Click the waterfall to tune."));
    r1->addWidget(m_freq);
    r1->addWidget(new QLabel(tr("RX trim")));
    m_trim = new QDoubleSpinBox();
    m_trim->setRange(-5.0, 5.0);
    m_trim->setDecimals(2);
    m_trim->setSingleStep(0.05);
    m_trim->setSuffix(tr(" %"));
    m_trim->setKeyboardTracking(false);
    m_trim->setToolTip(tr("Receive speed correction: if the text slants, nudge this until it is upright."));
    r1->addWidget(m_trim);
    r1->addWidget(new QLabel(tr("Zoom")));
    m_zoom = new QComboBox();
    for (int z = 1; z <= 4; ++z)
        m_zoom->addItem(QStringLiteral("%1x").arg(z), z);
    r1->addWidget(m_zoom);
    r1->addWidget(new QLabel(tr("Contrast")));
    m_contrast = new QSlider(Qt::Horizontal);
    m_contrast->setRange(5, 40);
    m_contrast->setMinimumWidth(70);
    r1->addWidget(m_contrast, 1);
    auto *clear = new QPushButton(tr("Clear"));
    r1->addWidget(clear);
    m_level = new QLabel();
    m_level->setMinimumWidth(QFontMetrics(font()).horizontalAdvance(QStringLiteral("Level -100 dB")) + 4);
    r1->addWidget(m_level);
    v->addLayout(r1);

    v->addWidget(waterfall(), 1);
    v->addWidget(statusLabel());

    connect(m_decode, &QPushButton::toggled, m_hell, &ModemEngine::setDecoding);
    connect(m_freq, &QDoubleSpinBox::valueChanged, m_hell, &ModemEngine::setCarrierHz);
    connect(m_trim, &QDoubleSpinBox::valueChanged, m_hell, &HellEngine::setRateTrim);
    connect(m_zoom,
            &QComboBox::activated,
            this,
            [this](int i)
            {
                m_rx->setZoom(m_zoom->itemData(i).toInt());
                if (m_settings)
                    m_settings->setValue(QStringLiteral("Hell/Zoom"), m_rx->zoom());
            });
    connect(m_contrast,
            &QSlider::valueChanged,
            this,
            [this](int val)
            {
                m_rx->setGain(val / 10.0);
                if (m_settings)
                    m_settings->setValue(QStringLiteral("Hell/Contrast"), m_rx->gain());
            });
    connect(clear,
            &QPushButton::clicked,
            this,
            [this]()
            {
                savePicture(true);
                m_rx->clear();
                m_savedTo = m_rx->endColumn();
                m_echo->clear();
            });
    return top;
}

void HellPanel::saveState()
{
    ModemPanel::saveState();
    savePicture(true);
}

QString HellPanel::savePicture(bool sinceLastAuto)
{
    if (sinceLastAuto && !m_autoSave->isChecked())
        return QString();
    const qint64 from = sinceLastAuto ? std::max(m_savedTo, m_rx->firstColumn()) : m_rx->firstColumn();
    const qint64 to = m_rx->endColumn();
    // 17.5 columns a second: at least 20 s for an automatic save.
    if (to <= from || (sinceLastAuto && to - from < 350))
        return QString();
    // 300 columns (about 43 letters) a line, 2x: readable at full size.
    const QImage img = m_rx->render(from, to, 300, 2);
    if (img.isNull())
        return QString();
    const QString path
        = QDir(AppPaths::logs())
              .filePath(QStringLiteral("%1 Hell%2.png")
                            .arg(AppPaths::stamp(), sinceLastAuto ? QStringLiteral(" auto") : QString()));
    if (!img.save(path))
        return QString();
    if (sinceLastAuto)
        m_savedTo = to;
    return path;
}

void HellPanel::syncAlign()
{
    const QSignalBlocker b1(m_autoAlign), b2(m_shift);
    m_autoAlign->setChecked(m_rx->autoAlign());
    m_shift->setValue(m_rx->manualShift());
    m_shiftInfo->setText(tr("(in use: %1)").arg(m_rx->rowShift()));
}

void HellPanel::syncSettings()
{
    ModemPanel::syncSettings();
    const QSignalBlocker b1(m_decode), b2(m_freq), b3(m_trim);
    m_decode->setChecked(m_hell->decoding());
    if (!m_freq->hasFocus())
        m_freq->setValue(m_hell->carrierHz());
    m_trim->setValue(m_hell->rateTrim());
}
