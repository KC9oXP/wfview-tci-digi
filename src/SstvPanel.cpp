#include "SstvPanel.h"

#include "AppPaths.h"
#include "ImageView.h"
#include "RadioModes.h"
#include "SstvEngine.h"
#include "TxLevelSlider.h"
#include "TxTools.h"
#include "UiStyle.h"
#include "WaterfallWidget.h"
#include "sstvcore.h"

#include <algorithm>

#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMimeData>
#include <QPainter>
#include <QProgressBar>
#include <QRadioButton>
#include <QPushButton>
#include <QSettings>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QSplitter>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

namespace
{
// Picture text choices: label, tooltip, default text. The text of each choice
// can be edited and is kept per choice (SSTV/MsgText<n>).
struct MsgChoice
{
    const char *label;
    const char *tip;
    const char *text;
};
const MsgChoice kMsgChoices[] = {
    {QT_TRANSLATE_NOOP("SstvPanel", "None"), QT_TRANSLATE_NOOP("SstvPanel", "No text on the picture."), ""},
    {QT_TRANSLATE_NOOP("SstvPanel", "CQ"), QT_TRANSLATE_NOOP("SstvPanel", "Calling CQ."), "CQ SSTV de {MYCALL} {GRID}"},
    {QT_TRANSLATE_NOOP("SstvPanel", "Answer"),
     QT_TRANSLATE_NOOP("SstvPanel", "Answering a station (fill in Call)."),
     "{CALL} de {MYCALL}"},
    {QT_TRANSLATE_NOOP("SstvPanel", "Report"),
     QT_TRANSLATE_NOOP("SstvPanel", "Answer with a signal report (RST box)."),
     "{CALL} de {MYCALL} UR {RST}"},
    {QT_TRANSLATE_NOOP("SstvPanel", "Thanks"),
     QT_TRANSLATE_NOOP("SstvPanel", "Thank the other station for the picture / report."),
     "{CALL} TNX PIC + RPT de {MYCALL}"},
    {QT_TRANSLATE_NOOP("SstvPanel", "73"),
     QT_TRANSLATE_NOOP("SstvPanel", "The goodbye at the end of the contact."),
     "{CALL} TNX QSO 73 de {MYCALL}"},
    {QT_TRANSLATE_NOOP("SstvPanel", "Test"),
     QT_TRANSLATE_NOOP("SstvPanel", "A test picture (no one called)."),
     "TEST de {MYCALL}"},
    {QT_TRANSLATE_NOOP("SstvPanel", "Custom"), QT_TRANSLATE_NOOP("SstvPanel", "Your own text."), ""},
};
constexpr int kMsgCount = int(sizeof(kMsgChoices) / sizeof(kMsgChoices[0]));
constexpr int kMsgNone = 0, kMsgAnswer = 2, kMsgCustom = kMsgCount - 1;

QString msgKey(int i)
{
    return QStringLiteral("SSTV/MsgText%1").arg(i);
}

// Largest bold font (from maxPx down) that fits text into width.
QFont fitFont(const QString &text, int width, int maxPx)
{
    QFont f(QStringLiteral("Arial"));
    f.setBold(true);
    for (int px = maxPx; px >= 8; --px)
    {
        f.setPixelSize(px);
        if (QFontMetrics(f).horizontalAdvance(text) <= width)
            break;
    }
    return f;
}
} // namespace

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
                updateMarkers();
            });
    // The mode list follows whatever was decoded (VIS or sync timing).
    connect(m_sstv,
            &SstvEngine::imageStarted,
            this,
            [this](int mode, bool)
            {
                const int i = m_manualMode->findData(mode);
                if (i >= 0)
                    m_manualMode->setCurrentIndex(i);
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
    m_decode = new UiStyle::DecodeButton();
    UiStyle::decodeButton(m_decode);
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
    m_trimTx = new QCheckBox(tr("Use when sending"));
    m_trimTx->setToolTip(
        tr("Also apply the Trim to pictures you send, so a sound card clock that is off does not make them "
           "lean at the other station.\nOnly worth it when the clock error is large (tens of ppm or more): "
           "a few ppm moves a whole picture by less than one pixel. The WWV tab measures the error.\n"
           "This assumes sending and receiving use the same audio clock (the radio's USB sound card, or "
           "a LAN radio)."));
    r1->addWidget(m_trimTx);
    m_trimTxNote = new QLabel();
    m_trimTxNote->setEnabled(false);
    r1->addWidget(m_trimTxNote);
    r1->addStretch(1);
    m_level = new QLabel();
    r1->addWidget(m_level);
    v->addLayout(r1);
    r1 = new QHBoxLayout();
    auto *modeLabel = new QLabel(tr("Mode if VIS missed"));
    r1->addWidget(modeLabel);
    m_manualMode = new QComboBox();
    for (int i = 0; i < sstv::ModeCount; ++i)
        m_manualMode->addItem(QLatin1String(sstv::info(i).name), i);
    m_manualMode->setToolTip(tr("Mode for Start now, when the VIS header was missed. It changes by itself to the "
                                "mode of each picture received."));
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
    m_autoStart = new QCheckBox(tr("Start without VIS"));
    m_autoStart->setToolTip(tr("Start a picture by itself when its sync pulses repeat at one mode's line timing "
                               "(you joined after the VIS header), as MMSSTV does. The picture starts at the line "
                               "where it was found."));
    r1->addWidget(m_autoStart);
    r1->addStretch(1);
    v->addLayout(r1);

    r1 = new QHBoxLayout();
    r1->addWidget(new QLabel(tr("Offset")));
    m_offset = new QSpinBox();
    m_offset->setRange(-300, 300);
    m_offset->setSuffix(tr(" Hz"));
    m_offset->setToolTip(tr("How far the received tones are above (+) or below (-) where they should be (sync "
                            "1200, black 1500, white 2300 Hz). Click the small waterfall at the middle of the "
                            "signal (1900 Hz) or use the mouse wheel there for small steps."));
    r1->addWidget(m_offset);
    m_autoOffset = new QCheckBox(tr("Auto"));
    m_autoOffset->setToolTip(tr("Correct the offset by itself from the 1900 Hz VIS leader and the 1200 Hz sync "
                                "pulses (added to the offset set here)."));
    r1->addWidget(m_autoOffset);
    m_offsetInfo = new QLabel();
    m_offsetInfo->setToolTip(tr("Offset in use: the one set here plus the automatic correction."));
    r1->addWidget(m_offsetInfo);
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
    m_wf->setMaximumHeight(110);
    m_wf->setToolTip(tr("Lines: sync 1200, black 1500, centre 1900, white 2300 Hz, moved by the offset. Click or "
                        "drag to put the centre on the middle of the signal; mouse wheel = 1 Hz steps. With Auto "
                        "on, the next VIS header or sync pulses refine it from where you put it."));
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
    connect(m_trimTx, &QCheckBox::toggled, m_sstv, &SstvEngine::setTrimOnTx);
    connect(m_autoStart, &QCheckBox::toggled, m_sstv, &SstvEngine::setAutoStart);
    connect(m_autoOffset, &QCheckBox::toggled, m_sstv, &SstvEngine::setAutoOffset);
    connect(m_offset, &QSpinBox::valueChanged, m_sstv, [this](int v) { m_sstv->setOffsetHz(v); });
    // Clicking / wheeling the waterfall: that is where the middle (1900 Hz) of the signal is.
    const auto tuneTo = [this](double hz) { m_sstv->setOffsetHz(hz - 1900.0); };
    connect(m_wf, &WaterfallWidget::clicked, this, tuneTo);
    connect(m_wf, &WaterfallWidget::nudged, this, tuneTo);
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

    // Picture text: a choice of the usual SSTV exchanges, each one editable.
    auto *msgBox = new QGroupBox(tr("Picture text"));
    auto *mv = new QVBoxLayout(msgBox);
    auto *mg = new QGridLayout();
    m_msgGroup = new QButtonGroup(this);
    for (int i = 0; i < kMsgCount; ++i)
    {
        auto *rb = new QRadioButton(tr(kMsgChoices[i].label));
        rb->setToolTip(tr(kMsgChoices[i].tip));
        rb->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed); // never squashed
        m_msgGroup->addButton(rb, i);
        mg->addWidget(rb, i / 4, i % 4);
    }
    mv->addLayout(mg);
    auto *r3 = new QHBoxLayout();
    m_overlay = new QLineEdit();
    m_overlay->setToolTip(tr("Drawn along the bottom of the picture. Edit it to change this choice's text "
                             "(kept for next time).\n{MYCALL} {CALL} {RST} {GRID} {DATE} {TIME} are filled in "
                             "(date and time in UTC)."));
    r3->addWidget(m_overlay, 1);
    auto *dflt = new QToolButton();
    dflt->setText(tr("Default"));
    dflt->setToolTip(tr("Put this choice's text back to the original."));
    r3->addWidget(dflt);
    mv->addLayout(r3);
    auto *r4 = new QHBoxLayout();
    r4->addWidget(new QLabel(tr("Call")));
    m_call = new QLineEdit();
    m_call->setPlaceholderText(tr("other station"));
    r4->addWidget(m_call, 2);
    r4->addWidget(new QLabel(tr("RST")));
    m_rst = new QLineEdit(m_settings->value(QStringLiteral("SSTV/Rst"), QStringLiteral("595")).toString());
    m_rst->setMaxLength(5);
    m_rst->setMaximumWidth(60);
    m_rst->setToolTip(tr("Report sent as {RST} (SSTV: readability, strength, picture quality; 595 = perfect)."));
    r4->addWidget(m_rst);
    mv->addLayout(r4);
    auto *r4b = new QHBoxLayout();
    m_stamp = new QCheckBox(tr("Timestamp"));
    m_stamp->setToolTip(tr("Put the date and time on the picture (taken when Send is pressed)."));
    m_stamp->setChecked(m_settings->value(QStringLiteral("SSTV/Stamp"), false).toBool());
    r4b->addWidget(m_stamp);
    m_stampZone = new QComboBox();
    m_stampZone->addItem(tr("UTC"), 0);
    m_stampZone->addItem(tr("Local time"), 1);
    m_stampZone->setCurrentIndex(m_settings->value(QStringLiteral("SSTV/StampLocal"), false).toBool() ? 1 : 0);
    r4b->addWidget(m_stampZone);
    m_stampPos = new QComboBox();
    m_stampPos->addItem(tr("Top right"), int(Qt::AlignRight));
    m_stampPos->addItem(tr("Top left"), int(Qt::AlignLeft));
    m_stampPos->addItem(tr("Top centre"), int(Qt::AlignHCenter));
    m_stampPos->setCurrentIndex(
        std::clamp(m_settings->value(QStringLiteral("SSTV/StampPos"), 0).toInt(), 0, m_stampPos->count() - 1));
    r4b->addWidget(m_stampPos);
    r4b->addStretch(1);
    mv->addLayout(r4b);
    // Keeps its full height in a short window; the preview below gives way instead.
    msgBox->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    v->addWidget(msgBox);

    // Which choice: saved, or worked out from the single text line of 0.5.0 and older.
    int choice = m_settings->value(QStringLiteral("SSTV/MsgChoice"), -1).toInt();
    if (choice < 0 || choice >= kMsgCount)
    {
        const QString old
            = m_settings->value(QStringLiteral("SSTV/Overlay"), QLatin1String(kMsgChoices[kMsgAnswer].text)).toString();
        if (old.trimmed().isEmpty())
            choice = kMsgNone;
        else if (old == QLatin1String(kMsgChoices[kMsgAnswer].text))
            choice = kMsgAnswer;
        else
        {
            choice = kMsgCustom;
            if (!m_settings->contains(msgKey(kMsgCustom)))
                m_settings->setValue(msgKey(kMsgCustom), old);
        }
    }
    m_msgGroup->button(choice)->setChecked(true);
    showMsgChoice(choice);

    // Preview the timestamp minute by minute.
    m_stampTimer = new QTimer(this);
    m_stampTimer->setInterval(15000);
    connect(m_stampTimer, &QTimer::timeout, this, [this]() { updatePreview(); });
    if (m_stamp->isChecked())
        m_stampTimer->start();
    m_stampZone->setEnabled(m_stamp->isChecked());
    m_stampPos->setEnabled(m_stamp->isChecked());

    connect(m_msgGroup,
            &QButtonGroup::idClicked,
            this,
            [this](int id)
            {
                m_settings->setValue(QStringLiteral("SSTV/MsgChoice"), id);
                showMsgChoice(id);
                updatePreview();
            });
    connect(dflt,
            &QToolButton::clicked,
            this,
            [this]()
            {
                const int id = m_msgGroup->checkedId();
                if (id < 0)
                    return;
                m_settings->remove(msgKey(id));
                showMsgChoice(id);
                updatePreview();
            });
    connect(m_rst,
            &QLineEdit::textChanged,
            this,
            [this](const QString &t)
            {
                m_settings->setValue(QStringLiteral("SSTV/Rst"), t.trimmed());
                updatePreview();
            });
    connect(m_stamp,
            &QCheckBox::toggled,
            this,
            [this](bool on)
            {
                m_settings->setValue(QStringLiteral("SSTV/Stamp"), on);
                m_stampZone->setEnabled(on);
                m_stampPos->setEnabled(on);
                if (on)
                    m_stampTimer->start();
                else
                    m_stampTimer->stop();
                updatePreview();
            });
    connect(m_stampZone,
            &QComboBox::activated,
            this,
            [this](int i)
            {
                m_settings->setValue(QStringLiteral("SSTV/StampLocal"), i == 1);
                updatePreview();
            });
    connect(m_stampPos,
            &QComboBox::activated,
            this,
            [this](int i)
            {
                m_settings->setValue(QStringLiteral("SSTV/StampPos"), i);
                updatePreview();
            });

    m_preview = new ImageView(ImageView::Fit::Window);
    m_preview->setMinimumSize(160, 90);
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
    m_txLevel = new TxLevelSlider();
    m_txLevel->setSliderToolTip(tr("Picture audio level sent to wfview, in dB below full level (0.5 dB steps; "
                                   "Page Up/Down 3 dB)."));
    r6->addWidget(m_txLevel, 1);
    v->addLayout(r6);
    auto *r7 = new QHBoxLayout();
    m_tools = new TxToolButtons([this]() { return m_sstv->txLevel(); }, true);
    r7->addWidget(m_tools);
    m_cwIdAfter = new QCheckBox(tr("CW ID after picture"));
    m_cwIdAfter->setToolTip(tr("Send your CW ID right after each picture, without dropping PTT (like MMSSTV). "
                               "Text, speed and pitch: Options > Tune tone and CW ID."));
    r7->addWidget(m_cwIdAfter);
    r7->addStretch(1);
    v->addLayout(r7);
    auto *note = new QLabel(tr("A picture keeps the transmitter on for up to several minutes: "
                               "consider lower power for the long modes. Tune sends a steady 1750 Hz tone "
                               "to set the level."));
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
                const int id = m_msgGroup->checkedId();
                if (id > kMsgNone)
                {
                    if (t == QLatin1String(kMsgChoices[id].text))
                        m_settings->remove(msgKey(id));
                    else
                        m_settings->setValue(msgKey(id), t);
                }
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
    connect(m_txLevel, &TxLevelSlider::levelChanged, this, [this](double level) { m_sstv->setTxLevel(level); });
    connect(m_cwIdAfter, &QCheckBox::toggled, m_sstv, &SstvEngine::setCwIdAfter);
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

void SstvPanel::showMsgChoice(int id)
{
    const QSignalBlocker b(m_overlay);
    if (id <= kMsgNone)
    {
        m_overlay->clear();
        m_overlay->setEnabled(false);
        m_overlay->setPlaceholderText(tr("no text"));
        return;
    }
    m_overlay->setEnabled(true);
    m_overlay->setPlaceholderText(id == kMsgCustom ? tr("your text, e.g. {CALL} de {MYCALL} {GRID}") : QString());
    m_overlay->setText(m_settings->value(msgKey(id), QLatin1String(kMsgChoices[id].text)).toString());
}

QString SstvPanel::fillText(QString text, const QDateTime &utc) const
{
    const QString grid = m_settings->value(QStringLiteral("Station/MyGrid")).toString().trimmed().toUpper();
    text.replace(QStringLiteral("{MYCALL}"), m_sstv->myCall(), Qt::CaseInsensitive);
    text.replace(QStringLiteral("{CALL}"), m_sstv->call(), Qt::CaseInsensitive);
    text.replace(QStringLiteral("{RST}"), m_rst->text().trimmed(), Qt::CaseInsensitive);
    text.replace(QStringLiteral("{GRID}"), grid, Qt::CaseInsensitive);
    text.replace(QStringLiteral("{DATE}"), utc.toString(QStringLiteral("yyyy-MM-dd")), Qt::CaseInsensitive);
    text.replace(QStringLiteral("{TIME}"), utc.toString(QStringLiteral("HHmm'z'")), Qt::CaseInsensitive);
    // A placeholder left empty (no call yet, no grid) must not leave double spaces.
    return text.simplified();
}

QImage SstvPanel::composeTx() const
{
    QImage out = SstvEngine::fitToMode(m_txSource, m_sstv->txMode());
    const QDateTime utc = QDateTime::currentDateTimeUtc();
    const QString text = m_msgGroup->checkedId() > kMsgNone ? fillText(m_overlay->text(), utc) : QString();
    const bool stamp = m_stamp->isChecked();
    if (text.isEmpty() && !stamp)
        return out;
    QPainter p(&out);
    p.setRenderHint(QPainter::TextAntialiasing);
    if (!text.isEmpty())
    {
        // Big and bold along the bottom; shrunk to fit a long line.
        const QFont f = fitFont(text, out.width() - 8, std::max(10, out.height() / 11));
        p.setFont(f);
        const int band = QFontMetrics(f).height() + 6;
        p.fillRect(0, out.height() - band, out.width(), band, QColor(0, 0, 0, 170));
        p.setPen(Qt::white);
        p.drawText(QRect(0, out.height() - band, out.width(), band), Qt::AlignCenter, text);
    }
    if (stamp)
    {
        const bool local = m_stampZone->currentData().toInt() == 1;
        const QString when = local
            ? QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm"))
            : utc.toString(QStringLiteral("yyyy-MM-dd HH:mm 'UTC'"));
        const QFont f = fitFont(when, out.width() - 8, std::max(9, out.height() / 18));
        p.setFont(f);
        const QFontMetrics fm(f);
        const int w = fm.horizontalAdvance(when) + 10, h = fm.height() + 4;
        const int align = m_stampPos->currentData().toInt();
        const int x = align == Qt::AlignLeft ? 0 : align == Qt::AlignRight ? out.width() - w : (out.width() - w) / 2;
        const QRect r(x, 0, w, h);
        p.fillRect(r, QColor(0, 0, 0, 150));
        p.setPen(QColor(255, 235, 120));
        p.drawText(r, Qt::AlignCenter, when);
    }
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

void SstvPanel::updateMarkers()
{
    const double off = m_sstv->autoOffset() ? m_sstv->totalOffsetHz() : m_sstv->offsetHz();
    m_wf->setMarker(1900.0 + off, 800.0);
    m_wf->setGuides({1200.0 + off, 1500.0 + off, 2300.0 + off}, {tr("sync"), tr("black"), tr("white")});
    if (m_sstv->autoOffset())
        m_offsetInfo->setText(tr("in use %1%2 Hz").arg(off >= 0 ? QStringLiteral("+") : QString()).arg(qRound(off)));
    else
        m_offsetInfo->clear();
}

void SstvPanel::sync()
{
    const QSignalBlocker b1(m_decode), b2(m_autoSlant), b3(m_trim), b4(m_txMode), b7(m_autoStart),
        b8(m_offset), b9(m_autoOffset), b10(m_cwIdAfter), b11(m_trimTx);
    m_decode->setChecked(m_sstv->decoding());
    m_autoSlant->setChecked(m_sstv->autoSlant());
    m_autoStart->setChecked(m_sstv->autoStart());
    m_autoOffset->setChecked(m_sstv->autoOffset());
    if (!m_offset->hasFocus())
        m_offset->setValue(qRound(m_sstv->offsetHz()));
    m_cwIdAfter->setChecked(m_sstv->cwIdAfter());
    updateMarkers();
    m_trim->setValue(qRound(m_sstv->slantPpm()));
    m_txMode->setCurrentIndex(m_txMode->findData(m_sstv->txMode()));
    m_trimTx->setChecked(m_sstv->trimOnTx());
    updateTrimTxNote();
    m_txLevel->setLevel(m_sstv->txLevel());
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
    m_tools->setTransmitAllowed(m_ready && m_canTx);
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
    const bool audioMode = RadioModes::kind(mode) == RadioModes::Kind::Unknown || RadioModes::carriesAudio(mode)
        || mode.toUpper().contains(QLatin1String("FM"));
    if (!ready)
        m_txWarn->setText(tr("Not connected."));
    else if (!canTransmit)
        m_txWarn->setText(tr("The server reports this radio as receive only."));
    else if (!audioMode)
        m_txWarn->setText(
            tr("The radio is in %1: SSTV needs USB/LSB, a data mode (DIGU/DIGL, DATA-U/DATA-L) or FM.").arg(mode));
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

void SstvPanel::updateTrimTxNote()
{
    if (!m_sstv->trimOnTx())
    {
        m_trimTxNote->clear();
        return;
    }
    const int mode = m_sstv->txMode();
    const int ppm = qRound(m_sstv->slantPpm());
    const double px = sstv::leanPixels(mode, ppm);
    const QString name = QString::fromLatin1(sstv::info(mode).name);
    if (ppm == 0)
        m_trimTxNote->setText(tr("(Trim is 0 ppm: nothing to correct)"));
    else if (px < 1.5)
        m_trimTxNote->setText(tr("(%1 ppm moves a %2 picture %3 px: too small to matter)")
                                  .arg(ppm)
                                  .arg(name)
                                  .arg(px, 0, 'f', 1));
    else
        m_trimTxNote->setText(tr("(%1 ppm: corrects about %2 px of lean on %3)").arg(ppm).arg(qRound(px)).arg(name));
}
