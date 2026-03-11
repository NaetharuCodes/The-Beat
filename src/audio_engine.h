#pragma once
#define _USE_MATH_DEFINES   // M_PI on MSVC
#define MA_NO_WINMM         // avoid APIENTRY clash with windows.h
#include <miniaudio.h>
#include <atomic>
#include <string>
#include <deque>
#include <cmath>
#include <algorithm>
#include <mutex>
#include <iostream>

// ── Biquad IIR filter (Audio EQ Cookbook) ────────────────────────────────────
struct Biquad {
    double b0=1, b1=0, b2=0, a1=0, a2=0;
    double z1=0, z2=0;

    float process(float x) {
        double y = b0*x + z1;
        z1 = b1*x - a1*y + z2;
        z2 = b2*x - a2*y;
        return (float)y;
    }

    static Biquad lowpass(double freq, double Q, double sr) {
        double w0 = 2.0*M_PI*freq/sr;
        double cw = cos(w0), sw = sin(w0);
        double alpha = sw/(2.0*Q);
        double a0 = 1.0+alpha;
        Biquad f;
        f.b0 = (1.0-cw)/(2.0*a0);
        f.b1 = (1.0-cw)/a0;
        f.b2 = f.b0;
        f.a1 = -2.0*cw/a0;
        f.a2 = (1.0-alpha)/a0;
        return f;
    }

    static Biquad highpass(double freq, double Q, double sr) {
        double w0 = 2.0*M_PI*freq/sr;
        double cw = cos(w0), sw = sin(w0);
        double alpha = sw/(2.0*Q);
        double a0 = 1.0+alpha;
        Biquad f;
        f.b0 =  (1.0+cw)/(2.0*a0);
        f.b1 = -(1.0+cw)/a0;
        f.b2 = f.b0;
        f.a1 = -2.0*cw/a0;
        f.a2 = (1.0-alpha)/a0;
        return f;
    }

    static Biquad bandpass(double freq, double Q, double sr) {
        double w0 = 2.0*M_PI*freq/sr;
        double cw = cos(w0), sw = sin(w0);
        double alpha = sw/(2.0*Q);
        double a0 = 1.0+alpha;
        Biquad f;
        f.b0 =  (sw/2.0)/a0;
        f.b1 =  0.0;
        f.b2 = -(sw/2.0)/a0;
        f.a1 = -2.0*cw/a0;
        f.a2 = (1.0-alpha)/a0;
        return f;
    }
};

// ── Analysis snapshot (read by main thread) ───────────────────────────────────
struct AudioAnalysis {
    float bass      = 0.0f;   // low-frequency energy  [0,1]
    float mid       = 0.0f;   // mid-frequency energy  [0,1]
    float high      = 0.0f;   // high-frequency energy [0,1]
    float total     = 0.0f;   // overall loudness      [0,1]
    float beatPulse = 0.0f;   // 1.0 on beat, decays to 0 over ~300ms
    float bpm       = 0.0f;   // estimated tempo
    bool  beat      = false;  // true for exactly one frame per detected beat
};

// ── AudioEngine ───────────────────────────────────────────────────────────────
class AudioEngine {
public:
    float sensitivity = 6.0f;  // analysis gain — increase for quiet sources

    AudioEngine()  = default;
    ~AudioEngine() { unloadFile(); }

    // ── File playback ─────────────────────────────────────────────────────────
    bool loadFile(const std::string& path) {
        unloadFile();

        ma_decoder_config dcfg = ma_decoder_config_init(ma_format_f32, 2, 44100);
        if (ma_decoder_init_file(path.c_str(), &dcfg, &m_decoder) != MA_SUCCESS) {
            std::cerr << "[Audio] Failed to open: " << path << "\n";
            return false;
        }
        m_decoderInit = true;

        ma_uint64 frames = 0;
        ma_decoder_get_length_in_pcm_frames(&m_decoder, &frames);
        m_duration.store((float)frames / 44100.0f);
        m_sampleRate = 44100;

        initFilters(44100);

        // Open playback device
        ma_device_config dcfg2 = ma_device_config_init(ma_device_type_playback);
        dcfg2.playback.format   = ma_format_f32;
        dcfg2.playback.channels = 2;
        dcfg2.sampleRate        = 44100;
        dcfg2.dataCallback      = audioCallback;
        dcfg2.pUserData         = this;

        if (ma_device_init(nullptr, &dcfg2, &m_device) != MA_SUCCESS) {
            std::cerr << "[Audio] Failed to open playback device\n";
            ma_decoder_uninit(&m_decoder);
            m_decoderInit = false;
            return false;
        }
        m_deviceInit = true;
        m_hasFile.store(true);
        m_framesRead.store(0);
        m_filename = path;

        // Extract just the filename for display
        size_t sep = path.find_last_of("/\\");
        m_displayName = (sep == std::string::npos) ? path : path.substr(sep+1);

        std::cout << "[Audio] Loaded: " << m_displayName
                  << "  (" << m_duration.load() << "s)\n";
        return true;
    }

    void unloadFile() {
        stop();
        if (m_deviceInit)  { ma_device_uninit (&m_device);  m_deviceInit  = false; }
        if (m_decoderInit) { ma_decoder_uninit(&m_decoder); m_decoderInit = false; }
        m_hasFile.store(false);
        m_playing.store(false);
        m_duration.store(0.0f);
        m_framesRead.store(0);
        m_displayName.clear();
    }

    void play() {
        if (!m_deviceInit) return;
        ma_device_start(&m_device);
        m_playing.store(true);
    }

    void pause() {
        if (!m_deviceInit) return;
        ma_device_stop(&m_device);
        m_playing.store(false);
    }

    void stop() {
        if (!m_deviceInit) return;
        ma_device_stop(&m_device);
        m_playing.store(false);
        seekTo(0.0f);
    }

    void seekTo(float seconds) {
        if (!m_decoderInit) return;
        ma_uint64 frame = (ma_uint64)(seconds * 44100.0f);
        std::lock_guard<std::mutex> lock(m_decoderMtx);
        ma_decoder_seek_to_pcm_frame(&m_decoder, frame);
        m_framesRead.store(frame);
    }

    bool isPlaying()  const { return m_playing.load(); }
    bool hasFile()    const { return m_hasFile.load(); }
    float getDuration() const { return m_duration.load(); }
    float getPosition() const {
        return (float)m_framesRead.load() / std::max(1u, m_sampleRate);
    }
    const std::string& getDisplayName() const { return m_displayName; }
    const std::string& getFilename()    const { return m_filename; }

    void setVolume(float v) { m_volume.store(std::clamp(v, 0.0f, 1.0f)); }
    float getVolume()  const { return m_volume.load(); }

    // ── Call once per frame — returns analysis + decays beat pulse ────────────
    AudioAnalysis update(float dt) {
        AudioAnalysis a;
        a.bass  = std::min(m_bassEnergy.load()  * sensitivity, 1.0f);
        a.mid   = std::min(m_midEnergy.load()   * sensitivity, 1.0f);
        a.high  = std::min(m_highEnergy.load()  * sensitivity, 1.0f);
        a.total = (a.bass + a.mid + a.high) / 3.0f;

        // Decay beat pulse on main thread (purely visual)
        if (m_beatFlag.exchange(false)) {
            m_beatPulse = 1.0f;
            a.beat = true;

            // BPM estimation from inter-beat intervals
            double now = getPosition();
            if (m_lastBeatTime > 0.0) {
                double interval = now - m_lastBeatTime;
                if (interval > 0.25 && interval < 2.0) {
                    m_beatIntervals.push_back(interval);
                    if (m_beatIntervals.size() > 8) m_beatIntervals.pop_front();
                    double avg = 0;
                    for (double iv : m_beatIntervals) avg += iv;
                    avg /= m_beatIntervals.size();
                    m_bpm = (float)(60.0 / avg);
                }
            }
            m_lastBeatTime = now;
        }

        m_beatPulse = std::max(m_beatPulse - dt * 3.3f, 0.0f); // decay ~300ms
        a.beatPulse = m_beatPulse;
        a.bpm       = m_bpm;
        return a;
    }

private:
    ma_device  m_device  {};
    ma_decoder m_decoder {};
    bool m_deviceInit  = false;
    bool m_decoderInit = false;
    std::mutex m_decoderMtx;

    std::atomic<bool>     m_playing  {false};
    std::atomic<bool>     m_hasFile  {false};
    std::atomic<float>    m_volume   {0.75f};
    std::atomic<float>    m_duration {0.0f};
    std::atomic<uint64_t> m_framesRead {0};
    uint32_t              m_sampleRate = 44100;

    std::string m_displayName;
    std::string m_filename;

    // ── Analysis atomics (written on audio thread, read on main thread) ───────
    std::atomic<float> m_bassEnergy  {0.0f};
    std::atomic<float> m_midEnergy   {0.0f};
    std::atomic<float> m_highEnergy  {0.0f};
    std::atomic<bool>  m_beatFlag    {false};

    // ── Beat / BPM state (main thread only) ───────────────────────────────────
    float        m_beatPulse = 0.0f;
    float        m_bpm       = 120.0f;
    double       m_lastBeatTime = -1.0;
    std::deque<double> m_beatIntervals;

    // ── Filters (audio thread only) ───────────────────────────────────────────
    Biquad m_bassLPF, m_midBPF, m_highHPF;
    float  m_bassEnv = 0.0f;
    float  m_midEnv  = 0.0f;
    float  m_highEnv = 0.0f;

    // Beat detection ring buffer
    static constexpr int BEAT_HISTORY = 43;
    float  m_beatHistory[BEAT_HISTORY] = {};
    int    m_beatHistoryIdx = 0;
    double m_lastBeatFrame  = -1.0;   // in frames, to enforce min gap

    void initFilters(uint32_t sr) {
        m_bassLPF = Biquad::lowpass (200.0,  0.707, sr);
        m_midBPF  = Biquad::bandpass(1500.0, 0.5,   sr);
        m_highHPF = Biquad::highpass(5000.0, 0.707, sr);
        m_bassEnv = m_midEnv = m_highEnv = 0.0f;
        std::fill(m_beatHistory, m_beatHistory + BEAT_HISTORY, 0.0f);
        m_beatHistoryIdx = 0;
        m_lastBeatFrame  = -1.0;
    }

    // ── Process a block of stereo f32 samples (audio thread) ─────────────────
    void processBlock(const float* stereo, uint32_t frames) {
        // Per-sample one-pole envelope on signal power (x²).
        // Coefficients are sample-rate independent of block size, so the time
        // constants are always the same regardless of driver buffer size:
        //   atk ≈ 3ms  (1 - exp(-1 / (44100 * 0.003))) ≈ 0.0077
        //   rel ≈ 38ms (1 - exp(-1 / (44100 * 0.038))) ≈ 0.0006
        // Result: snaps up on transients, drops cleanly between hits.
        const float atk_s = 0.0077f;
        const float rel_s = 0.0006f;

        for (uint32_t i = 0; i < frames; i++) {
            float mono = (stereo[i*2] + stereo[i*2+1]) * 0.5f;
            float b = m_bassLPF.process(mono);
            float m = m_midBPF .process(mono);
            float h = m_highHPF.process(mono);

            float b2 = b*b, m2 = m*m, h2 = h*h;
            m_bassEnv += (b2 - m_bassEnv) * (b2 > m_bassEnv ? atk_s : rel_s);
            m_midEnv  += (m2 - m_midEnv)  * (m2 > m_midEnv  ? atk_s : rel_s);
            m_highEnv += (h2 - m_highEnv) * (h2 > m_highEnv ? atk_s : rel_s);
        }

        // sqrt converts power envelope → amplitude scale (same range as before)
        float bassAmp = sqrtf(m_bassEnv);
        float midAmp  = sqrtf(m_midEnv);
        float highAmp = sqrtf(m_highEnv);

        m_bassEnergy.store(bassAmp, std::memory_order_relaxed);
        m_midEnergy .store(midAmp,  std::memory_order_relaxed);
        m_highEnergy.store(highAmp, std::memory_order_relaxed);

        // ── Beat detection: bass amplitude vs. running local average ──────────
        m_beatHistory[m_beatHistoryIdx] = bassAmp;
        m_beatHistoryIdx = (m_beatHistoryIdx + 1) % BEAT_HISTORY;

        float avg = 0.0f;
        for (int k = 0; k < BEAT_HISTORY; k++) avg += m_beatHistory[k];
        avg /= BEAT_HISTORY;

        uint64_t curFrame = m_framesRead.load(std::memory_order_relaxed);
        double   minGap   = (double)m_sampleRate * 0.25; // 250ms minimum gap

        bool beatThisBlock = (bassAmp > avg * 1.5f) && (bassAmp > 0.005f)
                          && ((double)curFrame - m_lastBeatFrame > minGap);
        if (beatThisBlock) {
            m_beatFlag.store(true, std::memory_order_relaxed);
            m_lastBeatFrame = (double)curFrame;
        }
    }

    // ── miniaudio data callback (audio thread) ────────────────────────────────
    static void audioCallback(ma_device* dev, void* out, const void* /*in*/, ma_uint32 frames) {
        auto* eng = static_cast<AudioEngine*>(dev->pUserData);
        float* output = static_cast<float*>(out);

        ma_uint64 read = 0;
        {
            std::lock_guard<std::mutex> lock(eng->m_decoderMtx);
            if (eng->m_decoderInit) {
                ma_decoder_read_pcm_frames(&eng->m_decoder, output, frames, &read);
            }
        }

        // Zero any unfilled frames (end of file)
        if (read < frames)
            memset(output + read*2, 0, (frames - read)*2*sizeof(float));

        // End-of-file: stop device
        if (read == 0) {
            eng->m_playing.store(false);
            return;
        }

        // Apply volume
        float vol = eng->m_volume.load(std::memory_order_relaxed);
        for (ma_uint32 i = 0; i < frames*2; i++)
            output[i] *= vol;

        eng->m_framesRead.fetch_add(read, std::memory_order_relaxed);
        eng->processBlock(output, (uint32_t)read);
    }
};
