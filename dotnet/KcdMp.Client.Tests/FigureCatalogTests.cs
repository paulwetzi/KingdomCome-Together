// Copyright (C) 2026 the Kingdom Come: Together contributors (AUTHORS). SPDX-License-Identifier: GPL-3.0-only
// GPLv3 section 7 additional terms: NOTICE. This project's own code only; Kingdom Come: Deliverance II and its
// content belong to Warhorse Studios and PLAION. Unofficial, free, not affiliated with or endorsed by them.
using System.IO.Compression;
using System.Text;
using Xunit;

namespace KcdMp.Client.Tests;

public class FigureCatalogTests
{
    private static FigureCatalog.SoulRow Row(string name, string cls = "NPC_Female", string role = "1", string vip = "0",
        string skald = "char_GENERIC_WOMAN_1", string faction = "tabc_commonFolk", string social = "varlet", string ui = "") =>
        new(name, Guid.NewGuid(), cls, social, role, vip, skald, faction, ui);

    private static readonly Dictionary<string, string> NoText = new();

    [Fact]
    public void Each_kind_of_npc_lands_in_its_group()
    {
        var c = FigureCatalog.Build(new[]
        {
            Row("ttkc_woman_6"),
            Row("ttkc_man_26", "NPC", skald: "char_GENERIC_MAN_1"),
            Row("ttkc_bailiff", "NPC", skald: "char_bailiff"),
            Row("ttac_man_9", "NPC", role: "2", skald: "char_GENERIC_MAN_GUARD_18"),
            Row("tkop_man_1", "NPC", role: "3", skald: "char_GENERIC_MAN_2"),
        }, NoText);
        Assert.Equal(FigureGroup.Women, c.ByName("ttkc_woman_6")!.Group);
        Assert.Equal(FigureGroup.Men, c.ByName("ttkc_man_26")!.Group);
        Assert.Equal(FigureGroup.Story, c.ByName("ttkc_bailiff")!.Group);
        Assert.Equal(FigureGroup.Guards, c.ByName("ttac_man_9")!.Group);
        Assert.Equal(FigureGroup.Enemies, c.ByName("tkop_man_1")!.Group);
        Assert.False(c.ByName("ttkc_woman_6")!.Calm);
        Assert.True(c.ByName("ttac_man_9")!.Calm);
    }

    [Theory]
    [InlineData("ttkc_child_1")]
    [InlineData("ttkc_boy_2")]
    [InlineData("ttkc_girl_2")]
    [InlineData("ttkc_dite_1")]
    [InlineData("test_cuman_enemy")]
    [InlineData("player_henry")]
    [InlineData("kdcmp_partner_1")]
    public void Children_tests_and_the_player_are_never_listed(string name) =>
        Assert.Null(FigureCatalog.Build(new[] { Row(name) }, NoText).ByName(name));

    [Fact]
    public void A_child_social_class_and_an_animal_are_never_listed()
    {
        var c = FigureCatalog.Build(new[] { Row("ttkc_woman_1", social: "child"), Row("ttkc_woman_2", cls: "Horse") }, NoText);
        Assert.Equal(0, c.Count);
    }

    [Fact]
    public void A_bandit_word_inside_a_name_is_not_mistaken_for_a_child()
    {
        var c = FigureCatalog.Build(new[] { Row("banditeast_woman_1", role: "3") }, NoText);
        Assert.Equal(FigureGroup.Enemies, c.ByName("banditeast_woman_1")!.Group);
    }

    [Fact]
    public void Names_come_from_the_game_and_fit_the_font()
    {
        var text = new Dictionary<string, string> { ["soul_ui_name_a"] = "Bäuerin", ["soul_ui_name_b"] = "Hans Čapon" };
        var c = FigureCatalog.Build(new[]
        {
            Row("ttkc_woman_6", ui: "soul_ui_name_a"),
            Row("ttkc_lord", "NPC", skald: "char_hans", ui: "soul_ui_name_b"),
            Row("ttkc_woman_7"),
        }, text);
        Assert.Equal("Baeuerin 6", c.ByName("ttkc_woman_6")!.Label);
        Assert.Equal("Hans Capon", c.ByName("ttkc_lord")!.Label);
        Assert.Equal("ttkc woman 7", c.ByName("ttkc_woman_7")!.Label);
    }

    [Fact]
    public void Every_character_can_be_reached_through_the_pages()
    {
        var rows = Enumerable.Range(1, 60).Select(i => Row($"ttkc_woman_{i}")).ToList();
        var c = FigureCatalog.Build(rows, NoText);
        var root = c.PageFor("root")!;
        Assert.Equal('r', root.Rows[0].Type);
        var group = c.PageFor(root.Rows[1].Key)!;
        Assert.Equal("root", group.Rows[0].Key);
        string? node = group.Rows[1].Key;
        var seen = new HashSet<string>();
        while (node is not null)
        {
            var p = c.PageFor(node)!;
            Assert.True(p.Rows.Count <= FigureCatalog.PageSize + 3);
            foreach (var r in p.Rows.Where(x => x.Type == 's')) Assert.True(seen.Add(r.Key));
            node = p.Rows.FirstOrDefault(x => x.Label.StartsWith("v "))?.Key;
        }
        Assert.Equal(60, seen.Count);
    }

    [Theory]
    [InlineData("")]
    [InlineData("g9")]
    [InlineData("g0.a b")]
    [InlineData("g0.tabc\") os.exit() --")]
    [InlineData("g0@-1")]
    public void A_bad_node_is_refused(string node) =>
        Assert.Null(FigureCatalog.Build(new[] { Row("tabc_woman_1") }, NoText).PageFor(node));

    [Fact]
    public void The_wire_round_trips_and_refuses_junk()
    {
        var f = FigureCatalog.Build(new[] { Row("ttkc_woman_6") }, NoText).ByName("ttkc_woman_6")!;
        Assert.True(FigureWire.TryParse(FigureWire.Build(f), out var cls, out var id));
        Assert.Equal(("NPC_Female", f.Id), (cls, id));
        Assert.True(FigureWire.TryParse(FigureWire.Build(null), out cls, out _));
        Assert.Equal("", cls);
        Assert.False(FigureWire.TryParse(new byte[16], out _, out _));
        var bad = new byte[FigureWire.Len]; bad[0] = 3;
        Assert.False(FigureWire.TryParse(bad, out _, out _));
        bad[0] = 1;
        Assert.False(FigureWire.TryParse(bad, out _, out _));
    }

    [Fact]
    public void A_pak_of_the_documented_shape_loads()
    {
        string dir = Path.Combine(Path.GetTempPath(), "figcat-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(Path.Combine(dir, "Data"));
        Directory.CreateDirectory(Path.Combine(dir, "Localization"));
        try
        {
            string guid = Guid.NewGuid().ToString();
            string pak = Path.Combine(dir, "Data", "Tables.pak");
            using (var z = ZipFile.Open(pak, ZipArchiveMode.Create))
            {
                Add(z, "Libs/Tables/rpg/soul_archetype.xml", "<d><soul_archetypes><soul_archetype soul_archetype_id=\"0\" soul_archetype_name=\"NPC\"/><soul_archetype soul_archetype_id=\"1\" soul_archetype_name=\"NPC_Female\"/></soul_archetypes></d>");
                Add(z, "Libs/Tables/rpg/social_class.xml", "<d><social_classes><social_class social_class_id=\"83\" social_class_name=\"varlet\" soul_crime_role_id=\"1\"/></social_classes></d>");
                Add(z, "Libs/Tables/rpg/soul__tabc.xml", $"<d><souls><soul soul_name=\"tabc_woman_1\" soul_id=\"{guid}\" soul_archetype_id=\"1\" social_class_id=\"83\" soul_vip_class_id=\"0\" skald_character_name=\"char_GENERIC_WOMAN_1\" factionName=\"abc_commonFolk\" soul_name_string_id=\"soul_ui_name_x\"/></souls></d>");
                Add(z, "Libs/Tables/rpg/soul__autotests.xml", $"<d><souls><soul soul_name=\"tabc_woman_2\" soul_id=\"{Guid.NewGuid()}\" soul_archetype_id=\"1\"/></souls></d>");
            }
            using (var l = ZipFile.Open(Path.Combine(dir, "Localization", "German_xml.pak"), ZipArchiveMode.Create))
                Add(l, "text_ui_soul.xml", "<Table><Row><Cell>soul_ui_name_x</Cell><Cell>Maid</Cell><Cell>Magd</Cell></Row></Table>");
            var c = FigureCatalog.Load(pak);
            Assert.Equal(1, c.Count);
            Assert.Equal("Magd 1", c.ByName("tabc_woman_1")!.Label);
            Assert.Equal("NPC_Female", c.ByName("tabc_woman_1")!.Class);
        }
        finally { Directory.Delete(dir, true); }
    }

    private static void Add(ZipArchive z, string name, string text)
    {
        using var s = z.CreateEntry(name).Open();
        var b = Encoding.UTF8.GetBytes(text);
        s.Write(b, 0, b.Length);
    }
}
