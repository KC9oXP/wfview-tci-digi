#include "ModemTxBox.h"

#include "ModemEngine.h"
#include "RadioModes.h"
#include "TxLevelSlider.h"
#include "TxTools.h"

#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QEvent>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QShortcut>
#include <QSignalBlocker>
#include <QVBoxLayout>

namespace
{
QFont monoFont()
{
    QFont f(QStringLiteral("Consolas"));
    f.setStyleHint(QFont::Monospace);
    f.setPointSizeF(f.pointSizeF() * 1.15);
    return f;
}
} // namespace

ModemTxBox::ModemTxBox(ModemEngine *engine, const QString &name, QWidget *parent)
    : QWidget(parent)
    , m_engine(engine)
    , m_name(name)
{
    auto *v = new QVBoxLayout(this);
    v->setContentsMargins(0, 0, 0, 0);

    m_modeWarning = new QLabel();
    m_modeWarning->setWordWrap(true);
    m_modeWarning->setStyleSheet(QStringLiteral("color: #e0a030;"));
    m_modeWarning->setVisible(false);
    v->addWidget(m_modeWarning);

    auto *macroRow = new QHBoxLayout();
    auto *callBox = new QGridLayout();
    callBox->addWidget(new QLabel(tr("Call")), 0, 0);
    m_call = new QLineEdit();
    m_call->setPlaceholderText(tr("their call"));
    m_call->setToolTip(
        tr("The other station's call, used by {CALL}. Double-click a call in the receive pane to fill it in."));
    m_call->setMaximumWidth(140);
    callBox->addWidget(m_call, 0, 1);
    callBox->addWidget(new QLabel(tr("My call")), 1, 0);
    m_myCall = new QLineEdit();
    m_myCall->setPlaceholderText(tr("your call"));
    m_myCall->setToolTip(tr("Your own call sign, used by {MYCALL} (shared by all modes)."));
    m_myCall->setMaximumWidth(140);
    callBox->addWidget(m_myCall, 1, 1);
    macroRow->addLayout(callBox);

    auto *grid = new QGridLayout();
    for (int i = 0; i < ModemEngine::kMacroCount; ++i)
    {
        auto *b = new QPushButton();
        b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        connect(b, &QPushButton::clicked, this, [this, i]() { m_engine->sendMacro(i); });
        grid->addWidget(b, i / 5, i % 5); // two rows: F1-F5, F6-F10
        m_macroButtons << b;
    }
    macroRow->addLayout(grid, 1);
    auto *edit = new QPushButton(tr("Edit..."));
    edit->setToolTip(tr("Edit the macro names and texts. {MYCALL} and {CALL} are filled in when sent."));
    macroRow->addWidget(edit, 0, Qt::AlignTop);
    v->addLayout(macroRow);

    m_txText = new QPlainTextEdit();
    m_txText->setFont(monoFont());
    m_txText->setPlaceholderText(
        tr("Type here, then Send (Ctrl+Enter). Text sent while transmitting is added to the queue."));
    m_txText->setMinimumHeight(QFontMetrics(monoFont()).lineSpacing() * 2 + 12);
    m_txText->installEventFilter(this);
    v->addWidget(m_txText, 1);

    auto *btns = new QHBoxLayout();
    m_send = new QPushButton(tr("Send"));
    m_send->setToolTip(tr("Send the typed text (keys up first if needed, releases when done unless Hold TX)."));
    m_txBtn = new QPushButton(tr("TX"));
    m_txBtn->setToolTip(tr("Key up and send idle until RX."));
    m_rxBtn = new QPushButton(tr("RX"));
    m_rxBtn->setToolTip(tr("Release after everything queued has been sent."));
    m_abort = new QPushButton(tr("Abort"));
    m_abort->setToolTip(tr("Stop transmitting now (Esc)."));
    m_hold = new QCheckBox(tr("Hold TX"));
    m_hold->setToolTip(tr("Stay keyed between sends until RX is pressed."));
    btns->addWidget(m_send);
    btns->addWidget(m_txBtn);
    btns->addWidget(m_rxBtn);
    btns->addWidget(m_abort);
    btns->addWidget(m_hold);
    m_tools = new TxToolButtons([this]() { return m_engine->txLevel(); },
                                [this](double level) { m_engine->setTxLevel(level); },
                                [this]()
                                {
                                    TxTools::TestSignal s;
                                    m_engine->autoLevelTones(s.hz, s.hz2, s.holdSec);
                                    return s;
                                });
    btns->addWidget(m_tools);
    btns->addStretch(1);
    btns->addWidget(m_tools->autoLevelButton());
    btns->addWidget(new QLabel(tr("TX level")));
    m_txLevel = new TxLevelSlider();
    m_txLevel->setSliderToolTip(
        tr("Audio level sent to wfview, in dB below full level (0.5 dB steps; Page Up/Down 3 dB). "
           "Set the radio's ALC/power with this and wfview's TX gain "
           "(PSK needs a clean, ALC-free signal). Kept for this mode (and sub-mode)."));
    btns->addWidget(m_txLevel, 1);
    v->addLayout(btns);

    connect(m_call, &QLineEdit::textEdited, m_engine, &ModemEngine::setCall);
    connect(m_myCall, &QLineEdit::editingFinished, this, [this]() { m_engine->setMyCall(m_myCall->text()); });
    connect(edit, &QPushButton::clicked, this, &ModemTxBox::editMacros);
    connect(m_send, &QPushButton::clicked, this, &ModemTxBox::sendTyped);
    connect(m_txBtn, &QPushButton::clicked, m_engine, &ModemEngine::startTx);
    connect(m_rxBtn, &QPushButton::clicked, m_engine, &ModemEngine::finishTx);
    connect(m_abort, &QPushButton::clicked, m_engine, &ModemEngine::abortTx);
    connect(m_hold, &QCheckBox::toggled, m_engine, &ModemEngine::setHoldTx);
    connect(m_txLevel, &TxLevelSlider::levelChanged, m_engine, &ModemEngine::setTxLevel);

    connect(m_engine, &ModemEngine::settingsChanged, this, &ModemTxBox::syncSettings);
    connect(m_engine, &ModemEngine::macrosChanged, this, &ModemTxBox::syncMacros);
    connect(m_engine, &ModemEngine::transmittingChanged, this, &ModemTxBox::syncTransmitting);
    syncSettings();
    syncMacros();
    syncTransmitting(false);
}

void ModemTxBox::installShortcuts(QWidget *p)
{
    for (int i = 0; i < ModemEngine::kMacroCount; ++i)
    {
        auto *sc = new QShortcut(QKeySequence(Qt::Key_F1 + i), p);
        sc->setContext(Qt::WidgetWithChildrenShortcut);
        connect(sc, &QShortcut::activated, this, [this, i]() { m_engine->sendMacro(i); });
    }
    auto *esc = new QShortcut(QKeySequence(Qt::Key_Escape), p);
    esc->setContext(Qt::WidgetWithChildrenShortcut);
    connect(esc, &QShortcut::activated, m_engine, &ModemEngine::abortTx);
}

void ModemTxBox::setRadioState(bool ready, bool canTransmit, const QString &mode)
{
    m_ready = ready;
    m_canTx = ready && canTransmit;
    m_mode = mode;
    syncTransmitting(m_engine->transmitting());
    updateModeWarning();
}

void ModemTxBox::syncSettings()
{
    const QSignalBlocker b1(m_hold);
    if (!m_myCall->hasFocus())
        m_myCall->setText(m_engine->myCall());
    if (!m_call->hasFocus())
        m_call->setText(m_engine->call());
    m_hold->setChecked(m_engine->holdTx());
    m_txLevel->setLevel(m_engine->txLevel());
}

void ModemTxBox::syncTransmitting(bool on)
{
    m_send->setEnabled(m_canTx);
    m_txBtn->setEnabled(m_canTx && !on);
    m_rxBtn->setEnabled(on);
    m_abort->setEnabled(on);
    for (QPushButton *b : m_macroButtons)
        b->setEnabled(m_canTx);
    m_tools->setTransmitAllowed(m_canTx);
    m_txBtn->setStyleSheet(on ? QStringLiteral("background-color: #a02020; color: white;") : QString());
}

void ModemTxBox::offerCall(const QString &w)
{
    const QString word = w.trimmed().toUpper();
    static const QRegularExpression callRe(QStringLiteral("^[A-Z0-9/]{3,12}$"));
    if (callRe.match(word).hasMatch() && word.contains(QRegularExpression(QStringLiteral("[0-9]")))
        && word.contains(QRegularExpression(QStringLiteral("[A-Z]"))))
    {
        m_engine->setCall(word);
        m_call->setText(word);
    }
}

bool ModemTxBox::eventFilter(QObject *obj, QEvent *event)
{
    if (obj == m_txText && event->type() == QEvent::KeyPress)
    {
        auto *ke = static_cast<QKeyEvent *>(event);
        if ((ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter) && (ke->modifiers() & Qt::ControlModifier))
        {
            sendTyped();
            return true;
        }
    }
    return QWidget::eventFilter(obj, event);
}

void ModemTxBox::updateModeWarning()
{
    QString warn;
    QString style = QStringLiteral("color: #e0a030;");
    if (m_ready)
    {
        switch (RadioModes::kind(m_mode))
        {
        case RadioModes::Kind::Unknown:
        case RadioModes::Kind::Data:
            break;
        case RadioModes::Kind::Cw:
            warn = tr("Radio is in %1. %2 transmit here sends audio, so it needs a data mode (DIGU/DIGL, "
                      "DATA-U/DATA-L, USB-D/LSB-D) or USB/LSB.")
                       .arg(m_mode, m_name);
            break;
        case RadioModes::Kind::Voice:
            // Fine on many radios; worth a quiet note since some take mic audio in plain SSB.
            warn = tr("Radio is in %1 (not a data mode). That works if the radio takes the TCI audio in %1; "
                      "if nothing goes out, use its data mode (on an IC-7610 over LAN: Data 2).")
                       .arg(m_mode);
            style = QStringLiteral("color: #9a9a9a;");
            break;
        case RadioModes::Kind::Other:
            warn = tr("Radio is in %1. %2 needs a data mode (DIGU/DIGL, DATA-U/DATA-L, USB-D/LSB-D) or USB/LSB.")
                       .arg(m_mode, m_name);
            break;
        }
    }
    m_modeWarning->setStyleSheet(style);
    m_modeWarning->setText(warn);
    m_modeWarning->setVisible(!warn.isEmpty());
}

void ModemTxBox::syncMacros()
{
    const QStringList names = m_engine->macroNames();
    const QStringList texts = m_engine->macroTexts();
    for (int i = 0; i < m_macroButtons.size() && i < names.size(); ++i)
    {
        m_macroButtons[i]->setText(QStringLiteral("F%1 %2").arg(i + 1).arg(names[i]));
        m_macroButtons[i]->setToolTip(texts.value(i).trimmed());
    }
}

void ModemTxBox::sendTyped()
{
    const QString t = m_txText->toPlainText();
    if (t.trimmed().isEmpty())
        return;
    m_engine->send(t.endsWith(QLatin1Char('\n')) ? t : t + QLatin1Char(' '));
    if (m_engine->transmitting())
        m_txText->clear();
}

void ModemTxBox::editMacros()
{
    QDialog dlg(this);
    dlg.setWindowTitle(tr("%1 macros").arg(m_name));
    auto *outer = new QVBoxLayout(&dlg);
    auto *help = new QLabel(tr("{MYCALL} and {CALL} are replaced when the macro is sent. End a macro with a new "
                               "line to start the next one on a fresh line. {CWID} sends your CW ID after the "
                               "transmission."));
    help->setWordWrap(true);
    outer->addWidget(help);
    auto *scroll = new QScrollArea();
    scroll->setWidgetResizable(true);
    auto *inner = new QWidget();
    auto *form = new QGridLayout(inner);
    QList<QLineEdit *> names;
    QList<QPlainTextEdit *> texts;
    const QStringList n = m_engine->macroNames();
    const QStringList t = m_engine->macroTexts();
    for (int i = 0; i < ModemEngine::kMacroCount; ++i)
    {
        auto *name = new QLineEdit(n.value(i));
        name->setMaximumWidth(120);
        auto *text = new QPlainTextEdit(t.value(i));
        text->setFont(monoFont());
        text->setFixedHeight(QFontMetrics(monoFont()).lineSpacing() * 3 + 12);
        form->addWidget(new QLabel(QStringLiteral("F%1").arg(i + 1)), i, 0, Qt::AlignTop);
        form->addWidget(name, i, 1, Qt::AlignTop);
        form->addWidget(text, i, 2);
        names << name;
        texts << text;
    }
    form->setColumnStretch(2, 1);
    scroll->setWidget(inner);
    outer->addWidget(scroll, 1);
    auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    outer->addWidget(bb);
    dlg.resize(640, 520);
    if (dlg.exec() != QDialog::Accepted)
        return;
    for (int i = 0; i < ModemEngine::kMacroCount; ++i)
        m_engine->setMacro(i, names[i]->text(), texts[i]->toPlainText());
}
