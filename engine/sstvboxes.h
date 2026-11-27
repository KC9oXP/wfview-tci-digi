#pragma once

// SSTV transmit picture: the Custom 1 / Custom 2 boxes (0.9.19).
// A box is kept as fractions of the picture (0..1), so it stays in the same place for every SSTV mode
// (320x256, 640x496, ...) and when the picture changes. Qt-free: tested by tests/test_sstvboxes.cpp.

#include <algorithm>
#include <cmath>

namespace sstvbox
{
// Left, top, width, height as fractions of the picture.
struct Frac
{
    double x = 0.0, y = 0.0, w = 0.0, h = 0.0;
};

struct PixRect
{
    int x = 0, y = 0, w = 0, h = 0;
};

// What a mouse press on a box grabs.
enum class Grip
{
    None,
    Move,
    TopLeft,
    TopRight,
    BottomLeft,
    BottomRight
};

// Smallest box: 5 % of the picture each way (16 x 13 px on a 320 x 256 picture).
constexpr double kMinSize = 0.05;

inline bool isNumber(double v)
{
    return std::isfinite(v);
}

// Inside the picture and at least kMinSize each way; a broken value (NaN, from a bad settings file)
// gives the fallback.
inline Frac clamp(Frac b, Frac fallback = {0.05, 0.05, 0.4, 0.18})
{
    if (!isNumber(b.x) || !isNumber(b.y) || !isNumber(b.w) || !isNumber(b.h))
        b = fallback;
    b.w = std::clamp(b.w, kMinSize, 1.0);
    b.h = std::clamp(b.h, kMinSize, 1.0);
    b.x = std::clamp(b.x, 0.0, 1.0 - b.w);
    b.y = std::clamp(b.y, 0.0, 1.0 - b.h);
    return b;
}

// The box after dragging grip by (dx, dy), in fractions of the picture. Moving keeps the size and stops
// at the picture edges; a corner moves only its two edges, never past the picture edge and never closer
// than kMinSize to the opposite edges.
inline Frac drag(Frac start, Grip g, double dx, double dy)
{
    start = clamp(start);
    if (g == Grip::None)
        return start;
    if (g == Grip::Move)
    {
        start.x += dx;
        start.y += dy;
        return clamp(start);
    }
    double l = start.x, t = start.y, r = start.x + start.w, b = start.y + start.h;
    if (g == Grip::TopLeft || g == Grip::BottomLeft)
        l = std::clamp(l + dx, 0.0, r - kMinSize);
    else
        r = std::clamp(r + dx, l + kMinSize, 1.0);
    if (g == Grip::TopLeft || g == Grip::TopRight)
        t = std::clamp(t + dy, 0.0, b - kMinSize);
    else
        b = std::clamp(b + dy, t + kMinSize, 1.0);
    return clamp(Frac{l, t, r - l, b - t});
}

// Which part of box b a press at (px, py) (fractions of the picture) grabs. gripX / gripY: how close to
// a corner counts as the corner (fractions; the handles are a few screen pixels). Corners win over Move.
inline Grip hit(Frac b, double px, double py, double gripX, double gripY)
{
    const double l = b.x, t = b.y, r = b.x + b.w, bt = b.y + b.h;
    const auto closeTo = [](double a, double c, double g) { return std::fabs(a - c) <= g; };
    if (closeTo(px, l, gripX) && closeTo(py, t, gripY))
        return Grip::TopLeft;
    if (closeTo(px, r, gripX) && closeTo(py, t, gripY))
        return Grip::TopRight;
    if (closeTo(px, l, gripX) && closeTo(py, bt, gripY))
        return Grip::BottomLeft;
    if (closeTo(px, r, gripX) && closeTo(py, bt, gripY))
        return Grip::BottomRight;
    if (px >= l && px <= r && py >= t && py <= bt)
        return Grip::Move;
    return Grip::None;
}

// The box in pixels of a W x H picture; edges rounded so two boxes side by side meet, at least 1 x 1,
// never past the picture.
inline PixRect toPixels(Frac b, int W, int H)
{
    b = clamp(b);
    PixRect p;
    p.x = int(std::lround(b.x * W));
    p.y = int(std::lround(b.y * H));
    const int r = std::min(W, int(std::lround((b.x + b.w) * W)));
    const int bt = std::min(H, int(std::lround((b.y + b.h) * H)));
    p.w = std::max(1, r - p.x);
    p.h = std::max(1, bt - p.y);
    return p;
}

// A srcW x srcH picture as large as fits in box, aspect kept, centred (letterbox).
inline PixRect fitAspect(int srcW, int srcH, PixRect box)
{
    if (srcW <= 0 || srcH <= 0 || box.w <= 0 || box.h <= 0)
        return PixRect{box.x, box.y, 0, 0};
    const double s = std::min(double(box.w) / srcW, double(box.h) / srcH);
    PixRect p;
    p.w = std::max(1, int(std::lround(srcW * s)));
    p.h = std::max(1, int(std::lround(srcH * s)));
    p.x = box.x + (box.w - p.w) / 2;
    p.y = box.y + (box.h - p.h) / 2;
    return p;
}
} // namespace sstvbox
