// Copyright (C) 2026 the Kingdom Come: Together contributors (AUTHORS). SPDX-License-Identifier: GPL-3.0-only
// GPLv3 section 7 additional terms: NOTICE. This project's own code only; Kingdom Come: Deliverance II and its
// content belong to Warhorse Studios and PLAION. Unofficial, free, not affiliated with or endorsed by them.
namespace KcdMp.Client;

/// <summary>Who hands the pipe's gate on after taking it: position read first, then a relay frame's handler, then background polls.</summary>
public static class PipeGateRules
{
    public static bool MustYield(int positionWaiting, bool isFrame, bool framesFirst, int framesWaiting) =>
        positionWaiting > 0 || (framesFirst && !isFrame && framesWaiting > 0);
}
