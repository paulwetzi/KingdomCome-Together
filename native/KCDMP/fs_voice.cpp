// Copyright (C) 2026 the Kingdom Come: Together contributors (AUTHORS). SPDX-License-Identifier: GPL-3.0-only
// GPLv3 section 7 additional terms: NOTICE. This project's own code only; Kingdom Come: Deliverance II and its
// content belong to Warhorse Studios and PLAION. Unofficial, free, not affiliated with or endorsed by them.
// Fixed-soul build: voice presets for the player's own voice (Henry's lines, a higher pitch).
//
// The game plays its voice lines with FMOD core (fmod.dll 2.02): CryAudioImplFmod.dll creates each line's sound by its file name
// (System::createSound / createStream) and plays it (System::playSound) -- all three through its import table, which is hooked
// here (iat_hook.h: one pointer each, nothing of the game's code changed). Henry's lines are the files of his voice actor,
// "tmck_*.ogg" (43,284 of the German files). FMOD Studio plays the voice lines itself (programmer sounds, through fmodstudio.dll's
// own playSound import, hooked too), so each of Henry's lines gets FMOD's own pitch shifter on its channel
// (FMOD_DSP_TYPE_PITCHSHIFT: the pitch, not the speed), released when the line ends. Every other sound is untouched. Preset 0 plays
// Henry's lines as before.
#include "fs_voice.h"
#include "iat_hook.h"
#include "wo203.h"
#include "wo163.h"
#include "fault_guard.h"
#include "log.h"
#include <windows.h>
#include <atomic>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace kcdmp::fsvoice {

namespace {

using CreateFn = int (*)(void* sys, const char* name, unsigned mode, void* exinfo, void** sound);
using PlayFn = int (*)(void* sys, void* sound, void* group, bool paused, void** channel);
using CreateDspFn = int (*)(void* sys, int type, void** dsp);
using SetParamFn = int (*)(void* dsp, int index, float value);
using AddDspFn = int (*)(void* group, int index, void* dsp);
using CreateGroupFn = int (*)(void* sys, const char* name, void** group);
using AddGroupFn = int (*)(void* parent, void* child, int propagateClock, void** connection);
using MasterFn = int (*)(void* sys, void** group);
using ChanAddDspFn = int (*)(void* channel, int index, void* dsp);
using ChanRemoveDspFn = int (*)(void* channel, void* dsp);
using ChanPlayingFn = int (*)(void* channel, int* playing);
using ChanSoundFn = int (*)(void* channel, void** sound);
using DspReleaseFn = int (*)(void* dsp);

constexpr const char* kImporter = "CryAudioImplFmod.dll";
constexpr const char* kFmod = "fmod.dll";
constexpr const char* kSymCreateSound = "?createSound@System@FMOD@@QEAA?AW4FMOD_RESULT@@PEBDIPEAUFMOD_CREATESOUNDEXINFO@@PEAPEAVSound@2@@Z";
constexpr const char* kSymCreateStream = "?createStream@System@FMOD@@QEAA?AW4FMOD_RESULT@@PEBDIPEAUFMOD_CREATESOUNDEXINFO@@PEAPEAVSound@2@@Z";
constexpr const char* kSymPlay = "?playSound@System@FMOD@@QEAA?AW4FMOD_RESULT@@PEAVSound@2@PEAVChannelGroup@2@_NPEAPEAVChannel@2@@Z";
constexpr int kDspPitchShift = 13;                 // FMOD_DSP_TYPE_PITCHSHIFT (2.02)
constexpr int kParamPitch = 0, kParamFftSize = 1;  // FMOD_DSP_PITCHSHIFT_PITCH / _FFTSIZE
constexpr unsigned kOpenNotAName = 0x8 | 0x800 | 0x10000000;   // FMOD_OPENUSER | FMOD_OPENMEMORY | FMOD_OPENMEMORY_POINT: no file name

CreateFn g_origSound = nullptr, g_origStream = nullptr;
PlayFn g_origPlay = nullptr;
CreateDspFn g_createDsp = nullptr;
SetParamFn g_setParam = nullptr;
AddDspFn g_addDsp = nullptr;
CreateGroupFn g_createGroup = nullptr;
AddGroupFn g_addGroup = nullptr;
MasterFn g_master = nullptr;
ChanAddDspFn g_chanAdd = nullptr;
ChanRemoveDspFn g_chanRemove = nullptr;
ChanPlayingFn g_chanPlaying = nullptr;
ChanSoundFn g_chanSound = nullptr;
DspReleaseFn g_dspRelease = nullptr;
PlayFn g_origPlayStudio = nullptr;
struct Active { void* channel; void* sound; void* dsp; };
std::vector<Active> g_active;                      // Henry's lines playing with a pitch shifter of ours (main-thread sweep)

std::atomic<int> g_preset{0};
std::atomic<bool> g_armed{false};
std::mutex g_lock;
std::unordered_set<void*> g_henry;                 // sounds made from Henry's voice files
std::atomic<uint32_t> c_henryMade{0}, c_henryPlayed{0}, c_rerouted{0}, c_groupFail{0};   // rerouted = lines shifted
std::string g_why = "not installed";

float pitch_of(int p) {
    switch (p) {
        case 1: return 1.12f;   // a little higher
        case 2: return 1.26f;   // a woman's range
        case 3: return 1.40f;   // brighter
        case 4: return 0.89f;   // deeper
        default: return 1.0f;
    }
}

// A file name the sound was made from: its "tmck_" marks Henry's voice actor. Read under the fault guard (the name is the game's).
bool henry_name(const char* name, unsigned mode) {
    if (!name || (mode & kOpenNotAName)) return false;
    char b[300]{};
    KCDMP_FAULT_READ(site, "fs_voice::name");
    const bool ok = fault::guarded(site, [&] {
        size_t i = 0;
        for (; i + 1 < sizeof b && name[i]; ++i) b[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(name[i])));
        b[i] = 0;
    });
    if (!ok) return false;
    const char* s = std::strrchr(b, '/');
    const char* t = std::strrchr(b, '\\');
    const char* base = s && t ? (s > t ? s : t) + 1 : s ? s + 1 : t ? t + 1 : b;
    return std::strncmp(base, "tmck_", 5) == 0;
}

void note_sound(void* sound, bool henry) {
    if (!sound) return;
    std::lock_guard<std::mutex> lk(g_lock);
    if (henry) { g_henry.insert(sound); c_henryMade.fetch_add(1); }
    else g_henry.erase(sound);   // a reused address is not Henry's any more
}

int hk_sound(void* sys, const char* name, unsigned mode, void* exinfo, void** sound) {
    const int r = g_origSound(sys, name, mode, exinfo, sound);
    if (r == 0 && sound) note_sound(*sound, henry_name(name, mode));
    return r;
}
int hk_stream(void* sys, const char* name, unsigned mode, void* exinfo, void** sound) {
    const int r = g_origStream(sys, name, mode, exinfo, sound);
    if (r == 0 && sound) note_sound(*sound, henry_name(name, mode));
    return r;
}

// A line of Henry's gets a pitch shifter of its own on its channel (FMOD Studio may move the channel between its groups; the
// shifter stays with it). The sweep below gives it back when the line has ended.
void shift(void* sys, void* sound, void** channel) {
    if (!channel || !*channel || g_preset.load() == 0) return;
    void* dsp = nullptr;
    if (g_createDsp(sys, kDspPitchShift, &dsp) != 0 || !dsp) { c_groupFail.fetch_add(1); return; }
    g_setParam(dsp, kParamFftSize, 2048.0f);
    g_setParam(dsp, kParamPitch, pitch_of(g_preset.load()));
    if (g_chanAdd(*channel, 0, dsp) != 0) { g_dspRelease(dsp); c_groupFail.fetch_add(1); return; }
    c_rerouted.fetch_add(1);
    std::lock_guard<std::mutex> lk(g_lock);
    g_active.push_back({*channel, sound, dsp});
}

bool is_henry(void* sound) {
    if (!sound) return false;
    std::lock_guard<std::mutex> lk(g_lock);
    return g_henry.count(sound) != 0;
}

int hk_play(void* sys, void* sound, void* group, bool paused, void** channel) {
    const int r = g_origPlay(sys, sound, group, paused, channel);
    if (r == 0 && is_henry(sound)) { c_henryPlayed.fetch_add(1); shift(sys, sound, channel); }
    return r;
}
int hk_play_studio(void* sys, void* sound, void* group, bool paused, void** channel) {
    const int r = g_origPlayStudio(sys, sound, group, paused, channel);
    if (r == 0 && is_henry(sound)) { c_henryPlayed.fetch_add(1); shift(sys, sound, channel); }
    return r;
}

// Main thread, every frame: a shifter whose line has ended (its channel stopped or plays another sound) is removed and released.
void tick() {
    std::vector<Active> done;
    {
        std::lock_guard<std::mutex> lk(g_lock);
        if (g_active.empty()) return;
        for (size_t i = 0; i < g_active.size();) {
            int playing = 0; void* cur = nullptr;
            const bool live = g_chanPlaying(g_active[i].channel, &playing) == 0 && playing && g_chanSound(g_active[i].channel, &cur) == 0 && cur == g_active[i].sound;
            if (live) { ++i; continue; }
            done.push_back(g_active[i]);
            g_active[i] = g_active.back(); g_active.pop_back();
        }
    }
    for (auto& a : done) { g_chanRemove(a.channel, a.dsp); g_dspRelease(a.dsp); }
}

void install() {
    HMODULE imp = GetModuleHandleA(kImporter), fm = GetModuleHandleA(kFmod);
    if (!imp || !fm) { g_why = "CryAudioImplFmod.dll / fmod.dll not loaded"; logf("FS-VOICE not armed: %s", g_why.c_str()); return; }
    g_createDsp = reinterpret_cast<CreateDspFn>(GetProcAddress(fm, "FMOD_System_CreateDSPByType"));
    g_setParam = reinterpret_cast<SetParamFn>(GetProcAddress(fm, "FMOD_DSP_SetParameterFloat"));
    g_addDsp = reinterpret_cast<AddDspFn>(GetProcAddress(fm, "FMOD_ChannelGroup_AddDSP"));
    g_createGroup = reinterpret_cast<CreateGroupFn>(GetProcAddress(fm, "FMOD_System_CreateChannelGroup"));
    g_addGroup = reinterpret_cast<AddGroupFn>(GetProcAddress(fm, "FMOD_ChannelGroup_AddGroup"));
    g_master = reinterpret_cast<MasterFn>(GetProcAddress(fm, "FMOD_System_GetMasterChannelGroup"));
    g_chanAdd = reinterpret_cast<ChanAddDspFn>(GetProcAddress(fm, "FMOD_Channel_AddDSP"));
    g_chanRemove = reinterpret_cast<ChanRemoveDspFn>(GetProcAddress(fm, "FMOD_Channel_RemoveDSP"));
    g_chanPlaying = reinterpret_cast<ChanPlayingFn>(GetProcAddress(fm, "FMOD_Channel_IsPlaying"));
    g_chanSound = reinterpret_cast<ChanSoundFn>(GetProcAddress(fm, "FMOD_Channel_GetCurrentSound"));
    g_dspRelease = reinterpret_cast<DspReleaseFn>(GetProcAddress(fm, "FMOD_DSP_Release"));
    if (!g_createDsp || !g_setParam || !g_chanAdd || !g_chanRemove || !g_chanPlaying || !g_chanSound || !g_dspRelease) { g_why = "an FMOD export is missing"; logf("FS-VOICE not armed: %s", g_why.c_str()); return; }
    // all three or none: a half-hooked set would mark sounds it never plays (or play sounds it never saw made)
    void* s = hook_iat(imp, kFmod, kSymCreateSound, reinterpret_cast<void*>(&hk_sound));
    if (!s) { g_why = "createSound not in the import table"; logf("FS-VOICE not armed: %s", g_why.c_str()); return; }
    g_origSound = reinterpret_cast<CreateFn>(s);
    void* t = hook_iat(imp, kFmod, kSymCreateStream, reinterpret_cast<void*>(&hk_stream));
    if (!t) { hook_iat(imp, kFmod, kSymCreateSound, s); g_why = "createStream not in the import table"; logf("FS-VOICE not armed: %s", g_why.c_str()); return; }
    g_origStream = reinterpret_cast<CreateFn>(t);
    void* p = hook_iat(imp, kFmod, kSymPlay, reinterpret_cast<void*>(&hk_play));
    if (!p) {
        hook_iat(imp, kFmod, kSymCreateSound, s); hook_iat(imp, kFmod, kSymCreateStream, t);
        g_why = "playSound not in the import table"; logf("FS-VOICE not armed: %s", g_why.c_str()); return;
    }
    g_origPlay = reinterpret_cast<PlayFn>(p);
    // FMOD Studio plays the voice lines itself (programmer sounds): its own playSound import too
    if (HMODULE st = GetModuleHandleA("fmodstudio.dll")) {
        void* q = hook_iat(st, kFmod, kSymPlay, reinterpret_cast<void*>(&hk_play_studio));
        if (q) g_origPlayStudio = reinterpret_cast<PlayFn>(q);
    }
    if (!g_origPlayStudio) logf("FS-VOICE: fmodstudio.dll's playSound not hooked (its lines keep their pitch)");
    g_armed = true;
    g_why.clear();
    logf("FS-VOICE armed: %s's createSound / createStream / playSound hooked (FMOD pitch shifter for the player's own voice)", kImporter);
}

void apply(int p) {
    g_preset = p;
    {
        std::lock_guard<std::mutex> lk(g_lock);
        for (auto& a : g_active) g_setParam(a.dsp, kParamPitch, pitch_of(p));
    }
    logf("FS-VOICE preset %d (pitch %.2f) armed=%d henry_sounds=%u played=%u rerouted=%u group_failures=%u", p, pitch_of(p), g_armed.load() ? 1 : 0,
         c_henryMade.load(), c_henryPlayed.load(), c_rerouted.load(), c_groupFail.load());
}

uint8_t op(uint8_t opc, const uint8_t* body, size_t len, uint8_t* out, size_t cap, size_t* outLen) {
    *outLen = 0;
    if (opc != kOpVoice || len != 2 || body[1] > 4 || cap < 1) return wo163::kRBadRequest;
    apply(body[1]);
    out[0] = g_armed.load() ? 1 : 0;
    *outLen = 1;
    return wo163::kROk;
}

int status(char* out, int n) {
    return std::snprintf(out, static_cast<size_t>(n), "fsvoice armed=%d%s%s preset=%d henry_sounds=%u played=%u shifted=%u live=%zu",
                         g_armed.load() ? 1 : 0, g_why.empty() ? "" : " why=", g_why.c_str(), g_preset.load(), c_henryMade.load(), c_henryPlayed.load(),
                         c_rerouted.load(), g_active.size());
}

const bool kReg = wo203::reg_install("fsvoice", &install) && wo203::reg_op(kOpVoice, &op) && wo203::reg_status("fsvoice", &status)
                  && wo203::reg_tick("fsvoice", &tick);

}  // namespace

void set_preset(int p) { if (p >= 0 && p <= 4) apply(p); }

}  // namespace kcdmp::fsvoice
