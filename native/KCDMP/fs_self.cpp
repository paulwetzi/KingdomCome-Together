// Copyright (C) 2026 the Kingdom Come: Together contributors (AUTHORS). SPDX-License-Identifier: GPL-3.0-only
// GPLv3 section 7 additional terms: NOTICE. This project's own code only; Kingdom Come: Deliverance II and its
// content belong to Warhorse Studios and PLAION. Unofficial, free, not affiliated with or endorsed by them.
// Fixed-soul build: the own figure's copy follows the local player (fs_self.h).
#include "fs_self.h"
#include "wo203.h"
#include "wo163.h"
#include "npc_drive.h"
#include "local_state.h"
#include "motion.h"
#include "engine.h"
#include "log.h"
#include "anchors.h"
#include "fault_guard.h"
#include <windows.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

namespace kcdmp::fsself {

namespace {

bool g_on = false;              // main thread only (ops and the tick both run there)
std::string g_name;
uint32_t g_eid = 0;
bool g_bound = false;
uint16_t g_seq = 0;
uint32_t g_samples = 0, g_binds = 0, g_bindRefused = 0;
double g_bindTryAt = -1e9, g_findAt = -1e9;
constexpr double kBindRetryS = 1.0;
constexpr uint16_t kDelayMs = 40;
float g_testAhead = 0.0f;   // the samples come every frame: two frames of play-out are enough

void unbind(const char* why) {
    if (g_bound && !g_name.empty()) {
        npcdrive::BindRequest r{};
        r.on = false;
        r.eid = g_eid;
        std::strncpy(r.name, g_name.c_str(), sizeof r.name - 1);
        npcdrive::bind_main(r);
    }
    if (g_bound || g_on) logf("FS-SELF %s off (%s) samples=%u binds=%u refused=%u", g_name.c_str(), why, g_samples, g_binds, g_bindRefused);
    g_bound = false;
    g_eid = 0;
}

// ---- the copy's lips follow Henry's -----------------------------------------------------------------------------------------
// Every actor's speech manager (EntityModule's C_ActorSpeakMan, RTTI) starts a line's lip sync as an animation on the face
// (OnPlayFileStarted, the function holding "Lipsync file '%s' started on actor %s."): its player at +0x98 is started with the
// file named at +0x48 (start(player, file, 0, speed), then play(player)) and stopped when the line ends (stop(player): its
// animation -1, its state 5). Henry's player is read every frame: a new start (its start counter at +0x44 changed) starts the same
// file on the copy's own player, Henry's stop stops it. The three functions are taken from OnPlayFileStarted's own calls and
// checked by their first bytes; anything unexpected and the lips stay as they were.
struct Lip {
    bool tried = false, ok = false;
    void* const* vSpeak = nullptr;
    using StartFn = void (*)(void*, const char*, uint8_t, float);
    using OneFn = void (*)(void*);
    StartFn start = nullptr;
    OneFn play = nullptr, stop = nullptr;
    uint32_t lastCount = 0;
    bool haveCount = false, copyPlaying = false;
    uint32_t starts = 0, stops = 0;
};
Lip g_lip;
constexpr size_t kSpeakFile = 0x48, kSpeakPlayer = 0x98, kPlayerState = 0x1C, kPlayerAnim = 0x24, kPlayerSpeed = 0x30, kPlayerCount = 0x44;
constexpr uint32_t kPlayerEntity = 0x7777;

template <class T> bool rdg(const void* base, size_t off, T* out) {
    if (!base || !fault::plausible_address(reinterpret_cast<uintptr_t>(base) + off)) return false;
    KCDMP_FAULT_READ(site, "fs_self::rd");
    return fault::guarded(site, [&] { *out = *reinterpret_cast<const T*>(static_cast<const char*>(base) + off); });
}

const uint8_t* call_target(const uint8_t* at) {   // E8 rel32 at `at`
    int32_t rel = 0;
    if (!rdg(at, 1, &rel)) return nullptr;
    return at + 5 + rel;
}

bool head_is(const uint8_t* f, const uint8_t* pat, size_t n) {
    if (!f) return false;
    for (size_t k = 0; k < n; ++k) { uint8_t c = 0; if (!rdg(f, k, &c) || c != pat[k]) return false; }
    return true;
}

bool lip_resolve() {
    if (g_lip.tried) return g_lip.ok;
    g_lip.tried = true;
    HMODULE em = GetModuleHandleA("EntityModule.dll");
    const uint8_t* fn = em ? anchor::function_by_string(em, "Lipsync file '%s' started on actor %s.") : nullptr;
    g_lip.vSpeak = em ? anchor::find_vftable(em, ".?AVC_ActorSpeakMan@entitymodule@wh@@", 0) : nullptr;
    if (!fn || !g_lip.vSpeak) {
        logf("FS-LIPS not armed: OnPlayFileStarted %p, speech manager vftable %p", static_cast<const void*>(fn), static_cast<const void*>(g_lip.vSpeak));
        return false;
    }
    static const uint8_t kStartCall[] = {0x48, 0x8D, 0x8E, 0x98, 0x00, 0x00, 0x00, 0x45, 0x33, 0xC0, 0xE8};   // lea rcx,[rsi+98h]; xor r8d,r8d; call start
    static const uint8_t kPlayCall[] = {0x48, 0x8D, 0x8E, 0x98, 0x00, 0x00, 0x00, 0xE8};                     // lea rcx,[rsi+98h]; call play
    static const uint8_t kStopCall[] = {0x48, 0x8D, 0x8E, 0x10, 0x01, 0x00, 0x00, 0xE8};                     // lea rcx,[rsi+110h]; call stop (the mood player)
    static const uint8_t kFile[] = {0x48, 0x8B, 0x56, 0x48};                                                  // mov rdx,[rsi+48h]
    const uint8_t *start = nullptr, *play = nullptr, *stop = nullptr;
    bool file = false;
    for (size_t i = 0; i < 0x200; ++i) {
        uint8_t w[11]{};
        bool readable = true;
        for (size_t k = 0; k < sizeof w && readable; ++k) readable = rdg(fn, i + k, &w[k]);
        if (!readable) break;
        if (!start && std::memcmp(w, kStartCall, sizeof kStartCall) == 0) start = call_target(fn + i + 10);
        else if (start && !play && std::memcmp(w, kPlayCall, sizeof kPlayCall) == 0) play = call_target(fn + i + 7);
        if (!stop && std::memcmp(w, kStopCall, sizeof kStopCall) == 0) stop = call_target(fn + i + 7);
        if (std::memcmp(w, kFile, sizeof kFile) == 0) file = true;
    }
    static const uint8_t kStartHead[] = {0x40, 0x53, 0x55, 0x57, 0x48, 0x81, 0xEC, 0x80, 0x00, 0x00, 0x00};
    static const uint8_t kStopHead[] = {0x48, 0x89, 0x5C, 0x24, 0x10, 0x56, 0x48, 0x83, 0xEC, 0x20};
    static const uint8_t kPlayHead[] = {0x40, 0x53, 0x48, 0x83, 0xEC, 0x20};
    g_lip.ok = file && head_is(start, kStartHead, sizeof kStartHead) && head_is(play, kPlayHead, sizeof kPlayHead) && head_is(stop, kStopHead, sizeof kStopHead);
    if (g_lip.ok) {
        g_lip.start = reinterpret_cast<Lip::StartFn>(const_cast<uint8_t*>(start));
        g_lip.play = reinterpret_cast<Lip::OneFn>(const_cast<uint8_t*>(play));
        g_lip.stop = reinterpret_cast<Lip::OneFn>(const_cast<uint8_t*>(stop));
    }
    logf("FS-LIPS %s: start=%p play=%p stop=%p file_read=%d", g_lip.ok ? "armed" : "NOT armed (the speech manager is not as expected)",
         static_cast<const void*>(start), static_cast<const void*>(play), static_cast<const void*>(stop), file ? 1 : 0);
    return g_lip.ok;
}

// The actor's speech manager: made the first time the actor speaks, kept at actor+0x210 (EntityModule's own getter reads it there:
// GetSpeakMan = "mov rax,[rcx+210h]"; GetOrCreateSpeakMan makes it when it is null). The copy never speaks, so its own is made here
// with the game's GetOrCreateSpeakMan, found once by its exact bytes in EntityModule's code (one match or none).
constexpr size_t kActorSpeak = 0x210;
using MakeSpeakFn = void* (*)(void* actor);
MakeSpeakFn g_makeSpeak = nullptr;
bool g_makeTried = false;

void find_make_speak() {
    if (g_makeTried) return;
    g_makeTried = true;
    HMODULE em = GetModuleHandleA("EntityModule.dll");
    if (!em) return;
    auto* base = reinterpret_cast<const uint8_t*>(em);
    auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + reinterpret_cast<const IMAGE_DOS_HEADER*>(base)->e_lfanew);
    const IMAGE_SECTION_HEADER* sec = IMAGE_FIRST_SECTION(nt);
    // push rbx; sub rsp,20h; mov rbx,rcx; lea rcx,[..]; call ..; cmp qword ptr [rbx+210h],0; jne
    static const int kPat[] = {0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xD9, 0x48, 0x8D, 0x0D, -1, -1, -1, -1, 0xE8, -1, -1, -1, -1,
                               0x48, 0x83, 0xBB, 0x10, 0x02, 0x00, 0x00, 0x00, 0x75};
    const size_t n = sizeof kPat / sizeof kPat[0];
    const uint8_t* found = nullptr;
    int matches = 0;
    for (int s = 0; s < nt->FileHeader.NumberOfSections; ++s, ++sec) {
        if (!(sec->Characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;
        const uint8_t* a = base + sec->VirtualAddress;
        const size_t len = sec->Misc.VirtualSize;
        for (size_t i = 0; i + n < len; ++i) {
            if (a[i] != 0x40 || a[i + 1] != 0x53) continue;
            size_t k = 0;
            for (; k < n; ++k) if (kPat[k] >= 0 && a[i + k] != static_cast<uint8_t>(kPat[k])) break;
            if (k == n) { found = a + i; ++matches; }
        }
    }
    if (matches == 1) g_makeSpeak = reinterpret_cast<MakeSpeakFn>(const_cast<uint8_t*>(found));
    logf("FS-LIPS GetOrCreateSpeakMan %s (%d match(es) at %p)", g_makeSpeak ? "found" : "NOT found", matches, static_cast<const void*>(found));
}

void* speak_of(void* actor, bool make) {
    if (!actor) return nullptr;
    void* s = nullptr; void* vp = nullptr;
    if (rdg(actor, kActorSpeak, &s) && s && rdg(s, 0, &vp) && vp == static_cast<const void*>(g_lip.vSpeak)) return s;
    if (!make) return nullptr;
    find_make_speak();
    if (!g_makeSpeak) return nullptr;
    KCDMP_FAULT_CALL(site, "fs_self::make_speak");
    void* made = nullptr;
    if (!fault::guarded(site, [&] { made = g_makeSpeak(actor); })) return nullptr;
    return made && rdg(made, 0, &vp) && vp == static_cast<const void*>(g_lip.vSpeak) ? made : nullptr;
}

void lip_tick(uint32_t copyEid) {
    if (!lip_resolve()) return;
    static void* s_henryActor = nullptr;
    static void* s_henrySpeak = nullptr;
    static uint32_t s_copyEid = 0;
    static void* s_copySpeak = nullptr;
    static double s_lookAt = -1e9;
    const double now = npcdrive::now_s();
    void* ha = motion::actor_of(kPlayerEntity);
    if (ha != s_henryActor || (!s_henrySpeak && now - s_lookAt >= 1.0)) {   // Henry's is made at his first line
        s_lookAt = now;
        const bool changed = ha != s_henryActor;
        s_henryActor = ha;
        void* hs = speak_of(ha, false);
        if (hs != s_henrySpeak || changed) logf("FS-LIPS Henry's speech manager %p (actor %p)", hs, ha);
        s_henrySpeak = hs;
    }
    if (copyEid != s_copyEid || !s_copySpeak) {
        const bool changed = copyEid != s_copyEid;
        s_copyEid = copyEid;
        if (changed || now - s_lookAt < 0.001) {   // once per body, and again only on Henry's once-a-second look
            s_copySpeak = speak_of(motion::actor_of(copyEid), true);
            g_lip.copyPlaying = false;
            logf("FS-LIPS the copy's speech manager %p", s_copySpeak);
        }
    }
    if (!s_henrySpeak || !s_copySpeak) return;
    char* hp = static_cast<char*>(s_henrySpeak) + kSpeakPlayer;
    char* cp = static_cast<char*>(s_copySpeak) + kSpeakPlayer;
    uint32_t count = 0;
    int32_t anim = -1, state = 0;
    float speed = 1.0f;
    const char* file = nullptr;
    if (!rdg(hp, kPlayerCount, &count) || !rdg(hp, kPlayerAnim, &anim) || !rdg(hp, kPlayerState, &state) || !rdg(hp, kPlayerSpeed, &speed)) return;
    // the first look: a line already running (Henry's manager is made at his first line, so that one usually is) is mirrored at once
    const bool first = !g_lip.haveCount;
    if (first) { g_lip.haveCount = true; g_lip.lastCount = count; }
    if (count != g_lip.lastCount || (first && anim >= 0 && state != 5)) {
        g_lip.lastCount = count;
        if (rdg(s_henrySpeak, kSpeakFile, &file) && file && anim >= 0) {
            char name[200]{};
            KCDMP_FAULT_READ(nsite, "fs_self::lip_file");
            const bool named = fault::guarded(nsite, [&] { std::strncpy(name, file, sizeof name - 1); });
            if (named && name[0]) {
                KCDMP_FAULT_CALL(csite, "fs_self::lip_start");
                const float sp = (speed > 0.1f && speed < 10.0f) ? speed : 1.0f;
                const bool ok = fault::guarded(csite, [&] { g_lip.start(cp, name, 0, sp); g_lip.play(cp); });
                g_lip.copyPlaying = ok;
                if (ok && (++g_lip.starts <= 20 || g_lip.starts % 100 == 0)) logf("FS-LIPS the copy speaks %s [n=%u]", name, g_lip.starts);
            }
        }
        return;
    }
    if (g_lip.copyPlaying && (anim < 0 || state == 5)) {   // Henry's line ended
        KCDMP_FAULT_CALL(ssite, "fs_self::lip_stop");
        fault::guarded(ssite, [&] { g_lip.stop(cp); });
        g_lip.copyPlaying = false;
        ++g_lip.stops;
    }
}

void tick() {
    if (!g_on) return;
    localstate::LocalState ls{};
    if (!localstate::read_local_state(&ls)) return;
    motion::State2 st2{};
    const bool have2 = motion::read_local_state2(&st2, ls.rotZ);
    const double now = npcdrive::now_s();
    // the body by its id every frame; the name walk (every entity) at most once a second, while it is not known
    void* e = g_eid ? engine::entity_by_id(g_eid) : nullptr;
    if (!e) {
        if (g_bound) { g_bound = false; g_eid = 0; logf("FS-SELF %s: the body is gone -- waiting for it", g_name.c_str()); }
        if (now - g_findAt < 1.0) return;
        g_findAt = now;
        e = npcdrive::entity_by_name(g_name.c_str());
        if (!e) return;   // the mod removed it, or a load did: bound again when it is back
        g_eid = engine::entity_id(e);
    }
    const uint32_t eid = g_eid;
    if (!g_bound && now - g_bindTryAt >= kBindRetryS) {
        g_bindTryAt = now;
        npcdrive::BindRequest r{};
        r.on = true;
        r.eid = eid;
        r.anchor[0] = ls.x; r.anchor[1] = ls.y; r.anchor[2] = ls.z;
        r.delayMs = kDelayMs;
        std::strncpy(r.name, g_name.c_str(), sizeof r.name - 1);
        const uint8_t res = npcdrive::bind_main(r);
        g_bound = res == npcdrive::kOk;
        if (g_bound) ++g_binds; else ++g_bindRefused;
        logf("FS-SELF bind %s eid=0x%X -> %s", g_name.c_str(), eid, g_bound ? "ok" : npcdrive::reason_name(res));
    }
    float sx = ls.x, sy = ls.y, sr = ls.rotZ;
    if (g_testAhead > 0.0f) {   // research only ("selfahead <m>"): the copy stands that far ahead, facing the player
        sx += -std::sin(ls.rotZ) * g_testAhead; sy += std::cos(ls.rotZ) * g_testAhead; sr = ls.rotZ + 3.14159265f;
    }
    npcdrive::inject_sample(g_name.c_str(), sx, sy, ls.z, sr, ls.flags, ++g_seq, have2 ? &st2 : nullptr);
    ++g_samples;
    if (g_bound) lip_tick(g_eid);
}

uint8_t op(uint8_t opc, const uint8_t* body, size_t len, uint8_t* out, size_t cap, size_t* outLen) {
    *outLen = 0;
    if (opc == kOpSelf) {
        if (len < 3 || cap < 1) return wo163::kRBadRequest;
        const uint8_t on = body[1], nl = body[2];
        if (on > 1 || len != 3u + nl || nl > 60) return wo163::kRBadRequest;
        std::string name(reinterpret_cast<const char*>(body + 3), nl);
        if (on && (name.rfind("kcd2mp_", 0) != 0)) return wo163::kRBadRequest;   // only a body of the mod's own
        if (!on || name != g_name) unbind(on ? "another body" : "asked");
        g_on = on != 0;
        if (g_on) { g_name = name; g_eid = 0; g_bindTryAt = g_findAt = -1e9; logf("FS-SELF on: %s follows the player", g_name.c_str()); }
        out[0] = !g_on ? 0 : g_bound ? 1 : 2;
        *outLen = 1;
        return wo163::kROk;
    }
    if (opc == kOpAttachNames) {
        if (len < 2 || cap < 2) return wo163::kRBadRequest;
        const uint8_t nl = body[1];
        if (len != 2u + nl || nl == 0 || nl > 60) return wo163::kRBadRequest;
        std::string name(reinterpret_cast<const char*>(body + 2), nl);
        void* e = npcdrive::entity_by_name(name.c_str());
        if (!e) { out[0] = 0; *outLen = 1; return wo163::kROk; }
        const int n = motion::attach_list(e, name.c_str(), reinterpret_cast<char*>(out), static_cast<int>(cap > 3000 ? 3000 : cap));
        *outLen = n > 0 ? std::strlen(reinterpret_cast<char*>(out)) : 0;
        return wo163::kROk;
    }
    return wo163::kRBadRequest;
}

int status(char* out, int n) {
    return std::snprintf(out, static_cast<size_t>(n), "fsself on=%d name=%s bound=%d samples=%u binds=%u refused=%u lips=%d lip_starts=%u lip_stops=%u",
                         g_on ? 1 : 0, g_name.c_str(), g_bound ? 1 : 0, g_samples, g_binds, g_bindRefused, g_lip.ok ? 1 : 0, g_lip.starts, g_lip.stops);
}

const bool kReg = wo203::reg_op(kOpSelf, &op) && wo203::reg_op(kOpAttachNames, &op) && wo203::reg_tick("fsself", &tick)
                  && wo203::reg_status("fsself", &status);

}  // namespace

void research_ahead(float m) { g_testAhead = (m > 0.0f && m < 5.0f) ? m : 0.0f; logf("FS-SELF research: the copy %.2f m ahead", g_testAhead); }

void research_set(const char* name) {   // the research verb "self <body>|off": the op's own work, without the agent
    uint8_t body[64]{}, out[4]{};
    size_t outLen = 0;
    const bool on = name && std::strcmp(name, "off") != 0;
    const size_t nl = on ? std::strlen(name) : std::strlen("kcd2mp_200");
    if (nl > 60) return;
    body[0] = kOpSelf; body[1] = on ? 1 : 0; body[2] = static_cast<uint8_t>(nl);
    std::memcpy(body + 3, on ? name : "kcd2mp_200", nl);
    op(kOpSelf, body, 3 + nl, out, sizeof out, &outLen);
}

}  // namespace kcdmp::fsself
