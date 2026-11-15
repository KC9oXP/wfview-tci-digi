#pragma once

// Small shared looks: the Decode buttons (dark red off, dark green on, in
// every tab) and the green "on" state of toggle buttons such as Log.

#include <QPushButton>
#include <QString>

namespace UiStyle
{
// Reads "Push To Decode" while off (dark red) and "Decode" while on (green).
// The text follows the checked state however it is set (click, or a panel
// syncing it with signals blocked).
class DecodeButton : public QPushButton
{
public:
    explicit DecodeButton(QWidget *parent = nullptr)
        : QPushButton(parent)
    {
        setCheckable(true);
        // Wide enough for the longer text, so the row does not shift on a click.
        setText(QObject::tr("Push To Decode"));
        setMinimumWidth(sizeHint().width() + 14); // + the style sheet's padding
        updateText();
    }

protected:
    void checkStateSet() override
    {
        QPushButton::checkStateSet();
        updateText();
    }

private:
    void updateText()
    {
        setText(isChecked() ? QObject::tr("Decode") : QObject::tr("Push To Decode"));
    }
};

inline void decodeButton(QPushButton *b)
{
    b->setObjectName(QStringLiteral("decodeButton"));
    b->setCheckable(true);
    b->setStyleSheet(
        QStringLiteral("QPushButton { background-color: #4a2c2c; color: #c8a8a8; border: 1px solid #6a3c3c;"
                       " border-radius: 3px; padding: 3px 12px; }"
                       "QPushButton:hover { border-color: #8a5050; }"
                       "QPushButton:checked { background-color: #1d6a33; color: #eaffea; border: 1px solid #2f9a4c;"
                       " font-weight: bold; }"
                       "QPushButton:checked:hover { border-color: #5fd07c; }"));
}

// Normal look when off, dark green when on.
inline void greenWhenOn(QPushButton *b)
{
    b->setCheckable(true);
    b->setStyleSheet(QStringLiteral("QPushButton:checked { background-color: #1d6a33; color: #eaffea;"
                                    " border: 1px solid #2f9a4c; border-radius: 3px; font-weight: bold; }"));
}
} // namespace UiStyle
