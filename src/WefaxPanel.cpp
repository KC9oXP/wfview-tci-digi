#include "WefaxPanel.h"

#include "AppPaths.h"
#include "ImageView.h"
#include "UiStyle.h"
#include "WaterfallWidget.h"
#include "WefaxEngine.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QUrl>
#include <QVBoxLayout>

WefaxPanel::WefaxPanel(WefaxEngine *engine, QWidget *parent)
    : QWidget(parent)
    , m_fax(engine)
{
    auto *root = new QVBoxLayout(this);
    auto *r1 = new QHBoxLayout();
    m_decode = new QPushButton(tr("Decode"));
    UiStyle::decodeButton(m_decode);
    r1->addWidget(m_decode);
    r1->addWidget(new QLabel(tr("LPM")));
    m_lpm = new QComboBox();
    for (int l : {60, 90, 120, 240})
        m_lpm->addItem(QString::number(l), l);
    m_lpm->setToolTip(tr("Lines per minute. Almost all stations use 120."));
    r1->addWidget(m_lpm);
    r1->addWidget(new QLabel(tr("IOC")));
    m_ioc = new QComboBox();
    m_ioc->addItem(QStringLiteral("576"), 576);
    m_ioc->addItem(QStringLiteral("288"), 288);
    m_ioc->setToolTip(tr("Index of cooperation (picture width). 576 is standard; the start tone sets it."));
    r1->addWidget(m_ioc);
    m_auto = new QCheckBox(tr("Auto start"));
    m_auto->setToolTip(tr("Start on the APT start tone, align on the phasing lines, stop on the stop tone."));
    r1->addWidget(m_auto);
    auto *now = new QPushButton(tr("Start now"));
    now->setToolTip(tr("Start a picture now (joined late: then click its left edge to align)."));
    r1->addWidget(now);
    auto *stop = new QPushButton(tr("Stop"));
    stop->setToolTip(tr("End the picture and save it."));
    r1->addWidget(stop);
    r1->addStretch(1);
    root->addLayout(r1);
    r1 = new QHBoxLayout();
    r1->addWidget(new QLabel(tr("Trim")));
    m_trim = new QSpinBox();
    m_trim->setRange(-5000, 5000);
    m_trim->setSingleStep(10);
    m_trim->setSuffix(tr(" ppm"));
    m_trim->setToolTip(tr("Line timing correction for a slanted picture (the phasing lines correct it by "
                          "themselves when the start was received)."));
    r1->addWidget(m_trim);
    m_align = new QPushButton(tr("Align"));
    m_align->setCheckable(true);
    m_align->setToolTip(tr("Then click the picture's left edge (its margin) to move it there."));
    r1->addWidget(m_align);
    auto *save = new QPushButton(tr("Save"));
    r1->addWidget(save);
    auto *folder = new QPushButton(tr("Folder"));
    r1->addWidget(folder);
    r1->addStretch(1);
    root->addLayout(r1);

    m_status = new QLabel();
    m_status->setWordWrap(true);
    root->addWidget(m_status);
    auto *hint = new QLabel(tr("Tune USB 1.9 kHz below the published frequency (NMF Boston 12750.0 kHz: dial "
                               "12748.1). The signal then sits between 1500 Hz (black) and 2300 Hz (white)."));
    hint->setWordWrap(true);
    hint->setEnabled(false);
    root->addWidget(hint);

    m_wf = new WaterfallWidget();
    m_wf->setMaximumHeight(80);
    m_wf->setMarker(1900, 800);
    root->addWidget(m_wf);

    m_view = new ImageView(ImageView::Fit::Width);
    m_view->setPlaceholder(tr("Weather fax pictures appear here."));
    m_scroll = new QScrollArea();
    m_scroll->setWidgetResizable(true);
    m_scroll->setWidget(m_view);
    root->addWidget(m_scroll, 1);

    connect(m_decode, &QPushButton::toggled, m_fax, &WefaxEngine::setDecoding);
    connect(m_lpm, &QComboBox::activated, this, [this](int i) { m_fax->setLpm(m_lpm->itemData(i).toInt()); });
    connect(m_ioc, &QComboBox::activated, this, [this](int i) { m_fax->setIoc(m_ioc->itemData(i).toInt()); });
    connect(m_auto, &QCheckBox::toggled, m_fax, &WefaxEngine::setAutoStart);
    connect(m_trim, &QSpinBox::valueChanged, m_fax, [this](int v) { m_fax->setSlantPpm(v); });
    connect(now,
            &QPushButton::clicked,
            this,
            [this]()
            {
                m_view->setColumnShift(0);
                m_fax->startManual();
            });
    connect(stop, &QPushButton::clicked, m_fax, &WefaxEngine::stopImage);
    connect(save,
            &QPushButton::clicked,
            this,
            [this]()
            {
                const QString p = m_fax->save(m_view->shownImage(), QStringLiteral("saved"));
                m_status->setText(p.isEmpty() ? tr("Nothing to save.")
                                              : tr("Saved %1").arg(QDir::toNativeSeparators(p)));
            });
    connect(folder,
            &QPushButton::clicked,
            this,
            []() { QDesktopServices::openUrl(QUrl::fromLocalFile(AppPaths::wefax())); });
    connect(m_view,
            &ImageView::clicked,
            this,
            [this](QPoint pos)
            {
                if (!m_align->isChecked())
                    return;
                // pos is in the shown (already shifted) picture.
                m_view->setColumnShift(m_view->columnShift() + pos.x());
                m_align->setChecked(false);
            });
    connect(m_fax, &WefaxEngine::settingsChanged, this, &WefaxPanel::sync);
    connect(m_fax, &WefaxEngine::statusChanged, this, &WefaxPanel::syncStatus);
    connect(m_fax, &WefaxEngine::imageChanged, this, &WefaxPanel::showImage);
    connect(m_fax,
            &WefaxEngine::pictureEnded,
            this,
            [this]()
            {
                const QString p = m_fax->save(m_view->shownImage());
                if (!p.isEmpty())
                    m_status->setText(tr("Picture ended, saved %1").arg(QDir::toNativeSeparators(p)));
            });
    sync();
    syncStatus();
}

void WefaxPanel::showImage()
{
    QScrollBar *sb = m_scroll->verticalScrollBar();
    const bool atEnd = sb->value() >= sb->maximum() - 4;
    if (m_fax->rows() == 0)
        m_view->setColumnShift(0);
    m_view->setImage(m_fax->image(), m_fax->rows());
    if (atEnd)
        sb->setValue(sb->maximum());
}

void WefaxPanel::addSpectrumLine(const QVector<float> &db, double hzPerBin)
{
    if (isVisible())
        m_wf->addLine(db, hzPerBin);
}

void WefaxPanel::sync()
{
    const QSignalBlocker b1(m_decode), b2(m_lpm), b3(m_ioc), b4(m_auto), b5(m_trim);
    m_decode->setChecked(m_fax->decoding());
    m_lpm->setCurrentIndex(m_lpm->findData(int(m_fax->lpm())));
    m_ioc->setCurrentIndex(m_ioc->findData(m_fax->ioc()));
    m_auto->setChecked(m_fax->autoStart());
    m_trim->setValue(qRound(m_fax->slantPpm()));
}

void WefaxPanel::syncStatus()
{
    if (!m_fax->decoding())
    {
        m_status->setText(tr("Off"));
        return;
    }
    QString s;
    switch (m_fax->state())
    {
    case 0:
        s = m_fax->autoStart() ? tr("Waiting for the start tone (or press Start now)") : tr("Idle: press Start now");
        break;
    case 1:
        s = tr("Start tone (IOC %1)").arg(m_fax->ioc());
        break;
    case 2:
        s = tr("Phasing...");
        break;
    default:
        s = tr("Receiving: %1 lines, timing %2 ppm%3")
                .arg(m_fax->rows())
                .arg(qRound((m_fax->periodRatio() - 1.0) * 1e6))
                .arg(m_fax->phasingLines() > 0 ? tr(", aligned on %1 phasing lines").arg(m_fax->phasingLines())
                                               : QString());
        break;
    }
    s += tr("    level %1 dB").arg(qRound(m_fax->levelDb()));
    m_status->setText(s);
}
