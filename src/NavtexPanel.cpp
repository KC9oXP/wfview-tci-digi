#include "NavtexPanel.h"

#include "NavtexEngine.h"
#include "RxText.h"
#include "UiStyle.h"
#include "WaterfallWidget.h"

#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSplitter>
#include <QVBoxLayout>

NavtexPanel::NavtexPanel(NavtexEngine *engine, QWidget *parent)
    : QWidget(parent)
    , m_nav(engine)
{
    auto *root = new QVBoxLayout(this);
    auto *r1 = new QHBoxLayout();
    m_decode = new QPushButton(tr("Decode"));
    UiStyle::decodeButton(m_decode);
    r1->addWidget(m_decode);
    r1->addWidget(new QLabel(tr("Centre")));
    m_freq = new QDoubleSpinBox();
    m_freq->setRange(300, 2700);
    m_freq->setDecimals(0);
    m_freq->setSuffix(tr(" Hz"));
    m_freq->setKeyboardTracking(false);
    m_freq->setToolTip(tr("Centre of the 170 Hz FSK pair. Click the waterfall between the two tones."));
    r1->addWidget(m_freq);
    m_reverse = new QCheckBox(tr("Reverse"));
    m_reverse->setToolTip(tr("Swap the tones (if you are on LSB, or nothing decodes on a strong signal)."));
    r1->addWidget(m_reverse);
    m_state = new QLabel();
    r1->addWidget(m_state, 1);
    auto *clear = new QPushButton(tr("Clear"));
    r1->addWidget(clear);
    root->addLayout(r1);

    auto *hint = new QLabel(tr("NAVTEX: 518 kHz (international, English) and 490 kHz (national). Set the radio to USB "
                               "1 kHz below (517.000 kHz) so the signal sits at 1000 Hz. Also SITOR-B on HF "
                               "(4209.5, 8416.5, 12579.0 kHz ...)."));
    hint->setWordWrap(true);
    hint->setEnabled(false);
    root->addWidget(hint);

    auto *split = new QSplitter(Qt::Vertical);
    m_wf = new WaterfallWidget();
    split->addWidget(m_wf);
    m_rx = new QPlainTextEdit();
    RxScrollback::attach(m_rx, QStringLiteral("NAVTEX"));
    m_rx->setReadOnly(true);
    QFont f(QStringLiteral("Consolas"));
    f.setStyleHint(QFont::Monospace);
    f.setPointSizeF(f.pointSizeF() * 1.15);
    m_rx->setFont(f);
    split->addWidget(m_rx);
    split->setStretchFactor(0, 0);
    split->setStretchFactor(1, 1);
    root->addWidget(split, 1);

    connect(m_decode, &QPushButton::toggled, m_nav, &NavtexEngine::setDecoding);
    connect(m_freq, &QDoubleSpinBox::valueChanged, m_nav, &NavtexEngine::setCentreHz);
    connect(m_reverse, &QCheckBox::toggled, m_nav, &NavtexEngine::setReverse);
    connect(clear, &QPushButton::clicked, m_rx, &QPlainTextEdit::clear);
    connect(m_wf, &WaterfallWidget::clicked, m_nav, &NavtexEngine::setCentreHz);
    connect(m_wf, &WaterfallWidget::nudged, m_nav, &NavtexEngine::setCentreHz);
    connect(m_nav, &NavtexEngine::settingsChanged, this, &NavtexPanel::sync);
    connect(m_nav, &NavtexEngine::statsChanged, this, &NavtexPanel::syncStats);
    connect(m_nav, &NavtexEngine::textAppended, this, [this](const QString &t) { RxText::append(m_rx, t, 1000000); });
    sync();
    syncStats();
}

void NavtexPanel::addSpectrumLine(const QVector<float> &db, double hzPerBin)
{
    if (isVisible())
        m_wf->addLine(db, hzPerBin);
}

void NavtexPanel::sync()
{
    const QSignalBlocker b1(m_decode), b2(m_freq), b3(m_reverse);
    m_decode->setChecked(m_nav->decoding());
    if (!m_freq->hasFocus())
        m_freq->setValue(m_nav->centreHz());
    m_reverse->setChecked(m_nav->reverse());
    m_wf->setMarker(m_nav->centreHz(), 170.0 + 100.0);
}

void NavtexPanel::syncStats()
{
    if (!m_nav->decoding())
        m_state->setText(tr("Off"));
    else
        m_state->setText(m_nav->locked() ? tr("<b>Locked</b> (receiving characters)") : tr("Searching for phasing..."));
}
