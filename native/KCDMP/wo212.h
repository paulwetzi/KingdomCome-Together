// Copyright (C) 2026 the Kingdom Come: Together contributors (AUTHORS). SPDX-License-Identifier: GPL-3.0-only
// GPLv3 section 7 additional terms: NOTICE. This project's own code only; Kingdom Come: Deliverance II and its
// content belong to Warhorse Studios and PLAION. Unofficial, free, not affiliated with or endorsed by them.
#pragma once
// Fixed-soul build (its own numbering, kept as WO-212): the inventory keys come back. The combat controller turns the action filter
// "no_inventory" on while the player is locked on an opponent and off again at the end. In a co-op fight the end can be missed: I, M and
// the other menu keys then stay dead until the game restarts. Lua can only read the filter, so the DLL makes the same call the controller
// makes at its own end of the lock.
#include <cstdint>

namespace kcdmp::wo212 {

// [op][mode] mode 0 = release the filter, 1 = set it (a live test of the release). Reply [result]. 240: the official ops use 140-215.
constexpr uint8_t kOpInputFilter = 240;
constexpr uint8_t kResultRefused = 0, kResultDone = 1, kResultFault = 2;

} // namespace kcdmp::wo212
