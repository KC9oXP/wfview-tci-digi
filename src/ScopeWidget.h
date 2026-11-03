#pragma once

// Crossed-ellipse RTTY tuning scope: mark on one axis, space on the other.
// A tuned signal shows a "+"; a mistuned one leans or smears. Points fade
// like phosphor. The drawing stays square and centred however the widget is
// sized.

#include <QImage>
#include <QString>
#include <QVector>
#include <QWidget>

class ScopeWidget : public QWidget
{
    Q_OBJECT

public:
    explicit ScopeWidget(QWidget *parent = nullptr);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;
    bool hasHeightForWidth() const override
    {
        return true;
    }
    int heightForWidth(int w) const override
    {
        return w;
    }

public slots:
    void addPoints(const QVector<float> &xy);
    // Shown in place of the trace (e.g. "Decode off", "TX"); empty = live.
    void setIdleText(const QString &text);

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    void decayAndDraw(const QVector<float> &xy);
    int side() const;

    QImage m_img;
    QString m_idle;
};
