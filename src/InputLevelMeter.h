#pragma once
// Small RX input meter for the top bar, next to SNR: a thin bar (RMS) with a peak-hold tick
// and "-24 dBFS" written on it. The same meter for every tab, since every tab decodes the
// same receive audio from wfview. Levels come from engine/inputlevel.h.
#include <QString>
#include <QWidget>

class InputLevelMeter : public QWidget
{
    Q_OBJECT

public:
    explicit InputLevelMeter(QWidget *parent = nullptr);

    // A reading: RMS and held peak, both dBFS.
    void setLevel(double rmsDb, double peakHoldDb);
    // No reading: show a short word instead ("TX", "--", "no audio").
    void setIdle(const QString &word);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    bool m_idle = true;
    QString m_word = QStringLiteral("--");
    double m_rmsDb = -120.0;
    double m_peakDb = -120.0;
};
