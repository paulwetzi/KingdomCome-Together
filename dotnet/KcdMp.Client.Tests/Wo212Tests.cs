// Copyright (C) 2026 the Kingdom Come: Together contributors (AUTHORS). SPDX-License-Identifier: GPL-3.0-only
// GPLv3 section 7 additional terms: NOTICE. This project's own code only; Kingdom Come: Deliverance II and its
// content belong to Warhorse Studios and PLAION. Unofficial, free, not affiliated with or endorsed by them.
using KcdMp.Client;
using Xunit;

namespace KcdMp.Client.Tests;

public class Wo212Tests
{
    [Theory]
    [InlineData(1, true, true, 0, true)]    // a waiting position read passes a frame's handler
    [InlineData(1, false, false, 0, true)]  // and a background poll, with frames-first off too
    [InlineData(0, false, true, 1, true)]   // a background poll still lets a frame go first
    [InlineData(0, true, true, 3, false)]   // a frame does not yield to another frame
    [InlineData(0, false, false, 1, false)] // frames-first off: no order among the others
    [InlineData(0, false, true, 0, false)]  // nobody waits
    public void Gate_order(int position, bool isFrame, bool framesFirst, int frames, bool yields) =>
        Assert.Equal(yields, PipeGateRules.MustYield(position, isFrame, framesFirst, frames));

    [Theory]
    [InlineData("CrimesOff on", true)]
    [InlineData("CrimesOff off", true)]
    [InlineData("CrimesOff maybe", false)]
    public void Crime_switch_is_remembered(string line, bool ok) =>
        Assert.Equal(ok, Wo154MenuRules.TryParseSet(line, out _, out _, out _));
}
