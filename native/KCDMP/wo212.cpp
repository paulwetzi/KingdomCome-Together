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
#include "hits.h"
#include "log.h"
#include "pipe_server.h"
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

// ---- A joiner's trade ---------------------------------------------------------------------------------------------------------
// The dialogue finds a shop through C_ShopList by asking each C_Shop whether this keeper has it open. A keeper opens it in their own work
// activity, which a joiner's paused copy never runs, so Trade was offered and then found no shop. The host says who is ready to sell;
// this makes the shop's own open / close call for that keeper's copy. Each slot is checked by what the function does to the keeper list.
constexpr size_t kGameShopModule = 0x118, kModuleList = 0x10, kListHead = 0x10, kNodeShop = 0x18, kShopEntity = 0xA8;
constexpr size_t kGameRpg = 0x1E0, kRpgSoulSlot = 0x20;
constexpr size_t kIsOpenSlot = 0x28, kOpenSlot = 0x30, kCloseSlot = 0x88;

struct ShopAbi { void* const* listVt = nullptr; void* const* shopVt = nullptr; };

const ShopAbi* shop_abi() {
    static ShopAbi s;
    static bool checked = false, good = false;
    if (checked) return good ? &s : nullptr;
    HMODULE mod = GetModuleHandleA("ShopModule.dll");
    if (!mod) return nullptr;   // not loaded yet: asked again next time
    checked = true;
    s.listVt = anchor::find_vftable(mod, ".?AVC_ShopList@shopmodule@wh@@");
    s.shopVt = anchor::find_vftable(mod, ".?AVC_Shop@shopmodule@wh@@");
    static const uint8_t kIsOpen[] = {0x48, 0x8B, 0x8B, 0x90, 0x01, 0x00, 0x00};   // mov rcx,[rbx+0x190]  the keeper list
    static const uint8_t kOpenA[] = {0x48, 0x8B, 0xBB, 0x90, 0x01, 0x00, 0x00};    // mov rdi,[rbx+0x190]
    static const uint8_t kOpenB[] = {0x48, 0xFF, 0x83, 0x98, 0x01, 0x00, 0x00};    // inc qword [rbx+0x198]  its size
    static const uint8_t kClose[] = {0x48, 0x8B, 0x9D, 0x90, 0x01, 0x00, 0x00};    // mov rbx,[rbp+0x190]
    good = s.listVt && s.shopVt &&
           anchor::function_has_bytes(mod, s.shopVt[kIsOpenSlot / 8], kIsOpen, sizeof kIsOpen) &&
           anchor::function_has_bytes(mod, s.shopVt[kOpenSlot / 8], kOpenA, sizeof kOpenA) &&
           anchor::function_has_bytes(mod, s.shopVt[kOpenSlot / 8], kOpenB, sizeof kOpenB) &&
           anchor::function_has_bytes(mod, s.shopVt[kCloseSlot / 8], kClose, sizeof kClose);
    logf("WO212-SHOP the shop's keeper calls %s", good ? "found and checked" : "NOT as expected: a joiner's trade stays off on this build");
    return good ? &s : nullptr;
}

uint8_t shop_keeper(uint8_t mode, uint32_t shopEid, const char* keeper, uint8_t* before, uint8_t* after) {
    if (!engine::resolve()) return kResultRefused;
    const ShopAbi* abi = shop_abi();
    void* game = engine::game_iface();
    if (!abi || !game) return kResultRefused;
    const uint32_t keeperEid = hits::eid_of_name(keeper);
    if (!keeperEid) return kResultNoKeeper;
    void* shopEnt = engine::entity_by_id(shopEid);
    if (!shopEnt) return kResultNoShop;
    uint8_t r = kResultNoShop;
    KCDMP_FAULT_CALL(site, "wo212::shop_keeper");
    const bool ok = fault::guarded(site, [&] {
        char* g = static_cast<char*>(game);
        char* module = *reinterpret_cast<char**>(g + kGameShopModule);
        char* list = *reinterpret_cast<char**>(module + kModuleList);
        if (*reinterpret_cast<void* const**>(list) != abi->listVt) { r = kResultRefused; return; }
        char* head = *reinterpret_cast<char**>(list + kListHead);
        char* node = *reinterpret_cast<char**>(head);
        char* shop = nullptr;
        for (int i = 0; i < 4096 && node != head; ++i, node = *reinterpret_cast<char**>(node)) {
            char* c = *reinterpret_cast<char**>(node + kNodeShop);
            if (c && *reinterpret_cast<void* const**>(c) == abi->shopVt && *reinterpret_cast<void**>(c + kShopEntity) == shopEnt) { shop = c; break; }
        }
        if (!shop) return;
        char* rpg = *reinterpret_cast<char**>(g + kGameRpg);
        void* soul = reinterpret_cast<void* (*)(void*, uint32_t)>((*reinterpret_cast<void***>(rpg))[kRpgSoulSlot / 8])(rpg, keeperEid);
        if (!soul) { r = kResultNoKeeper; return; }
        const uint64_t* key = reinterpret_cast<const uint64_t* (*)(void*)>((*reinterpret_cast<void***>(soul))[0])(soul);
        if (!key) { r = kResultNoKeeper; return; }
        auto isOpen = reinterpret_cast<bool (*)(void*, const uint64_t*)>(abi->shopVt[kIsOpenSlot / 8]);
        const bool was = isOpen(shop, key);
        *before = was ? 1 : 0;
        if (mode == 1 && !was) reinterpret_cast<void (*)(void*, const uint64_t*)>(abi->shopVt[kOpenSlot / 8])(shop, key);
        if (mode == 0 && was) reinterpret_cast<void (*)(void*, const uint64_t*)>(abi->shopVt[kCloseSlot / 8])(shop, key);
        *after = isOpen(shop, key) ? 1 : 0;
        r = kResultDone;
    });
    return ok ? r : kResultFault;
}

uint64_t now_ms() {
    LARGE_INTEGER q{}, f{};
    QueryPerformanceCounter(&q);
    QueryPerformanceFrequency(&f);
    return static_cast<uint64_t>(q.QuadPart / f.QuadPart * 1000 + q.QuadPart % f.QuadPart * 1000 / f.QuadPart);
}

std::atomic<uint64_t> g_pushUntilMs{0};
uint64_t g_nextPushMs = 0;   // main thread only

// Main thread, every frame. The next time is kept on a fixed grid, so frame times do not add up into an uneven pace.
void tick() {
    const uint64_t now = now_ms();
    if (now >= g_pushUntilMs.load(std::memory_order_relaxed) || now < g_nextPushMs) return;
    g_nextPushMs = now - g_nextPushMs > kPushEveryMs ? now + kPushEveryMs : g_nextPushMs + kPushEveryMs;
    pipe::push_local_state();
}

uint8_t op(uint8_t opc, const uint8_t* body, size_t len, uint8_t* out, size_t cap, size_t* outLen) {
    *outLen = 0;
    if (opc == kOpShopKeeper) {
        if (len < 8 || body[1] > 2 || cap < 3) return wo163::kRBadRequest;
        const size_t n = body[6];
        if (n == 0 || n > 63 || len != 7 + n) return wo163::kRBadRequest;
        char name[64]{};
        std::memcpy(name, body + 7, n);
        uint32_t shopEid = 0;
        std::memcpy(&shopEid, body + 2, 4);
        uint8_t before = 0, after = 0;
        out[0] = shop_keeper(body[1], shopEid, name, &before, &after);
        out[1] = before;
        out[2] = after;
        *outLen = 3;
        logf("WO212-SHOP keeper=%s shop=0x%X mode=%u -> result=%u open_before=%u open_after=%u", name, shopEid, body[1], out[0], before, after);
        return wo163::kROk;
    }
    if (opc == kOpPositionPush) {
        if (len != 2 || body[1] > 1 || cap < 1) return wo163::kRBadRequest;
        const bool was = now_ms() < g_pushUntilMs.load();
        g_pushUntilMs.store(body[1] ? now_ms() + kPushHoldMs : 0);
        if (was != (body[1] != 0)) logf("WO212-PUSH the own position is %s", body[1] ? "pushed at an even pace" : "no longer pushed");
        out[0] = 1;
        *outLen = 1;
        return wo163::kROk;
    }
    if (opc != kOpInputFilter || len != 2 || body[1] > 1 || cap < 1) return wo163::kRBadRequest;
    out[0] = apply(body[1]);
    *outLen = 1;
    logf("WO212-KEYS filter no_inventory %s -> %s", body[1] == 1 ? "set (test)" : "released",
         out[0] == kResultDone ? "done" : out[0] == kResultFault ? "fault" : "refused");
    return wo163::kROk;
}

const bool kReg = wo203::reg_op(kOpInputFilter, &op) && wo203::reg_op(kOpPositionPush, &op) && wo203::reg_op(kOpShopKeeper, &op) &&
                  wo203::reg_tick("w212", &tick);

} // namespace

} // namespace kcdmp::wo212
