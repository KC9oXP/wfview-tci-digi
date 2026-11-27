#pragma once

// One tab's squelch setting, kept by its engine: a threshold on the mode's
// own scale ("Squelch" in the settings), one in dB ("SquelchDb"), and which is
// in use ("SquelchInDb", the dB box). 0 = off on either.

#include <QSettings>

#include <algorithm>
#include <cmath>

struct SquelchSetting
{
    double native = 0.0;
    double db = 0.0;
    bool inDb = false;
    double nativeMax = 1.0;
    double dbMax = 30.0;

    double value() const
    {
        return inDb ? db : native;
    }
    double max() const
    {
        return inDb ? dbMax : nativeMax;
    }
    // Sets the value on the scale in use; false if nothing changed.
    bool setValue(double v)
    {
        v = std::clamp(v, 0.0, max());
        double &t = inDb ? db : native;
        if (std::fabs(v - t) < 1e-6)
            return false;
        t = v;
        return true;
    }
    // Reads the three keys from the current group (missing keys keep the defaults).
    void load(QSettings *s)
    {
        native = std::clamp(s->value(QStringLiteral("Squelch"), native).toDouble(), 0.0, nativeMax);
        db = std::clamp(s->value(QStringLiteral("SquelchDb"), db).toDouble(), 0.0, dbMax);
        inDb = s->value(QStringLiteral("SquelchInDb"), inDb).toBool();
    }
};
