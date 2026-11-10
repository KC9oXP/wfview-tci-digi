#include "SstvPanel.h"

#include "AppPaths.h"
#include "ImageView.h"
#include "SstvEngine.h"
#include "WaterfallWidget.h"
#include "sstvcore.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMimeData>
#include <QPainter>
#include <QProgressBar>
#include <QPushButton>
#include <QSettings>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QSplitter>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

SstvPanel::SstvPanel(SstvEngine *engine, QSettings *settings, QWidget *parent)
    : QWidget(parent)
    , m_sstv(engine)
    , m_settings(settings)
{
    auto *root = new QHBoxLayout(this);
    auto *split = new QSplitter(Qt::Horizontal);
    split->addWidget(buildRx());
    split->addWidget(buildTx());
    split->setStretchFactor(0, 3);
    split->setStretchFactor(1, 1);
    root->addWidget(split);

    connect(m_sstv, &ModemEngine::settingsChanged, this, &SstvPanel::sync);
    connect(m_sstv, &ModemEngine::statusChanged, m_status, &QLabel::setText);
    connect(m_sstv, &ModemEngine::transmittingChanged, this, &SstvPanel::syncTx);
    connect(m_sstv, &SstvEngine::imageUpdated, this, [this](const QImage &img, int) { m_view->setImage(img); });
    connect(m_sstv, &SstvEngine::imageSaved, this, &SstvPanel::addThumb);
    connect(m_sstv,
            &ModemEngine::statsChanged,
            this,
            [this]()
            {
                m_level->setText(m_sstv->decoding()
                                     ? tr("%1 Hz  %2 dB").arg(qRound(m_sstv->toneHz())).arg(qRound(m_sstv->levelDb()))
                                     : QString());
                m_stop->setEnabled(m_sstv->receiving());
            });

    // previously received pictures
    QDir dir(AppPaths::sstv());
    const QFileInfoList files = dir.entryInfoList({QStringLiteral("*.png")}, QDir::Files, QDir::Name | QDir::Reversed);
    for (int i = std::min(int(files.size()), 40) - 1; i >= 0; --i)
        addThumb(files.at(i).absoluteFilePath(), QImage(files.at(i).absoluteFilePath()));

    const QString last = m_settings->value(QStringLiteral("SSTV/TxImage")).toString();
    if (!last.isEmpty() && QFileInfo::exists(last))
        loadTxImage(QImage(last));
    else
        loadTxImage(QImage());

    auto *poll = new QTimer(this);
    poll->setInterval(250);
    connect(poll,
            &QTimer::timeout,
            this,
            [this]()
            {
                if (m_sstv->transmitting())
                {
                    const double total = m_sstv->txSeconds();
                    const double pr = m_sstv->txProgress();
                    m_progress->setValue(int(pr * 1000));
                    m_progress->setFormat(tr("%1 s left").arg(qRound(total * (1.0 - pr))));
                }
            });
    poll->start();
    sync();
    syncTx(false);
}

QWidget *SstvPanel::buildRx()
{
    auto *w = new QWidget();
    auto *v = new QVBoxLayout(w);
    v->setContentsMargins(0, 0, 0, 0);

    auto *r1 = new QHBoxLayout();
    m_decode = new QPushButton(tr("Decode"));
    m_decode->setCheckable(true);
    m_decode->setToolTip(tr("Listen for SSTV: pictures start by themselves on their VIS header."));
    r1->addWidget(m_decode);
    m_autoSlant = new QCheckBox(tr("Auto slant"));
    m_autoSlant->setToolTip(tr("Straighten the picture from its sync pulses as it comes in."));
    r1->addWidget(m_autoSlant);
    r1->addWidget(new QLabel(tr("Trim")));
    m_trim = new QSpinBox();
    m_trim->setRange(-5000, 5000);
    m_trim->setSingleStep(50);
    m_trim->setSuffix(tr(" ppm"));
    m_trim->setToolTip(tr("Fixed correction of the line timing (a sound card clock error), applied before the "
                          "auto slant. Positive = lines longer."));
    r1->addWidget(m_trim);
    r1->addStretch(1);
    m_level = new QLabel();
    r1->addWidget(m_level);
    v->addLayout(r1);
    r1 = new QHBoxLayout();
    m_manualMode = new QComboBox();
    for (int i = 0; i < sstv::ModeCount; ++i)
        m_manualMode->addItem(QLatin1String(sstv::info(i).name), i);
    m_manualMode->setToolTip(tr("Mode for Start now (when the VIS header was missed)."));
    r1->addWidget(m_manualMode);
    m_startNow = new QPushButton(tr("Start now"));
    m_startNow->setToolTip(tr("Start a picture now in the chosen mode, without waiting for a VIS header."));
    r1->addWidget(m_startNow);
    m_stop = new QPushButton(tr("Stop"));
    m_stop->setToolTip(tr("End the picture being received (it is saved if a quarter or more came in)."));
    r1->addWidget(m_stop);
    m_save = new QPushButton(tr("Save"));
    m_save->setToolTip(tr("Save the picture shown now."));
    r1->addWidget(m_save);
    r1->addStretch(1);
    v->addLayout(r1);

    auto *mid = new QSplitter(Qt::Horizontal);
    m_view = new ImageView(ImageView::Fit::Window);
    m_view->setPlaceholder(tr("Received SSTV pictures appear here.\nCommon frequencies: 14.230, 14.233, 21.340, "
                              "28.680, 7.171 (LSB in some regions), 3.845 MHz; ISS events on 145.800 MHz FM."));
    mid->addWidget(m_view);
    m_gallery = new QListWidget();
    m_gallery->setViewMode(QListView::IconMode);
    m_gallery->setIconSize(QSize(120, 90));
    m_gallery->setResizeMode(QListView::Adjust);
    m_gallery->setMovement(QListView::Static);
    m_gallery->setMinimumWidth(100);
    m_gallery->setToolTip(tr("Received pictures. Click to show, double-click to open the file, "
                             "right-click... use the folder button to see them all."));
    mid->addWidget(m_gallery);
    mid->setStretchFactor(0, 4);
    mid->setStretchFactor(1, 1);
    v->addWidget(mid, 1);

    m_wf = new WaterfallWidget();
    m_wf->setMaximumHeight(90);
    m_wf->setMarker(1700, 1200);
    v->addWidget(m_wf);
    auto *r3 = new QHBoxLayout();
    m_status = new QLabel();
    m_status->setWordWrap(true);
    m_status->setTextInteractionFlags(Qt::TextSelectableByMouse);
    r3->addWidget(m_status, 1);
    auto *folder = new QPushButton(tr("Folder"));
    folder->setToolTip(tr("Open the folder with the received pictures."));
    r3->addWidget(folder);
    v->addLayout(r3);

    connect(m_decode, &QPushButton::toggled, m_sstv, &ModemEngine::setDecoding);
    connect(m_autoSlant, &QCheckBox::toggled, m_sstv, &SstvEngine::setAutoSlant);
    connect(m_trim, &QSpinBox::valueChanged, m_sstv, [this](int v) { m_sstv->setSlantPpm(v); });
    connect(m_startNow,
            &QPushButton::clicked,
            this,
            [this]() { m_sstv->startManual(m_manualMode->currentData().toInt()); });
    connect(m_stop, &QPushButton::clicked, m_sstv, &SstvEngine::stopImage);
    connect(m_save,
            &QPushButton::clicked,
            this,
            [this]()
            {
                const QImage img = m_view->image();
                const int mode = m_sstv->rxMode() >= 0 ? m_sstv->rxMode() : 0;
                const QString p = m_sstv->saveImage(img, mode, QStringLiteral("saved"));
                m_status->setText(p.isEmpty() ? tr("Nothing to save.")
                                              : tr("Saved %1").arg(QDir::toNativeSeparators(p)));
            });
    connect(folder,
            &QPushButton::clicked,
            this,
            []() { QDesktopServices::openUrl(QUrl::fromLocalFile(AppPaths::sstv())); });
    connect(m_gallery,
            &QListWidget::itemClicked,
            this,
            [this](QListWidgetItem *it) { m_view->setImage(QImage(it->data(Qt::UserRole).toString())); });
    connect(m_gallery,
            &QListWidget::itemDoubleClicked,
            this,
            [](QListWidgetItem *it)
            { QDesktopServices::openUrl(QUrl::fromLocalFile(it->data(Qt::UserRole).toString())); });
    return w;
}

QWidget *SstvPanel::buildTx()
{
    auto *box = new QGroupBox(tr("Transmit"));
    auto *v = new QVBoxLayout(box);
    m_txWarn = new QLabel();
    m_txWarn->setWordWrap(true);
    m_txWarn->setStyleSheet(QStringLiteral("color: #ffb060;"));
    v->addWidget(m_txWarn);

    auto *r1 = new QHBoxLayout();
    auto *open = new QPushButton(tr("Open..."));
    auto *paste = new QPushButton(tr("Paste"));
    paste->setToolTip(tr("Use the picture on the clipboard."));
    auto *useRx = new QPushButton(tr("Use shown"));
    useRx->setToolTip(tr("Send back the picture shown on the left (e.g. a reply with your overlay)."));
    r1->addWidget(open);
    r1->addWidget(paste);
    r1->addWidget(useRx);
    v->addLayout(r1);

    auto *r2 = new QHBoxLayout();
    r2->addWidget(new QLabel(tr("Mode")));
    m_txMode = new QComboBox();
    for (int i = 0; i < sstv::ModeCount; ++i)
        m_txMode->addItem(QStringLiteral("%1  (%2 s)")
                              .arg(QLatin1String(sstv::info(i).name))
                              .arg(qRound(sstv::imageSeconds(i) + 0.9)),
                          i);
    r2->addWidget(m_txMode, 1);
    v->addLayout(r2);

    auto *r3 = new QHBoxLayout();
    r3->addWidget(new QLabel(tr("Text")));
    m_overlay = new QLineEdit(
        m_settings->value(QStringLiteral("SSTV/Overlay"), QStringLiteral("{CALL} de {MYCALL}")).toString());
    m_overlay->setToolTip(tr("Drawn along the bottom of the picture. {MYCALL}, {CALL} and {RST} are filled in; "
                             "leave empty for none."));
    r3->addWidget(m_overlay, 1);
    v->addLayout(r3);
    auto *r4 = new QHBoxLayout();
    r4->addWidget(new QLabel(tr("Call")));
    m_call = new QLineEdit();
    m_call->setPlaceholderText(tr("other station"));
    r4->addWidget(m_call, 1);
    v->addLayout(r4);

    m_preview = new ImageView(ImageView::Fit::Window);
    m_preview->setMinimumSize(160, 120);
    m_preview->setToolTip(tr("What will be sent (shown at 4:3 like the receiving end)."));
    v->addWidget(m_preview, 1);

    auto *r5 = new QHBoxLayout();
    m_send = new QPushButton(tr("Send"));
    m_send->setStyleSheet(QStringLiteral("font-weight: bold;"));
    m_abort = new QPushButton(tr("Abort"));
    r5->addWidget(m_send);
    r5->addWidget(m_abort);
    v->addLayout(r5);
    m_progress = new QProgressBar();
    m_progress->setRange(0, 1000);
    m_progress->setTextVisible(true);
    v->addWidget(m_progress);
    auto *r6 = new QHBoxLayout();
    r6->addWidget(new QLabel(tr("TX level")));
    m_txLevel = new QSlider(Qt::Horizontal);
    m_txLevel->setRange(1, 100);
    r6->addWidget(m_txLevel, 1);
    v->addLayout(r6);
    auto *note = new QLabel(tr("A picture keeps the transmitter on for up to several minutes: "
                               "consider lower power for the long modes."));
    note->setWordWrap(true);
    note->setEnabled(false);
    v->addWidget(note);

    connect(open,
            &QPushButton::clicked,
            this,
            [this]()
            {
                const QString start = m_settings->value(QStringLiteral("SSTV/TxDir"), QDir::homePath()).toString();
                const QString p = QFileDialog::getOpenFileName(
                    this, tr("Picture to send"), start, tr("Pictures (*.png *.jpg *.jpeg *.bmp *.gif *.webp)"));
                if (p.isEmpty())
                    return;
                m_settings->setValue(QStringLiteral("SSTV/TxDir"), QFileInfo(p).absolutePath());
                m_settings->setValue(QStringLiteral("SSTV/TxImage"), p);
                loadTxImage(QImage(p));
            });
    connect(paste,
            &QPushButton::clicked,
            this,
            [this]()
            {
                const QImage img = QApplication::clipboard()->image();
                if (!img.isNull())
                    loadTxImage(img);
            });
    connect(useRx, &QPushButton::clicked, this, [this]() { loadTxImage(m_view->image()); });
    connect(m_txMode,
            &QComboBox::activated,
            this,
            [this](int i)
            {
                m_sstv->setTxMode(m_txMode->itemData(i).toInt());
                updatePreview();
            });
    connect(m_overlay,
            &QLineEdit::textChanged,
            this,
            [this](const QString &t)
            {
                m_settings->setValue(QStringLiteral("SSTV/Overlay"), t);
                updatePreview();
            });
    connect(m_call,
            &QLineEdit::textChanged,
            this,
            [this](const QString &t)
            {
                m_sstv->setCall(t.trimmed().toUpper());
                updatePreview();
            });
    connect(m_send, &QPushButton::clicked, this, [this]() { m_sstv->sendImage(composeTx()); });
    connect(m_abort, &QPushButton::clicked, m_sstv, &ModemEngine::abortTx);
    connect(m_txLevel, &QSlider::valueChanged, this, [this](int v) { m_sstv->setTxLevel(v / 100.0); });
    return box;
}

void SstvPanel::loadTxImage(const QImage &img)
{
    if (img.isNull())
    {
        // A plain test card until a picture is chosen.
        QImage card(320, 240, QImage::Format_RGB32);
        QPainter p(&card);
        static const QColor bars[]
            = {Qt::white, Qt::yellow, Qt::cyan, Qt::green, Qt::magenta, Qt::red, Qt::blue, Qt::black};
        for (int i = 0; i < 8; ++i)
            p.fillRect(i * 40, 0, 40, 160, bars[i]);
        QLinearGradient g(0, 0, 320, 0);
        g.setColorAt(0, Qt::black);
        g.setColorAt(1, Qt::white);
        p.fillRect(0, 160, 320, 80, g);
        p.end();
        m_txSource = card;
    }
    else
        m_txSource = img;
    updatePreview();
}

QImage SstvPanel::composeTx() const
{
    QImage out = SstvEngine::fitToMode(m_txSource, m_sstv->txMode());
    QString text = m_overlay->text();
    text.replace(QStringLiteral("{MYCALL}"), m_sstv->myCall(), Qt::CaseInsensitive);
    text.replace(QStringLiteral("{CALL}"), m_sstv->call(), Qt::CaseInsensitive);
    text.replace(QStringLiteral("{RST}"), QStringLiteral("595"), Qt::CaseInsensitive);
    text = text.trimmed();
    if (text.isEmpty())
        return out;
    QPainter p(&out);
    QFont f(QStringLiteral("Arial"));
    f.setBold(true);
    f.setPixelSize(std::max(10, out.height() / 11));
    p.setFont(f);
    const int band = QFontMetrics(f).height() + 6;
    p.fillRect(0, out.height() - band, out.width(), band, QColor(0, 0, 0, 170));
    p.setPen(Qt::white);
    p.drawText(QRect(0, out.height() - band, out.width(), band), Qt::AlignCenter, text);
    return out;
}

void SstvPanel::updatePreview()
{
    m_preview->setImage(composeTx());
}

void SstvPanel::addThumb(const QString &path, const QImage &img)
{
    if (img.isNull())
        return;
    auto *it = new QListWidgetItem(
        QIcon(QPixmap::fromImage(img.scaled(120, 90, Qt::IgnoreAspectRatio, Qt::SmoothTransformation))),
        QFileInfo(path).completeBaseName().left(15));
    it->setData(Qt::UserRole, path);
    it->setToolTip(QDir::toNativeSeparators(path));
    m_gallery->insertItem(0, it);
    while (m_gallery->count() > 60)
        delete m_gallery->takeItem(m_gallery->count() - 1);
}

void SstvPanel::sync()
{
    const QSignalBlocker b1(m_decode), b2(m_autoSlant), b3(m_trim), b4(m_txMode), b5(m_txLevel);
    m_decode->setChecked(m_sstv->decoding());
    m_autoSlant->setChecked(m_sstv->autoSlant());
    m_trim->setValue(qRound(m_sstv->slantPpm()));
    m_txMode->setCurrentIndex(m_txMode->findData(m_sstv->txMode()));
    m_txLevel->setValue(qRound(m_sstv->txLevel() * 100));
    if (!m_call->hasFocus() && m_call->text() != m_sstv->call())
    {
        const QSignalBlocker b6(m_call);
        m_call->setText(m_sstv->call());
    }
    updatePreview();
}

void SstvPanel::syncTx(bool on)
{
    m_send->setEnabled(!on && m_ready && m_canTx);
    m_abort->setEnabled(on);
    m_txMode->setEnabled(!on);
    if (!on)
    {
        m_progress->setValue(0);
        m_progress->setFormat(QString());
    }
    m_wf->setTransmitting(on);
}

void SstvPanel::setRadioState(bool ready, bool canTransmit, const QString &mode)
{
    m_ready = ready;
    m_canTx = canTransmit;
    const QString m = mode.toUpper();
    const bool audioMode = m.isEmpty() || m.startsWith(QLatin1String("DIG")) || m == QLatin1String("USB")
        || m == QLatin1String("LSB") || m.contains(QLatin1String("FM"));
    if (!ready)
        m_txWarn->setText(tr("Not connected."));
    else if (!canTransmit)
        m_txWarn->setText(tr("The server reports this radio as receive only."));
    else if (!audioMode)
        m_txWarn->setText(tr("The radio is in %1: SSTV needs USB/LSB, a data mode (DIGU/DIGL) or FM.").arg(mode));
    else
        m_txWarn->clear();
    m_txWarn->setVisible(!m_txWarn->text().isEmpty());
    syncTx(m_sstv->transmitting());
}

void SstvPanel::addSpectrumLine(const QVector<float> &db, double hzPerBin)
{
    if (isVisible())
        m_wf->addLine(db, hzPerBin);
}

void SstvPanel::saveState() { }
