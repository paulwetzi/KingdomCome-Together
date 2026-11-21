// Copyright (C) 2026 the Kingdom Come: Together contributors (AUTHORS). SPDX-License-Identifier: GPL-3.0-only
// GPLv3 section 7 additional terms: NOTICE. This project's own code only; Kingdom Come: Deliverance II and its
// content belong to Warhorse Studios and PLAION. Unofficial, free, not affiliated with or endorsed by them.
using System.Globalization;
using System.IO.Compression;
using System.Net;
using System.Text;
using System.Text.RegularExpressions;
using System.Xml;

namespace KcdMp.Client;

public enum FigureGroup { Women, Men, Story, Guards, Enemies }

/// <summary>One NPC a player can appear as. Calm figures (story, guards, enemies) get their brain paused when spawned.</summary>
public sealed record Figure(string Soul, Guid Id, string Class, FigureGroup Group, string Place, string Label)
{
    public bool Calm => Group is FigureGroup.Story or FigureGroup.Guards or FigureGroup.Enemies;
}

/// <summary>
/// Modified build: every adult human NPC of the player's own game (Tables.pak, names from the game's localization), as pages
/// for the in-game character window. Nothing of the game is stored in this repository.
/// </summary>
public sealed class FigureCatalog
{
    public const int PageSize = 12;

    public readonly record struct SoulRow(string Name, Guid Id, string Class, string SocialClass, string CrimeRole, string Vip,
                                          string Skald, string Faction, string UiKey);
    public sealed record Row(char Type, string Key, string Label, string Guid, string Class);
    public sealed record Page(string Node, string Title, IReadOnlyList<Row> Rows);

    private static readonly string[] GroupNames = { "Frauen", "Maenner", "Story-Figuren", "Wachen und Soldaten", "Banditen und Feinde" };
    private static readonly IReadOnlyDictionary<string, string> Places = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase)
    {
        ["tsem"] = "Semine", ["tsla"] = "Slatejov", ["ttac"] = "Tachov", ["ttkc"] = "Troskowitz", ["ttro"] = "Trosky",
        ["tvez"] = "Roma-Lager", ["tvid"] = "Vidlak", ["tzel"] = "Zelejov", ["tneb"] = "Nebakov", ["tpod"] = "Podsemin",
        ["tkop"] = "Kopanina", ["tbuk"] = "Bukov", ["tzda"] = "Zdar", ["tland"] = "Umland Trosky",
        ["kkut"] = "Kuttenberg", ["ksuc"] = "Suchdol", ["kzik"] = "Sigismunds Lager", ["kgru"] = "Grunta", ["kmis"] = "Miskovice",
        ["kpri"] = "Pritoky", ["ksta"] = "Stara Kutna", ["kmal"] = "Malesov", ["kbyl"] = "Bylany", ["khor"] = "Horany",
        ["klor"] = "Lorec", ["krat"] = "Ratboricko", ["kcer"] = "Certovka", ["kboh"] = "Bohounovice", ["kvys"] = "Vysoka",
        ["kmez"] = "Mezholezy", ["kpec"] = "Pecky", ["ksed"] = "Sedlec", ["krab"] = "Rabstejnka", ["ssed"] = "Kloster Sedlec",
        ["kland"] = "Umland Kuttenberg", [SceneKey] = "Szenen und Ereignisse",
    };
    private const string SceneKey = "szenen";
    /// <summary>Small map areas with a few camps each: one entry per region keeps the place list short.</summary>
    private static readonly IReadOnlyDictionary<string, string> MergedPlaces = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase)
    {
        ["tapo"] = "tland", ["tkrc"] = "tland", ["kkop"] = "kland", ["kkuk"] = "kland", ["knab"] = "kland", ["kopa"] = "kland",
        ["ksus"] = "kland", ["kvlc"] = "kland", ["kvrc"] = "kland",
    };

    private static readonly Regex NotPlayable = new(
        @"(^|_)(test|tests|autotest|autotests|dummy|debug|template|placeholder|kdcmp|player|child|children|kid|kids|boy|girl|baby|infant|dite|deti|chlapec|holcicka|divka|kluk)\d*(_|$)",
        RegexOptions.IgnoreCase | RegexOptions.CultureInvariant);
    private static readonly Regex NotAWorldSoul = new(@"^(?:cart|cutscene|battleBenchmark|dummy[a-z]*)(?:_|$)", RegexOptions.IgnoreCase | RegexOptions.CultureInvariant);
    private static readonly Regex NotAWorldFaction = new(@"^(?:test_|deadBodies|trosecko_justice_league)", RegexOptions.IgnoreCase | RegexOptions.CultureInvariant);
    private static readonly Regex ChildClass = new("child|kid|boy|girl", RegexOptions.IgnoreCase | RegexOptions.CultureInvariant);
    // The game's tables have no child flag on a named character. Its name or German title is what says so; when in doubt it stays out.
    private static readonly Regex ChildSkald = new(@"(^|_)(HOLKA|DCERA|SYN|MALEJ|DITE|DETI|KLUK|CHLAPEC|DIVKA)(_|$)", RegexOptions.IgnoreCase | RegexOptions.CultureInvariant);
    private static readonly Regex ChildTitle = new(@"(?<!\p{L})(M(ä|ae)dchen|Kind|Knabe|Junge|Sohn|Tochter)(?!\p{L})", RegexOptions.IgnoreCase | RegexOptions.CultureInvariant);
    private static readonly Regex EnemyFaction = new("enem|bandit|cuman|kuman", RegexOptions.IgnoreCase | RegexOptions.CultureInvariant);
    private static readonly Regex GuardFaction = new("soldier|guard|militia", RegexOptions.IgnoreCase | RegexOptions.CultureInvariant);
    private static readonly Regex EnemySkald = new("_(ENEMY|CUMAN)(_|$)", RegexOptions.IgnoreCase | RegexOptions.CultureInvariant);
    private static readonly Regex GuardSkald = new("_(SOLDIER|GUARD)(_|$)", RegexOptions.IgnoreCase | RegexOptions.CultureInvariant);
    private static readonly Regex GuardClass = new("^(soldier|officer|guard|guardLeader|watchman|bailiff|shootmaster|soldier_crimeAuthority|huntsman_crimeAuthority)$",
        RegexOptions.IgnoreCase | RegexOptions.CultureInvariant);
    private static readonly Regex Numbered = new(@"_(?:man|woman)_(\d+)$", RegexOptions.IgnoreCase | RegexOptions.CultureInvariant);
    private static readonly Regex NodeShape = new(@"^(?:root|g([0-4])(?:\.([a-z0-9]{1,24}))?)(?:@(\d{1,5}))?$", RegexOptions.CultureInvariant);
    private static readonly Regex RowRx = new("<Row>(.*?)</Row>", RegexOptions.Singleline | RegexOptions.CultureInvariant);
    private static readonly Regex CellRx = new("<Cell>(.*?)</Cell>", RegexOptions.Singleline | RegexOptions.CultureInvariant);

    private readonly List<Figure> _all;
    private readonly Dictionary<string, Figure> _byName = new(StringComparer.OrdinalIgnoreCase);
    private readonly Dictionary<Guid, Figure> _byId = new();

    private FigureCatalog(List<Figure> all)
    {
        _all = all;
        foreach (var f in all) { _byName[f.Soul] = f; _byId[f.Id] = f; }
    }

    public int Count => _all.Count;
    public Figure? ByName(string name) => _byName.GetValueOrDefault(name);
    public Figure? ById(Guid id) => _byId.GetValueOrDefault(id);
    public int CountIn(FigureGroup g) => _all.Count(f => f.Group == g);

    public static FigureCatalog Build(IEnumerable<SoulRow> rows, IReadOnlyDictionary<string, string> text)
    {
        var list = new List<Figure>();
        var seen = new HashSet<Guid>();
        foreach (var r in rows)
        {
            if (r.Class is not ("NPC" or "NPC_Female") || NotPlayable.IsMatch(r.Name) || NotAWorldSoul.IsMatch(r.Name)
                || NotAWorldFaction.IsMatch(r.Faction) || ChildClass.IsMatch(r.SocialClass) || ChildSkald.IsMatch(r.Skald)
                || (r.UiKey.Length > 0 && text.TryGetValue(r.UiKey, out var title) && ChildTitle.IsMatch(title))
                || !seen.Add(r.Id)) continue;
            var group = GroupOf(r);
            list.Add(new Figure(r.Name, r.Id, r.Class, group, PlaceKey(r.Name), LabelOf(r, group, text)));
        }
        list.Sort((a, b) =>
        {
            int c = string.CompareOrdinal(PlaceName(a.Place), PlaceName(b.Place));
            if (c != 0) return c;
            c = NumberOf(a.Soul).CompareTo(NumberOf(b.Soul));
            return c != 0 ? c : string.CompareOrdinal(a.Soul, b.Soul);
        });
        // Many generic NPCs of one place share a title: a running number keeps every row of a page distinct.
        var taken = new Dictionary<(FigureGroup, string, string), int>();
        for (int i = 0; i < list.Count; i++)
        {
            var key = (list[i].Group, list[i].Place, list[i].Label);
            int n = taken.GetValueOrDefault(key) + 1;
            taken[key] = n;
            if (n > 1) list[i] = list[i] with { Label = $"{list[i].Label} #{n}" };
        }
        return new FigureCatalog(list);
    }

    private static FigureGroup GroupOf(SoulRow r)
    {
        bool generic = r.Skald.StartsWith("char_GENERIC", StringComparison.OrdinalIgnoreCase)
            || r.Skald.StartsWith("char_DONATIONS_GENERIC", StringComparison.OrdinalIgnoreCase)
            || (r.Skald.Length == 0 && Numbered.IsMatch(r.Name));
        if ((r.Vip.Length > 0 && r.Vip != "0") || !generic) return FigureGroup.Story;
        if (r.CrimeRole == "3" || EnemyFaction.IsMatch(r.Faction) || EnemySkald.IsMatch(r.Skald)) return FigureGroup.Enemies;
        if (r.CrimeRole == "2" || GuardFaction.IsMatch(r.Faction) || GuardSkald.IsMatch(r.Skald) || GuardClass.IsMatch(r.SocialClass)) return FigureGroup.Guards;
        return r.Class == "NPC_Female" ? FigureGroup.Women : FigureGroup.Men;
    }

    private static string PlaceKey(string soul)
    {
        int i = soul.IndexOf('_');
        string p = (i > 0 ? soul[..i] : soul).ToLowerInvariant();
        if (MergedPlaces.TryGetValue(p, out var merged)) return merged;
        return Places.ContainsKey(p) ? p : SceneKey;
    }

    private static string PlaceName(string key) =>
        Places.TryGetValue(key, out var n) ? n : char.ToUpperInvariant(key[0]) + key[1..];

    private static int NumberOf(string soul)
    {
        var m = Numbered.Match(soul);
        return m.Success && int.TryParse(m.Groups[1].Value, NumberStyles.Integer, CultureInfo.InvariantCulture, out int n) ? n : int.MaxValue;
    }

    private static string LabelOf(SoulRow r, FigureGroup g, IReadOnlyDictionary<string, string> text)
    {
        string name = r.UiKey.Length > 0 && text.TryGetValue(r.UiKey, out var t) ? Fold(t) : "";
        if (name.Length == 0) return Fold(r.Name.Replace('_', ' '));
        int n = NumberOf(r.Name);
        return g != FigureGroup.Story && n != int.MaxValue ? $"{name} {n}" : name;
    }

    /// <summary>Plain ASCII: the on-screen debug font is not known to carry umlauts.</summary>
    public static string Fold(string s)
    {
        var sb = new StringBuilder(s.Length);
        foreach (char c0 in s.Replace("ä", "ae").Replace("ö", "oe").Replace("ü", "ue").Replace("Ä", "Ae").Replace("Ö", "Oe").Replace("Ü", "Ue").Replace("ß", "ss").Normalize(NormalizationForm.FormD))
        {
            if (CharUnicodeInfo.GetUnicodeCategory(c0) == UnicodeCategory.NonSpacingMark) continue;
            if (c0 < 128 && (char.IsLetterOrDigit(c0) || " .,'()/-".IndexOf(c0) >= 0)) sb.Append(c0);
        }
        string r = Regex.Replace(sb.ToString(), @"\s+", " ").Trim();
        return r.Length > 34 ? r[..34].TrimEnd() : r;
    }

    // ---------------------------------------------------------------- pages

    public Page? PageFor(string node)
    {
        var m = NodeShape.Match(node ?? "");
        if (!m.Success) return null;
        int offset = m.Groups[3].Success ? int.Parse(m.Groups[3].Value, CultureInfo.InvariantCulture) : 0;
        if (!m.Groups[1].Success)
        {
            var root = new List<Row> { new('r', "none", "Standardfigur", "", "") };
            for (int g = 0; g < GroupNames.Length; g++)
                root.Add(new Row('n', $"g{g}", $"{GroupNames[g]} ({CountIn((FigureGroup)g)})", "", ""));
            return new Page("root", "Charakter waehlen", root);
        }
        int gi = int.Parse(m.Groups[1].Value, CultureInfo.InvariantCulture);
        var inGroup = _all.Where(f => (int)f.Group == gi).ToList();
        if (!m.Groups[2].Success)
        {
            var places = inGroup.GroupBy(f => f.Place).OrderBy(x => PlaceName(x.Key), StringComparer.Ordinal)
                .Select(x => new Row('n', $"g{gi}.{x.Key}", $"{PlaceName(x.Key)} ({x.Count()})", "", "")).ToList();
            return Paged($"g{gi}", GroupNames[gi], "root", places, offset);
        }
        string place = m.Groups[2].Value;
        var souls = inGroup.Where(f => f.Place == place)
            .Select(f => new Row('s', f.Soul, f.Calm ? f.Label + " (!)" : f.Label, f.Id.ToString(), f.Class)).ToList();
        return souls.Count == 0 ? null : Paged($"g{gi}.{place}", $"{GroupNames[gi]} / {PlaceName(place)}", $"g{gi}", souls, offset);
    }

    private static Page Paged(string node, string title, string parent, List<Row> all, int offset)
    {
        offset = Math.Clamp(offset, 0, Math.Max(0, (all.Count - 1) / PageSize * PageSize));
        offset -= offset % PageSize;
        var rows = new List<Row> { new('b', parent, "< Zurueck", "", "") };
        if (offset > 0) rows.Add(new Row('n', $"{node}@{offset - PageSize}", "^ vorige Seite", "", ""));
        rows.AddRange(all.Skip(offset).Take(PageSize));
        if (offset + PageSize < all.Count) rows.Add(new Row('n', $"{node}@{offset + PageSize}", "v naechste Seite", "", ""));
        string range = all.Count > PageSize ? $"  {offset + 1}-{Math.Min(offset + PageSize, all.Count)} von {all.Count}" : "";
        return new Page(offset == 0 ? node : $"{node}@{offset}", title + range, rows);
    }

    // ---------------------------------------------------------------- the player's own game

    public static FigureCatalog Load(string tablesPak)
    {
        using var zip = ZipFile.OpenRead(tablesPak);
        var arch = new Dictionary<string, string>(StringComparer.Ordinal);
        if (zip.GetEntry("Libs/Tables/rpg/soul_archetype.xml") is { } ae)
            foreach (var a in Elements(ae, "soul_archetype"))
                if (a.TryGetValue("soul_archetype_id", out var id) && a.TryGetValue("soul_archetype_name", out var n)) arch[id] = n;
        var classes = new Dictionary<string, (string Name, string Role)>(StringComparer.Ordinal);
        if (zip.GetEntry("Libs/Tables/rpg/social_class.xml") is { } ce)
            foreach (var a in Elements(ce, "social_class"))
                if (a.TryGetValue("social_class_id", out var id))
                    classes[id] = (a.GetValueOrDefault("social_class_name", ""), a.GetValueOrDefault("soul_crime_role_id", ""));
        var skaldTitles = new Dictionary<string, string>(StringComparer.Ordinal);
        if (zip.GetEntry("Libs/Tables/skald/skald_character.xml") is { } se)
            foreach (var a in Elements(se, "skald_character"))
                if (a.TryGetValue("skald_character_name", out var n) && a.TryGetValue("ui_name_string_name", out var k)) skaldTitles[n] = k;
        var text = LoadNames(Path.GetDirectoryName(Path.GetDirectoryName(Path.GetFullPath(tablesPak))));
        var rows = new List<SoulRow>();
        foreach (var e in zip.Entries)
        {
            if (!Regex.IsMatch(e.FullName, @"^Libs/Tables/rpg/soul(__[^/]+)?\.xml$", RegexOptions.IgnoreCase | RegexOptions.CultureInvariant)) continue;
            if (Regex.IsMatch(e.Name, @"^soul__(auto)?tests?\.xml$", RegexOptions.IgnoreCase | RegexOptions.CultureInvariant)
                || e.Name.Contains("kcdmp", StringComparison.OrdinalIgnoreCase)) continue;
            foreach (var a in Elements(e, "soul"))
            {
                if (!a.TryGetValue("soul_name", out var name) || !a.TryGetValue("soul_id", out var g) || !Guid.TryParse(g, out var id)) continue;
                string cls = a.TryGetValue("soul_archetype_id", out var aid) && arch.TryGetValue(aid, out var an) ? an : arch.GetValueOrDefault("0", "NPC");
                classes.TryGetValue(a.GetValueOrDefault("social_class_id", ""), out var sc);
                string skald = a.GetValueOrDefault("skald_character_name", "");
                string direct = a.Values.FirstOrDefault(v => v.StartsWith("soul_ui_name", StringComparison.Ordinal)) ?? "";
                string ui = direct.Length > 0 ? direct : TitleKey(skald, skaldTitles, sc.Name ?? "", cls == "NPC_Female", text);
                rows.Add(new SoulRow(name, id, cls, sc.Name ?? "", sc.Role ?? "", a.GetValueOrDefault("soul_vip_class_id", "0"),
                    skald, a.GetValueOrDefault("factionName", ""), ui));
            }
        }
        return Build(rows, text);
    }

    /// <summary>A named character's own title, otherwise the game's word for the NPC's job in the right gender.</summary>
    public static string TitleKey(string skald, IReadOnlyDictionary<string, string> skaldTitles, string social, bool female,
                                    IReadOnlyDictionary<string, string> text)
    {
        if (skaldTitles.TryGetValue(skald, out var own) && text.ContainsKey(own)) return own;
        string g = female ? "woman" : "man";
        string job = (social, female) switch
        {
            ("cook", true) => "cooky",
            ("townsman", true) => "townswoman",
            ("gypsy", _) => "nomad",
            ("nobleman", true) => "townswoman",
            ("bartender" or "bathhouseMaid" or "housekeeper" or "wench", false) => "villager",
            _ => social,
        };
        foreach (var k in new[] { $"soul_ui_name_{job}_{g}", $"soul_ui_name_{job}", $"soul_ui_name_villager_{g}" })
            if (text.ContainsKey(k)) return k;
        return "";
    }

    private static Dictionary<string, string> LoadNames(string? gameRoot)
    {
        var text = new Dictionary<string, string>(StringComparer.Ordinal);
        if (gameRoot is null) return text;
        foreach (var lang in new[] { "German_xml.pak", "English_xml.pak" })
        {
            string pak = Path.Combine(gameRoot, "Localization", lang);
            if (!File.Exists(pak)) continue;
            try
            {
                using var z = ZipFile.OpenRead(pak);
                var e = z.Entries.FirstOrDefault(x => x.FullName.EndsWith("text_ui_soul.xml", StringComparison.OrdinalIgnoreCase));
                if (e is null) continue;
                using var sr = new StreamReader(e.Open());
                foreach (Match row in RowRx.Matches(sr.ReadToEnd()))
                {
                    var cells = CellRx.Matches(row.Groups[1].Value);
                    if (cells.Count >= 2) text[cells[0].Groups[1].Value] = WebUtility.HtmlDecode(cells[cells.Count - 1].Groups[1].Value);
                }
                if (text.Count > 0) return text;
            }
            catch (Exception ex) when (ex is IOException or InvalidDataException) { }
        }
        return text;
    }

    private static IEnumerable<Dictionary<string, string>> Elements(ZipArchiveEntry e, string only)
    {
        using var s = e.Open();
        using var r = XmlReader.Create(s, new XmlReaderSettings { DtdProcessing = DtdProcessing.Ignore, IgnoreComments = true, IgnoreWhitespace = true });
        while (r.Read())
        {
            if (r.NodeType != XmlNodeType.Element || !r.HasAttributes || r.LocalName != only) continue;
            var d = new Dictionary<string, string>(StringComparer.Ordinal);
            while (r.MoveToNextAttribute()) d[r.LocalName] = r.Value;
            r.MoveToElement();
            yield return d;
        }
    }
}

/// <summary>The FigureLook action's payload: [class:1 (0 none, 1 NPC, 2 NPC_Female)][soul guid:16].</summary>
public static class FigureWire
{
    public const int Len = 17;

    public static byte[] Build(Figure? f)
    {
        var b = new byte[Len];
        if (f is null) return b;
        b[0] = f.Class == "NPC_Female" ? (byte)2 : (byte)1;
        f.Id.TryWriteBytes(b.AsSpan(1));
        return b;
    }

    /// <summary>False for a malformed payload. <paramref name="cls"/> is "" for "no figure chosen".</summary>
    public static bool TryParse(ReadOnlySpan<byte> p, out string cls, out Guid id)
    {
        cls = ""; id = Guid.Empty;
        if (p.Length != Len || p[0] > 2) return false;
        if (p[0] == 0) return true;
        cls = p[0] == 2 ? "NPC_Female" : "NPC";
        id = new Guid(p.Slice(1, 16));
        return id != Guid.Empty;
    }
}
