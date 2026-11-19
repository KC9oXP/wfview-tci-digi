#include "RxText.h"

#include "AppPaths.h"
#include "callspot.h"

#include <QAction>
#include <QContextMenuEvent>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMenu>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QResizeEvent>
#include <QScrollBar>
#include <QShortcut>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QToolButton>

namespace RxText
{
void append(QPlainTextEdit *edit, const QString &text, int maxChars)
{
    if (!edit || text.isEmpty())
        return;
    QScrollBar *sb = edit->verticalScrollBar();
    const bool following = sb->value() >= sb->maximum() - 2 && !edit->textCursor().hasSelection();
    QTextCursor c(edit->document());
    c.movePosition(QTextCursor::End);
    c.insertText(text);
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
    if (obj == m_edit && (event->type() == QEvent::Resize || event->type() == QEvent::Show))
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
