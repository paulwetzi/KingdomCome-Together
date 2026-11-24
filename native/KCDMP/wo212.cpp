// Copyright (C) 2026 the Kingdom Come: Together contributors (AUTHORS). SPDX-License-Identifier: GPL-3.0-only
// GPLv3 section 7 additional terms: NOTICE. This project's own code only; Kingdom Come: Deliverance II and its
// content belong to Warhorse Studios and PLAION. Unofficial, free, not affiliated with or endorsed by them.
// WO-212: the inventory keys come back (wo212.h).
//
// The one function in CombatModule that names "no_inventory" is the controller's own switch:
//   game = GetGameIface(); framework = game+8; manager = framework->vfunc(0xE0);
//   on:  manager->vfunc(0xF0)(name, 0x11)      off: manager->vfunc(0xF8)(name, 0x11, 0)
// Every part of that shape is checked on the running build before the call; a patch that changes it refuses, it never guesses.
#include "wo212.h"
#include "wo203.h"
#include "wo163.h"
#include "anchors.h"
#include "engine.h"
#include "fault_guard.h"
#include "log.h"
#include <windows.h>
#include <atomic>
#include <cstddef>
#include <cstring>

namespace kcdmp::wo212 {

namespace {

constexpr size_t kManagerSlot = 0xE0, kSetSlot = 0xF0, kClearSlot = 0xF8;
constexpr uint8_t kFilterMask = 0x11;

const char* filter_name() {
    static const char* s_name = nullptr;
    static bool s_checked = false;
    if (s_checked) return s_name;
    HMODULE mod = GetModuleHandleA("CombatModule.dll");
    if (!mod) return nullptr;   // not loaded yet: asked again next time
    s_checked = true;
    const char* name = anchor::find_cstring(mod, "no_inventory");
    const uint8_t* fn = name ? anchor::function_by_string(mod, "no_inventory") : nullptr;
    static const uint8_t kFramework[] = {0x48, 0x8B, 0x48, 0x08};                    // mov rcx,[rax+8]
    static const uint8_t kManager[] = {0xFF, 0x90, 0xE0, 0x00, 0x00, 0x00};          // call [rax+0xE0]
    static const uint8_t kMask[] = {0x41, 0xB0, kFilterMask};                        // mov r8b,0x11
    static const uint8_t kSet[] = {0x49, 0xFF, 0xA2, 0xF0, 0x00, 0x00, 0x00};        // rex.W jmp [r10+0xF0] (a tail call)
    static const uint8_t kClear[] = {0x41, 0xFF, 0x92, 0xF8, 0x00, 0x00, 0x00};      // call [r10+0xF8]
    const bool shape = fn && anchor::function_has_bytes(mod, fn, kFramework, sizeof kFramework) &&
                       anchor::function_has_bytes(mod, fn, kManager, sizeof kManager) &&
                       anchor::function_has_bytes(mod, fn, kMask, sizeof kMask) &&
                       anchor::function_has_bytes(mod, fn, kSet, sizeof kSet) &&
                       anchor::function_has_bytes(mod, fn, kClear, sizeof kClear);
    if (shape) s_name = name;
    logf("WO212-KEYS the controller's filter switch %s", shape ? "found and checked" : "NOT as expected: the release stays off on this build");
    return s_name;
}

uint8_t apply(uint8_t mode) {
    if (!engine::resolve()) return kResultRefused;
    const char* name = filter_name();
    void* game = engine::game_iface();
    if (!name || !game) return kResultRefused;
    KCDMP_FAULT_CALL(site, "wo212::input_filter");
    const bool ok = fault::guarded(site, [&] {
        void* framework = *reinterpret_cast<void**>(static_cast<char*>(game) + 8);
        auto** fv = *reinterpret_cast<void***>(framework);
        void* manager = reinterpret_cast<void* (*)(void*)>(fv[kManagerSlot / 8])(framework);
        auto** mv = *reinterpret_cast<void***>(manager);
        if (mode == 1) reinterpret_cast<void (*)(void*, const char*, uint8_t)>(mv[kSetSlot / 8])(manager, name, kFilterMask);
        else reinterpret_cast<void (*)(void*, const char*, uint8_t, int)>(mv[kClearSlot / 8])(manager, name, kFilterMask, 0);
    });
    return ok ? kResultDone : kResultFault;
}

uint8_t op(uint8_t opc, const uint8_t* body, size_t len, uint8_t* out, size_t cap, size_t* outLen) {
    *outLen = 0;
    if (opc != kOpInputFilter || len != 2 || body[1] > 1 || cap < 1) return wo163::kRBadRequest;
    out[0] = apply(body[1]);
    *outLen = 1;
    logf("WO212-KEYS filter no_inventory %s -> %s", body[1] == 1 ? "set (test)" : "released",
         out[0] == kResultDone ? "done" : out[0] == kResultFault ? "fault" : "refused");
    return wo163::kROk;
}

const bool kReg = wo203::reg_op(kOpInputFilter, &op);

} // namespace

} // namespace kcdmp::wo212