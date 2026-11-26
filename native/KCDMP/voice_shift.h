// Copyright (C) 2026 the Kingdom Come: Together contributors (AUTHORS). SPDX-License-Identifier: GPL-3.0-only
// GPLv3 section 7 additional terms: NOTICE. This project's own code only; Kingdom Come: Deliverance II and its
// content belong to Warhorse Studios and PLAION. Unofficial, free, not affiliated with or endorsed by them.
#pragma once
// Fixed-soul build: the voice filter of the voice presets (fs_voice.cpp), a streaming TD-PSOLA that moves a voice's pitch and its
// formants apart. A plain pitch shift (FMOD's own DSP) moves both by the same factor, which makes a man's voice sound like a small,
// sped-up creature; a woman's voice is a higher pitch (P ~1.5-1.7) from a vocal tract only a little shorter (formants Q ~1.2).
//
// How: the pitch is tracked every 5 ms (YIN on a low-passed (800 Hz), 4x decimated copy, 70-400 Hz, a looser threshold right
// after a voiced hop at about the same period); pitch marks are laid one period apart, each one
// moved to where the signal best matches the one before (so the grains line up). A grain is two periods under a Hann window,
// squeezed by Q (the formants go up by Q) and laid down every period/P (the pitch goes up by P); the tempo stays. Unvoiced sound
// (s, f, breath) gets 5 ms grains at its own pace, squeezed by Q too. Overlaps are normalised by the window sum, and a slow loudness
// match keeps the line as loud as it came in. Output delay kDelay samples (54 ms at 48 kHz). No allocation after construction and no
// locks: it runs on FMOD's mixer thread.
#include <cmath>
#include <cstdint>
#include <cstring>

namespace kcdmp::voiceshift {

class Shifter {
public:
    static constexpr int kRing = 16384, kMask = kRing - 1;
    static constexpr int kHop = 240;                       // pitch track step (5 ms)
    static constexpr int kDec = 4, kYw = 240, kTmin = 30, kTmax = 172;   // YIN at 12 kHz: 20 ms window, 70-400 Hz
    static constexpr int kNeed = (kYw + kTmax) * kDec / 2; // input needed past a hop's centre
    static constexpr int kUnv = 240;                       // unvoiced grain half length
    static constexpr int kDelay = 2600;
    static constexpr int kMarks = 512, kPer = 1024;

    Shifter() { reset(); }

    void reset() {
        std::memset(in_, 0, sizeof in_); std::memset(lp_, 0, sizeof lp_); lp1_ = lp2_ = 0.0f; std::memset(acc_, 0, sizeof acc_); std::memset(ws_, 0, sizeof ws_);
        std::memset(per_, 0, sizeof per_);
        n_ = 0; hops_ = 0; nm_ = 0; cur_ = 0; ts_ = 0.0; emitted_ = -1;
        eIn_ = eOut_ = 1e-6f; gain_ = 1.0f;
    }

    // mono in -> mono out, any block length; P pitch factor, Q formant factor (the last two arguments are unused, kept for the caller)
    void process(const float* in, float* out, int n, float P, float Q, int, float sr) {
        if (P < 0.5f) P = 0.5f; if (P > 2.5f) P = 2.5f;
        if (Q < 0.5f) Q = 0.5f; if (Q > 2.0f) Q = 2.0f;
        const float a = 1.0f - std::exp(-1.0f / (0.35f * (sr > 0 ? sr : 48000.0f)));
        for (int i = 0; i < n; ++i) {
            in_[n_ & kMask] = in[i];
            lp1_ += kLp * (in[i] - lp1_); lp2_ += kLp * (lp1_ - lp2_);
            lp_[n_ & kMask] = lp2_;
            ++n_;
            if ((n_ & 63) == 0) step(P, Q);
            const int64_t o = n_ - 1 - kDelay;
            float y = 0.0f;
            if (o >= 0) {
                const int k = static_cast<int>(o & kMask);
                const float w = ws_[k];
                y = acc_[k] / (w > 0.3f ? w : 0.3f);
                acc_[k] = 0.0f; ws_[k] = 0.0f;
                emitted_ = o;
            }
            const float x = in[i];
            eIn_ += a * (x * x - eIn_);
            eOut_ += a * (y * y - eOut_);
            y *= gain_;
            out[i] = y > 0.95f ? 0.95f + 0.05f * std::tanh((y - 0.95f) * 20.0f) : y < -0.95f ? -0.95f + 0.05f * std::tanh((y + 0.95f) * 20.0f) : y;
            if ((n_ & 255) == 0 && eOut_ > 1e-9f) {
                float g = 0.78f * std::sqrt(eIn_ / eOut_);   // 0.78: the shifted line measured ~1.3x louder than it came in
                g = g < 0.25f ? 0.25f : g > 4.0f ? 4.0f : g;
                gain_ += 0.1f * (g - gain_);
            }
        }
    }

private:
    struct Mark { int64_t pos; float T; };   // T 0: unvoiced

    static constexpr float kLp = 0.0995f;   // 1 - exp(-2 pi 800 / 48000): two of these one-pole low-passes for the pitch track
    float in_[kRing], lp_[kRing], acc_[kRing], ws_[kRing];
    float lp1_, lp2_;
    float per_[kPer];          // period (samples) per hop, 0 unvoiced
    Mark marks_[kMarks];
    int64_t n_, hops_, nm_, cur_, emitted_;
    double ts_;
    float eIn_, eOut_, gain_;

    float at(int64_t p) const { return p < 0 || p >= n_ || p < n_ - kRing ? 0.0f : in_[p & kMask]; }
    float atl(int64_t p) const { return p < 0 || p >= n_ || p < n_ - kRing ? 0.0f : lp_[p & kMask]; }
    float lerp(double p) const { const int64_t i = static_cast<int64_t>(std::floor(p)); const float f = static_cast<float>(p - i); return at(i) + (at(i + 1) - at(i)) * f; }

    // YIN for one hop: period in samples, 0 when unvoiced or quiet
    float yin(int64_t centre, float prev) const {
        float x[kYw + kTmax];
        const int64_t s = centre - kNeed;
        if (s < 0) return 0.0f;
        double e = 0;
        for (int j = 0; j < kYw + kTmax; ++j) {
            float v = 0; for (int q = 0; q < kDec; ++q) v += atl(s + j * kDec + q);
            x[j] = v * (1.0f / kDec); e += x[j] * x[j];
        }
        if (e / (kYw + kTmax) < 4e-7) return 0.0f;   // quiet
        float d[kTmax + 1]; d[0] = 1.0f;
        double run = 0;
        for (int t = 1; t <= kTmax; ++t) {
            double acc = 0; for (int j = 0; j < kYw; ++j) { const float df = x[j] - x[j + t]; acc += df * df; }
            run += acc;
            d[t] = run > 0 ? static_cast<float>(acc * t / run) : 1.0f;
        }
        int best = -1;
        for (int t = kTmin; t < kTmax; ++t) {
            if (d[t] < 0.15f) { while (t + 1 < kTmax && d[t + 1] < d[t]) ++t; best = t; break; }
        }
        if (best < 0) {   // no dip under the threshold: the global minimum, if it is clear enough
            float m = 1e9f; for (int t = kTmin; t < kTmax; ++t) if (d[t] < m) { m = d[t]; best = t; }
            const bool near = prev > 0 && std::fabs(best * kDec - prev) < 0.2f * prev;   // the voiced hop before at about this period
            if (m > (near ? 0.6f : 0.45f)) return 0.0f;
        }
        float t = static_cast<float>(best);
        if (best > kTmin && best < kTmax - 1) {
            const float l = d[best - 1], c = d[best], r = d[best + 1], den = l - 2 * c + r;
            if (den > 1e-9f) t += 0.5f * (l - r) / den;
        }
        return t * kDec;
    }

    float period_at(int64_t pos) const { const int64_t h = pos / kHop; return per_[h % kPer]; }

    void step(float P, float Q) {
        // 1. the pitch track
        while (hops_ * kHop + kNeed <= n_ - 1) { per_[hops_ % kPer] = yin(hops_ * kHop, hops_ > 0 ? per_[(hops_ - 1) % kPer] : 0.0f); ++hops_; }
        // 2. pitch marks
        for (;;) {
            int64_t cand; float lastT = 0; int64_t lastP = 0;
            if (nm_ == 0) cand = 0;
            else { const Mark& m = marks_[(nm_ - 1) % kMarks]; lastP = m.pos; lastT = m.T; cand = m.pos + (m.T > 0 ? static_cast<int64_t>(m.T + 0.5f) : kUnv); }
            if (cand / kHop >= hops_) break;
            const float T = period_at(cand);
            if (T > 0 && lastT > 0) {
                const int t = static_cast<int>(T + 0.5f), r = t / 6, h = t / 2;
                if (cand + r + h >= n_) break;
                double best = -1e30; int64_t bp = cand;
                for (int dd = -r; dd <= r; dd += 2) {
                    const int64_t c = cand + dd; double s = 0;
                    for (int j = -h; j < h; j += 2) s += at(c + j) * at(lastP + j);
                    if (s > best) { best = s; bp = c; }
                }
                cand = bp > lastP + t / 2 ? bp : lastP + t / 2;
            }
            marks_[nm_ % kMarks] = {cand, T};
            ++nm_;
            if (cur_ < nm_ - kMarks + 8) cur_ = nm_ - kMarks + 8;
        }
        // 3. synthesis grains
        for (;;) {
            while (cur_ + 1 < nm_ && marks_[(cur_ + 1) % kMarks].pos <= ts_) ++cur_;
            if (cur_ + 1 >= nm_) break;   // the mark after ts is not known yet
            const Mark& m0 = marks_[cur_ % kMarks]; const Mark& m1 = marks_[(cur_ + 1) % kMarks];
            const Mark& m = (ts_ - m0.pos) <= (m1.pos - ts_) ? m0 : m1;
            const bool voiced = m.T > 0;
            const double T = voiced ? m.T : kUnv;
            if (m.pos + static_cast<int64_t>(T) + 2 >= n_) break;
            const int L = static_cast<int>(2.0 * T / Q + 0.5);
            const int64_t s = static_cast<int64_t>(ts_ + 0.5) - L / 2;
            const double kw = 2.0 * 3.14159265358979 / (2.0 * T);
            for (int j = 0; j < L; ++j) {
                const int64_t p = s + j;
                if (p <= emitted_) continue;
                const double src = j * static_cast<double>(Q);
                const float w = static_cast<float>(0.5 - 0.5 * std::cos(kw * src));
                const float v = lerp(m.pos - T + src) * w;
                acc_[p & kMask] += v; ws_[p & kMask] += w;
            }
            ts_ += voiced ? T / P : T / 2.0;
        }
    }
};

}  // namespace kcdmp::voiceshift
