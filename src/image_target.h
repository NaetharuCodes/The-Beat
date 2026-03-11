#pragma once
#include <glad/glad.h>
#include <stb_image.h>
#include <stb_truetype.h>
#include <vector>
#include <string>
#include <random>
#include <algorithm>
#include <numeric>
#include <iostream>
#include <fstream>
#include <cmath>

// ── ImageTarget ───────────────────────────────────────────────────────────────
// Loads a PNG/JPG image OR renders text via stb_truetype, converts brightness
// to a weighted particle target distribution, and uploads those target positions
// to a GPU SSBO (binding=2).
class ImageTarget {
public:
    GLuint ssbo        = 0;
    int    targetCount = 0;
    std::string displayName;

    bool isLoaded() const { return ssbo != 0 && targetCount > 0; }

    // ── Load from image file ──────────────────────────────────────────────────
    bool load(const std::string& path, int particleCount, int screenW, int screenH) {
        unload();

        int w, h, ch;
        stbi_set_flip_vertically_on_load(0);
        unsigned char* pixels = stbi_load(path.c_str(), &w, &h, &ch, 4);
        if (!pixels) {
            std::cerr << "[Image] Failed to load: " << path << "\n";
            return false;
        }

        int totalPx = w * h;
        std::vector<float> brightness(totalPx);
        for (int i = 0; i < totalPx; i++) {
            float r = pixels[i*4+0] / 255.0f;
            float g = pixels[i*4+1] / 255.0f;
            float b = pixels[i*4+2] / 255.0f;
            brightness[i] = 0.299f*r + 0.587f*g + 0.114f*b;
            // Small floor so fully black areas still receive a trickle of particles
            brightness[i] = brightness[i] * 0.97f + 0.03f;
        }
        stbi_image_free(pixels);

        size_t sep = path.find_last_of("/\\");
        std::string name = (sep == std::string::npos) ? path : path.substr(sep+1);

        bool ok = buildTargets(brightness, w, h, particleCount, screenW, screenH, name);
        if (ok)
            std::cout << "[Image] Loaded: " << name
                      << "  (" << w << "x" << h << ")  targets=" << particleCount << "\n";
        return ok;
    }

    // ── Render text and use as particle target ────────────────────────────────
    // fontSize  : target glyph height in pixels (auto-scales if text overflows width)
    bool loadFromText(const std::string& text, int fontSize,
                      int particleCount, int screenW, int screenH)
    {
        if (text.empty()) return false;
        unload();

        // ── Load a system font ────────────────────────────────────────────────
        static const char* kFontPaths[] = {
            "C:/Windows/Fonts/arial.ttf",
            "C:/Windows/Fonts/calibri.ttf",
            "C:/Windows/Fonts/segoeui.ttf",
            "C:/Windows/Fonts/times.ttf",
            nullptr
        };

        std::vector<unsigned char> fontData;
        for (int i = 0; kFontPaths[i]; i++) {
            std::ifstream f(kFontPaths[i], std::ios::binary | std::ios::ate);
            if (!f) continue;
            auto sz = f.tellg(); f.seekg(0);
            fontData.resize((size_t)sz);
            f.read(reinterpret_cast<char*>(fontData.data()), sz);
            if (f) break;
            fontData.clear();
        }
        if (fontData.empty()) {
            std::cerr << "[Text] No system font found\n";
            return false;
        }

        stbtt_fontinfo font;
        if (!stbtt_InitFont(&font, fontData.data(), 0)) {
            std::cerr << "[Text] stbtt_InitFont failed\n";
            return false;
        }

        // ── Measure text at requested size ────────────────────────────────────
        float scale = stbtt_ScaleForPixelHeight(&font, (float)fontSize);

        int totalWidth = 0;
        for (int i = 0; i < (int)text.size(); i++) {
            int advance, lsb;
            stbtt_GetCodepointHMetrics(&font, (unsigned char)text[i], &advance, &lsb);
            totalWidth += (int)(advance * scale);
            if (i + 1 < (int)text.size())
                totalWidth += (int)(stbtt_GetCodepointKernAdvance(
                                    &font, text[i], text[i+1]) * scale);
        }

        // Auto-shrink if wider than screen (5% margin each side)
        int margin = (int)(screenW * 0.05f);
        int maxW   = screenW - 2 * margin;
        if (totalWidth > maxW) {
            scale      *= (float)maxW / (float)totalWidth;
            totalWidth  = maxW;
        }

        // ── Vertical metrics ──────────────────────────────────────────────────
        int ascent, descent, lineGap;
        stbtt_GetFontVMetrics(&font, &ascent, &descent, &lineGap);
        int textH   = (int)((ascent - descent) * scale);
        int baseline = screenH / 2 + (int)(ascent * scale) - textH / 2;

        // ── Render glyphs into a screen-sized canvas ──────────────────────────
        std::vector<unsigned char> canvas((size_t)screenW * screenH, 0);

        int x = (screenW - totalWidth) / 2;
        for (int i = 0; i < (int)text.size(); i++) {
            int c = (unsigned char)text[i];
            int bw, bh, bx, by;
            unsigned char* bmp = stbtt_GetCodepointBitmap(
                                    &font, 0, scale, c, &bw, &bh, &bx, &by);
            if (bmp) {
                for (int row = 0; row < bh; row++) {
                    for (int col = 0; col < bw; col++) {
                        int dx = x + bx + col;
                        int dy = baseline + by + row;
                        if (dx >= 0 && dx < screenW && dy >= 0 && dy < screenH) {
                            auto& dst = canvas[(size_t)dy * screenW + dx];
                            dst = (unsigned char)std::max((int)dst, (int)bmp[row * bw + col]);
                        }
                    }
                }
                stbtt_FreeBitmap(bmp, nullptr);
            }
            int advance, lsb;
            stbtt_GetCodepointHMetrics(&font, c, &advance, &lsb);
            x += (int)(advance * scale);
            if (i + 1 < (int)text.size())
                x += (int)(stbtt_GetCodepointKernAdvance(&font, c, text[i+1]) * scale);
        }

        // ── Build brightness (no floor — particles cluster only on glyph pixels)
        std::vector<float> brightness((size_t)screenW * screenH);
        for (size_t i = 0; i < brightness.size(); i++)
            brightness[i] = canvas[i] / 255.0f;

        bool ok = buildTargets(brightness, screenW, screenH,
                               particleCount, screenW, screenH,
                               "\"" + text + "\"");
        if (ok)
            std::cout << "[Text] Rendered: \"" << text
                      << "\"  sz=" << fontSize << "px  targets=" << particleCount << "\n";
        return ok;
    }

    // ── Upload pre-computed positions (procedural shapes) ─────────────────────
    // positions: interleaved [x0,y0, x1,y1, ...]; targetCount = size()/2
    bool loadFromShape(const std::vector<float>& positions, const std::string& name) {
        unload();
        if (positions.empty()) return false;
        glGenBuffers(1, &ssbo);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo);
        glBufferData(GL_SHADER_STORAGE_BUFFER,
                     (GLsizeiptr)(positions.size() * sizeof(float)),
                     positions.data(), GL_STATIC_DRAW);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
        targetCount = (int)(positions.size() / 2);
        displayName = name;
        std::cout << "[Shape] " << name << "  targets=" << targetCount << "\n";
        return true;
    }

    // ── Bind SSBO to slot 2 for the compute shader ────────────────────────────
    void bind() const {
        if (ssbo) glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, ssbo);
    }

    void unload() {
        if (ssbo) { glDeleteBuffers(1, &ssbo); ssbo = 0; }
        targetCount = 0;
        displayName.clear();
    }

    ~ImageTarget() { unload(); }

private:
    // ── Shared CDF sampling + SSBO upload ─────────────────────────────────────
    // brightness : per-pixel weight (w×h), already in [0,1]
    // Samples particleCount positions weighted by brightness, scaled from
    // (w,h) to (screenW,screenH), and uploads them to a new SSBO.
    bool buildTargets(const std::vector<float>& brightness,
                      int w, int h,
                      int particleCount,
                      int screenW, int screenH,
                      const std::string& name)
    {
        int totalPx = w * h;

        // Build CDF
        std::vector<float> cdf(totalPx);
        float running = 0.0f;
        for (int i = 0; i < totalPx; i++) {
            running += brightness[i];
            cdf[i]   = running;
        }
        float total = running;
        if (total <= 0.0f) {
            std::cerr << "[Target] Zero-weight brightness map for: " << name << "\n";
            return false;
        }

        // Sample target positions via inverse CDF
        std::vector<float> targets;
        targets.resize((size_t)particleCount * 2);

        std::mt19937 rng(0xBEA7BEEF);
        std::uniform_real_distribution<float> randSample(0.0f, total);
        std::uniform_real_distribution<float> jitter(-0.5f, 0.5f);

        float scaleX = (float)screenW / (float)w;
        float scaleY = (float)screenH / (float)h;

        for (int i = 0; i < particleCount; i++) {
            float r  = randSample(rng);
            int lo = 0, hi = totalPx - 1;
            while (lo < hi) {
                int mid = (lo + hi) >> 1;
                if (cdf[mid] < r) lo = mid + 1;
                else              hi = mid;
            }
            int px = lo % w;
            int py = lo / w;
            targets[i*2+0] = (px + 0.5f + jitter(rng)) * scaleX;
            targets[i*2+1] = (py + 0.5f + jitter(rng)) * scaleY;
        }

        // Upload to GPU
        glGenBuffers(1, &ssbo);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo);
        glBufferData(GL_SHADER_STORAGE_BUFFER,
                     (GLsizeiptr)(targets.size() * sizeof(float)),
                     targets.data(), GL_STATIC_DRAW);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

        targetCount = particleCount;
        displayName = name;
        return true;
    }
};
