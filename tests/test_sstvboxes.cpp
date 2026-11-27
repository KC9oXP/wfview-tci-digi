// SSTV Custom 1 / Custom 2 box maths (engine/sstvboxes.h): clamp, drag, hit, pixels, aspect fit.
#include "sstvboxes.h"

#include <cmath>
#include <cstdio>
#include <limits>

static int g_fail = 0;
#define CHECK(cond)                                                                                                    \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(cond))                                                                                                   \
        {                                                                                                              \
            std::printf("  FAIL line %d: %s\n", __LINE__, #cond);                                                      \
            ++g_fail;                                                                                                  \
        }                                                                                                              \
        else                                                                                                           \
            std::printf("  ok   %s\n", #cond);                                                                         \
    } while (0)

static bool eq(double a, double b)
{
    return std::fabs(a - b) < 1e-9;
}
static bool same(sstvbox::Frac a, sstvbox::Frac b)
{
    return eq(a.x, b.x) && eq(a.y, b.y) && eq(a.w, b.w) && eq(a.h, b.h);
}

int main()
{
    using namespace sstvbox;
    std::printf("=== clamp ===\n");
    CHECK(same(clamp({0.1, 0.2, 0.3, 0.4}), Frac{0.1, 0.2, 0.3, 0.4}));
    CHECK(same(clamp({0.9, 0.9, 0.3, 0.4}), Frac{0.7, 0.6, 0.3, 0.4}));      // pushed back inside
    CHECK(same(clamp({-0.5, -1.0, 0.3, 0.4}), Frac{0.0, 0.0, 0.3, 0.4}));
    CHECK(same(clamp({0.2, 0.2, 0.0, 0.001}), Frac{0.2, 0.2, kMinSize, kMinSize})); // minimum size
    CHECK(same(clamp({0.0, 0.0, 3.0, 2.0}), Frac{0.0, 0.0, 1.0, 1.0}));
    const double nan = std::numeric_limits<double>::quiet_NaN();
    CHECK(same(clamp({nan, 0.1, 0.2, 0.2}, {0.5, 0.05, 0.4, 0.18}), Frac{0.5, 0.05, 0.4, 0.18}));

    std::printf("=== drag: move ===\n");
    const Frac b{0.2, 0.3, 0.4, 0.2};
    CHECK(same(drag(b, Grip::Move, 0.1, -0.1), Frac{0.3, 0.2, 0.4, 0.2}));
    CHECK(same(drag(b, Grip::Move, 5.0, 5.0), Frac{0.6, 0.8, 0.4, 0.2}));   // stops at right / bottom
    CHECK(same(drag(b, Grip::Move, -5.0, -5.0), Frac{0.0, 0.0, 0.4, 0.2})); // stops at left / top
    CHECK(same(drag(b, Grip::None, 0.3, 0.3), b));

    std::printf("=== drag: corners ===\n");
    CHECK(same(drag(b, Grip::BottomRight, 0.1, 0.1), Frac{0.2, 0.3, 0.5, 0.3}));
    CHECK(same(drag(b, Grip::BottomRight, 5.0, 5.0), Frac{0.2, 0.3, 0.8, 0.7}));          // to the picture edge
    CHECK(same(drag(b, Grip::BottomRight, -5.0, -5.0), Frac{0.2, 0.3, kMinSize, kMinSize})); // minimum, left/top fixed
    CHECK(same(drag(b, Grip::TopLeft, -0.1, -0.1), Frac{0.1, 0.2, 0.5, 0.3}));
    CHECK(same(drag(b, Grip::TopLeft, 5.0, 5.0), Frac{0.6 - kMinSize, 0.5 - kMinSize, kMinSize, kMinSize}));
    CHECK(same(drag(b, Grip::TopLeft, -5.0, -5.0), Frac{0.0, 0.0, 0.6, 0.5}));
    CHECK(same(drag(b, Grip::TopRight, 0.1, -0.1), Frac{0.2, 0.2, 0.5, 0.3}));
    CHECK(same(drag(b, Grip::BottomLeft, -0.1, 0.1), Frac{0.1, 0.3, 0.5, 0.3}));

    std::printf("=== hit ===\n");
    const double g = 0.02;
    CHECK(hit(b, 0.2, 0.3, g, g) == Grip::TopLeft);
    CHECK(hit(b, 0.61, 0.29, g, g) == Grip::TopRight);
    CHECK(hit(b, 0.19, 0.5, g, g) == Grip::BottomLeft);
    CHECK(hit(b, 0.6, 0.5, g, g) == Grip::BottomRight);
    CHECK(hit(b, 0.4, 0.4, g, g) == Grip::Move);
    CHECK(hit(b, 0.4, 0.3, g, g) == Grip::Move); // top edge, not a corner
    CHECK(hit(b, 0.1, 0.4, g, g) == Grip::None);
    CHECK(hit(b, 0.4, 0.55, g, g) == Grip::None);

    std::printf("=== toPixels ===\n");
    PixRect p = toPixels({0.25, 0.5, 0.5, 0.25}, 320, 256);
    CHECK(p.x == 80 && p.y == 128 && p.w == 160 && p.h == 64);
    p = toPixels({0.25, 0.5, 0.5, 0.25}, 640, 496);
    CHECK(p.x == 160 && p.y == 248 && p.w == 320 && p.h == 124);
    // Side by side boxes meet with no gap or overlap.
    const PixRect a = toPixels({0.0, 0.0, 1.0 / 3.0, 0.2}, 320, 256), c = toPixels({1.0 / 3.0, 0.0, 1.0 / 3.0, 0.2}, 320, 256);
    CHECK(a.x + a.w == c.x);
    p = toPixels({0.95, 0.95, 0.05, 0.05}, 320, 256);
    CHECK(p.x + p.w <= 320 && p.y + p.h <= 256 && p.w >= 1 && p.h >= 1);

    std::printf("=== fitAspect ===\n");
    p = fitAspect(200, 100, PixRect{10, 20, 100, 100}); // wide logo: full width, centred up/down
    CHECK(p.x == 10 && p.w == 100 && p.h == 50 && p.y == 45);
    p = fitAspect(100, 400, PixRect{0, 0, 100, 100}); // tall: full height, centred left/right
    CHECK(p.h == 100 && p.w == 25 && p.x == 37 && p.y == 0);
    p = fitAspect(0, 10, PixRect{5, 5, 50, 50});
    CHECK(p.w == 0 && p.h == 0);

    std::printf(g_fail ? "\nFAILED: %d\n" : "\nALL PASSED\n", g_fail);
    return g_fail ? 1 : 0;
}
