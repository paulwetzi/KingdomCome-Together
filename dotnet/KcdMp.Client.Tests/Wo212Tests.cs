// Copyright (C) 2026 the Kingdom Come: Together contributors (AUTHORS). SPDX-License-Identifier: GPL-3.0-only
// GPLv3 section 7 additional terms: NOTICE. This project's own code only; Kingdom Come: Deliverance II and its
// content belong to Warhorse Studios and PLAION. Unofficial, free, not affiliated with or endorsed by them.
using KcdMp.Client;
using Xunit;

namespace KcdMp.Client.Tests;

public class Wo212Tests
{
    [Theory]
    [InlineData("CrimesOff on", true)]
    [InlineData("CrimesOff off", true)]
    [InlineData("CrimesOff maybe", false)]
    public void Crime_switch_is_remembered(string line, bool ok) =>
        Assert.Equal(ok, Wo154MenuRules.TryParseSet(line, out _, out _, out _));
}
