// Copyright (C) 2026 the Kingdom Come: Together contributors (AUTHORS). SPDX-License-Identifier: GPL-3.0-only
// GPLv3 section 7 additional terms: NOTICE. This project's own code only; Kingdom Come: Deliverance II and its
// content belong to Warhorse Studios and PLAION. Unofficial, free, not affiliated with or endorsed by them.
#pragma once
// Fixed-soul build: voice presets for the player's own voice (fs_voice.cpp).
#include <cstdint>

namespace kcdmp::fsvoice {

// [op][preset:1] 0 off (Henry as recorded), 1 a woman, 2 a woman brighter, 3 a woman warmer, 4 deeper. Reply [armed 0/1].
constexpr uint8_t kOpVoice = 243;
void set_preset(int p);   // the research verb "voice <n>" (and the op): logs the counters too
void research_tune(const char* p, const char* rest);   // the research verb "vtune <P> <Q> [lift]" | "vtune off"

}  // namespace kcdmp::fsvoice
