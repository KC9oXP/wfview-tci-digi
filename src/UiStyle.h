#pragma once

// Small shared looks: the Decode buttons (dark red off, dark green on, in
// every tab) and the green "on" state of toggle buttons such as Log.

#include <QPushButton>
#include <QString>

namespace UiStyle
{
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
    b->setToolTip(b->toolTip().isEmpty() ? QObject::tr("Decode on (green) / off (dark red).")
                                         : b->toolTip() + QObject::tr("\nGreen = decoding, dark red = off."));
}

// Normal look when off, dark green when on.
inline void greenWhenOn(QPushButton *b)
{
    b->setCheckable(true);
    b->setStyleSheet(QStringLiteral("QPushButton:checked { background-color: #1d6a33; color: #eaffea;"
                                    " border: 1px solid #2f9a4c; border-radius: 3px; font-weight: bold; }"));
}
} // namespace UiStyle
