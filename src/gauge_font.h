// gauge_font.h: text in the game's own font (TT Lakes Compressed, read from the game folder at run time, never
// shipped) from a glyph atlas baked with stb_truetype. Shared by the mod and the offline preview, so both show the
// same pixels. The includer turns GaugeFonts::rgba into a texture and sets GaugeFonts::tex; until then, or without
// the font files, the current ImGui font stands in. The SnowRunner Tire Inflation System's way, for any
// table of text styles. Include after imgui.h, with IMGUI_DEFINE_MATH_OPERATORS.
#pragma once
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#pragma warning(push, 0)
#define STBRP_STATIC
#define STB_RECT_PACK_IMPLEMENTATION
#include "imstb_rectpack.h"
#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#include "imstb_truetype.h"
#pragma warning(pop)

// A kind of text: its cap height in reference pixels and its weight (0 Medium, 1 DemiBold).
struct FontStyle { float cap; int bold; };
static const int kFontMaxStyles = 12;

struct GaugeFonts
{
    unsigned char *file[2] = {};                   // the Medium and DemiBold .ttf files (stb_truetype reads them in place)
    stbtt_fontinfo info[2] = {};
    float capUnits[2] = {};                        // height of 'H' in font units
    short kern[2][95][95] = {};                    // kerning of printable ASCII pairs, font units
    const FontStyle *styles = nullptr;
    int styleCount = 0;
    stbtt_packedchar glyphs[kFontMaxStyles][95] = {};
    float unit[kFontMaxStyles] = {};               // font units to pixels, per style
    unsigned char *rgba = nullptr;                 // the baked atlas (white, coverage in alpha) until it is a texture
    int w = 0, h = 0;
    float u = 0.0f;                                // the scale the atlas was baked for
    ImTextureID tex = 0;                           // the atlas texture, set by the includer
};

static unsigned char *FontReadFile(const wchar_t *path)
{
    FILE *f = nullptr;
    if (_wfopen_s(&f, path, L"rb") != 0 || !f) return nullptr;
    unsigned char *data = nullptr;
    if (fseek(f, 0, SEEK_END) == 0)
    {
        const long n = ftell(f);
        if (n > 0 && fseek(f, 0, SEEK_SET) == 0 && (data = (unsigned char *)malloc((size_t)n)) != nullptr && fread(data, 1, (size_t)n, f) != (size_t)n)
        {
            free(data);
            data = nullptr;
        }
    }
    fclose(f);
    return data;
}

// Reads the two font files from dir (the game's folder) and takes the style table. False = no game font.
static bool FontsLoad(GaugeFonts &f, const wchar_t *dir, const FontStyle *styles, int count)
{
    f.styles = styles;
    f.styleCount = count > kFontMaxStyles ? kFontMaxStyles : count;
    static const wchar_t *names[2] = { L"TTLakesCompressed-Medium.ttf", L"TTLakesCompressed-DemiBold.ttf" };
    for (int i = 0; i < 2; i++)
    {
        wchar_t path[MAX_PATH];
        swprintf_s(path, L"%s\\%s", dir, names[i]);
        f.file[i] = FontReadFile(path);
        int x0, y0, x1, y1;
        if (!f.file[i] || !stbtt_InitFont(&f.info[i], f.file[i], stbtt_GetFontOffsetForIndex(f.file[i], 0)) ||
            !stbtt_GetCodepointBox(&f.info[i], 'H', &x0, &y0, &x1, &y1) || y1 <= 0)
            return false;
        f.capUnits[i] = (float)y1;
        for (int a = 0; a < 95; a++)
            for (int b = 0; b < 95; b++) f.kern[i][a][b] = (short)stbtt_GetCodepointKernAdvance(&f.info[i], 32 + a, 32 + b);
    }
    return true;
}

// Rasterises every style at scale u (screen pixels per reference pixel) into f.rgba, the smallest square atlas it
// fits. The old texture, if any, is the includer's to free first.
static bool FontsBake(GaugeFonts &f, float u)
{
    free(f.rgba);
    f.rgba = nullptr;
    f.tex = 0;
    if (!f.file[0] || !f.file[1] || !f.styles) return false;
    for (int size = 256; size <= 4096; size *= 2)
    {
        unsigned char *alpha = (unsigned char *)calloc((size_t)size * size, 1);
        if (!alpha) return false;
        stbtt_pack_context pc;
        bool ok = stbtt_PackBegin(&pc, alpha, size, size, 0, 1, nullptr) != 0;
        if (ok)
        {
            stbtt_PackSetOversampling(&pc, 2, 1);
            for (int i = 0; i < f.styleCount && ok; i++)
            {
                const int font = f.styles[i].bold;
                // the pixel height that gives this cap height
                const float px = f.styles[i].cap * u / (f.capUnits[font] * stbtt_ScaleForPixelHeight(&f.info[font], 1.0f));
                f.unit[i] = stbtt_ScaleForPixelHeight(&f.info[font], px);
                ok = stbtt_PackFontRange(&pc, f.file[font], 0, px, 32, 95, f.glyphs[i]) != 0;
            }
            stbtt_PackEnd(&pc);
        }
        if (ok && (f.rgba = (unsigned char *)malloc((size_t)size * size * 4)) != nullptr)
        {
            for (size_t p = 0, n = (size_t)size * size; p < n; p++)
            {
                f.rgba[p * 4 + 0] = f.rgba[p * 4 + 1] = f.rgba[p * 4 + 2] = 255;
                f.rgba[p * 4 + 3] = alpha[p];
            }
            f.w = f.h = size;
            f.u = u;
        }
        free(alpha);
        if (f.rgba) return true;
    }
    return false;
}

static int FontGlyph(unsigned char c) { return c >= 32 && c < 127 ? c - 32 : '?' - 32; }

static float TextWidth(const GaugeFonts &f, int style, const char *s, float u)
{
    if (!f.tex)
    {
        const float size = f.styles[style].cap * u * 1.45f;
        return ImGui::CalcTextSize(s).x * (size / ImGui::GetFontSize());
    }
    const int font = f.styles[style].bold;
    float w = 0.0f;
    int prev = -1;
    for (const unsigned char *p = (const unsigned char *)s; *p; p++)
    {
        const int g = FontGlyph(*p);
        if (prev >= 0) w += f.kern[font][prev][g] * f.unit[style];
        w += f.glyphs[style][g].xadvance;
        prev = g;
    }
    return w;
}

// Text with its capitals centred on at.y, centred on at.x (align 0) or starting / ending there (align -1 / 1).
// shear slants it like the game's HUD text (the top of each glyph moves right by shear x its height above the baseline).
static void TextDraw(ImDrawList *dl, const GaugeFonts &f, int style, ImVec2 at, ImU32 col, const char *s, int align, float u, float shear = 0.0f)
{
    const float w = TextWidth(f, style, s, u);
    float x = at.x - (align == 0 ? w * 0.5f : align > 0 ? w : 0.0f);
    if (!f.tex)
    {
        // the ImGui font: no bold, so a second pass a little to the right stands in for it
        const float size = f.styles[style].cap * u * 1.45f;
        const ImVec2 p(x, at.y - size * 0.5f);
        dl->AddText(ImGui::GetFont(), size, p, col, s);
        if (f.styles[style].bold) dl->AddText(ImGui::GetFont(), size, ImVec2(p.x + size * 0.035f, p.y), col, s);
        return;
    }
    const int font = f.styles[style].bold;
    const float base = floorf(at.y + f.styles[style].cap * u * 0.5f + 0.5f);
    const float iw = 1.0f / f.w, ih = 1.0f / f.h;
    dl->PushTexture(ImTextureRef(f.tex));
    int prev = -1;
    for (const unsigned char *p = (const unsigned char *)s; *p; p++)
    {
        const int g = FontGlyph(*p);
        const stbtt_packedchar &q = f.glyphs[style][g];
        if (prev >= 0) x += f.kern[font][prev][g] * f.unit[style];
        if (q.x1 > q.x0)
        {
            const ImVec2 p0(x + q.xoff, base + q.yoff), p1(x + q.xoff2, base + q.yoff2), uv0(q.x0 * iw, q.y0 * ih), uv1(q.x1 * iw, q.y1 * ih);
            dl->PrimReserve(6, 4);
            if (shear == 0.0f) dl->PrimRectUV(p0, p1, uv0, uv1, col);
            else
            {
                const float s0 = shear * (base - p0.y), s1 = shear * (base - p1.y);
                dl->PrimQuadUV(ImVec2(p0.x + s0, p0.y), ImVec2(p1.x + s0, p0.y), ImVec2(p1.x + s1, p1.y), ImVec2(p0.x + s1, p1.y),
                               uv0, ImVec2(uv1.x, uv0.y), uv1, ImVec2(uv0.x, uv1.y), col);
            }
        }
        x += q.xadvance;
        prev = g;
    }
    dl->PopTexture();
}
