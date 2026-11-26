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
constexpr uint16_t kDelayMs = 40;   // the samples come every frame: two frames of play-out are enough

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
    npcdrive::inject_sample(g_name.c_str(), ls.x, ls.y, ls.z, ls.rotZ, ls.flags, ++g_seq, have2 ? &st2 : nullptr);
    ++g_samples;
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
    return std::snprintf(out, static_cast<size_t>(n), "fsself on=%d name=%s bound=%d samples=%u binds=%u refused=%u", g_on ? 1 : 0, g_name.c_str(),
                         g_bound ? 1 : 0, g_samples, g_binds, g_bindRefused);
}

const bool kReg = wo203::reg_op(kOpSelf, &op) && wo203::reg_op(kOpAttachNames, &op) && wo203::reg_tick("fsself", &tick)
                  && wo203::reg_status("fsself", &status);

}  // namespace

}  // namespace kcdmp::fsself
