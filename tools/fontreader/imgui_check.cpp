// Offline check: load a TTF from memory through Dear ImGui's own atlas builder (stb_truetype, the
// exact loader AMF uses), report every probe character's glyph and draw a line of text from the
// atlas into a PGM image. Built in a scratch folder against extern/imgui; never part of AMF.
//
//   imgui_check.exe <font.ttf> <px> <out.pgm> <utf8 text file>
#include "imgui.h"
#include "imgui_internal.h"
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

static std::vector<unsigned char> ReadAll(const char* p)
{
    std::vector<unsigned char> v;
    FILE* f = fopen(p, "rb");
    if (!f) return v;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    v.resize(n);
    fread(v.data(), 1, n, f);
    fclose(f);
    return v;
}

int main(int argc, char** argv)
{
    if (argc < 5) { printf("usage: imgui_check font.ttf px out.pgm text.txt\n"); return 2; }
    std::vector<unsigned char> ttf = ReadAll(argv[1]);
    float px = (float)atof(argv[2]);
    std::vector<unsigned char> txt = ReadAll(argv[4]);
    std::string text(txt.begin(), txt.end());
    if (ttf.empty()) { printf("cannot read font\n"); return 1; }

    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();

    ImFontGlyphRangesBuilder rb;
    rb.AddRanges(io.Fonts->GetGlyphRangesDefault());
    rb.AddRanges(io.Fonts->GetGlyphRangesCyrillic());
    rb.AddText(text.c_str());
    ImVector<ImWchar> ranges;
    rb.BuildRanges(&ranges);

    // the atlas takes ownership of an IM_ALLOC'd copy, as AMF would hand it the in-memory TTF
    void* mem = IM_ALLOC(ttf.size());
    memcpy(mem, ttf.data(), ttf.size());
    auto t0 = std::chrono::high_resolution_clock::now();
    ImFont* font = io.Fonts->AddFontFromMemoryTTF(mem, (int)ttf.size(), px, nullptr, ranges.Data);
    bool built = font && io.Fonts->Build();
    auto t1 = std::chrono::high_resolution_clock::now();
    printf("AddFontFromMemoryTTF %s, Build %s, %.1f ms\n", font ? "ok" : "FAILED", built ? "ok" : "FAILED",
        std::chrono::duration<double, std::milli>(t1 - t0).count());
    if (!built) return 1;

    unsigned char* pixels = nullptr;  // never null: GetTexData* writes through it (logic library 5917)
    int aw = 0, ah = 0;
    io.Fonts->GetTexDataAsAlpha8(&pixels, &aw, &ah);
    printf("atlas %dx%d, glyphs %d, ascent %.1f descent %.1f\n", aw, ah, font->Glyphs.Size, font->Ascent, font->Descent);

    // decode UTF-8, look up each glyph without fallback
    std::vector<unsigned int> cps;
    for (const char* s = text.c_str(); *s;) {
        unsigned int c = 0;
        int n = ImTextCharFromUtf8(&c, s, nullptr);
        if (n <= 0) break;
        s += n;
        if (c == '\r' || c == '\n') continue;
        cps.push_back(c);
    }
    int missing = 0;
    float width = 0;
    for (unsigned int c : cps) {
        const ImFontGlyph* g = font->FindGlyphNoFallback((ImWchar)c);
        if (!g) { printf("missing U+%04X\n", c); ++missing; continue; }
        width += g->AdvanceX;
    }
    printf("probe: %zu chars, %d missing\n", cps.size(), missing);

    int W = (int)width + 40, H = (int)(px * 1.6f) + 20;
    std::vector<unsigned char> img(W * H, 12);
    float x = 20, y = 10;
    for (unsigned int c : cps) {
        const ImFontGlyph* g = font->FindGlyphNoFallback((ImWchar)c);
        if (!g) continue;
        if (g->Visible) {
            // the quad (X0..X1) is in screen pixels; the UV rect is OversampleH times wider, so
            // sample the atlas the way the GPU would (box-average the oversampled texels)
            int gx0 = (int)(x + g->X0), gy0 = (int)(y + g->Y0);
            int gx1 = (int)(x + g->X1 + 0.999f), gy1 = (int)(y + g->Y1 + 0.999f);
            float qw = g->X1 - g->X0, qh = g->Y1 - g->Y0;
            for (int iy = gy0; iy < gy1; ++iy)
                for (int ix = gx0; ix < gx1; ++ix) {
                    if (ix < 0 || iy < 0 || ix >= W || iy >= H) continue;
                    float fu0 = (g->U0 + (ix - (x + g->X0)) / qw * (g->U1 - g->U0)) * aw;
                    float fu1 = (g->U0 + (ix + 1 - (x + g->X0)) / qw * (g->U1 - g->U0)) * aw;
                    float fv = (g->V0 + (iy + 0.5f - (y + g->Y0)) / qh * (g->V1 - g->V0)) * ah;
                    int v = (int)fv, ua = (int)fu0, ub = (int)fu1;
                    if (ub <= ua) ub = ua + 1;
                    int sum = 0, cnt = 0;
                    for (int u = ua; u < ub; ++u)
                        if (u >= 0 && u < aw && v >= 0 && v < ah) { sum += pixels[v * aw + u]; ++cnt; }
                    unsigned char a = (unsigned char)(cnt ? sum / cnt : 0);
                    unsigned char& d = img[iy * W + ix];
                    d = (unsigned char)(d + ((235 - d) * a) / 255);
                }
        }
        x += g->AdvanceX;
    }
    FILE* o = fopen(argv[3], "wb");
    fprintf(o, "P5\n%d %d\n255\n", W, H);
    fwrite(img.data(), 1, img.size(), o);
    fclose(o);
    printf("wrote %s\n", argv[3]);
    ImGui::DestroyContext();
    return missing ? 3 : 0;
}
