// Copyright (C) 2026 the Kingdom Come: Together contributors (AUTHORS). SPDX-License-Identifier: GPL-3.0-only
// GPLv3 section 7 additional terms: NOTICE. This project's own code only; Kingdom Come: Deliverance II and its
// content belong to Warhorse Studios and PLAION. Unofficial, free, not affiliated with or endorsed by them.
using System.Text.Json;
using Xunit;

namespace KcdMp.Client.Tests;

public class FixedSoulsTests
{
    [Fact]
    public void A_catalog_soul_resolves_to_its_guid_and_the_name_ignores_case()
    {
        var map = FixedSouls.Parse("{ \"Some Player\": \"TTKC_woman_6\" }", _ => { });
        Assert.Equal("ttkc_woman_6", map["some player"].Soul);
        Assert.Equal("4763a986-8361-a712-61d9-bf6dd706ddb6", map["SOME PLAYER"].Guid);
    }

    [Fact]
    public void A_custom_soul_needs_a_name_and_a_guid()
    {
        var map = FixedSouls.Parse("{ \"A\": \"tabc_woman_1:4763A986-8361-A712-61D9-BF6DD706DDB6\" }", _ => { });
        Assert.Equal("tabc_woman_1", map["A"].Soul);
        Assert.Equal("4763a986-8361-a712-61d9-bf6dd706ddb6", map["A"].Guid);
    }

    [Theory]
    [InlineData("not_a_soul")]
    [InlineData("x\\\") end os.exit() --:4763a986-8361-a712-61d9-bf6dd706ddb6")]
    [InlineData("tabc_woman_1:not-a-guid")]
    public void A_bad_value_is_skipped_and_said(string value)
    {
        var logged = new List<string>();
        var json = JsonSerializer.Serialize(new Dictionary<string, string> { ["A"] = value, ["B"] = "tsem_woman_9" });
        var map = FixedSouls.Parse(json, logged.Add);
        Assert.False(map.ContainsKey("A"));
        Assert.True(map.ContainsKey("B"));
        Assert.Single(logged);
    }

    [Fact]
    public void A_missing_or_broken_file_means_no_fixed_souls()
    {
        string dir = Path.Combine(Path.GetTempPath(), "fixedsouls-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(dir);
        try
        {
            string path = Path.Combine(dir, FixedSouls.FileName);
            Assert.Empty(FixedSouls.Load(path, _ => { }));
            File.WriteAllText(path, "{ broken");
            Assert.Empty(FixedSouls.Load(path, _ => { }));
        }
        finally { Directory.Delete(dir, true); }
    }
}
