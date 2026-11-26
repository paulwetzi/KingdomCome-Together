// Copyright (C) 2026 the Kingdom Come: Together contributors (AUTHORS). SPDX-License-Identifier: GPL-3.0-only
// GPLv3 section 7 additional terms: NOTICE. This project's own code only; Kingdom Come: Deliverance II and its
// content belong to Warhorse Studios and PLAION. Unofficial, free, not affiliated with or endorsed by them.
#pragma once
// Fixed-soul build: the player's own chosen figure. A local copy of the player (an NPC body with the chosen soul, named like a
// partner's figure, kcd2mp_200) is driven by the same native writer and gait as a partner's figure -- its samples are made here,
// every frame, from the local player's own position and body state (the data the partner receives), instead of arriving over the
// pipe. The mod shows it whenever the camera is not the player's own eyes (dialogues, cutscenes) and hides Henry's body then.
#include <cstddef>
#include <cstdint>

namespace kcdmp::fsself {

// [op][on:1][nameLen:1][name] -- on 1: follow the player with the copy of that name (bound now, or as soon as it exists); 0: stop.
// Reply [state]: 0 off, 1 following (bound), 2 waiting for the body. 241: the official ops use 140-215, the fork's keys op is 240.
constexpr uint8_t kOpSelf = 241;
// [op][nameLen:1][name] -- the attachment names of that body's character, comma-joined (reply text, at most ~3 KB).
constexpr uint8_t kOpAttachNames = 242;
void research_set(const char* name);   // the research verb "self <body>|off"
void research_ahead(float m);          // the research verb "selfahead <m>" (0 = in place)
void research_twin(bool on);           // the research verb "twinlook on|off": the dialogue stand-in remade from the copy

}  // namespace kcdmp::fsself
