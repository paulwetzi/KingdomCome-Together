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

    [Theory]
    [InlineData("kboh_kuratkoDaughter")]
    [InlineData("kvrc_millersSon")]
    [InlineData("taboryUCesty_archery_urs_son")]
    [InlineData("taboryUCesty_shop_henslin_daughter")]
    [InlineData("ttkc_bailiffSon")]
    [InlineData("tvid_huntsmansSon")]
    public void A_soul_named_as_someones_son_or_daughter_is_never_listed(string name) =>
        Assert.Null(FigureCatalog.Build(new[] { Row(name) }, NoText).ByName(name));

    [Theory]
    [InlineData("karavanyVeSvete_civilianCaravan_mason_1")]
    [InlineData("pocestny_prisoner")]
    [InlineData("pranyr_prisoner_2")]
    public void A_word_that_only_contains_son_stays_listed(string name) =>
        Assert.NotNull(FigureCatalog.Build(new[] { Row(name) }, NoText).ByName(name));

    [Fact]
    public void A_child_social_class_and_an_animal_are_never_listed()
    {
        var c = FigureCatalog.Build(new[] { Row("ttkc_woman_1", social: "child"), Row("ttkc_woman_2", cls: "Horse") }, NoText);
        Assert.Equal(0, c.Count);
    }

    [Theory]
    [InlineData("cart_test_npcDriver", "tabc_commonFolk", "char_GENERIC_MAN_1")]
    [InlineData("cutscene_zizkaScar", "tabc_commonFolk", "char_GENERIC_MAN_1")]
    [InlineData("dummyWanderer_3", "tabc_commonFolk", "char_GENERIC_MAN_1")]
    [InlineData("battleBenchmark_7", "tabc_commonFolk", "char_GENERIC_MAN_1")]
    [InlineData("ttkc_man_1", "deadBodies", "char_GENERIC_MAN_1")]
    [InlineData("ttkc_man_2", "test_npcs", "char_GENERIC_MAN_1")]
    [InlineData("ttkc_woman_3", "tabc_commonFolk", "char_MLYNAROVA_DCERA_VANKA")]
    [InlineData("ttkc_man_4", "tabc_commonFolk", "char_HOSPODSKEHO_SYN_MAREK")]
    public void Staging_souls_corpses_and_sons_or_daughters_are_never_listed(string name, string faction, string skald) =>
        Assert.Null(FigureCatalog.Build(new[] { Row(name, faction: faction, skald: skald) }, NoText).ByName(name));

    [Fact]
    public void A_child_title_is_refused_but_a_job_with_the_same_word_inside_is_not()
    {
        var text = new Dictionary<string, string> { ["a"] = "Junges Mädchen", ["b"] = "Zubermädchen" };
        var c = FigureCatalog.Build(new[] { Row("ttkc_woman_1", skald: "char_X", ui: "a"), Row("ttkc_woman_2", ui: "b") }, text);
        Assert.Null(c.ByName("ttkc_woman_1"));
        Assert.Equal("Zubermaedchen 2", c.ByName("ttkc_woman_2")!.Label);
    }

    [Fact]
    public void Generic_soldiers_are_guards_even_with_a_civil_crime_role()
    {
        var c = FigureCatalog.Build(new[]
        {
            Row("kkut_man_1", "NPC", skald: "char_GENERIC_MAN_SOLDIER_30", social: "villager"),
            Row("kkut_man_2", "NPC", skald: "char_GENERIC_MAN_COMMONER_1", social: "soldier"),
            Row("kkut_man_3", "NPC", skald: "char_GENERIC_MAN_CUMAN_1"),
        }, NoText);
        Assert.Equal(FigureGroup.Guards, c.ByName("kkut_man_1")!.Group);
        Assert.Equal(FigureGroup.Guards, c.ByName("kkut_man_2")!.Group);
        Assert.Equal(FigureGroup.Enemies, c.ByName("kkut_man_3")!.Group);
    }

    [Fact]
    public void Places_are_merged_and_scene_souls_share_one_entry()
    {
        var c = FigureCatalog.Build(new[] { Row("kopa_woman_1"), Row("kvrc_woman_2"), Row("pogrom_woman_3"), Row("hladAZmar_woman_4") }, NoText);
        Assert.Equal("kland", c.ByName("kopa_woman_1")!.Place);
        Assert.Equal("kland", c.ByName("kvrc_woman_2")!.Place);
        Assert.Equal("scenes", c.ByName("pogrom_woman_3")!.Place);
        Assert.Equal("scenes", c.ByName("hladAZmar_woman_4")!.Place);
    }

    [Fact]
    public void Equal_titles_in_one_place_get_a_running_number()
    {
        var text = new Dictionary<string, string> { ["a"] = "Magd" };
        var c = FigureCatalog.Build(new[] { Row("ttkc_a", skald: "char_GENERIC_WOMAN_1", ui: "a"), Row("ttkc_b", skald: "char_GENERIC_WOMAN_1", ui: "a") }, text);
        Assert.Equal("Magd", c.ByName("ttkc_a")!.Label);
        Assert.Equal("Magd #2", c.ByName("ttkc_b")!.Label);
    }

    [Theory]
    [InlineData("wench", true, "soul_ui_name_wench")]
    [InlineData("wench", false, "soul_ui_name_villager_man")]
    [InlineData("gypsy", true, "soul_ui_name_nomad_woman")]
    [InlineData("merchant", true, "soul_ui_name_merchant_woman")]
    [InlineData("unknownJob", true, "soul_ui_name_villager_woman")]
    public void A_generic_title_follows_the_job_and_the_gender(string social, bool female, string expected)
    {
        var text = new[] { "soul_ui_name_wench", "soul_ui_name_villager_man", "soul_ui_name_villager_woman", "soul_ui_name_nomad_woman", "soul_ui_name_merchant_woman" }
            .ToDictionary(k => k, k => k);
        Assert.Equal(expected, FigureCatalog.TitleKey("char_GENERIC_1", new Dictionary<string, string>(), social, female, text));
    }

    /// <summary>Only runs against a real game: set KCDMP_REAL_TABLES to its Data\Tables.pak.</summary>
    [Fact]
    public void The_real_game_tables_give_a_clean_list()
    {
        string? pak = Environment.GetEnvironmentVariable("KCDMP_REAL_TABLES");
        if (string.IsNullOrEmpty(pak)) return;
        var c = FigureCatalog.Load(pak);
        Assert.InRange(c.Count, 3000, 8000);
        foreach (var (soul, guid) in FixedSouls.Catalog)
        {
            var f = c.ById(Guid.Parse(guid));
            Assert.True(f is not null, soul);
            Assert.Equal(FigureGroup.Women, f!.Group);
            Assert.Equal("NPC_Female", f.Class);
        }
        var report = new StringBuilder();
        foreach (FigureGroup g in Enum.GetValues<FigureGroup>()) report.AppendLine($"{g}: {c.CountIn(g)}");
        var seen = new HashSet<string>();
        var nodes = new Stack<string>();
        nodes.Push("root");
        int pages = 0;
        while (nodes.Count > 0)
        {
            string n = nodes.Pop();
            if (!seen.Add(n)) continue;
            var p = c.PageFor(n);
            Assert.True(p is not null, n);
            pages++;
            Assert.True(p!.Rows.Count <= FigureCatalog.PageSize + 3, n);
            if (pages <= 3 || n.EndsWith(".tsem")) report.AppendLine($"[{p.Title}] " + string.Join(" | ", p.Rows.Select(r => r.Label)));
            foreach (var r in p.Rows.Where(x => x.Type == 'n')) nodes.Push(r.Key);
            foreach (var r in p.Rows.Where(x => x.Type == 's'))
            {
                Assert.False(string.IsNullOrWhiteSpace(r.Label), r.Key);
                Assert.All(r.Label, ch => Assert.True(ch < 128, r.Key));
                Assert.DoesNotContain("_", r.Label);
                seen.Add("soul:" + r.Key);
            }
        }
        Assert.Equal(c.Count, seen.Count(x => x.StartsWith("soul:")));
        report.AppendLine($"total {c.Count}, pages {pages}");
        string? output = Environment.GetEnvironmentVariable("KCDMP_REAL_TABLES_REPORT");
        if (!string.IsNullOrEmpty(output)) File.WriteAllText(output, report.ToString());
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
