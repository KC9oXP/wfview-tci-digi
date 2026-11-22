#include "RxText.h"

#include "AppPaths.h"
#include "callspot.h"

#include <QAction>
#include <QContextMenuEvent>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFrame>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMenu>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QResizeEvent>
#include <QScrollBar>
#include <QShortcut>
#include <QColor>
#include <QFont>
#include <QTextBlock>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QToolButton>
#include <QVariant>

namespace RxText
{
namespace
{
// Received text that starts after this long without any decoded characters
// starts a new paragraph: a blank line between overs.
constexpr qint64 kBreakMs = 8000;

// Newlines at the end of the document (0, 1 or 2 or more).
int trailingNewlines(const QTextDocument *doc)
{
    if (doc->isEmpty())
        return 2; // nothing to separate from
    QTextBlock b = doc->lastBlock();
    int n = 0;
    while (n < 2 && b.isValid() && b.text().isEmpty() && b.previous().isValid())
    {
        ++n;
        b = b.previous();
    }
    return n;
}

// After a pause, put a blank line in front of text that has something to read
// in it (never more than one blank line).
QString breakAfterPause(QPlainTextEdit *edit, const QString &text)
{
    if (text.trimmed().isEmpty())
        return text;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const QVariant last = edit->property("rxLastInkMs");
    edit->setProperty("rxLastInkMs", now);
    if (!last.isValid() || now - last.toLongLong() < kBreakMs)
        return text;
    int lead = 0;
    while (lead < text.size() && lead < 2 && text.at(lead) == QLatin1Char('\n'))
        ++lead;
    const int need = 2 - trailingNewlines(edit->document()) - lead;
    return need > 0 ? QString(need, QLatin1Char('\n')) + text : text;
}

void insert(QPlainTextEdit *edit, const QString &textIn, const QTextCharFormat &fmt, int maxChars)
{
    if (!edit || textIn.isEmpty())
        return;
    const QString text = breakAfterPause(edit, textIn);
    QScrollBar *sb = edit->verticalScrollBar();
    // Follow the newest text whenever the view is at the bottom. A selection
    // (a double-clicked call sign, Ctrl+A, text marked for copying) no longer
    // stops it; only a drag that is still in progress does, so the text does
    // not run away under the mouse while marking.
    const bool dragging = edit->textCursor().hasSelection() && (QGuiApplication::mouseButtons() & Qt::LeftButton);
    const bool following = sb->value() >= sb->maximum() - 2 && !dragging;
    QTextCursor c(edit->document());
    c.movePosition(QTextCursor::End);
    c.insertText(text, fmt);
    if (following && edit->document()->characterCount() > maxChars)
    {
        QTextCursor t(edit->document());
        t.movePosition(QTextCursor::Start);
        t.movePosition(QTextCursor::Down, QTextCursor::KeepAnchor, 200);
        t.removeSelectedText();
    }
    if (following)
        sb->setValue(sb->maximum());
}
} // namespace

void append(QPlainTextEdit *edit, const QString &text, int maxChars)
{
    insert(edit, text, QTextCharFormat(), maxChars); // plain, also after a styled line
}

void appendStyled(QPlainTextEdit *edit, const QString &text, const QColor &color, int maxChars)
{
    QTextCharFormat f;
    f.setForeground(color);
    f.setFontWeight(QFont::Bold);
    insert(edit, text, f, maxChars);
}
void appendNote(QPlainTextEdit *edit, const QString &text)
{
    if (!edit)
        return;
    const bool atLineStart = edit->document()->isEmpty() || edit->document()->lastBlock().text().isEmpty();
    QTextCharFormat f;
    f.setForeground(QColor(0xe8, 0xb0, 0x40));
    f.setFontWeight(QFont::Bold);
    insert(edit, (atLineStart ? QString() : QStringLiteral("\n")) + text + QLatin1Char('\n'), f, 400000);
}
} // namespace RxText

RxCalls *RxCalls::instance()
{
    static RxCalls *calls = new RxCalls();
    return calls;
}

QString RxCalls::callAt(QPlainTextEdit *edit, const QPoint &pos)
{
    const QTextCursor c = edit->cursorForPosition(pos);
    const QString line = c.block().text();
    int a = c.positionInBlock(), b = a;
    const auto part = [](QChar ch) { return ch.isLetterOrNumber() || ch == QLatin1Char('/'); };
    while (a > 0 && part(line.at(a - 1)))
        --a;
    while (b < line.size() && part(line.at(b)))
        ++b;
    const QString w = line.mid(a, b - a).toUpper();
    return callspot::isCallsign(w.toStdString()) ? w : QString();
}

RxScrollback *RxScrollback::attach(QPlainTextEdit *edit, const QString &name)
{
    return new RxScrollback(edit, name);
}

RxScrollback::RxScrollback(QPlainTextEdit *edit, const QString &name)
    : QObject(edit)
    , m_edit(edit)
    , m_name(name)
{
    m_live = new QToolButton(edit);
    m_live->setText(tr("▼ Live"));
    m_live->setToolTip(tr("Back to the newest text"));
    m_live->setAutoRaise(false);
    m_live->setCursor(Qt::ArrowCursor);
    m_live->hide();
    connect(m_live,
            &QToolButton::clicked,
            this,
            [this]()
            {
                m_edit->moveCursor(QTextCursor::End);
                m_edit->verticalScrollBar()->setValue(m_edit->verticalScrollBar()->maximum());
            });

    m_findBar = new QFrame(edit);
    m_findBar->setFrameShape(QFrame::StyledPanel);
    m_findBar->setAutoFillBackground(true);
    auto *h = new QHBoxLayout(m_findBar);
    h->setContentsMargins(4, 2, 4, 2);
    m_findText = new QLineEdit();
    m_findText->setPlaceholderText(tr("Find (Enter: older, Shift+Enter: newer)"));
    m_findText->setMinimumWidth(200);
    h->addWidget(m_findText);
    auto *up = new QToolButton();
    up->setText(QStringLiteral("▲"));
    up->setToolTip(tr("Older"));
    auto *down = new QToolButton();
    down->setText(QStringLiteral("▼"));
    down->setToolTip(tr("Newer"));
    auto *close = new QToolButton();
    close->setText(QStringLiteral("✕"));
    h->addWidget(up);
    h->addWidget(down);
    h->addWidget(close);
    m_findBar->hide();
    connect(up, &QToolButton::clicked, this, [this]() { findNext(true); });
    connect(down, &QToolButton::clicked, this, [this]() { findNext(false); });
    connect(close, &QToolButton::clicked, m_findBar, &QWidget::hide);
    m_findText->installEventFilter(this);

    auto *sc = new QShortcut(QKeySequence::Find, edit);
    sc->setContext(Qt::WidgetWithChildrenShortcut);
    connect(sc, &QShortcut::activated, this, &RxScrollback::openFind);

    edit->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(edit,
            &QWidget::customContextMenuRequested,
            this,
            [this](const QPoint &pos)
            {
                QMenu *menu = m_edit->createStandardContextMenu(pos);
                const QString call = RxCalls::callAt(m_edit, pos);
                if (!call.isEmpty())
                {
                    QAction *log = menu->addAction(tr("Log %1 (QSO log)").arg(call));
                    connect(log,
                            &QAction::triggered,
                            this,
                            [this, call]() { emit RxCalls::instance() -> callPicked(call, m_name); });
                    menu->insertAction(menu->actions().value(0), log);
                    menu->insertSeparator(menu->actions().value(1));
                }
                menu->addSeparator();
                menu->addAction(tr("Find...\tCtrl+F"), this, &RxScrollback::openFind);
                menu->addAction(tr("Save text as..."), this, &RxScrollback::saveAs);
                menu->addAction(tr("Clear"), m_edit, &QPlainTextEdit::clear);
                menu->exec(m_edit->viewport()->mapToGlobal(pos));
                delete menu;
            });

    QScrollBar *sb = edit->verticalScrollBar();
    connect(sb, &QScrollBar::valueChanged, this, &RxScrollback::updateLive);
    connect(sb, &QScrollBar::rangeChanged, this, &RxScrollback::updateLive);
    edit->installEventFilter(this);
    edit->viewport()->installEventFilter(this); // double-click on a callsign
    place();
}

void RxScrollback::place()
{
    const QRect vr = m_edit->viewport()->geometry();
    m_live->adjustSize();
    m_live->move(vr.right() - m_live->width() - 6, vr.bottom() - m_live->height() - 6);
    m_findBar->adjustSize();
    m_findBar->move(std::max(vr.left(), vr.right() - m_findBar->width() - 6), vr.top() + 4);
}

void RxScrollback::updateLive()
{
    QScrollBar *sb = m_edit->verticalScrollBar();
    const bool back = sb->maximum() > 0 && sb->value() < sb->maximum() - 2;
    m_live->setVisible(back);
    if (back)
        m_live->raise();
}

void RxScrollback::openFind()
{
    place();
    m_findBar->show();
    m_findBar->raise();
    if (m_edit->textCursor().hasSelection())
        m_findText->setText(m_edit->textCursor().selectedText());
    m_findText->setFocus();
    m_findText->selectAll();
}

void RxScrollback::findNext(bool older)
{
    const QString t = m_findText->text();
    if (t.isEmpty())
        return;
    QTextDocument::FindFlags f;
    if (older)
        f |= QTextDocument::FindBackward;
    if (!m_edit->find(t, f))
    {
        // wrap around
        m_edit->moveCursor(older ? QTextCursor::End : QTextCursor::Start);
        if (!m_edit->find(t, f))
            m_findText->setStyleSheet(QStringLiteral("background-color: #6a2020;"));
        else
            m_findText->setStyleSheet(QString());
    }
    else
        m_findText->setStyleSheet(QString());
}

void RxScrollback::saveAs()
{
    const QString def
        = QDir(AppPaths::logs()).filePath(AppPaths::safe(m_name) + QLatin1Char('-') + AppPaths::stamp() + ".txt");
    const QString path = QFileDialog::getSaveFileName(m_edit, tr("Save received text"), def, tr("Text (*.txt)"));
    if (path.isEmpty())
        return;
    QFile f(path);
    if (f.open(QIODevice::WriteOnly | QIODevice::Text))
        f.write(m_edit->toPlainText().toUtf8());
}

bool RxScrollback::eventFilter(QObject *obj, QEvent *event)
{
    // The viewport's own Resize: when the text edit's Resize reaches this
    // filter the viewport still has its old size, so the Live button landed
    // in the middle of the pane.
    if ((obj == m_edit && event->type() == QEvent::Show)
        || (obj == m_edit->viewport() && event->type() == QEvent::Resize))
        place();
    if (obj == m_edit->viewport() && event->type() == QEvent::MouseButtonDblClick)
    {
        auto *m = static_cast<QMouseEvent *>(event);
        const QString call = RxCalls::callAt(m_edit, m->position().toPoint());
        if (!call.isEmpty())
            emit RxCalls::instance() -> callPicked(call, m_name); // the word is still selected as usual
    }
    if (obj == m_findText && event->type() == QEvent::KeyPress)
    {
        auto *k = static_cast<QKeyEvent *>(event);
        if (k->key() == Qt::Key_Return || k->key() == Qt::Key_Enter)
        {
            findNext(!(k->modifiers() & Qt::ShiftModifier));
            return true;
        }
        if (k->key() == Qt::Key_Escape)
        {
            m_findBar->hide();
            m_edit->setFocus();
            return true;
        }
    }
    return QObject::eventFilter(obj, event);
}
