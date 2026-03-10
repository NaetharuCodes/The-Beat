#pragma once
#include <vector>
#include <string>
#include <algorithm>

// ── SequencerEvent ────────────────────────────────────────────────────────────
struct SequencerEvent {
    float       time          = 0.0f;   // when to load (absolute seconds)
    float       unloadTime    = 0.0f;   // when to unload (absolute seconds, 0 = never)
    std::string imagePath;              // used when isText == false
    float       blendInTime   = 1.5f;
    float       blendOutTime  = 1.0f;
    bool        loadTriggered   = false;
    bool        unloadTriggered = false;

    // ── Text event fields ─────────────────────────────────────────────────────
    bool        isText   = false;
    std::string text;          // the string to render when isText == true
    int         fontSize = 120; // glyph height in pixels
};

// ── Sequencer ─────────────────────────────────────────────────────────────────
class Sequencer {
public:
    std::vector<SequencerEvent> events;

    void addEvent(float time, const std::string& imagePath, float unloadTime = 0.0f,
                  float blendIn = 1.5f, float blendOut = 1.0f)
    {
        SequencerEvent e;
        e.time        = time;
        e.unloadTime  = unloadTime;
        e.imagePath   = imagePath;
        e.blendInTime = blendIn;
        e.blendOutTime= blendOut;
        events.push_back(e);
        sortEvents();
    }

    void addTextEvent(float time, const std::string& text, int fontSize,
                      float unloadTime = 0.0f,
                      float blendIn = 1.5f, float blendOut = 1.0f)
    {
        SequencerEvent e;
        e.time        = time;
        e.unloadTime  = unloadTime;
        e.isText      = true;
        e.text        = text;
        e.fontSize    = fontSize;
        e.blendInTime = blendIn;
        e.blendOutTime= blendOut;
        events.push_back(e);
        sortEvents();
    }

    void removeEvent(int idx) {
        if (idx >= 0 && idx < (int)events.size())
            events.erase(events.begin() + idx);
    }

    void clear() { events.clear(); }

    void sortEvents() {
        std::sort(events.begin(), events.end(),
                  [](const SequencerEvent& a, const SequencerEvent& b){
                      return a.time < b.time; });
    }

    void resetTriggers() {
        for (auto& e : events) { e.loadTriggered = false; e.unloadTriggered = false; }
        m_prevTime = -1.0f;
    }

    // Returns pointer to a fired event, or nullptr.
    // isText==false and imagePath=="" means "unload now" (from duration expiry).
    const SequencerEvent* update(float currentTime) {
        // Seeked backwards — recompute triggered state
        if (currentTime < m_prevTime) {
            for (auto& e : events) {
                e.loadTriggered   = (e.time <= currentTime - 0.05f);
                e.unloadTriggered = (e.unloadTime > 0.0f &&
                                     e.unloadTime <= currentTime - 0.05f);
            }
        }

        const SequencerEvent* fired = nullptr;

        // Check unloads first (a load at the same timestamp wins below)
        for (auto& e : events) {
            if (e.unloadTime > 0.0f && e.loadTriggered && !e.unloadTriggered
                && currentTime >= e.unloadTime)
            {
                e.unloadTriggered             = true;
                m_unloadSentinel.blendOutTime = e.blendOutTime;
                m_unloadSentinel.imagePath    = "";
                m_unloadSentinel.isText       = false;
                fired = &m_unloadSentinel;
            }
        }

        // Check loads (overwrites fired so a load at exact unload time wins)
        for (auto& e : events) {
            if (!e.loadTriggered && currentTime >= e.time) {
                e.loadTriggered = true;
                fired = &e;
            }
        }

        m_prevTime = currentTime;
        return fired;
    }

private:
    float          m_prevTime = -1.0f;
    SequencerEvent m_unloadSentinel;   // reused each frame for auto-unloads
};
