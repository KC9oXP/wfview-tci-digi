#pragma once

// Receive-pane helpers shared by every text mode.
//
// RxText::append() adds decoded text at the end without moving the reader:
// if they have scrolled back (or selected text to copy) the view stays put,
// otherwise it follows the new text.
//
// RxScrollback::attach() adds to a QPlainTextEdit:
//   - a "Live" button while scrolled back, to jump to the newest text
//   - Ctrl+F: a find bar (Enter = next older, Shift+Enter = newer, Esc closes)
//   - context menu: Find..., Save text as..., Clear, and "Log <call>" over a callsign
//   - double-click on a callsign: RxCalls::instance()->callPicked (fills the QSO log)

#include <QObject>
#include <QPoint>
#include <QString>

class QLineEdit;
class QPlainTextEdit;
class QToolButton;
class QFrame;

namespace RxText
{
// maxChars: older text is trimmed from the top beyond this (only while the
// reader follows the newest text, so a scrolled-back view does not jump).
void append(QPlainTextEdit *edit, const QString &text, int maxChars = 400000);
} // namespace RxText

// Callsigns picked in any receive pane (double-click, or the context menu).
class RxCalls : public QObject
{
    Q_OBJECT

public:
    static RxCalls *instance();
    // The callsign at pos (viewport coordinates) in edit, or empty.
    static QString callAt(QPlainTextEdit *edit, const QPoint &pos);

signals:
    void callPicked(const QString &call, const QString &source);
};

class RxScrollback : public QObject
{
    Q_OBJECT

public:
    // name is used in the default file name when saving ("PSK").
    static RxScrollback *attach(QPlainTextEdit *edit, const QString &name);

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;

private:
    RxScrollback(QPlainTextEdit *edit, const QString &name);
    void place();
    void updateLive();
    void openFind();
    void findNext(bool older);
    void saveAs();

    QPlainTextEdit *m_edit;
    QString m_name;
    QToolButton *m_live = nullptr;
    QFrame *m_findBar = nullptr;
    QLineEdit *m_findText = nullptr;
};
