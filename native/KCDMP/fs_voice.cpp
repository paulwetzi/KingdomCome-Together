// Copyright (C) 2026 the Kingdom Come: Together contributors (AUTHORS). SPDX-License-Identifier: GPL-3.0-only
// GPLv3 section 7 additional terms: NOTICE. This project's own code only; Kingdom Come: Deliverance II and its
// content belong to Warhorse Studios and PLAION. Unofficial, free, not affiliated with or endorsed by them.
// Fixed-soul build: voice presets for the player's own voice (Henry's lines, a higher pitch).
//
// The game plays its voice lines with FMOD core (fmod.dll 2.02): CryAudioImplFmod.dll creates each line's sound by its file name
// (System::createSound / createStream) and plays it (System::playSound) -- all three through its import table, which is hooked
// here (iat_hook.h: one pointer each, nothing of the game's code changed). Henry's lines are the files of his voice actor,
// "tmck_*.ogg" (43,284 of the German files). FMOD Studio plays the voice lines itself (programmer sounds, through fmodstudio.dll's
// own playSound import, hooked too), so each of Henry's lines gets a voice filter of ours on its channel (a custom FMOD DSP running
// voice_shift.h, a TD-PSOLA: the pitch moved by P, the formants only by Q -- FMOD's own pitch shifter moves both and makes a man sound like a
// small creature), released when the line ends. Every other sound is untouched. Preset 0 plays Henry's lines as before. The
// research verb "vtune <P> <Q> [lift]" sets the active preset's values live (for tuning by ear), "vtune off" goes back to the table.
#include "fs_voice.h"
#include "voice_shift.h"
#include "iat_hook.h"
#include "wo203.h"
#include "wo163.h"
#include "fault_guard.h"
#include "log.h"
#include <windows.h>
#include <atomic>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <new>
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
using CreateCustomFn = int (*)(void* sys, const void* desc, void** dsp);

// FMOD 2.02 plug-in SDK (fmod_dsp.h): the description of a DSP of our own, and the head of the state FMOD hands its callbacks
struct DspState { void* instance; void* plugindata; };
using DspCb = int (*)(DspState*);
using DspReadCb = int (*)(DspState*, float* in, float* out, unsigned len, int inch, int* outch);
struct DspDesc {
    unsigned pluginsdkversion; char name[32]; unsigned version; int numinputbuffers, numoutputbuffers;
    DspCb create, release, reset; DspReadCb read; void* process; void* setposition;
    int numparameters; void** paramdesc;
    void *setfloat, *setint, *setbool, *setdata, *getfloat, *getint, *getbool, *getdata, *shouldiprocess;
    void* userdata; void *sys_register, *sys_deregister, *sys_mix;
};
constexpr unsigned kPluginSdk = 110;   // FMOD_PLUGIN_SDK_VERSION (2.02)

constexpr const char* kImporter = "CryAudioImplFmod.dll";
constexpr const char* kFmod = "fmod.dll";
constexpr const char* kSymCreateSound = "?createSound@System@FMOD@@QEAA?AW4FMOD_RESULT@@PEBDIPEAUFMOD_CREATESOUNDEXINFO@@PEAPEAVSound@2@@Z";
constexpr const char* kSymCreateStream = "?createStream@System@FMOD@@QEAA?AW4FMOD_RESULT@@PEBDIPEAUFMOD_CREATESOUNDEXINFO@@PEAPEAVSound@2@@Z";
constexpr const char* kSymPlay = "?playSound@System@FMOD@@QEAA?AW4FMOD_RESULT@@PEAVSound@2@PEAVChannelGroup@2@_NPEAPEAVChannel@2@@Z";
constexpr unsigned kOpenNotAName = 0x8 | 0x800 | 0x10000000;   // FMOD_OPENUSER | FMOD_OPENMEMORY | FMOD_OPENMEMORY_POINT: no file name

CreateFn g_origSound = nullptr, g_origStream = nullptr;
PlayFn g_origPlay = nullptr;
CreateCustomFn g_createCustom = nullptr;
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

// The presets: pitch P, formants Q (lift: unused by the PSOLA filter). 0 is Henry as recorded; 1 a woman (Henry's ~137 Hz to ~215 Hz,
// chosen by ear against the WORLD vocoder's rendering), 2 brighter, 3 warmer, 4 deeper.
struct Tune { float P, Q; int lift; };
constexpr Tune kTable[5] = {{1.0f, 1.0f, 90}, {1.57f, 1.20f, 90}, {1.68f, 1.25f, 90}, {1.46f, 1.15f, 90}, {0.85f, 0.95f, 90}};
std::atomic<float> g_P{1.0f}, g_Q{1.0f};
std::atomic<int> g_lift{90};
std::atomic<bool> g_tuned{false};             // "vtune": the values are set by hand, not the table's
std::atomic<uint32_t> c_blocks{0};

void load_tune(int p) {
    if (g_tuned.load()) return;
    const Tune& t = kTable[p >= 0 && p <= 4 ? p : 0];
    g_P = t.P; g_Q = t.Q; g_lift = t.lift;
}

// The DSP's callbacks (FMOD's mixer thread): one Shifter per line (~200 KB), mono (Henry's lines are mono; a wider input is mixed down and
// the result written to every channel).
int dsp_create(DspState* s) { s->plugindata = new (std::nothrow) voiceshift::Shifter(); return s->plugindata ? 0 : 38 /* FMOD_ERR_MEMORY */; }
int dsp_release(DspState* s) { delete static_cast<voiceshift::Shifter*>(s->plugindata); s->plugindata = nullptr; return 0; }
int dsp_reset(DspState* s) { if (auto* sh = static_cast<voiceshift::Shifter*>(s->plugindata)) sh->reset(); return 0; }
int dsp_read(DspState* s, float* in, float* out, unsigned len, int inch, int* outch) {
    auto* sh = static_cast<voiceshift::Shifter*>(s->plugindata);
    const int oc = outch && *outch > 0 ? *outch : inch;
    if (!sh || inch <= 0 || oc <= 0) { if (oc > 0) std::memset(out, 0, sizeof(float) * len * oc); return 0; }
    float mono[1024], res[1024];
    for (unsigned base = 0; base < len; base += 1024) {
        const unsigned n = len - base < 1024 ? len - base : 1024;
        for (unsigned i = 0; i < n; ++i) { float a = 0; for (int c = 0; c < inch; ++c) a += in[(base + i) * inch + c]; mono[i] = a / inch; }
        sh->process(mono, res, static_cast<int>(n), g_P.load(), g_Q.load(), g_lift.load(), 48000.0f);
        for (unsigned i = 0; i < n; ++i) for (int c = 0; c < oc; ++c) out[(base + i) * oc + c] = res[i];
    }
    c_blocks.fetch_add(1, std::memory_order_relaxed);
    return 0;
}
DspDesc make_desc() {
    DspDesc d{};
    d.pluginsdkversion = kPluginSdk;
    std::strncpy(d.name, "KCDMP voice", sizeof d.name - 1);
    d.version = 1; d.numinputbuffers = 1; d.numoutputbuffers = 1;
    d.create = &dsp_create; d.release = &dsp_release; d.reset = &dsp_reset; d.read = &dsp_read;
    return d;
}
const DspDesc kDesc = make_desc();

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
    if (g_createCustom(sys, &kDesc, &dsp) != 0 || !dsp) { c_groupFail.fetch_add(1); return; }
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
    g_createCustom = reinterpret_cast<CreateCustomFn>(GetProcAddress(fm, "FMOD_System_CreateDSP"));
    g_addDsp = reinterpret_cast<AddDspFn>(GetProcAddress(fm, "FMOD_ChannelGroup_AddDSP"));
    g_createGroup = reinterpret_cast<CreateGroupFn>(GetProcAddress(fm, "FMOD_System_CreateChannelGroup"));
    g_addGroup = reinterpret_cast<AddGroupFn>(GetProcAddress(fm, "FMOD_ChannelGroup_AddGroup"));
    g_master = reinterpret_cast<MasterFn>(GetProcAddress(fm, "FMOD_System_GetMasterChannelGroup"));
    g_chanAdd = reinterpret_cast<ChanAddDspFn>(GetProcAddress(fm, "FMOD_Channel_AddDSP"));
    g_chanRemove = reinterpret_cast<ChanRemoveDspFn>(GetProcAddress(fm, "FMOD_Channel_RemoveDSP"));
    g_chanPlaying = reinterpret_cast<ChanPlayingFn>(GetProcAddress(fm, "FMOD_Channel_IsPlaying"));
    g_chanSound = reinterpret_cast<ChanSoundFn>(GetProcAddress(fm, "FMOD_Channel_GetCurrentSound"));
    g_dspRelease = reinterpret_cast<DspReleaseFn>(GetProcAddress(fm, "FMOD_DSP_Release"));
    if (!g_createCustom || !g_chanAdd || !g_chanRemove || !g_chanPlaying || !g_chanSound || !g_dspRelease) { g_why = "an FMOD export is missing"; logf("FS-VOICE not armed: %s", g_why.c_str()); return; }
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
    logf("FS-VOICE armed: %s's createSound / createStream / playSound hooked (voice filter for the player's own voice)", kImporter);
}

void apply(int p) {
    g_preset = p;
    load_tune(p);   // lines already playing take the new values at once (the DSP reads them per block)
    logf("FS-VOICE preset %d (P %.2f Q %.2f lift %d%s) armed=%d henry_sounds=%u played=%u rerouted=%u group_failures=%u blocks=%u", p, g_P.load(),
         g_Q.load(), g_lift.load(), g_tuned.load() ? " by hand" : "", g_armed.load() ? 1 : 0, c_henryMade.load(), c_henryPlayed.load(),
         c_rerouted.load(), c_groupFail.load(), c_blocks.load());
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
    return std::snprintf(out, static_cast<size_t>(n),
                         "fsvoice armed=%d%s%s preset=%d P=%.2f Q=%.2f lift=%d%s henry_sounds=%u played=%u shifted=%u live=%zu blocks=%u",
                         g_armed.load() ? 1 : 0, g_why.empty() ? "" : " why=", g_why.c_str(), g_preset.load(), g_P.load(), g_Q.load(),
                         g_lift.load(), g_tuned.load() ? " by-hand" : "", c_henryMade.load(), c_henryPlayed.load(), c_rerouted.load(),
                         g_active.size(), c_blocks.load());
}

const bool kReg = wo203::reg_install("fsvoice", &install) && wo203::reg_op(kOpVoice, &op) && wo203::reg_status("fsvoice", &status)
                  && wo203::reg_tick("fsvoice", &tick);

}  // namespace

void set_preset(int p) { if (p >= 0 && p <= 4) apply(p); }

void research_tune(const char* a1, const char* rest) {
    if (!a1 || !std::strcmp(a1, "off")) { g_tuned = false; load_tune(g_preset.load()); logf("FS-VOICE tune: back to the table"); return; }
    float P = static_cast<float>(std::atof(a1)), Q = 1.0f;
    int lift = 90;
    if (rest && *rest) std::sscanf(rest, "%f %d", &Q, &lift);
    if (!(P >= 0.5f && P <= 2.5f) || !(Q >= 0.5f && Q <= 2.0f)) { logf("FS-VOICE tune: out of range (P 0.5-2.5, Q 0.5-2.0)"); return; }
    g_P = P; g_Q = Q; g_lift = lift; g_tuned = true;
    logf("FS-VOICE tune by hand: P %.2f Q %.2f lift %d (preset %d)", P, Q, lift, g_preset.load());
}

}  // namespace kcdmp::fsvoice
