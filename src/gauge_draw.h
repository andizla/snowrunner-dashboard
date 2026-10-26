// gauge_draw.h: draws the gauge cluster: analogue dials in the style of a truck's dashboard (dark faces behind a
// bevelled ring, white ticks and needles, red zones, lamps with the symbols a dashboard uses), drawn crisp at the
// screen's resolution with Dear ImGui's drawing calls only: own point arrays into AddPolyline, AddConvexPolyFilled and
// AddConcavePolyFilled, circles and rectangles, all of which ReShade's function table carries. No textures but the
// font's: the depth comes from layers (soft shadows as fading rings, a ring that is brighter where the light falls,
// a needle with a bright and a shaded half). Curves are cubic Beziers flattened at the size they are drawn at.
// Text is the game's font (gauge_font.h).
// Sizes are reference pixels at 1080p; u converts them to the screen (u = screen height / 1080 x the ini's scale).
// Shared by the mod (gauge_overlay.cpp, where Dear ImGui comes through ReShade's function table) and the offline
// preview (test\gauge_preview.cpp, Dear ImGui compiled in), so the preview shows the mod's pixels. Include after
// imgui.h, with IMGUI_DEFINE_MATH_OPERATORS.
#pragma once
#include "gauges.h"
#include "gauge_font.h"

enum GaugeTextStyle { kGsBig, kGsSmall, kGsTiny, kGsReadout, kGsGear, kGsGearMode, kGsLamp, kGsUnit, kGsStyleCount };
static const FontStyle kGaugeStyles[kGsStyleCount] = {
    { 11.0f, 1 }, // kGsBig: numbers on the big dials
    { 8.0f, 1 },  // kGsSmall: numbers on the small dials
    { 6.5f, 0 },  // kGsTiny: captions ("km/h", "x100 rpm", "bar")
    { 13.0f, 1 }, // kGsReadout: digital speed, range
    { 20.0f, 1 }, // kGsGear: the gear in the tachometer
    { 8.0f, 1 },  // kGsGearMode: "A", "H", "L+"
    { 6.5f, 1 },  // kGsLamp: the letters inside lamp symbols
    { 7.0f, 0 },  // kGsUnit: small labels next to readouts
};

struct GaugeLayout
{
    int variant;                    // 0 = strip (bottom centre), 1 = block (bottom right)
    bool swapDials;                 // the speedometer where the tachometer is and the tachometer where the speedometer is
    bool showTemp, showBoost, showAir, showLamps, showClock, showBurn;
    int oilGauge;                  // the oil temperature: kOilOff, kOilNeedle (a second needle in the temperature dial), kOilDial (a dial of its own)
    float faceAlpha;                // 1 = solid dial faces; lower = see-through (the game shows through, drop shadows under the marks)
    bool openArc;                   // no face or bezel at all: only the scale arc with its ticks
};

// The face opacity of the cluster being drawn (set by GaugeDraw): below 0.9 the marks, text and lamps get drop
// shadows and the lamps lose their cells, so they read over the game image like the vanilla HUD.
static float s_faceAlpha = 1.0f;
static bool s_open = false;
static bool GGlass() { return s_faceAlpha < 0.9f; }

static const float kGPi = 3.14159265f;

static ImU32 GCol(int r, int g, int b, float a) { return IM_COL32(r, g, b, (int)(fminf(fmaxf(a, 0.0f), 1.0f) * 255.0f + 0.5f)); }
struct GRgb { int r, g, b; };
static ImU32 GCol(GRgb c, float a) { return GCol(c.r, c.g, c.b, a); }
static GRgb GMix(GRgb a, GRgb b, float k) { return GRgb{ a.r + (int)((b.r - a.r) * k), a.g + (int)((b.g - a.g) * k), a.b + (int)((b.b - a.b) * k) }; }
static float GAlpha(ImU32 col) { return (float)((col >> 24) & 0xff) / 255.0f; }
static const GRgb kFace = { 11, 12, 14 }, kFaceMid = { 24, 25, 29 }, kTick = { 236, 236, 236 }, kRed = { 222, 52, 48 }, kBlue = { 66, 138, 232 },
                  kNeedle = { 250, 250, 250 }, kNeedleShade = { 196, 198, 204 }, kDim = { 62, 64, 68 }, kAmber = { 250, 172, 36 }, kGreen = { 72, 214, 98 },
                  kText = { 228, 228, 228 }, kMuted = { 150, 152, 156 }, kMetalDark = { 52, 54, 58 }, kMetalBright = { 226, 228, 232 };

// Screen degrees: 0 = right, growing clockwise (y points down).
static ImVec2 GPolar(ImVec2 c, float r, float deg) { const float a = deg * kGPi / 180.0f; return ImVec2(c.x + cosf(a) * r, c.y + sinf(a) * r); }

// A line with round ends.
static void GLine(ImDrawList *dl, ImVec2 a, ImVec2 b, ImU32 col, float thick)
{
    ImVec2 p[2] = { a, b };
    dl->AddPolyline(p, 2, col, 0, thick);
    if (thick >= 2.5f) // thinner than this the ends cannot be told from square ones
    {
        dl->AddCircleFilled(a, thick * 0.5f, col, 8);
        dl->AddCircleFilled(b, thick * 0.5f, col, 8);
    }
}

// A line with square ends, for ticks that stand on an arc.
static void GBar(ImDrawList *dl, ImVec2 a, ImVec2 b, ImU32 col, float thick)
{
    ImVec2 p[2] = { a, b };
    dl->AddPolyline(p, 2, col, 0, thick);
}

static void GArc(ImDrawList *dl, ImVec2 c, float r, float d0, float d1, float thick, ImU32 col)
{
    ImVec2 pts[128];
    const float sweep = d1 - d0;
    int n = (int)(fabsf(sweep) / 4.5f) + 2; // at 4.5 degrees a piece the arc is within a tenth of a pixel of round on a 2160p screen
    if (n > 127) n = 127;
    for (int i = 0; i <= n; i++) pts[i] = GPolar(c, r, d0 + sweep * i / n);
    dl->AddPolyline(pts, n + 1, col, 0, thick);
}

// ---- shapes from curves ----

// A contour being built, in screen pixels: straight lines, arcs and cubic Beziers, flattened as they are added.
enum { kGPathMax = 200 };
struct GPath { ImVec2 p[kGPathMax]; int n = 0; };
static void GTo(GPath &g, ImVec2 q) { if (g.n < kGPathMax) g.p[g.n++] = q; }

// A cubic Bezier from the contour's last point by the control points c1 and c2 to q, in steps of about three pixels.
static void GCurve(GPath &g, ImVec2 c1, ImVec2 c2, ImVec2 q)
{
    if (!g.n) return;
    const ImVec2 from = g.p[g.n - 1];
    const float length = fabsf(c1.x - from.x) + fabsf(c1.y - from.y) + fabsf(c2.x - c1.x) + fabsf(c2.y - c1.y) + fabsf(q.x - c2.x) + fabsf(q.y - c2.y);
    int steps = (int)(length / 3.0f) + 3;
    if (steps > 20) steps = 20;
    for (int i = 1; i <= steps; i++)
    {
        const float t = (float)i / (float)steps, s = 1.0f - t;
        GTo(g, from * (s * s * s) + c1 * (3.0f * s * s * t) + c2 * (3.0f * s * t * t) + q * (t * t * t));
    }
}

static void GArcTo(GPath &g, ImVec2 c, float r, float d0, float d1)
{
    int steps = (int)(fabsf(d1 - d0) * r / 170.0f) + 3; // about three pixels a step
    if (steps > 40) steps = 40;
    for (int i = 0; i <= steps; i++) GTo(g, GPolar(c, r, d0 + (d1 - d0) * (float)i / (float)steps));
}

static void GFill(ImDrawList *dl, const GPath &g, ImU32 col) { if (g.n >= 3) dl->AddConcavePolyFilled(g.p, g.n, col); }

// The contour as a line; an open one gets round ends.
static void GStroke(ImDrawList *dl, const GPath &g, ImU32 col, float thick, bool closed)
{
    if (g.n < 2) return;
    dl->AddPolyline(g.p, g.n, col, closed ? ImDrawFlags_Closed : 0, thick);
    if (!closed && thick >= 2.5f)
    {
        dl->AddCircleFilled(g.p[0], thick * 0.5f, col, 8);
        dl->AddCircleFilled(g.p[g.n - 1], thick * 0.5f, col, 8);
    }
}

// A rectangle with round corners as a contour.
static void GRound(GPath &g, ImVec2 a, ImVec2 b, float r)
{
    GArcTo(g, ImVec2(b.x - r, a.y + r), r, -90.0f, 0.0f);
    GArcTo(g, ImVec2(b.x - r, b.y - r), r, 0.0f, 90.0f);
    GArcTo(g, ImVec2(a.x + r, b.y - r), r, 90.0f, 180.0f);
    GArcTo(g, ImVec2(a.x + r, a.y + r), r, 180.0f, 270.0f);
}

// A ring that looks turned from metal: brighter where the light from the upper left falls on it, dark opposite.
static void GRingLit(ImDrawList *dl, ImVec2 c, float r, float thick, GRgb dark, GRgb bright, float a)
{
    const int segments = r < 24.0f ? 16 : r < 120.0f ? 28 : 40; // each is an arc of three points
    for (int i = 0; i < segments; i++)
    {
        const float d0 = 360.0f * (float)i / (float)segments, d1 = 360.0f * (float)(i + 1) / (float)segments, mid = (d0 + d1) * 0.5f;
        float k = 0.5f + 0.5f * cosf((mid - 235.0f) * kGPi / 180.0f);
        k = k * k * (3.0f - 2.0f * k);
        ImVec2 p[3] = { GPolar(c, r, d0), GPolar(c, r, mid), GPolar(c, r, d1) };
        dl->AddPolyline(p, 3, GCol(GMix(dark, bright, k), a), 0, thick);
    }
}

// A soft shadow or glow around a circle: rings that fade outwards from radius r over spread.
static void GSoftRing(ImDrawList *dl, ImVec2 c, float r, float spread, GRgb col, float a)
{
    enum { kRings = 3 };
    const float step = spread / kRings;
    for (int i = 0; i < kRings; i++)
    {
        const float k = 1.0f - (float)i / kRings;
        dl->AddCircle(c, r + step * ((float)i + 0.5f), GCol(col, a * k * k), r < 60.0f ? 32 : 56, step * 1.15f);
    }
}

// A dial's value range and the screen angles it spans (the needle sweeps clockwise from d0 to d1).
struct GScale { float v0, v1, d0, d1; };
static float GDeg(const GScale &s, float v)
{
    float f = (v - s.v0) / (s.v1 - s.v0);
    f = f < 0.0f ? 0.0f : f > 1.0f ? 1.0f : f;
    return s.d0 + (s.d1 - s.d0) * f;
}
// The usual 240 degree sweep: from the lower left round the top to the lower right.
static GScale GSweep(float v0, float v1) { return GScale{ v0, v1, 150.0f, 390.0f }; }

// The dial's ground: in the open style the scale's arc alone, shadowed; else a dark face inside a bevelled ring.
static void GDialFace(ImDrawList *dl, ImVec2 c, float r, float u, float a)
{
    if (s_open)
    {
        // just the scale arc (the ticks start at it), over a soft and a sharp shadow, nothing behind it
        const ImVec2 low(c.x + 1.0f * u, c.y + 1.3f * u);
        GArc(dl, low, r - 2.5f * u, 150.0f, 390.0f, 4.6f * u, GCol(0, 0, 0, 0.16f * a));
        GArc(dl, low, r - 2.5f * u, 150.0f, 390.0f, 2.4f * u, GCol(0, 0, 0, 0.5f * a));
        GArc(dl, c, r - 2.5f * u, 150.0f, 390.0f, 1.9f * u, GCol(kTick, a));
        return;
    }
    const float ring = GGlass() ? 2.2f * u : 3.4f * u; // the bezel's width
    GSoftRing(dl, ImVec2(c.x, c.y + 1.2f * u), r + ring, 5.0f * u, GRgb{ 0, 0, 0 }, 0.34f * a);
    if (GGlass())
    {
        // see-through: a tinted face the game shows through
        if (s_faceAlpha > 0.0f) dl->AddCircleFilled(c, r + 0.5f * u, GCol(kFace, s_faceAlpha * a), 64);
    }
    else
    {
        // the face: darkest at the rim, a little lighter towards the middle
        dl->AddCircleFilled(c, r + 0.5f * u, GCol(kFace, a), 64);
        for (int i = 1; i <= 3; i++) dl->AddCircleFilled(c, r * (1.0f - 0.22f * (float)i), GCol(kFaceMid, 0.14f * a), 40);
        // the rim's shadow on the face
        for (int i = 0; i < 3; i++) dl->AddCircle(c, r - (1.0f + 2.0f * (float)i) * u, GCol(0, 0, 0, (0.4f - 0.13f * (float)i) * a), 64, 2.1f * u);
    }
    // the bezel: a metal ring between two dark edges
    GRingLit(dl, c, r + ring * 0.5f, ring, kMetalDark, kMetalBright, a);
    dl->AddCircle(c, r + ring, GCol(0, 0, 0, 0.75f * a), 64, 1.0f * u);
    dl->AddCircle(c, r, GCol(0, 0, 0, 0.8f * a), 64, 1.0f * u);
}

// Dial text: with a drop shadow when the faces are see-through.
static void GText(ImDrawList *dl, const GaugeFonts &f, int style, ImVec2 at, ImU32 col, const char *s, int align, float u)
{
    if (GGlass()) TextDraw(dl, f, style, ImVec2(at.x + 1.2f * u, at.y + 1.4f * u), GCol(0, 0, 0, 0.7f * GAlpha(col)), s, align, u);
    TextDraw(dl, f, style, at, col, s, align, u);
}

// Major ticks every majorStep with minorPer - 1 minor ticks between them. A major tick is wider at the rim than at
// its inner end.
static void GTicks(ImDrawList *dl, ImVec2 c, float r, const GScale &s, float majorStep, int minorPer, float lenMaj, float lenMin, float u, float a)
{
    const int majors = (int)((s.v1 - s.v0) / majorStep + 0.5f);
    const ImVec2 sh(1.0f * u, 1.3f * u);
    for (int pass = GGlass() ? 0 : 1; pass < 2; pass++)
    {
        // pass 0: the shadows under see-through faces, pass 1: the ticks
        const ImVec2 o = pass ? ImVec2(0, 0) : sh;
        const ImU32 major = pass ? GCol(kTick, a) : GCol(0, 0, 0, 0.55f * a), minor = pass ? GCol(kTick, 0.8f * a) : GCol(0, 0, 0, 0.45f * a);
        for (int i = 0; i <= majors; i++)
        {
            const float v = s.v0 + i * majorStep;
            const float d = GDeg(s, v), rad = d * kGPi / 180.0f;
            const ImVec2 side(-sinf(rad), cosf(rad)), outer = GPolar(c, r, d) + o, inner = GPolar(c, r - lenMaj * u, d) + o;
            ImVec2 q[4] = { outer + side * (1.25f * u), inner + side * (0.75f * u), inner - side * (0.75f * u), outer - side * (1.25f * u) };
            dl->AddConvexPolyFilled(q, 4, major);
            if (i < majors)
                for (int j = 1; j < minorPer; j++)
                {
                    const float dm = GDeg(s, v + majorStep * j / minorPer);
                    GBar(dl, GPolar(c, r, dm) + o, GPolar(c, r - lenMin * u, dm) + o, minor, 1.2f * u);
                }
        }
    }
}

// A zone (red, blue) as a band just inside the rim, with a fainter edge towards the middle.
static void GZone(ImDrawList *dl, ImVec2 c, float r, const GScale &s, float v0, float v1, float thick, float u, ImU32 col)
{
    const float d0 = GDeg(s, v0), d1 = GDeg(s, v1);
    if (GGlass()) GArc(dl, ImVec2(c.x + 1.0f * u, c.y + 1.3f * u), r - thick * u * 0.5f - 0.5f * u, d0, d1, thick * u, GCol(0, 0, 0, 0.4f * GAlpha(col)));
    GArc(dl, c, r - thick * u * 0.5f - 0.5f * u, d0, d1, thick * u, col);
    GArc(dl, c, r - thick * u - 1.3f * u, d0, d1, 1.4f * u, (col & 0x00ffffff) | ((ImU32)(GAlpha(col) * 0.38f * 255.0f) << 24));
}

// A needle: a blade that tapers to a point, its upper half bright and its lower half shaded as if it had a ridge,
// a dark counterweight behind the hub, and a soft shadow under all of it.
static void GNeedle(ImDrawList *dl, ImVec2 c, float deg, float len, float wBase, float wTip, float u, float a, ImU32 col)
{
    const float rad = deg * kGPi / 180.0f;
    const ImVec2 dir(cosf(rad), sinf(rad)), side(-dir.y, dir.x);
    const ImVec2 neck = c + dir * (len * 0.95f), tip = c + dir * len, tail = c - dir * (len * 0.24f);
    const ImVec2 b0 = c + side * (wBase * u), b1 = c - side * (wBase * u), n0 = neck + side * (wTip * u), n1 = neck - side * (wTip * u);
    // the shadow: a wide faint one and a closer darker one
    for (int i = 0; i < 2; i++)
    {
        const ImVec2 sh = i ? ImVec2(1.0f * u, 1.6f * u) : ImVec2(1.8f * u, 3.0f * u);
        ImVec2 q[5] = { b0 + sh, n0 + sh, tip + sh, n1 + sh, b1 + sh };
        dl->AddConvexPolyFilled(q, 5, GCol(0, 0, 0, (i ? 0.36f : 0.14f) * a));
    }
    // the counterweight
    const ImVec2 t0 = tail + side * (wBase * 1.25f * u), t1 = tail - side * (wBase * 1.25f * u);
    ImVec2 w[4] = { c + side * (wBase * 1.25f * u), t0, t1, c - side * (wBase * 1.25f * u) };
    dl->AddConvexPolyFilled(w, 4, GCol(54, 55, 59, a));
    dl->AddCircleFilled(tail, wBase * 1.25f * u, GCol(54, 55, 59, a), 16);
    // the blade's two halves; which is the bright one depends on where the light from the upper left falls
    const bool upper = side.x * -0.57f + side.y * -0.82f > 0.0f;
    const ImU32 lit = col, shade = GCol(kNeedleShade, GAlpha(col));
    ImVec2 h0[4] = { c, b0, n0, tip }, h1[4] = { c, tip, n1, b1 };
    dl->AddConvexPolyFilled(h0, 4, upper ? lit : shade);
    dl->AddConvexPolyFilled(h1, 4, upper ? shade : lit);
}

// The hub over the needle's foot: a dark cap with a lighter top, a metal ring and a centre dot.
static void GHub(ImDrawList *dl, ImVec2 c, float r, float u, float a)
{
    dl->AddCircleFilled(ImVec2(c.x + 0.8f * u, c.y + 1.5f * u), r * 1.32f, GCol(0, 0, 0, 0.13f * a), 20);
    dl->AddCircleFilled(ImVec2(c.x + 0.6f * u, c.y + 1.1f * u), r * 1.08f, GCol(0, 0, 0, 0.3f * a), 20);
    dl->AddCircleFilled(c, r, GCol(22, 23, 26, a), 28);
    dl->AddCircleFilled(ImVec2(c.x - r * 0.12f, c.y - r * 0.14f), r * 0.74f, GCol(44, 45, 50, a), 20);
    dl->AddCircleFilled(ImVec2(c.x - r * 0.2f, c.y - r * 0.24f), r * 0.42f, GCol(64, 66, 72, a), 14);
    GRingLit(dl, c, r, 1.3f * u, GRgb{ 40, 41, 45 }, GRgb{ 205, 207, 212 }, a);
    dl->AddCircleFilled(c, r * 0.22f, GCol(168, 170, 175, a), 10);
}

// ---- icons, each centred on p with height h (screen pixels), after the symbols a dashboard uses ----

// The fuel pump: a body with a window, a foot, and a hose up its right side to the nozzle.
static void GIconFuel(ImDrawList *dl, ImVec2 p, float h, ImU32 col)
{
    auto P = [&](float x, float y) { return ImVec2(p.x + x * h, p.y + y * h); };
    const float line = h * 0.085f;
    GPath body;
    GRound(body, P(-0.33f, -0.46f), P(0.11f, 0.38f), h * 0.05f);
    GStroke(dl, body, col, line, true);
    dl->AddRectFilled(P(-0.33f, -0.06f), P(0.11f, 0.38f), col);
    dl->AddRectFilled(P(-0.41f, 0.38f), P(0.19f, 0.47f), col, h * 0.02f);
    GPath hose;
    GTo(hose, P(0.11f, 0.12f));
    GTo(hose, P(0.2f, 0.12f));
    GCurve(hose, P(0.3f, 0.12f), P(0.3f, 0.12f), P(0.3f, 0.02f));
    GTo(hose, P(0.3f, -0.2f));
    GTo(hose, P(0.19f, -0.34f));
    GStroke(dl, hose, col, line * 0.9f, false);
}

// Coolant temperature: a thermometer standing in waves.
static void GIconTemp(ImDrawList *dl, ImVec2 p, float h, ImU32 col)
{
    auto P = [&](float x, float y) { return ImVec2(p.x + x * h, p.y + y * h); };
    GLine(dl, P(0.0f, -0.44f), P(0.0f, 0.1f), col, h * 0.11f);
    dl->AddCircleFilled(P(0.0f, 0.15f), h * 0.135f, col, 16);
    GLine(dl, P(0.1f, -0.36f), P(0.25f, -0.36f), col, h * 0.06f);
    GLine(dl, P(0.1f, -0.2f), P(0.25f, -0.2f), col, h * 0.06f);
    for (int k = 0; k < 2; k++)
    {
        GPath wave;
        for (int i = 0; i <= 12; i++)
        {
            const float x = -0.46f + 0.92f * (float)i / 12.0f;
            GTo(wave, P(x, 0.3f + 0.15f * (float)k + 0.04f * sinf((x + 0.46f) / 0.92f * 3.0f * 2.0f * kGPi)));
        }
        GStroke(dl, wave, col, h * 0.065f, false);
    }
}

// Engine oil: an oil can with its filler cap, the spout rising to the right and a drop falling from it.
static void GIconOil(ImDrawList *dl, ImVec2 p, float h, ImU32 col)
{
    auto P = [&](float x, float y) { return ImVec2(p.x + x * h, p.y + y * h); };
    GPath body;
    GRound(body, P(-0.44f, -0.06f), P(0.1f, 0.3f), h * 0.05f);
    GFill(dl, body, col);
    GPath spout;
    GTo(spout, P(0.06f, -0.06f));
    GTo(spout, P(0.43f, -0.24f));
    GTo(spout, P(0.47f, -0.17f));
    GTo(spout, P(0.08f, 0.1f));
    GFill(dl, spout, col);
    GLine(dl, P(-0.3f, -0.06f), P(-0.3f, -0.16f), col, h * 0.08f);
    GLine(dl, P(-0.39f, -0.19f), P(-0.21f, -0.19f), col, h * 0.08f);
    GPath drop;
    GTo(drop, P(0.44f, -0.06f));
    GCurve(drop, P(0.49f, 0.04f), P(0.52f, 0.1f), P(0.44f, 0.14f));
    GCurve(drop, P(0.36f, 0.1f), P(0.39f, 0.04f), P(0.44f, -0.06f));
    GFill(dl, drop, col);
}

// The turbocharger: a snail housing with its wheel and the outlet at the top.
static void GIconTurbo(ImDrawList *dl, ImVec2 p, float h, ImU32 col)
{
    auto P = [&](float x, float y) { return ImVec2(p.x + x * h, p.y + y * h); };
    const ImVec2 c = P(-0.08f, 0.08f);
    const float line = h * 0.085f;
    GPath snail;
    GTo(snail, P(0.44f, -0.24f));
    GTo(snail, P(-0.08f, -0.24f));
    GArcTo(snail, c, h * 0.32f, 270.0f, 20.0f);
    GStroke(dl, snail, col, line, false);
    GLine(dl, P(0.44f, -0.36f), P(0.44f, -0.12f), col, line);
    dl->AddCircle(c, h * 0.13f, col, 20, line * 0.9f);
    for (int i = 0; i < 3; i++) GLine(dl, GPolar(c, h * 0.13f, 120.0f * (float)i + 20.0f), GPolar(c, h * 0.3f, 120.0f * (float)i + 62.0f), col, line * 0.55f);
}

// A circle between two brackets, as the brake symbols have it.
static void GIconBrake(ImDrawList *dl, ImVec2 p, float h, ImU32 col)
{
    const float line = h * 0.075f;
    dl->AddCircle(p, h * 0.31f, col, 20, line);
    GPath left, right;
    GArcTo(left, p, h * 0.46f, 138.0f, 222.0f);
    GArcTo(right, p, h * 0.46f, -42.0f, 42.0f);
    GStroke(dl, left, col, line, false);
    GStroke(dl, right, col, line, false);
}

// Brake air pressure: the brake symbol with an exclamation mark.
static void GIconAir(ImDrawList *dl, ImVec2 p, float h, ImU32 col)
{
    GIconBrake(dl, p, h, col);
    GBar(dl, ImVec2(p.x, p.y - h * 0.19f), ImVec2(p.x, p.y + h * 0.05f), col, h * 0.075f);
    dl->AddCircleFilled(ImVec2(p.x, p.y + h * 0.17f), h * 0.045f, col, 10);
}

// The parking brake: the brake symbol with a P.
static void GIconHandbrake(ImDrawList *dl, const GaugeFonts &f, ImVec2 p, float h, ImU32 col, float u)
{
    GIconBrake(dl, p, h, col);
    TextDraw(dl, f, kGsLamp, ImVec2(p.x + 0.2f * u, p.y), col, "P", 0, u);
}

// Headlights: a lamp with its rays falling forwards.
static void GIconBeam(ImDrawList *dl, ImVec2 p, float h, ImU32 col)
{
    auto P = [&](float x, float y) { return ImVec2(p.x + x * h, p.y + y * h); };
    GPath lamp;
    GTo(lamp, P(-0.04f, -0.31f));
    GCurve(lamp, P(-0.5f, -0.31f), P(-0.5f, 0.31f), P(-0.04f, 0.31f));
    GFill(dl, lamp, col);
    for (int i = 0; i < 4; i++)
    {
        const float y = -0.27f + 0.18f * (float)i;
        GLine(dl, P(0.09f, y), P(0.46f, y + 0.09f), col, h * 0.07f);
    }
}

// A wheel seen from above, for the two drive train symbols.
static void GWheel(ImDrawList *dl, ImVec2 c, float w, float h, ImU32 col) { dl->AddRectFilled(ImVec2(c.x - w * 0.5f, c.y - h * 0.5f), ImVec2(c.x + w * 0.5f, c.y + h * 0.5f), col, w * 0.3f); }

// All wheel drive: the drive train from above, four wheels on two axles and the shaft between them.
static void GIconAwd(ImDrawList *dl, ImVec2 p, float h, ImU32 col)
{
    auto P = [&](float x, float y) { return ImVec2(p.x + x * h, p.y + y * h); };
    const float line = h * 0.075f;
    for (int i = 0; i < 4; i++) GWheel(dl, P(i & 1 ? 0.37f : -0.37f, i & 2 ? 0.28f : -0.28f), h * 0.17f, h * 0.3f, col);
    GBar(dl, P(-0.3f, -0.28f), P(0.3f, -0.28f), col, line);
    GBar(dl, P(-0.3f, 0.28f), P(0.3f, 0.28f), col, line);
    GBar(dl, P(0.0f, -0.28f), P(0.0f, 0.28f), col, line);
    dl->AddCircleFilled(P(0.0f, -0.28f), h * 0.085f, col, 12);
    dl->AddCircleFilled(P(0.0f, 0.28f), h * 0.085f, col, 12);
}

// The differential lock: one axle with its two wheels and a cross in the differential's case.
static void GIconDiff(ImDrawList *dl, ImVec2 p, float h, ImU32 col)
{
    auto P = [&](float x, float y) { return ImVec2(p.x + x * h, p.y + y * h); };
    const float line = h * 0.075f;
    GWheel(dl, P(-0.39f, 0.0f), h * 0.17f, h * 0.5f, col);
    GWheel(dl, P(0.39f, 0.0f), h * 0.17f, h * 0.5f, col);
    GBar(dl, P(-0.32f, 0.0f), P(-0.19f, 0.0f), col, line);
    GBar(dl, P(0.19f, 0.0f), P(0.32f, 0.0f), col, line);
    GPath box;
    GRound(box, P(-0.19f, -0.19f), P(0.19f, 0.19f), h * 0.04f);
    GStroke(dl, box, col, line, true);
    GLine(dl, P(-0.1f, -0.1f), P(0.1f, 0.1f), col, line);
    GLine(dl, P(-0.1f, 0.1f), P(0.1f, -0.1f), col, line);
}

// The low range: a gear wheel with an L.
static void GIconLow(ImDrawList *dl, const GaugeFonts &f, ImVec2 p, float h, ImU32 col, float u)
{
    GPath cog;
    for (int i = 0; i < 8; i++)
    {
        const float d = 45.0f * (float)i;
        GTo(cog, GPolar(p, h * 0.37f, d - 14.0f));
        GTo(cog, GPolar(p, h * 0.47f, d - 8.0f));
        GTo(cog, GPolar(p, h * 0.47f, d + 8.0f));
        GTo(cog, GPolar(p, h * 0.37f, d + 14.0f));
    }
    GStroke(dl, cog, col, h * 0.07f, true);
    TextDraw(dl, f, kGsLamp, ImVec2(p.x + 0.3f * u, p.y), col, "L", 0, u);
}

// The tires' pressure: a tire seen from the side, its tread as short strokes around it, with the mode's letter inside:
// L, R, N, or + for the increased pressure.
static void GIconTire(ImDrawList *dl, const GaugeFonts &f, ImVec2 p, float h, int lamp, ImU32 col, float u)
{
    GPath ring;
    for (int i = 0; i < 24; i++) GTo(ring, GPolar(p, h * 0.35f, 15.0f * (float)i));
    GStroke(dl, ring, col, h * 0.07f, true);
    for (int i = 0; i < 12; i++)
    {
        GPath tread;
        GTo(tread, GPolar(p, h * 0.41f, 30.0f * (float)i));
        GTo(tread, GPolar(p, h * 0.49f, 30.0f * (float)i));
        GStroke(dl, tread, col, h * 0.07f, false);
    }
    static const char *letters[4] = { "L", "R", "N", "+" };
    TextDraw(dl, f, kGsLamp, ImVec2(p.x + 0.3f * u, p.y), col, letters[lamp < 1 || lamp > 4 ? 2 : lamp - 1], 0, u);
}

// The engine: its block with the intake on top, the fan's stub on the left and the gearbox flange on the right.
static void GIconEngine(ImDrawList *dl, ImVec2 p, float h, ImU32 col)
{
    static const float k[][2] = { { -0.14f, -0.36f }, { 0.14f, -0.36f }, { 0.14f, -0.28f }, { 0.05f, -0.28f }, { 0.05f, -0.19f }, { 0.2f, -0.19f }, { 0.28f, -0.08f },
                                  { 0.36f, -0.08f }, { 0.36f, -0.16f }, { 0.46f, -0.16f }, { 0.46f, 0.22f }, { 0.36f, 0.22f }, { 0.36f, 0.12f }, { 0.28f, 0.12f },
                                  { 0.28f, 0.3f }, { -0.12f, 0.3f }, { -0.22f, 0.18f }, { -0.32f, 0.18f }, { -0.32f, 0.04f }, { -0.4f, 0.04f }, { -0.4f, 0.16f },
                                  { -0.47f, 0.16f }, { -0.47f, -0.16f }, { -0.4f, -0.16f }, { -0.4f, -0.04f }, { -0.32f, -0.04f }, { -0.32f, -0.19f }, { -0.05f, -0.19f },
                                  { -0.05f, -0.28f }, { -0.14f, -0.28f } };
    GPath block;
    for (const auto &q : k) GTo(block, ImVec2(p.x + q[0] * h, p.y + (q[1] + 0.03f) * h));
    GFill(dl, block, col);
}

// Frost: a snow crystal.
static void GIconSnow(ImDrawList *dl, ImVec2 p, float h, ImU32 col)
{
    for (int i = 0; i < 3; i++)
    {
        const float d = 90.0f + 60.0f * i;
        GLine(dl, GPolar(p, h * 0.45f, d), GPolar(p, h * 0.45f, d + 180.0f), col, h * 0.075f);
        for (int s = 0; s < 2; s++)
        {
            const float arm = d + 180.0f * s;
            const ImVec2 m = GPolar(p, h * 0.26f, arm);
            GLine(dl, m, GPolar(m, h * 0.15f, arm + 45.0f), col, h * 0.065f);
            GLine(dl, m, GPolar(m, h * 0.15f, arm - 45.0f), col, h * 0.065f);
        }
    }
}

// The tilt: a truck seen from behind, leaning as the driven one does, over level ground. rollDeg above 0: the truck
// leans to its right.
static void GIconTilt(ImDrawList *dl, ImVec2 p, float h, float rollDeg, ImU32 col)
{
    const float rad = rollDeg * kGPi / 180.0f, cs = cosf(rad), sn = sinf(rad);
    const ImVec2 o(p.x, p.y + h * 0.1f);
    auto R = [&](float x, float y) { return ImVec2(o.x + (x * cs - y * sn) * h, o.y + (x * sn + y * cs) * h); };
    auto box = [&](float x0, float y0, float x1, float y1) {
        ImVec2 q[4] = { R(x0, y0), R(x1, y0), R(x1, y1), R(x0, y1) };
        dl->AddConvexPolyFilled(q, 4, col);
    };
    // the cab as a frame with its window open, the body under it, the axle and the two wheels
    ImVec2 cab[4] = { R(-0.24f, -0.44f), R(0.24f, -0.44f), R(0.24f, -0.2f), R(-0.24f, -0.2f) };
    dl->AddPolyline(cab, 4, col, ImDrawFlags_Closed, h * 0.075f);
    box(-0.32f, -0.2f, 0.32f, 0.06f);
    box(-0.26f, 0.06f, 0.26f, 0.12f);
    box(-0.4f, 0.02f, -0.24f, 0.3f);
    box(0.24f, 0.02f, 0.4f, 0.3f);
    GLine(dl, ImVec2(p.x - h * 0.48f, p.y + h * 0.47f), ImVec2(p.x + h * 0.48f, p.y + h * 0.47f), col, h * 0.055f);
}

// A lamp's ground. With solid faces: a dark cell with a light upper edge, and the lamp's colour in it when lit. With
// see-through faces and in the open style there is no cell: a lit lamp glows behind its symbol, and one that is off
// is faint, like the game's own icons. Returns the colour to draw the symbol in.
static ImU32 GLampCell(ImDrawList *dl, ImVec2 p, float s, bool on, GRgb colOn, float u, float a)
{
    if (!GGlass())
    {
        const ImVec2 a0(p.x - s * 0.5f, p.y - s * 0.5f), a1(p.x + s * 0.5f, p.y + s * 0.5f);
        dl->AddRectFilled(ImVec2(a0.x, a0.y + 1.0f * u), ImVec2(a1.x, a1.y + 1.4f * u), GCol(0, 0, 0, 0.35f * a), 3.4f * u);
        dl->AddRectFilled(a0, a1, GCol(15, 16, 18, 0.96f * a), 3.0f * u);
        dl->AddRect(a0, a1, GCol(70, 72, 77, a), 3.0f * u, 0, 1.0f * u);
        dl->AddRect(ImVec2(a0.x + 1.0f * u, a0.y + 1.0f * u), ImVec2(a1.x - 1.0f * u, a1.y - 1.0f * u), GCol(0, 0, 0, 0.6f * a), 2.2f * u, 0, 1.0f * u);
        if (on) dl->AddRectFilled(ImVec2(a0.x + 1.5f * u, a0.y + 1.5f * u), ImVec2(a1.x - 1.5f * u, a1.y - 1.5f * u), GCol(colOn, 0.13f * a), 2.0f * u);
    }
    if (on)
    {
        // the glow: the lamp's colour fading outwards
        const float most = GGlass() ? s * 0.62f : s * 0.42f;
        for (int i = 0; i < 3; i++) dl->AddCircleFilled(p, most * (1.0f - 0.26f * (float)i), GCol(colOn, (GGlass() ? 0.07f : 0.09f) * a), 20);
    }
    return on ? GCol(GMix(colOn, GRgb{ 255, 255, 255 }, 0.12f), a) : GGlass() ? GCol(kText, 0.38f * a) : GCol(kDim, a);
}

// ---- dials ----

static void GSpeedo(ImDrawList *dl, const GaugeFonts &f, ImVec2 c, float r, float u, float a, const GaugeState &st)
{
    // the top in the unit shown, rounded up to a whole step: numbers every 10 up to 80, every 20 above
    const float raw = st.mph ? st.speedMax / 1.609f : st.speedMax;
    const float step = raw <= 80.0f ? 10.0f : 20.0f, top = ceilf(raw / step - 0.01f) * step;
    const GScale s = GSweep(0.0f, top);
    GDialFace(dl, c, r, u, a);
    GTicks(dl, c, r - 3.0f * u, s, step, step >= 20.0f ? 4 : 2, 9.0f, 5.0f, u, a);
    char buf[32];
    for (float v = 0.0f; v <= top + 0.5f; v += step)
    {
        sprintf_s(buf, "%.0f", v);
        GText(dl, f, kGsBig, GPolar(c, r - 21.0f * u, GDeg(s, v)), GCol(kText, a), buf, 0, u);
    }
    const float v = st.mph ? st.speedKmh / 1.609f : st.speedKmh;
    sprintf_s(buf, "%.0f", v);
    GText(dl, f, kGsReadout, ImVec2(c.x, c.y + r * 0.46f), GCol(kText, a), buf, 0, u);
    GText(dl, f, kGsTiny, ImVec2(c.x, c.y + r * 0.66f), GCol(kMuted, a), st.mph ? "mph" : "km/h", 0, u);
    GNeedle(dl, c, GDeg(s, v), r * 0.76f, 3.0f, 0.9f, u, a, GCol(kNeedle, a));
    GHub(dl, c, 7.5f * u, u, a);
}

static void GTacho(ImDrawList *dl, const GaugeFonts &f, ImVec2 c, float r, float u, float a, const GaugeState &st)
{
    const float top = st.rpmMax / 100.0f, step = st.rpmMax >= 5000.0f ? 10.0f : 5.0f;
    const GScale s = GSweep(0.0f, top);
    GDialFace(dl, c, r, u, a);
    GZone(dl, c, r - 3.0f * u, s, st.rpmRedline / 100.0f, top, 5.0f, u, GCol(kRed, a));
    GTicks(dl, c, r - 3.0f * u, s, step, step >= 10.0f ? 2 : 5, 9.0f, 5.0f, u, a);
    char buf[32];
    for (float v = 0.0f; v <= top + 0.5f; v += step)
    {
        sprintf_s(buf, "%.0f", v);
        GText(dl, f, kGsBig, GPolar(c, r - 21.0f * u, GDeg(s, v)), GCol(kText, a), buf, 0, u);
    }
    GText(dl, f, kGsTiny, ImVec2(c.x, c.y - r * 0.36f), GCol(kMuted, a), "x100 rpm", 0, u);
    // the gear: "4" with "A" beside it in automatic; the gearbox's own positions by their names ("H", "L", "L+", "L-");
    // "R", "N"
    const bool named = st.gear > 0 && st.gearMode && st.gearMode[0] && st.gearMode[0] != 'A';
    const char *gear = st.gear < 0 ? "R" : st.gear == 0 ? "N" : named ? st.gearMode : (sprintf_s(buf, "%d", st.gear), buf);
    const bool mode = st.gear > 0 && !named && st.gearMode && st.gearMode[0];
    const float gw = TextWidth(f, kGsGear, gear, u), mw = mode ? TextWidth(f, kGsGearMode, st.gearMode, u) + 3.0f * u : 0.0f;
    const float gx = c.x - (gw + mw) * 0.5f;
    GText(dl, f, kGsGear, ImVec2(gx, c.y + r * 0.5f), GCol(kText, a), gear, -1, u);
    if (mode) GText(dl, f, kGsGearMode, ImVec2(gx + gw + 3.0f * u, c.y + r * 0.5f + 4.0f * u), GCol(kMuted, a), st.gearMode, -1, u);
    // the engine speed as it is, with the engine off too: the needle runs down with a stopping engine
    GNeedle(dl, c, GDeg(s, st.rpm / 100.0f), r * 0.76f, 3.0f, 0.9f, u, a, GCol(kNeedle, a));
    GHub(dl, c, 7.5f * u, u, a);
}

static void GSmallDial(ImDrawList *dl, ImVec2 c, float r, float u, float a) { GDialFace(dl, c, r, u, a); }

static void GSmallNeedle(ImDrawList *dl, ImVec2 c, float r, float deg, float u, float a)
{
    GNeedle(dl, c, deg, r * 0.58f, 2.2f, 0.7f, u, a, GCol(kNeedle, a));
    GHub(dl, c, 5.2f * u, u, a);
}

// A symbol that stands on a dial or beside a readout, in the colour col: with a shadow when the faces are
// see-through. draw(at, colour) draws it.
template <typename Draw> static void GDialIcon(ImVec2 at, float u, ImU32 col, Draw draw)
{
    if (GGlass()) draw(ImVec2(at.x + 1.0f * u, at.y + 1.2f * u), GCol(0, 0, 0, 0.6f * GAlpha(col)));
    draw(at, col);
}

static void GFuel(ImDrawList *dl, const GaugeFonts &f, ImVec2 c, float r, float u, float a, const GaugeState &st)
{
    const GScale s = GSweep(0.0f, 1.0f);
    GSmallDial(dl, c, r, u, a);
    GZone(dl, c, r - 2.5f * u, s, 0.0f, 0.1f, 4.0f, u, GCol(kRed, a));
    GTicks(dl, c, r - 2.5f * u, s, 0.5f, 2, 7.0f, 4.0f, u, a);
    GText(dl, f, kGsSmall, GPolar(c, r - 15.0f * u, GDeg(s, 0.0f)), GCol(kText, a), "E", 0, u);
    GText(dl, f, kGsSmall, GPolar(c, r - 15.0f * u, GDeg(s, 0.5f)), GCol(kText, a), "1/2", 0, u);
    GText(dl, f, kGsSmall, GPolar(c, r - 15.0f * u, GDeg(s, 1.0f)), GCol(kText, a), "F", 0, u);
    GDialIcon(ImVec2(c.x, c.y + r * 0.42f), u, GCol(kText, a), [&](ImVec2 q, ImU32 col) { GIconFuel(dl, q, 10.5f * u, col); });
    GSmallNeedle(dl, c, r, GDeg(s, st.fuelFrac), u, a);
}

// The coolant temperature. oilNeedle: the oil's needle in the same dial, amber and shorter, under the coolant's,
// with the oil can beside the thermometer.
static void GTemp(ImDrawList *dl, const GaugeFonts &f, ImVec2 c, float r, float u, float a, const GaugeState &st, bool oilNeedle)
{
    const GScale s = GSweep(50.0f, 130.0f);
    GSmallDial(dl, c, r, u, a);
    GZone(dl, c, r - 2.5f * u, s, 50.0f, kTempCold, 4.0f, u, GCol(kBlue, a));
    GZone(dl, c, r - 2.5f * u, s, kTempRed, 130.0f, 4.0f, u, GCol(kRed, a));
    GTicks(dl, c, r - 2.5f * u, s, 40.0f, 2, 7.0f, 4.0f, u, a);
    char buf[16];
    for (float v = 50.0f; v <= 130.5f; v += 40.0f)
    {
        sprintf_s(buf, "%.0f", st.fahrenheit ? v * 1.8f + 32.0f : v);
        GText(dl, f, kGsSmall, GPolar(c, r - 15.0f * u, GDeg(s, v)), GCol(kText, a), buf, 0, u);
    }
    if (!oilNeedle)
    {
        GDialIcon(ImVec2(c.x, c.y + r * 0.42f), u, GCol(kText, a), [&](ImVec2 q, ImU32 col) { GIconTemp(dl, q, 10.5f * u, col); });
        GSmallNeedle(dl, c, r, GDeg(s, st.tempC), u, a);
        return;
    }
    GDialIcon(ImVec2(c.x - r * 0.15f, c.y + r * 0.45f), u, GCol(kText, a), [&](ImVec2 q, ImU32 col) { GIconTemp(dl, q, 9.0f * u, col); });
    GDialIcon(ImVec2(c.x + r * 0.16f, c.y + r * 0.47f), u, GCol(kAmber, a), [&](ImVec2 q, ImU32 col) { GIconOil(dl, q, 9.0f * u, col); });
    GNeedle(dl, c, GDeg(s, st.oilC), r * 0.5f, 2.4f, 0.8f, u, a, GCol(kAmber, a));
    GSmallNeedle(dl, c, r, GDeg(s, st.tempC), u, a);
}

// The oil temperature on a dial of its own: 50 to 150 C, red from the settings' OilWarnC.
static void GOil(ImDrawList *dl, const GaugeFonts &f, ImVec2 c, float r, float u, float a, const GaugeState &st)
{
    const GScale s = GSweep(50.0f, 150.0f);
    GSmallDial(dl, c, r, u, a);
    GZone(dl, c, r - 2.5f * u, s, st.oilWarnC, 150.0f, 4.0f, u, GCol(kRed, a));
    GTicks(dl, c, r - 2.5f * u, s, 50.0f, 2, 7.0f, 4.0f, u, a);
    char buf[16];
    for (float v = 50.0f; v <= 150.5f; v += 50.0f)
    {
        sprintf_s(buf, "%.0f", st.fahrenheit ? v * 1.8f + 32.0f : v);
        GText(dl, f, kGsSmall, GPolar(c, r - 15.0f * u, GDeg(s, v)), GCol(kText, a), buf, 0, u);
    }
    GDialIcon(ImVec2(c.x, c.y + r * 0.42f), u, GCol(kText, a), [&](ImVec2 q, ImU32 col) { GIconOil(dl, q, 10.5f * u, col); });
    GSmallNeedle(dl, c, r, GDeg(s, st.oilC), u, a);
}

static void GBoost(ImDrawList *dl, const GaugeFonts &f, ImVec2 c, float r, float u, float a, const GaugeState &st)
{
    const float top = st.psi ? 30.0f : 2.0f, step = st.psi ? 10.0f : 1.0f;
    const GScale s = GSweep(0.0f, top);
    GSmallDial(dl, c, r, u, a);
    GTicks(dl, c, r - 2.5f * u, s, step, 2, 7.0f, 4.0f, u, a);
    char buf[16];
    for (float v = 0.0f; v <= top + 0.5f; v += step)
    {
        sprintf_s(buf, "%.0f", v);
        GText(dl, f, kGsSmall, GPolar(c, r - 15.0f * u, GDeg(s, v)), GCol(kText, a), buf, 0, u);
    }
    GDialIcon(ImVec2(c.x, c.y + r * 0.42f), u, GCol(kText, a), [&](ImVec2 q, ImU32 col) { GIconTurbo(dl, q, 10.5f * u, col); });
    GText(dl, f, kGsTiny, ImVec2(c.x, c.y - r * 0.36f), GCol(kMuted, a), st.psi ? "psi" : "bar", 0, u);
    GSmallNeedle(dl, c, r, GDeg(s, st.psi ? st.boostBar * 14.5f : st.boostBar), u, a);
}

static void GAir(ImDrawList *dl, const GaugeFonts &f, ImVec2 c, float r, float u, float a, const GaugeState &st)
{
    const GScale s = GSweep(0.0f, 10.0f);
    GSmallDial(dl, c, r, u, a);
    GZone(dl, c, r - 2.5f * u, s, 0.0f, kAirWarn, 4.0f, u, GCol(kRed, a));
    GTicks(dl, c, r - 2.5f * u, s, 5.0f, 5, 7.0f, 4.0f, u, a);
    char buf[16];
    for (float v = 0.0f; v <= 10.5f; v += 5.0f)
    {
        sprintf_s(buf, "%.0f", v);
        GText(dl, f, kGsSmall, GPolar(c, r - 15.0f * u, GDeg(s, v)), GCol(kText, a), buf, 0, u);
    }
    GDialIcon(ImVec2(c.x, c.y + r * 0.42f), u, GCol(kText, a), [&](ImVec2 q, ImU32 col) { GIconAir(dl, q, 10.5f * u, col); });
    GText(dl, f, kGsTiny, ImVec2(c.x, c.y - r * 0.36f), GCol(kMuted, a), "bar", 0, u);
    GSmallNeedle(dl, c, r, GDeg(s, st.airBar), u, a);
}

// A clock as on an older vehicle's dash: twelve hour marks (the 12, 3, 6 and 9 longer), no numbers, plain hour and
// minute hands, no second hand; the game's time of day (st.hour, none drawn below 0) and the outside temperature as a
// number in the lower half of the face.
static void GClock(ImDrawList *dl, const GaugeFonts &f, ImVec2 c, float r, float u, float a, const GaugeState &st)
{
    const GScale s = GScale{ 0.0f, 12.0f, -90.0f, 270.0f };
    if (s_open)
    {
        // the whole round, not a scale's 240 degrees
        const ImVec2 low(c.x + 1.0f * u, c.y + 1.3f * u);
        GArc(dl, low, r - 2.5f * u, 0.0f, 360.0f, 4.6f * u, GCol(0, 0, 0, 0.16f * a));
        GArc(dl, low, r - 2.5f * u, 0.0f, 360.0f, 2.4f * u, GCol(0, 0, 0, 0.5f * a));
        GArc(dl, c, r - 2.5f * u, 0.0f, 360.0f, 1.9f * u, GCol(kTick, a));
    }
    else GSmallDial(dl, c, r, u, a);
    GTicks(dl, c, r - 2.5f * u, s, 1.0f, 1, 4.0f, 4.0f, u, a);
    GTicks(dl, c, r - 2.5f * u, s, 3.0f, 1, 8.0f, 8.0f, u, a);
    char buf[16];
    sprintf_s(buf, "%.0f %s", st.fahrenheit ? st.outsideC * 1.8f + 32.0f : st.outsideC, st.fahrenheit ? "F" : "C");
    GText(dl, f, kGsSmall, ImVec2(c.x, c.y + r * 0.42f), GCol(kText, a), buf, 0, u);
    if (!(st.hour >= 0.0f)) return;
    const float h = fmodf(st.hour, 12.0f), minutes = (st.hour - floorf(st.hour)) * 60.0f;
    auto hand = [&](float deg, float len, float thick) {
        const ImVec2 tip = GPolar(c, len, deg);
        GLine(dl, ImVec2(c.x + 1.0f * u, c.y + 1.6f * u), ImVec2(tip.x + 1.0f * u, tip.y + 1.6f * u), GCol(0, 0, 0, 0.36f * a), thick * u);
        GLine(dl, c, tip, GCol(kNeedle, a), thick * u);
    };
    hand(-90.0f + h * 30.0f, r * 0.55f, 3.2f);
    hand(-90.0f + minutes * 6.0f, r * 0.85f, 2.0f);
    GHub(dl, c, 3.6f * u, u, a);
}

// The fuel range readout: a pump and "412 km", with a tenth below ten ("6.4 km"), or "---" until the mod has measured
// the consumption; with burn the fuel burn after it, "412 km  4.1 l/min" (US gallons an hour with mph, "1.1 gal/h").
static void GRange(ImDrawList *dl, const GaugeFonts &f, ImVec2 c, float u, float a, const GaugeState &st, bool burn)
{
    char buf[64];
    if (st.rangeKm < 0.0f) sprintf_s(buf, "--- %s", st.mph ? "mi" : "km");
    else
    {
        const float range = st.mph ? st.rangeKm / 1.609f : st.rangeKm;
        sprintf_s(buf, range < 9.95f ? "%.1f %s" : "%.0f %s", range, st.mph ? "mi" : "km");
    }
    if (burn)
    {
        char more[32];
        if (st.burnPerMin < 0.0f) sprintf_s(more, "  --- %s", st.mph ? "gal/h" : "l/min");
        else sprintf_s(more, "  %.1f %s", st.mph ? st.burnPerMin * 60.0f / 3.785f : st.burnPerMin, st.mph ? "gal/h" : "l/min");
        strcat_s(buf, more);
    }
    const float w = TextWidth(f, kGsReadout, buf, u), ih = 11.0f * u;
    const float x0 = c.x - (w + ih + 4.0f * u) * 0.5f;
    GDialIcon(ImVec2(x0 + ih * 0.42f, c.y), u, GCol(kMuted, a), [&](ImVec2 q, ImU32 col) { GIconFuel(dl, q, ih, col); });
    GText(dl, f, kGsReadout, ImVec2(x0 + ih + 4.0f * u, c.y), GCol(kText, a), buf, -1, u);
}

// The lamps as a block of columns x rows cells of size s, centred on c: handbrake, headlights, all wheel drive,
// differential lock, low range, engine damage, cold engine, tilt, oil temperature. A last row that is not full is
// centred.
static void GLamps(ImDrawList *dl, const GaugeFonts &f, ImVec2 c, int columns, float s, float gap, float u, float a, const GaugeState &st)
{
    const int count = st.tireLamp > 0 ? 10 : 9, rows = (count + columns - 1) / columns; // the tire lamp only with a tire mod that publishes its state
    const float pitch = s + gap;
    const ImVec2 o(c.x - (columns - 1) * pitch * 0.5f, c.y - (rows - 1) * pitch * 0.5f);
    for (int i = 0; i < count; i++)
    {
        const int row = i / columns, inRow = row == rows - 1 ? count - row * columns : columns;
        const ImVec2 p(o.x + (i % columns + (columns - inRow) * 0.5f) * pitch, o.y + row * pitch);
        const float h = s * 0.74f;
        ImU32 col;
        // the symbol at q in the colour k (drawn twice when see-through: a shadow, then the symbol)
        auto icon = [&](ImVec2 q, ImU32 k) {
            switch (i)
            {
            case 0: GIconHandbrake(dl, f, q, h, k, u); break;
            case 1: GIconBeam(dl, q, h, k); break;
            case 2: GIconAwd(dl, q, h, k); break;
            case 3: GIconDiff(dl, q, h, k); break;
            case 4: GIconLow(dl, f, q, h, k, u); break;
            case 5: GIconEngine(dl, q, h, k); break;
            case 6: GIconSnow(dl, q, h, k); break;
            case 7: GIconTilt(dl, q, h, st.rollDeg, k); break;
            case 8: GIconOil(dl, q, h, k); break;
            default: GIconTire(dl, f, q, h, st.tireLamp, k, u); break;
            }
        };
        switch (i)
        {
        case 0: col = GLampCell(dl, p, s, st.handbrake, kRed, u, a); break;
        case 1: col = GLampCell(dl, p, s, st.headlights, kGreen, u, a); break;
        case 2: col = GLampCell(dl, p, s, st.awd, kGreen, u, a); break;
        case 3: col = GLampCell(dl, p, s, st.diffLock, kAmber, u, a); break;
        case 4: col = GLampCell(dl, p, s, st.lowGear, kAmber, u, a); break;
        case 5: col = GLampCell(dl, p, s, st.damage, kAmber, u, a); break;
        case 6: col = GLampCell(dl, p, s, st.cold, kBlue, u, a); break;
        case 7: col = GLampCell(dl, p, s, st.tilt > 0, st.tilt >= 2 ? kRed : kAmber, u, a);
            if (st.tilt == 0) col = GCol(kGreen, a); // level: the truck is green, without a glow
            break;
        case 8: col = GLampCell(dl, p, s, st.oilHot, kRed, u, a); break;
        default: col = GLampCell(dl, p, s, st.tireLit, st.tireWarn ? kRed : st.tireLamp == 4 ? kGreen : kAmber, u, a); break;
        }
        if (GGlass()) icon(ImVec2(p.x + 1.0f * u, p.y + 1.3f * u), GCol(0, 0, 0, 0.55f * a));
        icon(p, col);
    }
}

// ---- the warning boxes: the clutch's and the engine's, the shape of the tire mod's TIRE DAMAGE box (its panel_draw.h,
// PanelWarning and PanelWarnOrigin), stacked below it on the HUD's stage ----
// The box in reference pixels of the tire mod's panel (an 800 wide panel on a 400 wide stage slot), the HUD's 1920 x 1080
// stage, and the tire warning's corner on it (52, 205: in line with the game's truck card's text, below it). Slot 0 is
// the tire mod's own; the clutch's box takes slot 1 and the engine's slot 2, 16 apart as the tire mod stacks its hint.
static const float kWarnW = 800.0f, kWarnH = 148.0f, kWarnGap = 16.0f;
static const float kWarnStageW = 1920.0f, kWarnStageH = 1080.0f, kWarnStageX = 52.0f, kWarnStageY = 205.0f, kWarnStageWidth = 400.0f;
enum WarnTextStyle { kWsTitle, kWsLine, kWsStyleCount };
static const FontStyle kWarnStyles[kWsStyleCount] = { { 26.0f, 1 }, { 26.0f, 0 } }; // the tire mod's kMode and kLabel
// The stage's scale on a screen this size (fitted in), and screen pixels per reference pixel of the box.
static float WarnStageScale(float w, float h) { return fminf(w / kWarnStageW, h / kWarnStageH); }
static float WarnUnit(float w, float h) { return WarnStageScale(w, h) * (kWarnStageWidth / kWarnW); }
// The top left corner of the box in slot (0 the tire mod's warning, 1 the clutch's, 2 the engine's): the stage centred top
// to bottom, the box at the same distance from the left edge on a wider screen.
static ImVec2 WarnOrigin(float w, float h, int slot)
{
    const float s = WarnStageScale(w, h);
    return ImVec2(s * kWarnStageX, (h - kWarnStageH * s) * 0.5f + s * kWarnStageY + slot * (kWarnH + kWarnGap) * WarnUnit(w, h));
}

// One box at `at` (u: screen pixels per reference pixel, a: opacity): a dark rounded body with a stripe down its left
// in the box's colour (red, or amber), the title, the line of advice, the number at the right and the bar along the
// bottom. f: fonts with kWarnStyles.
static void GWarnBox(ImDrawList *dl, const GaugeFonts &f, ImVec2 at, float u, float a, const WarnBox &b)
{
    if (b.state == kWarnNone) return;
    const float x0 = floorf(at.x), y0 = floorf(at.y);
    auto P = [&](float x, float y) { return ImVec2(x0 + x * u, y0 + y * u); };
    const ImU32 col = b.state == kWarnRed ? GCol(225, 58, 70, a) : GCol(236, 182, 48, a);
    dl->AddRectFilled(P(0, 0), P(kWarnW, kWarnH), GCol(5, 5, 5, 0.94f * a), 3.0f * u);
    dl->AddRectFilled(P(0, 0), P(12, kWarnH), col, 3.0f * u, ImDrawFlags_RoundCornersLeft);
    TextDraw(dl, f, kWsTitle, P(44, 50), GCol(255, 255, 255, a), b.title, -1, u);
    TextDraw(dl, f, kWsLine, P(44, 101), GCol(142, 150, 152, a), b.line, -1, u);
    const int shown = b.percent < 0 ? 0 : b.percent > 100 ? 100 : b.percent;
    char text[8];
    snprintf(text, sizeof text, "%d%%", shown);
    TextDraw(dl, f, kWsTitle, P(kWarnW - 44, 50), col, text, 1, u);
    dl->AddRectFilled(P(12, kWarnH - 9), P(kWarnW, kWarnH), GCol(33, 33, 33, a), 3.0f * u, ImDrawFlags_RoundCornersBottomRight);
    dl->AddRectFilled(P(12, kWarnH - 9), P(12 + (kWarnW - 12) * (float)shown / 100.0f, kWarnH), col);
}

// The cluster. origin: the strip's bottom centre (variant 0) or the block's bottom right corner (variant 1).
static void GaugeDraw(ImDrawList *dl, const GaugeFonts &f, ImVec2 origin, float u, float a, const GaugeState &st, const GaugeLayout &lay)
{
    s_open = lay.openArc;
    s_faceAlpha = s_open ? 0.0f : lay.faceAlpha;
    if (lay.variant == 0)
    {
        // a strip: [oil][temp][fuel] [SPEED] lamps [TACH] [boost][air][clock], the range under the fuel dial; with swapDials
        // the two big dials change places and the rest stays
        const float R = 72.0f * u, r = 42.0f * u, cy = origin.y - 96.0f * u, cx = origin.x;
        const float side = lay.swapDials ? -1.0f : 1.0f;
        GSpeedo(dl, f, ImVec2(cx - side * 98.0f * u, cy), R, u, a, st);
        GTacho(dl, f, ImVec2(cx + side * 98.0f * u, cy), R, u, a, st);
        if (lay.showLamps) GLamps(dl, f, ImVec2(cx, cy), 2, 20.0f * u, 3.5f * u, u, a, st);
        float x = cx - 98.0f * u - R - 10.0f * u - r;
        GFuel(dl, f, ImVec2(x, cy + 20.0f * u), r, u, a, st);
        GRange(dl, f, ImVec2(x, cy + 20.0f * u + r + 12.0f * u), u, a, st, lay.showBurn);
        if (lay.showTemp) { x -= 2.0f * r + 8.0f * u; GTemp(dl, f, ImVec2(x, cy + 20.0f * u), r, u, a, st, lay.oilGauge == kOilNeedle); }
        if (lay.oilGauge == kOilDial) { x -= 2.0f * r + 8.0f * u; GOil(dl, f, ImVec2(x, cy + 20.0f * u), r, u, a, st); }
        x = cx + 98.0f * u + R + 10.0f * u + r;
        if (lay.showBoost) { GBoost(dl, f, ImVec2(x, cy + 20.0f * u), r, u, a, st); x += 2.0f * r + 8.0f * u; }
        if (lay.showAir) { GAir(dl, f, ImVec2(x, cy + 20.0f * u), r, u, a, st); x += 2.0f * r + 8.0f * u; }
        if (lay.showClock) GClock(dl, f, ImVec2(x, cy + 20.0f * u), r, u, a, st);
    }
    else
    {
        // a block: speed and rpm with the lamps between them, the small dials in a row below, the range last
        const float R = 58.0f * u, r = 36.0f * u;
        const float y1 = origin.y - 196.0f * u, y2 = origin.y - 62.0f * u;
        GSpeedo(dl, f, ImVec2(origin.x - (lay.swapDials ? 76.0f : 238.0f) * u, y1), R, u, a, st);
        GTacho(dl, f, ImVec2(origin.x - (lay.swapDials ? 238.0f : 76.0f) * u, y1), R, u, a, st);
        if (lay.showLamps) GLamps(dl, f, ImVec2(origin.x - 157.0f * u, y1), 2, 17.5f * u, 3.5f * u, u, a, st);
        int n = 1 + (lay.oilGauge == kOilDial ? 1 : 0) + (lay.showTemp ? 1 : 0) + (lay.showBoost ? 1 : 0) + (lay.showAir ? 1 : 0) + (lay.showClock ? 1 : 0);
        const float pitch = 2.0f * r + 10.0f * u;
        // centred under the big dials, and moved left where a long row would pass the block's right edge
        float x = fminf(origin.x - 157.0f * u - (n - 1) * pitch * 0.5f, origin.x - (n - 1) * pitch - r);
        if (lay.oilGauge == kOilDial) { GOil(dl, f, ImVec2(x, y2), r, u, a, st); x += pitch; }
        if (lay.showTemp) { GTemp(dl, f, ImVec2(x, y2), r, u, a, st, lay.oilGauge == kOilNeedle); x += pitch; }
        GFuel(dl, f, ImVec2(x, y2), r, u, a, st);
        const float fuelX = x;
        x += pitch;
        if (lay.showBoost) { GBoost(dl, f, ImVec2(x, y2), r, u, a, st); x += pitch; }
        if (lay.showAir) { GAir(dl, f, ImVec2(x, y2), r, u, a, st); x += pitch; }
        if (lay.showClock) GClock(dl, f, ImVec2(x, y2), r, u, a, st);
        GRange(dl, f, ImVec2(fuelX, y2 + r + 12.0f * u), u, a, st, lay.showBurn);
    }
}

// ---- where the settings put the cluster (the mod and the preview place it through these) ----

// Screen pixels per reference pixel: the cluster keeps its share of the screen's height, times the ini's Scale.
static float GaugeUnit(float screenH, const GaugeSettings &s) { return screenH / 1080.0f * s.scale; }

static GaugeLayout GaugeLayoutOf(const GaugeSettings &s)
{
    GaugeLayout lay = {};
    lay.variant = s.layout == kLayoutBlock ? 1 : 0;
    lay.swapDials = s.swapDials;
    lay.showTemp = s.showTemp;
    lay.showBoost = s.showBoost;
    lay.showAir = s.showAir && s.tachClass != kClassScout; // scouts brake by hydraulics: no air to show
    lay.showLamps = s.showLamps;
    lay.showClock = s.showClock;
    lay.showBurn = s.showBurn;
    lay.oilGauge = s.oilGauge;
    lay.faceAlpha = s.style == kFaceTinted ? s.faceOpacity : 1.0f;
    lay.openArc = s.style == kFaceOpen;
    return lay;
}

// The strip stands 16 pixels (of a 1080p screen) above the middle of the bottom edge, the block 24 from the right
// edge and 16 above the bottom; the ini's offsets move them from there. The margins and offsets follow the screen's
// height, not the ini's Scale, so a cluster made larger stays where it is.
static ImVec2 GaugeOrigin(const GaugeSettings &s, float screenW, float screenH)
{
    const float k = screenH / 1080.0f;
    const float x = s.layout == kLayoutBlock ? screenW - 24.0f * k : screenW * 0.5f;
    return ImVec2(floorf(x + s.offsetX * k + 0.5f), floorf(screenH - 16.0f * k + s.offsetY * k + 0.5f));
}
