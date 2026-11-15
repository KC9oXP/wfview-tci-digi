#pragma once

// Adjusting the computer's clock (the WWV Time tab's "Set PC clock").
//
// Windows only lets an elevated process set the time, so the program starts
// a second copy of itself with "--adjust-clock-ms <n>" through the UAC
// prompt; that copy shifts the clock by n milliseconds and exits. A shift
// (not an absolute time) is passed, so the time spent at the prompt does not
// matter.

#include <QString>

namespace ClockSet
{
bool supported();
// Asks for elevation and shifts the clock by ms (+ = forward). False with a
// reason if it was refused or failed.
bool requestAdjust(qint64 ms, QString *error);
// In the elevated copy: shift the clock; the process exit code (0 = done).
int runAdjust(qint64 ms);
} // namespace ClockSet
