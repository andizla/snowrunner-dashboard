// gauge_draw.h: draws the gauge cluster: analogue dials in the style of a truck's dashboard (dark faces, white ticks
// and needles, red zones, simple icons), drawn crisp at the screen's resolution with Dear ImGui primitives only (own
// point arrays with AddPolyline / AddConvexPolyFilled: no path helpers, as in the SnowRunner Tire Inflation System,
// so the same code runs through ReShade's function table). Text is the game's font (gauge_font.h).
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
    { 6.5f, 1 },  // kGsLamp: text badges in the lamps
    { 7.0f, 0 },  // kGsUnit: small labels next to readouts
};

struct GaugeLayout
{
    int variant;                    // 0 = strip (bottom centre), 1 = block (bottom right)
    bool showTemp, showBoost, showAir, showLamps;
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
static const GRgb kFace = { 13, 14, 16 }, kBezel = { 150, 150, 152 }, kTick = { 232, 232, 232 }, kRed = { 214, 58, 58 }, kBlue = { 70, 135, 225 },
                  kNeedle = { 240, 240, 240 }, kHub = { 36, 37, 40 }, kDim = { 68, 70, 74 }, kAmber = { 245, 170, 40 }, kGreen = { 80, 210, 100 },
                  kText = { 225, 225, 225 }, kMuted = { 150, 152, 156 };

// Screen degrees: 0 = right, growing clockwise (y points down).
static ImVec2 GPolar(ImVec2 c, float r, float deg) { const float a = deg * kGPi / 180.0f; return ImVec2(c.x + cosf(a) * r, c.y + sinf(a) * r); }

static void GLine(ImDrawList *dl, ImVec2 a, ImVec2 b, ImU32 col, float thick)
{
    ImVec2 p[2] = { a, b };
    dl->AddPolyline(p, 2, col, 0, thick);
}

static void GArc(ImDrawList *dl, ImVec2 c, float r, float d0, float d1, float thick, ImU32 col)
{
    ImVec2 pts[128];
    const float sweep = d1 - d0;
    int n = (int)(fabsf(sweep) / 3.0f) + 2;
    if (n > 127) n = 127;
    for (int i = 0; i <= n; i++) pts[i] = GPolar(c, r, d0 + sweep * i / n);
    dl->AddPolyline(pts, n + 1, col, 0, thick);
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

static void GDialFace(ImDrawList *dl, ImVec2 c, float r, float u, float a)
{
    if (s_open)
    {
        // the reference's look: just the scale arc (the ticks start at it), shadowed, nothing behind it
        GArc(dl, ImVec2(c.x + 1.0f * u, c.y + 1.2f * u), r - 2.5f * u, 150.0f, 390.0f, 2.0f * u, GCol(0, 0, 0, 0.55f * a));
        GArc(dl, c, r - 2.5f * u, 150.0f, 390.0f, 1.8f * u, GCol(kTick, a));
        return;
    }
    if (GGlass())
    {
        // see-through: a shadowed bezel ring and a tinted face the game shows through
        dl->AddCircle(c, r + 2.4f * u, GCol(0, 0, 0, 0.45f * a), 72, 3.0f * u);
        if (s_faceAlpha > 0.0f) dl->AddCircleFilled(c, r, GCol(kFace, s_faceAlpha * a), 72);
        dl->AddCircle(c, r + 0.8f * u, GCol(kBezel, a), 72, 1.6f * u);
        return;
    }
    dl->AddCircleFilled(c, r + 3.5f * u, GCol(0, 0, 0, 0.40f * a), 72);   // soft edge against the game image
    dl->AddCircleFilled(c, r + 1.6f * u, GCol(kBezel, a), 72);            // bezel
    dl->AddCircleFilled(c, r, GCol(kFace, 0.97f * a), 72);                 // face
    dl->AddCircle(c, r * 0.96f, GCol(34, 35, 38, a), 72, 1.0f * u);        // a faint inner ring
}

// Dial text: with a drop shadow when the faces are see-through.
static void GText(ImDrawList *dl, const GaugeFonts &f, int style, ImVec2 at, ImU32 col, const char *s, int align, float u)
{
    if (GGlass()) TextDraw(dl, f, style, ImVec2(at.x + 1.2f * u, at.y + 1.4f * u), GCol(0, 0, 0, 0.7f * ((col >> 24) & 0xff) / 255.0f), s, align, u);
    TextDraw(dl, f, style, at, col, s, align, u);
}

// Major ticks every majorStep with minorPer - 1 minor ticks between them.
static void GTicks(ImDrawList *dl, ImVec2 c, float r, const GScale &s, float majorStep, int minorPer, float lenMaj, float lenMin, float u, float a)
{
    const int majors = (int)((s.v1 - s.v0) / majorStep + 0.5f);
    const ImVec2 sh(1.0f * u, 1.2f * u);
    for (int pass = GGlass() ? 0 : 1; pass < 2; pass++)
    {
        // pass 0: the shadows under see-through faces, pass 1: the ticks
        const ImVec2 o = pass ? ImVec2(0, 0) : sh;
        const ImU32 major = pass ? GCol(kTick, a) : GCol(0, 0, 0, 0.6f * a), minor = pass ? GCol(kTick, 0.75f * a) : GCol(0, 0, 0, 0.5f * a);
        for (int i = 0; i <= majors; i++)
        {
            const float v = s.v0 + i * majorStep;
            const float d = GDeg(s, v);
            GLine(dl, GPolar(c, r, d) + o, GPolar(c, r - lenMaj * u, d) + o, major, 2.2f * u);
            if (i < majors)
                for (int j = 1; j < minorPer; j++)
                {
                    const float dm = GDeg(s, v + majorStep * j / minorPer);
                    GLine(dl, GPolar(c, r, dm) + o, GPolar(c, r - lenMin * u, dm) + o, minor, 1.3f * u);
                }
        }
    }
}

// A zone (red, blue) as a thick arc just inside the rim.
static void GZone(ImDrawList *dl, ImVec2 c, float r, const GScale &s, float v0, float v1, float thick, float u, ImU32 col)
{
    GArc(dl, c, r - thick * u * 0.5f - 0.5f * u, GDeg(s, v0), GDeg(s, v1), thick * u, col);
}

// A tapered white needle with a dark counterweight tail and a drop shadow, then the hub.
static void GNeedle(ImDrawList *dl, ImVec2 c, float deg, float len, float wBase, float wTip, float u, float a, ImU32 col)
{
    const float rad = deg * kGPi / 180.0f;
    const ImVec2 dir(cosf(rad), sinf(rad)), side(-dir.y, dir.x);
    const ImVec2 tip = c + dir * len, tail = c - dir * (len * 0.2f), sh(1.0f * u, 1.5f * u);
    ImVec2 q[4] = { c + side * (wBase * u) + sh, tip + side * (wTip * u) + sh, tip - side * (wTip * u) + sh, c - side * (wBase * u) + sh };
    dl->AddConvexPolyFilled(q, 4, GCol(0, 0, 0, 0.45f * a));
    ImVec2 t[4] = { c + side * (wBase * 1.1f * u), tail + side * (wBase * 0.9f * u), tail - side * (wBase * 0.9f * u), c - side * (wBase * 1.1f * u) };
    dl->AddConvexPolyFilled(t, 4, GCol(88, 88, 90, a));
    ImVec2 p[4] = { c + side * (wBase * u), tip + side * (wTip * u), tip - side * (wTip * u), c - side * (wBase * u) };
    dl->AddConvexPolyFilled(p, 4, col);
}

static void GHub(ImDrawList *dl, ImVec2 c, float r, float u, float a)
{
    dl->AddCircleFilled(c, r, GCol(kHub, a), 32);
    dl->AddCircle(c, r, GCol(160, 160, 162, a), 32, 1.2f * u);
    dl->AddCircleFilled(c, r * 0.32f, GCol(125, 125, 128, a), 16);
}

// ---- icons, each centred on p with height h (screen pixels) ----

static void GIconFuel(ImDrawList *dl, ImVec2 p, float h, ImU32 col, float faceA)
{
    const float w = h * 0.62f;
    dl->AddRectFilled(ImVec2(p.x - w * 0.5f, p.y - h * 0.5f), ImVec2(p.x + w * 0.5f, p.y + h * 0.5f), col, h * 0.1f);
    dl->AddRectFilled(ImVec2(p.x - w * 0.32f, p.y - h * 0.36f), ImVec2(p.x + w * 0.32f, p.y - h * 0.1f), GCol(kFace, faceA), h * 0.04f);
    ImVec2 hose[4] = { ImVec2(p.x + w * 0.5f, p.y - h * 0.12f), ImVec2(p.x + w * 0.82f, p.y - h * 0.12f), ImVec2(p.x + w * 0.82f, p.y - h * 0.42f), ImVec2(p.x + w * 0.62f, p.y - h * 0.5f) };
    dl->AddPolyline(hose, 4, col, 0, h * 0.09f);
    dl->AddRectFilled(ImVec2(p.x - w * 0.64f, p.y + h * 0.42f), ImVec2(p.x + w * 0.64f, p.y + h * 0.5f), col);
}

static void GIconTemp(ImDrawList *dl, ImVec2 p, float h, ImU32 col)
{
    const float x = p.x - h * 0.08f;
    GLine(dl, ImVec2(x, p.y - h * 0.5f), ImVec2(x, p.y + h * 0.04f), col, h * 0.16f);
    dl->AddCircleFilled(ImVec2(x, p.y + h * 0.14f), h * 0.16f, col, 12);
    GLine(dl, ImVec2(x + h * 0.12f, p.y - h * 0.36f), ImVec2(x + h * 0.26f, p.y - h * 0.36f), col, h * 0.06f);
    GLine(dl, ImVec2(x + h * 0.12f, p.y - h * 0.2f), ImVec2(x + h * 0.26f, p.y - h * 0.2f), col, h * 0.06f);
    for (int k = 0; k < 2; k++)
    {
        const float y = p.y + h * (0.36f + 0.15f * k);
        ImVec2 w[7];
        for (int i = 0; i < 7; i++) w[i] = ImVec2(p.x - h * 0.46f + h * 0.92f * i / 6.0f, y + ((i & 1) ? -1.0f : 1.0f) * h * 0.045f);
        dl->AddPolyline(w, 7, col, 0, h * 0.08f);
    }
}

static void GIconTurbo(ImDrawList *dl, ImVec2 p, float h, ImU32 col)
{
    const ImVec2 c(p.x - h * 0.1f, p.y + h * 0.08f);
    dl->AddCircle(c, h * 0.3f, col, 24, h * 0.1f);
    dl->AddCircleFilled(c, h * 0.1f, col, 12);
    ImVec2 out[3] = { ImVec2(c.x, c.y - h * 0.3f), ImVec2(p.x + h * 0.46f, c.y - h * 0.3f), ImVec2(p.x + h * 0.46f, c.y - h * 0.05f) };
    dl->AddPolyline(out, 3, col, 0, h * 0.1f);
}

static void GIconAir(ImDrawList *dl, ImVec2 p, float h, ImU32 col)
{
    GArc(dl, p, h * 0.44f, 125.0f, 235.0f, h * 0.08f, col);
    GArc(dl, p, h * 0.44f, -55.0f, 55.0f, h * 0.08f, col);
    GLine(dl, ImVec2(p.x, p.y - h * 0.3f), ImVec2(p.x, p.y + h * 0.08f), col, h * 0.12f);
    dl->AddCircleFilled(ImVec2(p.x, p.y + h * 0.27f), h * 0.075f, col, 8);
}

static void GIconHandbrake(ImDrawList *dl, const GaugeFonts &f, ImVec2 p, float h, ImU32 col, float u)
{
    GArc(dl, p, h * 0.5f, 120.0f, 240.0f, h * 0.08f, col);
    GArc(dl, p, h * 0.5f, -60.0f, 60.0f, h * 0.08f, col);
    TextDraw(dl, f, kGsLamp, ImVec2(p.x, p.y), col, "P", 0, u);
}

static void GIconBeam(ImDrawList *dl, ImVec2 p, float h, ImU32 col)
{
    ImVec2 pts[18];
    int n = 0;
    const ImVec2 lc(p.x - h * 0.16f, p.y);
    for (int i = 0; i <= 16; i++) pts[n++] = GPolar(lc, h * 0.3f, 90.0f + 180.0f * i / 16.0f);
    dl->AddConvexPolyFilled(pts, n, col);
    for (int i = -1; i <= 1; i++)
        GLine(dl, ImVec2(p.x + h * 0.02f, p.y + i * h * 0.21f), ImVec2(p.x + h * 0.46f, p.y + i * h * 0.21f - h * 0.07f), col, h * 0.08f);
}

static void GIconEngine(ImDrawList *dl, ImVec2 p, float h, ImU32 col)
{
    const float r = h * 0.05f;
    dl->AddRectFilled(ImVec2(p.x - h * 0.3f, p.y - h * 0.16f), ImVec2(p.x + h * 0.34f, p.y + h * 0.24f), col, r);
    dl->AddRectFilled(ImVec2(p.x - h * 0.18f, p.y - h * 0.36f), ImVec2(p.x + h * 0.08f, p.y - h * 0.12f), col, r);
    dl->AddRectFilled(ImVec2(p.x - h * 0.46f, p.y - h * 0.02f), ImVec2(p.x - h * 0.28f, p.y + h * 0.16f), col);
    dl->AddRectFilled(ImVec2(p.x - h * 0.06f, p.y + h * 0.22f), ImVec2(p.x + h * 0.14f, p.y + h * 0.38f), col);
    dl->AddRectFilled(ImVec2(p.x + h * 0.32f, p.y - h * 0.06f), ImVec2(p.x + h * 0.46f, p.y + h * 0.08f), col);
}

static void GIconSnow(ImDrawList *dl, ImVec2 p, float h, ImU32 col)
{
    for (int i = 0; i < 3; i++)
    {
        const float d = 90.0f + 60.0f * i;
        GLine(dl, GPolar(p, h * 0.44f, d), GPolar(p, h * 0.44f, d + 180.0f), col, h * 0.08f);
        for (int s = 0; s < 2; s++)
        {
            const float arm = d + 180.0f * s;
            const ImVec2 m = GPolar(p, h * 0.27f, arm);
            GLine(dl, m, GPolar(m, h * 0.15f, arm + 40.0f), col, h * 0.07f);
            GLine(dl, m, GPolar(m, h * 0.15f, arm - 40.0f), col, h * 0.07f);
        }
    }
}

// A truck seen from behind, leaning with the roll, over a horizon line.
static void GIconTilt(ImDrawList *dl, ImVec2 p, float h, float rollDeg, ImU32 col)
{
    const float rad = rollDeg * kGPi / 180.0f, cs = cosf(rad), sn = sinf(rad);
    const ImVec2 o(p.x, p.y + h * 0.06f);
    auto R = [&](float x, float y) { return ImVec2(o.x + x * cs - y * sn, o.y + x * sn + y * cs); };
    const float w = h * 0.36f, t = h * 0.2f;
    ImVec2 body[4] = { R(-w, -t * 1.4f), R(w, -t * 1.4f), R(w, t * 0.5f), R(-w, t * 0.5f) };
    dl->AddConvexPolyFilled(body, 4, col);
    dl->AddCircleFilled(R(-w * 0.62f, t * 0.8f), h * 0.1f, col, 10);
    dl->AddCircleFilled(R(w * 0.62f, t * 0.8f), h * 0.1f, col, 10);
    GLine(dl, ImVec2(p.x - h * 0.5f, p.y + h * 0.47f), ImVec2(p.x + h * 0.5f, p.y + h * 0.47f), col, h * 0.06f);
}

// A lamp cell: dark when off, a coloured glow behind the icon when on. Returns the colour to draw the icon in.
// See-through: no cell, the icon floats with a shadow (drawn by GLamps), faint when off like the game's own icons.
static ImU32 GLampCell(ImDrawList *dl, ImVec2 p, float s, bool on, GRgb colOn, float u, float a)
{
    if (GGlass()) return on ? GCol(colOn, a) : GCol(kText, 0.4f * a);
    const ImVec2 a0(p.x - s * 0.5f, p.y - s * 0.5f), a1(p.x + s * 0.5f, p.y + s * 0.5f);
    dl->AddRectFilled(a0, a1, GCol(16, 17, 19, 0.92f * a), 3.0f * u);
    if (on) dl->AddRectFilled(a0, a1, GCol(colOn, 0.2f * a), 3.0f * u);
    dl->AddRect(a0, a1, GCol(56, 58, 62, a), 3.0f * u, 0, 1.0f * u);
    return on ? GCol(colOn, a) : GCol(kDim, a);
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
    GNeedle(dl, c, GDeg(s, v), r * 0.74f, 3.2f, 1.0f, u, a, GCol(kNeedle, a));
    GHub(dl, c, 7.0f * u, u, a);
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
    const float v = st.engineOn ? st.rpm / 100.0f : 0.0f;
    GNeedle(dl, c, GDeg(s, v), r * 0.74f, 3.2f, 1.0f, u, a, GCol(kNeedle, a));
    GHub(dl, c, 7.0f * u, u, a);
}

static void GSmallDial(ImDrawList *dl, ImVec2 c, float r, float u, float a) { GDialFace(dl, c, r, u, a); }

static void GSmallNeedle(ImDrawList *dl, ImVec2 c, float r, float deg, float u, float a)
{
    GNeedle(dl, c, deg, r * 0.56f, 2.4f, 0.8f, u, a, GCol(kNeedle, a));
    GHub(dl, c, 5.0f * u, u, a);
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
    GIconFuel(dl, ImVec2(c.x, c.y + r * 0.4f), 9.5f * u, GCol(kText, a), 0.97f * a);
    GSmallNeedle(dl, c, r, GDeg(s, st.fuelFrac), u, a);
}

static void GTemp(ImDrawList *dl, const GaugeFonts &f, ImVec2 c, float r, float u, float a, const GaugeState &st)
{
    const GScale s = GSweep(50.0f, 130.0f);
    GSmallDial(dl, c, r, u, a);
    GZone(dl, c, r - 2.5f * u, s, 50.0f, 60.0f, 4.0f, u, GCol(kBlue, a));
    GZone(dl, c, r - 2.5f * u, s, 118.0f, 130.0f, 4.0f, u, GCol(kRed, a));
    GTicks(dl, c, r - 2.5f * u, s, 40.0f, 2, 7.0f, 4.0f, u, a);
    char buf[16];
    for (float v = 50.0f; v <= 130.5f; v += 40.0f)
    {
        sprintf_s(buf, "%.0f", st.fahrenheit ? v * 1.8f + 32.0f : v);
        GText(dl, f, kGsSmall, GPolar(c, r - 15.0f * u, GDeg(s, v)), GCol(kText, a), buf, 0, u);
    }
    GIconTemp(dl, ImVec2(c.x, c.y + r * 0.4f), 9.5f * u, GCol(kText, a));
    GSmallNeedle(dl, c, r, GDeg(s, st.tempC), u, a);
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
    GIconTurbo(dl, ImVec2(c.x, c.y + r * 0.4f), 9.5f * u, GCol(kText, a));
    GText(dl, f, kGsTiny, ImVec2(c.x, c.y - r * 0.36f), GCol(kMuted, a), st.psi ? "psi" : "bar", 0, u);
    GSmallNeedle(dl, c, r, GDeg(s, st.psi ? st.boostBar * 14.5f : st.boostBar), u, a);
}

static void GAir(ImDrawList *dl, const GaugeFonts &f, ImVec2 c, float r, float u, float a, const GaugeState &st)
{
    const GScale s = GSweep(0.0f, 10.0f);
    GSmallDial(dl, c, r, u, a);
    GZone(dl, c, r - 2.5f * u, s, 0.0f, 5.5f, 4.0f, u, GCol(kRed, a));
    GTicks(dl, c, r - 2.5f * u, s, 5.0f, 5, 7.0f, 4.0f, u, a);
    char buf[16];
    for (float v = 0.0f; v <= 10.5f; v += 5.0f)
    {
        sprintf_s(buf, "%.0f", v);
        GText(dl, f, kGsSmall, GPolar(c, r - 15.0f * u, GDeg(s, v)), GCol(kText, a), buf, 0, u);
    }
    GIconAir(dl, ImVec2(c.x, c.y + r * 0.4f), 9.5f * u, GCol(kText, a));
    GText(dl, f, kGsTiny, ImVec2(c.x, c.y - r * 0.36f), GCol(kMuted, a), "bar", 0, u);
    GSmallNeedle(dl, c, r, GDeg(s, st.airBar), u, a);
}

// The fuel range readout: a pump and "412 km" (or "---" until the mod has measured the consumption).
static void GRange(ImDrawList *dl, const GaugeFonts &f, ImVec2 c, float u, float a, const GaugeState &st)
{
    char buf[32];
    if (st.rangeKm < 0.0f) sprintf_s(buf, "--- %s", st.mph ? "mi" : "km");
    else sprintf_s(buf, "%.0f %s", st.mph ? st.rangeKm / 1.609f : st.rangeKm, st.mph ? "mi" : "km");
    const float w = TextWidth(f, kGsReadout, buf, u), ih = 10.0f * u;
    const float x0 = c.x - (w + ih + 4.0f * u) * 0.5f;
    GIconFuel(dl, ImVec2(x0 + ih * 0.4f, c.y), ih, GCol(kMuted, a), 0.0f);
    GText(dl, f, kGsReadout, ImVec2(x0 + ih + 4.0f * u, c.y), GCol(kText, a), buf, -1, u);
}

// The lamps as a block of columns x rows cells of size s, centred on c: handbrake, headlights, AWD, diff lock,
// low gear, damage, cold, tilt.
static void GLamps(ImDrawList *dl, const GaugeFonts &f, ImVec2 c, int columns, float s, float gap, float u, float a, const GaugeState &st)
{
    const int count = 8, rows = (count + columns - 1) / columns;
    const float pitch = s + gap;
    const ImVec2 o(c.x - (columns - 1) * pitch * 0.5f, c.y - (rows - 1) * pitch * 0.5f);
    for (int i = 0; i < count; i++)
    {
        const ImVec2 p(o.x + (i % columns) * pitch, o.y + (i / columns) * pitch);
        const float h = s * 0.7f;
        ImU32 col;
        // the icon at q in colour c (drawn twice when see-through: a shadow, then the icon)
        auto icon = [&](ImVec2 q, ImU32 c) {
            switch (i)
            {
            case 0: GIconHandbrake(dl, f, q, h, c, u); break;
            case 1: GIconBeam(dl, q, h, c); break;
            case 2: TextDraw(dl, f, kGsLamp, q, c, "AWD", 0, u); break;
            case 3: TextDraw(dl, f, kGsLamp, q, c, "DIFF", 0, u); break;
            case 4: TextDraw(dl, f, kGsLamp, q, c, "LOW", 0, u); break;
            case 5: GIconEngine(dl, q, h, c); break;
            case 6: GIconSnow(dl, q, h, c); break;
            default: GIconTilt(dl, q, h, st.rollDeg, c); break;
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
        default: col = GLampCell(dl, p, s, st.tilt > 0, st.tilt >= 2 ? kRed : kAmber, u, a);
            if (st.tilt == 0) col = GCol(kGreen, a);
            break;
        }
        if (GGlass()) icon(ImVec2(p.x + 1.2f * u, p.y + 1.4f * u), GCol(0, 0, 0, 0.6f * a));
        icon(p, col);
    }
}

// The cluster. origin: the strip's bottom centre (variant 0) or the block's bottom right corner (variant 1).
static void GaugeDraw(ImDrawList *dl, const GaugeFonts &f, ImVec2 origin, float u, float a, const GaugeState &st, const GaugeLayout &lay)
{
    s_open = lay.openArc;
    s_faceAlpha = s_open ? 0.0f : lay.faceAlpha;
    if (lay.variant == 0)
    {
        // a strip: [temp][fuel] [SPEED] lamps [TACH] [boost][air], the range under the fuel dial
        const float R = 72.0f * u, r = 42.0f * u, cy = origin.y - 96.0f * u, cx = origin.x;
        GSpeedo(dl, f, ImVec2(cx - 98.0f * u, cy), R, u, a, st);
        GTacho(dl, f, ImVec2(cx + 98.0f * u, cy), R, u, a, st);
        if (lay.showLamps) GLamps(dl, f, ImVec2(cx, cy), 2, 21.0f * u, 4.0f * u, u, a, st);
        float x = cx - 98.0f * u - R - 10.0f * u - r;
        GFuel(dl, f, ImVec2(x, cy + 20.0f * u), r, u, a, st);
        GRange(dl, f, ImVec2(x, cy + 20.0f * u + r + 12.0f * u), u, a, st);
        if (lay.showTemp) { x -= 2.0f * r + 8.0f * u; GTemp(dl, f, ImVec2(x, cy + 20.0f * u), r, u, a, st); }
        x = cx + 98.0f * u + R + 10.0f * u + r;
        if (lay.showBoost) { GBoost(dl, f, ImVec2(x, cy + 20.0f * u), r, u, a, st); x += 2.0f * r + 8.0f * u; }
        if (lay.showAir) GAir(dl, f, ImVec2(x, cy + 20.0f * u), r, u, a, st);
    }
    else
    {
        // a block: speed and rpm with the lamps between them, the small dials in a row below, the range last
        const float R = 58.0f * u, r = 36.0f * u;
        const float y1 = origin.y - 196.0f * u, y2 = origin.y - 62.0f * u;
        GSpeedo(dl, f, ImVec2(origin.x - 238.0f * u, y1), R, u, a, st);
        GTacho(dl, f, ImVec2(origin.x - 76.0f * u, y1), R, u, a, st);
        if (lay.showLamps) GLamps(dl, f, ImVec2(origin.x - 157.0f * u, y1), 2, 19.0f * u, 4.0f * u, u, a, st);
        int n = 1 + (lay.showTemp ? 1 : 0) + (lay.showBoost ? 1 : 0) + (lay.showAir ? 1 : 0);
        const float pitch = 2.0f * r + 10.0f * u;
        float x = origin.x - 157.0f * u - (n - 1) * pitch * 0.5f;
        if (lay.showTemp) { GTemp(dl, f, ImVec2(x, y2), r, u, a, st); x += pitch; }
        GFuel(dl, f, ImVec2(x, y2), r, u, a, st);
        const float fuelX = x;
        x += pitch;
        if (lay.showBoost) { GBoost(dl, f, ImVec2(x, y2), r, u, a, st); x += pitch; }
        if (lay.showAir) GAir(dl, f, ImVec2(x, y2), r, u, a, st);
        GRange(dl, f, ImVec2(fuelX, y2 + r + 12.0f * u), u, a, st);
    }
}

// ---- where the settings put the cluster (the mod and the preview place it through these) ----

// Screen pixels per reference pixel: the cluster keeps its share of the screen's height, times the ini's Scale.
static float GaugeUnit(float screenH, const GaugeSettings &s) { return screenH / 1080.0f * s.scale; }

static GaugeLayout GaugeLayoutOf(const GaugeSettings &s)
{
    GaugeLayout lay = {};
    lay.variant = s.layout == kLayoutBlock ? 1 : 0;
    lay.showTemp = s.showTemp;
    lay.showBoost = s.showBoost;
    lay.showAir = s.showAir;
    lay.showLamps = s.showLamps;
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
