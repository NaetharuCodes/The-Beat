#define NOMINMAX
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <windows.h>
#include <commdlg.h>
#include <algorithm>
#include <iostream>
#include <string>
#include <cmath>

#include "particle_system.h"
#include "audio_engine.h"
#include "image_target.h"
#include "sequencer.h"

// ── Globals ────────────────────────────────────────────────────────────────────
static ParticleSystem ps;
static AudioEngine    audio;
static ImageTarget    imageTarget;
static Sequencer      sequencer;

static int   windowW = 1600, windowH = 900;
static bool  paused  = false;

// Mouse
static double mouseX = 0, mouseY = 0;
static bool   leftDown = false;

// Image blend state
static bool  imageBlendingIn  = false;
static bool  imageBlendingOut = false;

// Hue accumulator driven by mid energy reactivity
static float hueAccum   = 0.0f;
// Base point size before audio modulation (set from UI slider each frame)
static float basePointSize = 2.0f;
// Panel visibility (Tab to toggle)
static bool showPanel = true;

// ── File dialog ────────────────────────────────────────────────────────────────
static std::string openFileDialog(const char* title, const char* filter) {
    char buf[MAX_PATH] = {};
    OPENFILENAMEA ofn  = {};
    ofn.lStructSize    = sizeof(ofn);
    ofn.hwndOwner      = NULL;
    ofn.lpstrFilter    = filter;
    ofn.lpstrFile      = buf;
    ofn.nMaxFile       = MAX_PATH;
    ofn.lpstrTitle     = title;
    ofn.Flags          = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    return GetOpenFileNameA(&ofn) ? std::string(buf) : std::string{};
}

// ── GLFW callbacks ─────────────────────────────────────────────────────────────
static void onFramebufferSize(GLFWwindow*, int w, int h) {
    glViewport(0, 0, w, h);
    windowW = w; windowH = h;
    ps.resize(w, h);
}

static void onKey(GLFWwindow* win, int key, int, int action, int) {
    if (action != GLFW_PRESS) return;
    switch (key) {
        case GLFW_KEY_ESCAPE: glfwSetWindowShouldClose(win, true); break;
        case GLFW_KEY_SPACE:  paused = !paused;                    break;
        case GLFW_KEY_R:      ps.resetParticles();                 break;
        case GLFW_KEY_C:      ps.clearTrail();                     break;
        case GLFW_KEY_F5:     ps.reloadShaders();                  break;
        case GLFW_KEY_TAB:    showPanel = !showPanel;              break;
    }
}

static void onMouseButton(GLFWwindow*, int btn, int action, int) {
    if (btn == GLFW_MOUSE_BUTTON_LEFT)
        leftDown = (action == GLFW_PRESS);
}

// ── UI Styling ─────────────────────────────────────────────────────────────────
static void applyStyle() {
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding    = 6.0f;
    s.FrameRounding     = 4.0f;
    s.GrabRounding      = 4.0f;
    s.ScrollbarRounding = 4.0f;
    s.WindowBorderSize  = 0.0f;
    s.FramePadding      = ImVec2(8, 4);
    s.ItemSpacing       = ImVec2(8, 5);
    s.WindowPadding     = ImVec2(12, 12);

    ImVec4* c = s.Colors;
    c[ImGuiCol_WindowBg]         = ImVec4(0.06f, 0.06f, 0.09f, 0.95f);
    c[ImGuiCol_Header]           = ImVec4(0.15f, 0.15f, 0.22f, 1.00f);
    c[ImGuiCol_HeaderHovered]    = ImVec4(0.22f, 0.22f, 0.36f, 1.00f);
    c[ImGuiCol_FrameBg]          = ImVec4(0.12f, 0.12f, 0.17f, 1.00f);
    c[ImGuiCol_FrameBgHovered]   = ImVec4(0.18f, 0.18f, 0.26f, 1.00f);
    c[ImGuiCol_SliderGrab]       = ImVec4(0.30f, 0.65f, 1.00f, 1.00f);
    c[ImGuiCol_SliderGrabActive] = ImVec4(0.50f, 0.85f, 1.00f, 1.00f);
    c[ImGuiCol_Button]           = ImVec4(0.18f, 0.38f, 0.68f, 1.00f);
    c[ImGuiCol_ButtonHovered]    = ImVec4(0.28f, 0.52f, 0.88f, 1.00f);
    c[ImGuiCol_ButtonActive]     = ImVec4(0.12f, 0.28f, 0.52f, 1.00f);
    c[ImGuiCol_Text]             = ImVec4(0.90f, 0.90f, 0.95f, 1.00f);
    c[ImGuiCol_TitleBg]          = ImVec4(0.04f, 0.04f, 0.07f, 1.00f);
    c[ImGuiCol_TitleBgActive]    = ImVec4(0.07f, 0.07f, 0.12f, 1.00f);
    c[ImGuiCol_CheckMark]        = ImVec4(0.30f, 0.65f, 1.00f, 1.00f);
    c[ImGuiCol_Separator]        = ImVec4(0.20f, 0.20f, 0.30f, 1.00f);
    c[ImGuiCol_PlotHistogram]    = ImVec4(0.20f, 0.60f, 1.00f, 1.00f);
}

// ── Animated VU bar ────────────────────────────────────────────────────────────
static void vuBar(float value, ImVec4 color, float w = -1.0f, float h = 10.0f) {
    ImVec2 pos  = ImGui::GetCursorScreenPos();
    float  avail = (w < 0) ? ImGui::GetContentRegionAvail().x : w;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(pos, ImVec2(pos.x + avail, pos.y + h),
                      IM_COL32(25, 25, 40, 255), 3.0f);
    float fill = avail * std::clamp(value, 0.0f, 1.0f);
    if (fill > 2.0f) {
        ImVec4 bright = { std::min(color.x*1.6f,1.0f),
                          std::min(color.y*1.6f,1.0f),
                          std::min(color.z*1.6f,1.0f), 1.0f };
        dl->AddRectFilledMultiColor(
            pos, ImVec2(pos.x + fill, pos.y + h),
            ImGui::ColorConvertFloat4ToU32(color),
            ImGui::ColorConvertFloat4ToU32(bright),
            ImGui::ColorConvertFloat4ToU32(bright),
            ImGui::ColorConvertFloat4ToU32(color));
    }
    ImGui::Dummy(ImVec2(avail, h));
}

// ── Image helpers ──────────────────────────────────────────────────────────────
static void loadImageAndTargets(const std::string& path) {
    if (path.empty()) return;
    if (ps.params.targetStrength > 0.0f) {
        imageBlendingOut = true;
        imageBlendingIn  = false;
    }
    if (imageTarget.load(path, ps.params.count, windowW, windowH)) {
        ps.setTargetSSBO(imageTarget.ssbo, imageTarget.targetCount);
        imageBlendingIn  = true;
        imageBlendingOut = false;
    }
}

static void loadTextAndTargets(const std::string& text, int fontSize) {
    if (text.empty()) return;
    if (ps.params.targetStrength > 0.0f) {
        imageBlendingOut = true;
        imageBlendingIn  = false;
    }
    if (imageTarget.loadFromText(text, fontSize, ps.params.count, windowW, windowH)) {
        ps.setTargetSSBO(imageTarget.ssbo, imageTarget.targetCount);
        imageBlendingIn  = true;
        imageBlendingOut = false;
    }
}

static void releaseImage() {
    if (ps.params.targetStrength > 0.0f || imageTarget.isLoaded()) {
        imageBlendingOut = true;
        imageBlendingIn  = false;
    }
}

// ── Control panel ──────────────────────────────────────────────────────────────
static void drawPanel(ImGuiIO& io, const AudioAnalysis& an) {
    const float W      = windowW * 0.5f;
    const float TAB_W  = 24.0f;
    const float TAB_H  = 64.0f;

    // Animate slide: 0 = fully open, 1 = fully closed
    static float slide = 0.0f;
    float target = showPanel ? 0.0f : 1.0f;
    slide += (target - slide) * std::min(1.0f, io.DeltaTime * 12.0f);

    float panelX = -W * slide;   // panel left edge (goes negative when closing)
    float tabX   = panelX + W;   // tab sits at the panel's right edge

    // ── Pull tab (always visible) ──────────────────────────────────────────────
    ImGui::SetNextWindowPos (ImVec2(tabX, (float)windowH * 0.5f - TAB_H * 0.5f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(TAB_W, TAB_H), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.65f);
    ImGui::Begin("##tab", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize    |
        ImGuiWindowFlags_NoMove     | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoScrollWithMouse);
    if (ImGui::Button(showPanel ? "<" : ">", ImVec2(TAB_W - 10.0f, TAB_H - 10.0f)))
        showPanel = !showPanel;
    ImGui::End();

    // Don't render panel contents when fully off-screen
    if (slide >= 0.999f) return;

    ImGui::SetNextWindowPos (ImVec2(panelX, 0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(W, (float)windowH), ImGuiCond_Always);
    ImGui::Begin("##panel", nullptr,
        ImGuiWindowFlags_NoTitleBar  | ImGuiWindowFlags_NoResize  |
        ImGuiWindowFlags_NoMove      | ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    // ── Title ─────────────────────────────────────────────────────────────────
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 4);
    ImGui::TextColored(ImVec4(0.3f,0.7f,1.0f,1.0f), "THE  BEAT");
    ImGui::TextDisabled("Music Particle Visualizer");
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    float bw = (W - 36.0f) * 0.5f - 4.0f;

    // ── AUDIO ─────────────────────────────────────────────────────────────────
    if (ImGui::CollapsingHeader("Audio", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Spacing();

        if (audio.hasFile())
            ImGui::TextColored(ImVec4(0.5f,1.0f,0.6f,1.0f),
                               "%s", audio.getDisplayName().c_str());
        else
            ImGui::TextDisabled("No file loaded");

        if (ImGui::Button("Browse Audio...", ImVec2(-1, 0))) {
            auto p = openFileDialog("Open Audio File",
                "Audio\0*.mp3;*.wav;*.flac;*.ogg\0All Files\0*.*\0");
            if (!p.empty()) {
                audio.loadFile(p);
                sequencer.resetTriggers();
            }
        }
        ImGui::Spacing();

        // Transport
        float tbW = (W - 36.0f) / 3.0f - 2.0f;
        bool playing = audio.isPlaying();

        if (!playing) {
            if (ImGui::Button(" Play ", ImVec2(tbW, 28))) audio.play();
        } else {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.55f,0.25f,0.05f,1.0f));
            if (ImGui::Button("Pause", ImVec2(tbW, 28))) audio.pause();
            ImGui::PopStyleColor();
        }
        ImGui::SameLine();
        if (ImGui::Button(" Stop ", ImVec2(tbW, 28))) {
            audio.stop();
            sequencer.resetTriggers();
        }
        ImGui::SameLine();
        // Beat flash button
        float bp = an.beatPulse;
        ImGui::PushStyleColor(ImGuiCol_Button,
            ImVec4(0.08f + bp*0.55f, 0.12f + bp*0.25f, 0.20f + bp*0.80f, 1.0f));
        ImGui::Button("BEAT", ImVec2(tbW, 28));
        ImGui::PopStyleColor();

        ImGui::Spacing();

        // Progress
        float dur  = audio.getDuration();
        float pos  = audio.getPosition();
        float prog = (dur > 0.0f) ? pos / dur : 0.0f;
        char tstr[32];
        snprintf(tstr, sizeof(tstr), "%02d:%02d / %02d:%02d",
                 (int)pos/60,(int)pos%60,(int)dur/60,(int)dur%60);
        ImGui::TextDisabled("%s", tstr);
        ImGui::SetNextItemWidth(-1);
        if (ImGui::SliderFloat("##prog", &prog, 0.0f, 1.0f, ""))
            if (ImGui::IsItemActive()) {
                audio.seekTo(prog * dur);
                sequencer.resetTriggers();
            }

        ImGui::Spacing();
        float vol = audio.getVolume();
        ImGui::TextDisabled("Volume");
        ImGui::SetNextItemWidth(-1);
        if (ImGui::SliderFloat("##vol", &vol, 0.0f, 1.0f, "%.2f"))
            audio.setVolume(vol);

        ImGui::TextDisabled("Sensitivity");
        ImGui::SetNextItemWidth(-1);
        ImGui::SliderFloat("##sens", &audio.sensitivity, 1.0f, 20.0f, "%.1f",
                           ImGuiSliderFlags_Logarithmic);

        ImGui::Spacing();
        if (an.bpm > 5.0f)
            ImGui::TextColored(ImVec4(0.5f,0.9f,1.0f,1.0f), "%.0f BPM", an.bpm);
        else
            ImGui::TextDisabled("BPM: detecting...");

        ImGui::Spacing();

        // VU meters — Bass / Mid / High
        ImGui::TextDisabled("Bass          Mid           High");
        float mw = (W - 48.0f) / 3.0f;
        vuBar(an.bass,  ImVec4(1.0f, 0.35f, 0.05f, 1.0f), mw);
        ImGui::SameLine(0, 4);
        vuBar(an.mid,   ImVec4(0.15f, 0.85f, 0.25f, 1.0f), mw);
        ImGui::SameLine(0, 4);
        vuBar(an.high,  ImVec4(0.25f, 0.45f, 1.00f, 1.0f), mw);

        ImGui::Spacing();
    }

    // ── PARTICLES ─────────────────────────────────────────────────────────────
    if (ImGui::CollapsingHeader("Particles", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Spacing();

        ImGui::TextDisabled("Count  (ctrl+click to type)");
        int nc = ps.params.count;
        ImGui::SetNextItemWidth(-1);
        if (ImGui::SliderInt("##cnt", &nc, 1000, 100000000, "%d",
                             ImGuiSliderFlags_Logarithmic))
            ps.params.count = nc;
        if (ImGui::IsItemDeactivatedAfterEdit())
            ps.setCount(ps.params.count);

        ImGui::TextDisabled("Field Strength");
        ImGui::SetNextItemWidth(-1);
        ImGui::SliderFloat("##fstr", &ps.params.fieldStrength, 0.0f, 1.0f, "%.3f");

        ImGui::TextDisabled("Field Scale");
        ImGui::SetNextItemWidth(-1);
        ImGui::SliderFloat("##fscl", &ps.params.noiseScale, 0.0005f, 0.008f, "%.4f",
                           ImGuiSliderFlags_Logarithmic);

        ImGui::TextDisabled("Evolution Speed");
        ImGui::SetNextItemWidth(-1);
        ImGui::SliderFloat("##evol", &ps.params.evolutionSpeed, 0.005f, 0.5f, "%.3f",
                           ImGuiSliderFlags_Logarithmic);

        ImGui::TextDisabled("Max Speed");
        ImGui::SetNextItemWidth(-1);
        ImGui::SliderFloat("##mspd", &ps.params.maxSpeed, 0.5f, 12.0f, "%.1f");

        ImGui::TextDisabled("Damping");
        ImGui::SetNextItemWidth(-1);
        ImGui::SliderFloat("##damp", &ps.params.damping, 0.85f, 1.0f, "%.3f");

        ImGui::TextDisabled("Cohesion (+) / Separation (-)");
        ImGui::SetNextItemWidth(-1);
        ImGui::SliderFloat("##mass", &ps.params.particleMass, -1.0f, 1.0f, "%.3f");

        ImGui::Spacing();
    }

    // ── RENDERING ─────────────────────────────────────────────────────────────
    if (ImGui::CollapsingHeader("Rendering")) {
        ImGui::Spacing();

        ImGui::TextDisabled("Point Size");
        ImGui::SetNextItemWidth(-1);
        if (ImGui::SliderFloat("##psz", &basePointSize, 1.0f, 8.0f, "%.1f"))
            ps.params.pointSize = basePointSize;

        ImGui::TextDisabled("Trail  (lower = longer trails)");
        ImGui::SetNextItemWidth(-1);
        ImGui::SliderFloat("##trail", &ps.params.trailFade, 0.005f, 0.3f, "%.3f",
                           ImGuiSliderFlags_Logarithmic);

        ImGui::TextDisabled("Brightness");
        ImGui::SetNextItemWidth(-1);
        ImGui::SliderFloat("##brt", &ps.params.brightness, 0.1f, 1.0f, "%.2f");

        ImGui::TextDisabled("Density Radius (px)");
        ImGui::SetNextItemWidth(-1);
        ImGui::SliderFloat("##dr", &ps.params.densityRadius, 5.0f, 120.0f, "%.0f");

        ImGui::Spacing();
        ImGui::TextDisabled("Color Mode");
        const char* cm[] = {"Density", "Fire", "Spectrum", "Cool"};
        ImGui::SetNextItemWidth(-1);
        ImGui::Combo("##cm", &ps.params.colorMode, cm, 4);

        ImGui::TextDisabled("Hue Offset  (audio-driven when Mid React > 0)");
        ImGui::SetNextItemWidth(-1);
        ImGui::SliderFloat("##hue", &hueAccum, 0.0f, 1.0f, "%.3f");

        ImGui::Spacing();
    }

    // ── IMAGE ─────────────────────────────────────────────────────────────────
    if (ImGui::CollapsingHeader("Image", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Spacing();

        if (imageTarget.isLoaded())
            ImGui::TextColored(ImVec4(0.5f,1.0f,0.6f,1.0f),
                               "%s", imageTarget.displayName.c_str());
        else
            ImGui::TextDisabled("No image loaded");

        if (ImGui::Button("Load Image", ImVec2(bw, 0))) {
            auto p = openFileDialog("Open Image",
                "Images\0*.png;*.jpg;*.jpeg;*.bmp;*.tga\0All Files\0*.*\0");
            loadImageAndTargets(p);
        }
        ImGui::SameLine();
        if (ImGui::Button("Release", ImVec2(bw, 0))) releaseImage();

        ImGui::Spacing();
        ImGui::TextDisabled("Blend  %.0f%%", ps.params.targetStrength * 100.0f);
        ImGui::ProgressBar(ps.params.targetStrength, ImVec2(-1, 6));

        ImGui::Spacing();
        ImGui::TextDisabled("Blend In  (sec)");
        ImGui::SetNextItemWidth(-1);
        ImGui::SliderFloat("##bi", &ps.params.blendInTime,  0.2f, 12.0f, "%.1f");

        ImGui::TextDisabled("Blend Out  (sec)");
        ImGui::SetNextItemWidth(-1);
        ImGui::SliderFloat("##bo", &ps.params.blendOutTime, 0.2f, 6.0f,  "%.1f");

        ImGui::TextDisabled("Target Damping  (settling speed)");
        ImGui::SetNextItemWidth(-1);
        ImGui::SliderFloat("##td", &ps.params.targetDamping, 0.80f, 0.99f, "%.3f");

        ImGui::Spacing();
    }

    // ── AUDIO REACTIVITY ──────────────────────────────────────────────────────
    if (ImGui::CollapsingHeader("Audio Reactivity")) {
        ImGui::Spacing();
        ImGui::TextDisabled("0 = off   1 = full   >1 = exaggerated");
        ImGui::Spacing();

        ImGui::TextDisabled("Beat -> Velocity Kick  (most physical)");
        ImGui::SetNextItemWidth(-1);
        ImGui::SliderFloat("##r0", &ps.params.reactBeatKick,  0.0f, 10.0f, "%.2f");

        ImGui::TextDisabled("Beat -> Field Burst");
        ImGui::SetNextItemWidth(-1);
        ImGui::SliderFloat("##r1", &ps.params.reactBeatField, 0.0f, 10.0f, "%.2f");

        ImGui::TextDisabled("Beat -> Point Size Pulse");
        ImGui::SetNextItemWidth(-1);
        ImGui::SliderFloat("##r2", &ps.params.reactBeatSize,  0.0f, 10.0f, "%.2f");

        ImGui::TextDisabled("Bass -> Point Size");
        ImGui::SetNextItemWidth(-1);
        ImGui::SliderFloat("##r3", &ps.params.reactBassSize,  0.0f, 10.0f, "%.2f");

        ImGui::TextDisabled("Mid  -> Hue Rotation Speed");
        ImGui::SetNextItemWidth(-1);
        ImGui::SliderFloat("##r4", &ps.params.reactMidColor,  0.0f, 10.0f, "%.2f");

        ImGui::TextDisabled("High -> Evolution Speed");
        ImGui::SetNextItemWidth(-1);
        ImGui::SliderFloat("##r5", &ps.params.reactHighEvol,  0.0f, 10.0f, "%.2f");

        ImGui::Spacing();
    }

    // ── SEQUENCER ─────────────────────────────────────────────────────────────
    if (ImGui::CollapsingHeader("Sequencer")) {
        ImGui::Spacing();
        ImGui::TextDisabled("Load at / Unload at = absolute seconds into playback.");
        ImGui::TextDisabled("Set Unload to 0 to hold until next event or forever.");
        ImGui::Spacing();

        static float seqLoadTime   = 0.0f;
        static float seqUnloadTime = 0.0f;
        static char  seqPath[MAX_PATH] = {};
        static float seqBI = 1.5f, seqBO = 1.0f;
        static int   seqMode = 0;          // 0 = Image, 1 = Text
        static char  seqText[256]  = {};
        static int   seqFontSize   = 120;

        // ── Mode toggle ───────────────────────────────────────────────────────
        ImGui::TextDisabled("Type");
        ImGui::SameLine();
        ImGui::RadioButton("Image##seqmode", &seqMode, 0);
        ImGui::SameLine();
        ImGui::RadioButton("Text##seqmode",  &seqMode, 1);
        ImGui::Spacing();

        ImGui::TextDisabled("Load at (s)");
        ImGui::SetNextItemWidth(-1);
        ImGui::InputFloat("##st", &seqLoadTime, 1.0f, 10.0f, "%.1f");
        seqLoadTime = std::max(seqLoadTime, 0.0f);

        ImGui::TextDisabled("Unload at (s)  [0 = never unload]");
        ImGui::SetNextItemWidth(-1);
        ImGui::InputFloat("##su", &seqUnloadTime, 1.0f, 10.0f, "%.1f");
        seqUnloadTime = std::max(seqUnloadTime, 0.0f);

        ImGui::TextDisabled("Blend In (s)");
        ImGui::SetNextItemWidth(-1);
        ImGui::InputFloat("##sbi", &seqBI, 0.1f, 1.0f, "%.1f");
        seqBI = std::max(seqBI, 0.0f);

        ImGui::TextDisabled("Blend Out (s)");
        ImGui::SetNextItemWidth(-1);
        ImGui::InputFloat("##sbo", &seqBO, 0.1f, 1.0f, "%.1f");
        seqBO = std::max(seqBO, 0.0f);

        if (seqMode == 0) {
            // ── Image mode ────────────────────────────────────────────────────
            if (ImGui::Button("Browse...", ImVec2(-1, 0))) {
                auto p = openFileDialog("Sequencer Image",
                    "Images\0*.png;*.jpg;*.jpeg;*.bmp;*.tga\0All\0*.*\0");
                if (!p.empty()) strncpy_s(seqPath, MAX_PATH, p.c_str(), MAX_PATH-1);
            }
            if (seqPath[0]) {
                std::string sp(seqPath);
                size_t sep2 = sp.find_last_of("/\\");
                std::string fn2 = (sep2 == std::string::npos) ? sp : sp.substr(sep2 + 1);
                ImGui::TextColored(ImVec4(0.6f,0.9f,0.6f,1.0f), "  %s", fn2.c_str());
            } else {
                ImGui::TextDisabled("  no image selected");
            }
            bool canAdd = seqPath[0] != '\0';
            if (ImGui::Button("Add to Sequence", ImVec2(-1, 0)) && canAdd) {
                sequencer.addEvent(seqLoadTime, seqPath, seqUnloadTime, seqBI, seqBO);
                seqPath[0] = '\0';
            }
        } else {
            // ── Text mode ─────────────────────────────────────────────────────
            ImGui::TextDisabled("Text");
            ImGui::SetNextItemWidth(-1);
            ImGui::InputText("##stxt", seqText, sizeof(seqText));

            ImGui::TextDisabled("Font Size (px)");
            ImGui::SetNextItemWidth(-1);
            ImGui::SliderInt("##sfsz", &seqFontSize, 24, 400);

            bool canAdd = seqText[0] != '\0';
            if (ImGui::Button("Add to Sequence", ImVec2(-1, 0)) && canAdd) {
                sequencer.addTextEvent(seqLoadTime, seqText, seqFontSize,
                                       seqUnloadTime, seqBI, seqBO);
                seqText[0] = '\0';
            }
        }

        // Event list
        ImGui::Spacing();
        auto& evts = sequencer.events;
        float lh = std::max(std::min((float)evts.size() * 21.0f + 8.0f, 150.0f), 30.0f);
        ImGui::BeginChild("##seqlist", ImVec2(-1, lh), true);
        for (int i = 0; i < (int)evts.size(); i++) {
            ImGui::PushID(i);
            auto& e = evts[i];

            // Build a short display label
            std::string label;
            if (e.isText) {
                label = "T: \"";
                label += e.text.size() <= 18 ? e.text : e.text.substr(0,18) + "...";
                label += "\"";
            } else {
                size_t sep = e.imagePath.find_last_of("/\\");
                label = (sep==std::string::npos) ? e.imagePath : e.imagePath.substr(sep+1);
                if (label.size() > 22) label = label.substr(0,22);
            }

            bool active = e.loadTriggered && !e.unloadTriggered;
            ImVec4 col = active
                ? ImVec4(0.3f,1.0f,0.4f,1.0f)
                : (e.loadTriggered ? ImVec4(0.4f,0.4f,0.4f,1.0f)
                                   : ImVec4(0.75f,0.75f,0.75f,1.0f));

            char endStr[16];
            if (e.unloadTime > 0)
                snprintf(endStr, sizeof(endStr), "%5.1fs", e.unloadTime);
            else
                snprintf(endStr, sizeof(endStr), "  ---  ");

            ImGui::TextColored(col, "%5.1fs -> %s  %s", e.time, endStr, label.c_str());
            ImGui::SameLine();
            if (ImGui::SmallButton("x")) { sequencer.removeEvent(i); ImGui::PopID(); break; }
            ImGui::PopID();
        }
        ImGui::EndChild();
        if (ImGui::Button("Clear All", ImVec2(-1, 0))) sequencer.clear();
        ImGui::Spacing();
    }

    // ── ACTIONS ───────────────────────────────────────────────────────────────
    ImGui::Separator();
    ImGui::Spacing();
    if (ImGui::Button(paused ? "Resume [Spc]" : " Pause [Spc]", ImVec2(bw, 28)))
        paused = !paused;
    ImGui::SameLine();
    if (ImGui::Button("Reset  [R]", ImVec2(bw, 28))) ps.resetParticles();
    if (ImGui::Button("Clear Trail [C]", ImVec2(bw, 26))) ps.clearTrail();
    ImGui::SameLine();
    if (ImGui::Button("Shaders [F5]", ImVec2(bw, 26))) ps.reloadShaders();

    // ── STATS ─────────────────────────────────────────────────────────────────
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    ImGui::TextDisabled("%.0f FPS   |   %d particles", io.Framerate, ps.params.count);

    ImGui::End();
}

// ── Entry point ────────────────────────────────────────────────────────────────
int main() {
    if (!glfwInit()) { std::cerr << "GLFW init failed\n"; return -1; }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_SAMPLES, 0);

    GLFWwindow* win = glfwCreateWindow(windowW, windowH, "The Beat", nullptr, nullptr);
    if (!win) { std::cerr << "Window creation failed\n"; glfwTerminate(); return -1; }

    glfwMakeContextCurrent(win);
    glfwSwapInterval(1);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        std::cerr << "GLAD init failed\n"; return -1;
    }
    std::cout << "OpenGL: " << glGetString(GL_VERSION)  << "\n";
    std::cout << "GPU   : " << glGetString(GL_RENDERER) << "\n";

    glfwSetFramebufferSizeCallback(win, onFramebufferSize);
    glfwSetKeyCallback            (win, onKey);
    glfwSetMouseButtonCallback    (win, onMouseButton);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    ImGui_ImplGlfw_InitForOpenGL(win, true);
    ImGui_ImplOpenGL3_Init("#version 430");
    applyStyle();

    ps.init(windowW, windowH);
    basePointSize = ps.params.pointSize;

    double prevTime = glfwGetTime();

    while (!glfwWindowShouldClose(win)) {
        glfwPollEvents();

        double now = glfwGetTime();
        float  dt  = std::min((float)(now - prevTime), 0.05f);
        prevTime   = now;

        glfwGetCursorPos(win, &mouseX, &mouseY);
        ps.params.mouseActive = leftDown && !io.WantCaptureMouse;

        // ── Audio analysis ────────────────────────────────────────────────────
        AudioAnalysis an = audio.update(dt);

        // ── Sequencer ─────────────────────────────────────────────────────────
        if (audio.isPlaying()) {
            const SequencerEvent* evt = sequencer.update(audio.getPosition());
            if (evt) {
                ps.params.blendInTime  = evt->blendInTime;
                ps.params.blendOutTime = evt->blendOutTime;
                if (evt->isText) {
                    loadTextAndTargets(evt->text, evt->fontSize);
                } else if (!evt->imagePath.empty()) {
                    loadImageAndTargets(evt->imagePath);
                } else {
                    releaseImage();
                }
            }
        }

        // ── Image blend in / out ──────────────────────────────────────────────
        if (imageBlendingIn) {
            float rate = (ps.params.blendInTime > 0) ? dt / ps.params.blendInTime : 1.0f;
            ps.params.targetStrength += rate;
            if (ps.params.targetStrength >= 1.0f) {
                ps.params.targetStrength = 1.0f;
                imageBlendingIn = false;
            }
        }
        if (imageBlendingOut) {
            float rate = (ps.params.blendOutTime > 0) ? dt / ps.params.blendOutTime : 1.0f;
            ps.params.targetStrength -= rate;
            if (ps.params.targetStrength <= 0.0f) {
                ps.params.targetStrength = 0.0f;
                imageBlendingOut = false;
                ps.clearTargetSSBO();
                imageTarget.unload();
            }
        }

        // ── Audio reactivity ──────────────────────────────────────────────────
        // All modulations are temporary — base params are restored after update.
        float bp = an.beatPulse;  // 0-1, decays ~300ms after each beat

        // Field burst: beat hits hard (up to 6x field strength at full reactivity)
        float audioFieldMult = 1.0f + bp * ps.params.reactBeatField * 5.0f;

        // Speed: beat temporarily allows particles to fly much faster
        float audioSpeedMult = 1.0f + bp * ps.params.reactBeatSize * 2.0f;
        // On exact beat frame, raise speed cap high enough to let the kick through
        if (an.beat) audioSpeedMult = std::max(audioSpeedMult, ps.params.reactBeatKick * 2.0f);

        // Damping: THE key dramatic effect — lower damping on beat means particles
        // keep their energy and shoot outward instead of settling immediately.
        // e.g. 0.97 drops to ~0.82 at full beat + full reactivity
        float savedDamping = ps.params.damping;
        ps.params.damping = std::clamp(
            ps.params.damping - bp * ps.params.reactBeatField * 0.15f,
            0.70f, 1.0f);

        // Point size: big swing on beat + bass.
        // bass² gives a sharper, more punchy response curve — quiet sustained
        // bass barely registers while loud transients hit hard.
        float bassSharp = an.bass * an.bass;
        float modPointSize = basePointSize
            * (1.0f + bp        * ps.params.reactBeatSize * 3.0f)
            * (1.0f + bassSharp * ps.params.reactBassSize * 4.0f);
        ps.params.pointSize = std::clamp(modPointSize, 1.0f, 20.0f);

        // Evolution speed: total energy drives the field's rate of change
        float baseEvol = ps.params.evolutionSpeed;
        ps.params.evolutionSpeed *= (1.0f + an.total * ps.params.reactHighEvol * 2.5f);

        // Brightness: overall loudness pulses the glow
        float baseBright = ps.params.brightness;
        ps.params.brightness = std::clamp(
            baseBright * (0.65f + an.total * 0.70f + bp * 0.35f),
            0.05f, 1.0f);

        // Hue rotation driven by mid energy
        hueAccum += an.mid * ps.params.reactMidColor * dt * 0.45f;
        hueAccum  = fmodf(hueAccum, 1.0f);
        ps.params.hueShift = hueAccum;

        // Beat kick: fires on exactly ONE frame per beat (an.beat) so it's frame-accurate.
        // beatPulse smears over 300ms — too loose. an.beat is a single-frame spike.
        // Slider × 15 = pixels/frame. At 1.5 default → ~22px, at 10 → 150px.
        // audioSpeedMult is raised above to ensure speed cap won't kill the kick.
        float audioBeatKick = an.beat ? ps.params.reactBeatKick * 15.0f : 0.0f;

        // ── Simulate ──────────────────────────────────────────────────────────
        if (!paused)
            ps.update(dt, (float)mouseX, (float)mouseY, audioFieldMult, audioSpeedMult, audioBeatKick);

        // Restore params that were temporarily modulated
        ps.params.evolutionSpeed = baseEvol;
        ps.params.damping        = savedDamping;
        ps.params.brightness     = baseBright;

        // ── Render ────────────────────────────────────────────────────────────
        ps.drawToFBO();

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, windowW, windowH);
        glDisable(GL_BLEND);
        glClearColor(0,0,0,1);
        glClear(GL_COLOR_BUFFER_BIT);
        ps.blitToScreen();
        ps.drawObjects();

        // ── ImGui ─────────────────────────────────────────────────────────────
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        drawPanel(io, an);  // always call — handles its own slide + tab

        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(win);
    }

    imageTarget.unload();
    ps.cleanup();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(win);
    glfwTerminate();
    return 0;
}
