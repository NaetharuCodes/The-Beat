#pragma once
// ── The Beat — Preset I/O ─────────────────────────────────────────────────────
// Simple INI-style .tbpreset file: human-readable, easy to edit by hand.
//
//   [section]
//   key=value
//
// String values are stored as-is (no quoting) after the first '='.
// Comments start with '#'.
// ─────────────────────────────────────────────────────────────────────────────
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <stdexcept>
#include <algorithm>
#include "particle_system.h"
#include "sequencer.h"
#include "shape_target.h"

struct PresetData {
    // ── Mirrored fields from Params + extra UI state ──────────────────────────
    Params params;

    // Shape UI state
    int   shapeType    = (int)ShapeType::Star5;
    float shapeRadius  = 250.0f;
    float shapeRotDeg  = 0.0f;

    // Audio file (full path, informational — user re-loads if they want)
    std::string audioFile;

    // Sequencer events (copies, not references)
    std::vector<SequencerEvent> events;

    // ── Save to file ──────────────────────────────────────────────────────────
    bool save(const std::string& path) const {
        std::ofstream f(path);
        if (!f) return false;

        const Params& p = params;

        f << "# The Beat Preset v1\n";

        f << "\n[particles]\n";
        wf(f, "count",            p.count);
        wf(f, "noise_scale",      p.noiseScale);
        wf(f, "field_strength",   p.fieldStrength);
        wf(f, "evolution_speed",  p.evolutionSpeed);
        wf(f, "max_speed",        p.maxSpeed);
        wf(f, "damping",          p.damping);
        wf(f, "particle_mass",    p.particleMass);
        wf(f, "collision",        p.collisionEnabled ? 1 : 0);
        wf(f, "collision_radius", p.collisionRadius);
        wf(f, "density_radius",   p.densityRadius);

        f << "\n[rendering]\n";
        wf(f, "point_size",  p.pointSize);
        wf(f, "trail_fade",  p.trailFade);
        wf(f, "brightness",  p.brightness);
        wf(f, "color_mode",  p.colorMode);
        wf(f, "hue_shift",   p.hueShift);

        f << "\n[targeting]\n";
        wf(f, "blend_in",       p.blendInTime);
        wf(f, "blend_out",      p.blendOutTime);
        wf(f, "target_damping", p.targetDamping);

        f << "\n[reactivity]\n";
        wf(f, "beat_field", p.reactBeatField);
        wf(f, "beat_size",  p.reactBeatSize);
        wf(f, "beat_kick",  p.reactBeatKick);
        wf(f, "bass_size",  p.reactBassSize);
        wf(f, "mid_color",  p.reactMidColor);
        wf(f, "high_evol",  p.reactHighEvol);

        f << "\n[swarm]\n";
        wf(f, "separation", p.swarmSeparation);
        wf(f, "alignment",  p.swarmAlignment);
        wf(f, "cohesion",   p.swarmCohesion);
        wf(f, "radius",     p.swarmRadius);

        f << "\n[shapes]\n";
        wf(f, "type",     shapeType);
        wf(f, "radius",   shapeRadius);
        wf(f, "rotation", shapeRotDeg);

        f << "\n[audio]\n";
        f << "file=" << audioFile << "\n";

        f << "\n[sequencer]\n";
        wf(f, "count", (int)events.size());
        for (int i = 0; i < (int)events.size(); i++) {
            const SequencerEvent& e = events[i];
            f << "\n[event_" << i << "]\n";
            wf(f, "time",      e.time);
            wf(f, "unload",    e.unloadTime);
            wf(f, "blend_in",  e.blendInTime);
            wf(f, "blend_out", e.blendOutTime);
            if (e.isText) {
                f << "type=text\n";
                f << "text=" << e.text << "\n";
                wf(f, "font_size", e.fontSize);
            } else {
                f << "type=image\n";
                f << "path=" << e.imagePath << "\n";
            }
        }

        return f.good();
    }

    // ── Load from file ────────────────────────────────────────────────────────
    bool load(const std::string& path) {
        std::ifstream f(path);
        if (!f) return false;

        // Collect all section→{key→value} mappings
        using KV  = std::unordered_map<std::string, std::string>;
        using Sec = std::unordered_map<std::string, KV>;
        Sec sections;

        std::string line, sec;
        while (std::getline(f, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty() || line[0] == '#') continue;
            if (line[0] == '[') {
                auto e = line.find(']');
                if (e != std::string::npos) sec = line.substr(1, e - 1);
                continue;
            }
            auto eq = line.find('=');
            if (eq == std::string::npos) continue;
            sections[sec][line.substr(0, eq)] = line.substr(eq + 1);
        }

        // ── Helper lambdas ────────────────────────────────────────────────────
        auto gf = [&](const std::string& s, const std::string& k, float d) -> float {
            auto si = sections.find(s); if (si == sections.end()) return d;
            auto ki = si->second.find(k); if (ki == si->second.end()) return d;
            try { return std::stof(ki->second); } catch(...) { return d; }
        };
        auto gi = [&](const std::string& s, const std::string& k, int d) -> int {
            auto si = sections.find(s); if (si == sections.end()) return d;
            auto ki = si->second.find(k); if (ki == si->second.end()) return d;
            try { return std::stoi(ki->second); } catch(...) { return d; }
        };
        auto gs = [&](const std::string& s, const std::string& k,
                      const std::string& d) -> std::string {
            auto si = sections.find(s); if (si == sections.end()) return d;
            auto ki = si->second.find(k); if (ki == si->second.end()) return d;
            return ki->second;
        };

        Params& p = params;

        p.count           = gi("particles", "count",            p.count);
        p.noiseScale      = gf("particles", "noise_scale",      p.noiseScale);
        p.fieldStrength   = gf("particles", "field_strength",   p.fieldStrength);
        p.evolutionSpeed  = gf("particles", "evolution_speed",  p.evolutionSpeed);
        p.maxSpeed        = gf("particles", "max_speed",        p.maxSpeed);
        p.damping         = gf("particles", "damping",          p.damping);
        p.particleMass    = gf("particles", "particle_mass",    p.particleMass);
        p.collisionEnabled= gi("particles", "collision",        p.collisionEnabled ? 1 : 0) != 0;
        p.collisionRadius = gf("particles", "collision_radius", p.collisionRadius);
        p.densityRadius   = gf("particles", "density_radius",   p.densityRadius);

        p.pointSize  = gf("rendering", "point_size", p.pointSize);
        p.trailFade  = gf("rendering", "trail_fade", p.trailFade);
        p.brightness = gf("rendering", "brightness", p.brightness);
        p.colorMode  = gi("rendering", "color_mode", p.colorMode);
        p.hueShift   = gf("rendering", "hue_shift",  p.hueShift);

        p.blendInTime   = gf("targeting", "blend_in",       p.blendInTime);
        p.blendOutTime  = gf("targeting", "blend_out",      p.blendOutTime);
        p.targetDamping = gf("targeting", "target_damping", p.targetDamping);

        p.reactBeatField = gf("reactivity", "beat_field", p.reactBeatField);
        p.reactBeatSize  = gf("reactivity", "beat_size",  p.reactBeatSize);
        p.reactBeatKick  = gf("reactivity", "beat_kick",  p.reactBeatKick);
        p.reactBassSize  = gf("reactivity", "bass_size",  p.reactBassSize);
        p.reactMidColor  = gf("reactivity", "mid_color",  p.reactMidColor);
        p.reactHighEvol  = gf("reactivity", "high_evol",  p.reactHighEvol);

        p.swarmSeparation = gf("swarm", "separation", p.swarmSeparation);
        p.swarmAlignment  = gf("swarm", "alignment",  p.swarmAlignment);
        p.swarmCohesion   = gf("swarm", "cohesion",   p.swarmCohesion);
        p.swarmRadius     = gf("swarm", "radius",     p.swarmRadius);

        shapeType   = std::clamp(gi("shapes","type",shapeType), 0, (int)ShapeType::COUNT-1);
        shapeRadius = gf("shapes", "radius",   shapeRadius);
        shapeRotDeg = gf("shapes", "rotation", shapeRotDeg);

        audioFile = gs("audio", "file", "");

        events.clear();
        int n = gi("sequencer", "count", 0);
        for (int i = 0; i < n; i++) {
            std::string s = "event_" + std::to_string(i);
            SequencerEvent e;
            e.time         = gf(s, "time",      0.f);
            e.unloadTime   = gf(s, "unload",    0.f);
            e.blendInTime  = gf(s, "blend_in",  1.5f);
            e.blendOutTime = gf(s, "blend_out", 1.0f);
            std::string type = gs(s, "type", "image");
            if (type == "text") {
                e.isText   = true;
                e.text     = gs(s, "text", "");
                e.fontSize = gi(s, "font_size", 120);
            } else {
                e.isText    = false;
                e.imagePath = gs(s, "path", "");
            }
            events.push_back(e);
        }

        return true;
    }

private:
    // ── Tiny write helpers (no external deps) ─────────────────────────────────
    static void wf(std::ofstream& f, const std::string& k, float v) {
        f << k << "=" << v << "\n";
    }
    static void wf(std::ofstream& f, const std::string& k, int v) {
        f << k << "=" << v << "\n";
    }
};
