// Copyright (C) 2026 the Kingdom Come: Together contributors (AUTHORS). SPDX-License-Identifier: GPL-3.0-only
// GPLv3 section 7 additional terms: NOTICE. This project's own code only; Kingdom Come: Deliverance II and its
// content belong to Warhorse Studios and PLAION. Unofficial, free, not affiliated with or endorsed by them.
#pragma once
// WO-212:
//  * The inventory keys come back. The combat controller turns the action filter "no_inventory" on while the player is locked on an
//    opponent and off again at the end. In a co-op fight the end can be missed: I, M and the other menu keys then stay dead until the
//    game restarts. Lua can only read the filter, so the DLL makes the same call the controller makes at its own end of the lock.
//  * The own position is pushed. The pipe answers one request per frame, and on a joiner the position read queued behind everything
//    else (~400 ms, up to 3.7 s): the partner saw stutter. The DLL now sends its state itself at an even pace, stamped when it was read.
#include <cstdint>

namespace kcdmp::wo212 {

// [op][mode] mode 0 = release the filter, 1 = set it (a live test of the release). Reply [result].
constexpr uint8_t kOpInputFilter = 150;
constexpr uint8_t kResultRefused = 0, kResultDone = 1, kResultFault = 2;

// [op][on]: the agent's keep-alive for the push. Without another one the push stops after kPushHoldMs (the agent left).
constexpr uint8_t kOpPositionPush = 151;
constexpr uint64_t kPushEveryMs = 50, kPushHoldMs = 15000;

// [op][mode 0 close, 1 open, 2 ask][shopEid:4][nameLen][keeper] -> [result][open before][open after]: a joiner's copy of a keeper the host
// says is ready to sell gets the shop's own open call, because the copy never runs the work activity that makes it.
constexpr uint8_t kOpShopKeeper = 152;
constexpr uint8_t kResultNoShop = 3, kResultNoKeeper = 4;

} // namespace kcdmp::wo212
