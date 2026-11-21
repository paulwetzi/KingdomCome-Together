// Copyright (C) 2026 the Kingdom Come: Together contributors (AUTHORS). SPDX-License-Identifier: GPL-3.0-only
// GPLv3 section 7 additional terms: NOTICE. This project's own code only; Kingdom Come: Deliverance II and its
// content belong to Warhorse Studios and PLAION. Unofficial, free, not affiliated with or endorsed by them.
using System.Text.Json;
using System.Text.RegularExpressions;

namespace KcdMp.Client;

/// <summary>
/// Modified build (not the official project): a player's figure can be a fixed female NPC soul. The mapping lives in
/// <c>fixed-souls.json</c> beside the agent, so a player's name is never part of the repository:
/// <c>{ "Steam name": "ttkc_woman_9" }</c>. The value is a name from <see cref="Catalog"/> or "soul_name:guid".
/// </summary>
public static class FixedSouls
{
    public const string FileName = "fixed-souls.json";

    public sealed record Entry(string Soul, string Guid);

    private static readonly Regex Custom = new(
        "^([A-Za-z0-9_]{1,64}):([0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12})$",
        RegexOptions.CultureInvariant);

    /// <summary>
    /// Generic civilian women (no named character, no guard or authority role) whose SharedSoulGuid was read from a running
    /// game twice (tools/soul-identity-run1/2.csv). The game's table spells one of them tsem_women_9, the running game tsem_woman_9.
    /// </summary>
    public static readonly IReadOnlyDictionary<string, string> Catalog = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase)
    {
        ["tsem_woman_8"] = "456dc2bd-1ede-2372-7ee7-fed064e80ea8",
        ["tsem_woman_9"] = "4187a4bf-c27a-dd4b-c348-7bec934968ad",
        ["tsem_women_9"] = "4187a4bf-c27a-dd4b-c348-7bec934968ad",
        ["tsem_woman_12"] = "46ec6bf1-3bac-85d6-8ee7-f90b1b25a4a8",
        ["ttac_woman_3"] = "48de9403-4fa6-32c3-7dd7-007ef5dc1489",
        ["ttac_woman_4"] = "49daaf6f-5119-420a-b7c6-33825b912bb3",
        ["ttac_woman_7"] = "ddf4ac93-d15d-4728-8083-16cf46f68444",
        ["ttkc_woman_9"] = "7ac037fe-60ca-4212-a39f-0093cff270ba",
        ["ttkc_woman_17"] = "f9a94c81-d804-44ab-9d0e-9c4decefbcd0",
        ["tvez_woman_2"] = "488e80ea-f98d-d0e1-8dc7-4359d4701b8d",
        ["tvez_woman_3"] = "00ec8c08-21d3-4f65-8c84-cf28958f0cde",
        ["tvez_woman_5"] = "9349eb0d-91e3-4f48-94bd-6ef73370036e",
        ["tzel_woman_3"] = "499b3100-8025-ece1-c741-ef13d59db783",
    };

    /// <summary>Reads the mapping. A missing or broken file means no fixed souls: the game runs as usual.</summary>
    public static IReadOnlyDictionary<string, Entry> Load(string path, Action<string> log)
    {
        try
        {
            if (!File.Exists(path)) return new Dictionary<string, Entry>();
            var map = Parse(File.ReadAllText(path), log);
            log($"FIXED-SOUL {map.Count} player(s) mapped in {FileName}");
            return map;
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException or JsonException)
        {
            log($"FIXED-SOUL {FileName} ignored: {ex.GetType().Name}");
            return new Dictionary<string, Entry>();
        }
    }

    public static IReadOnlyDictionary<string, Entry> Parse(string json, Action<string> log)
    {
        var map = new Dictionary<string, Entry>(StringComparer.OrdinalIgnoreCase);
        using var doc = JsonDocument.Parse(json, new JsonDocumentOptions
        {
            CommentHandling = JsonCommentHandling.Skip,
            AllowTrailingCommas = true,
        });
        if (doc.RootElement.ValueKind != JsonValueKind.Object) throw new JsonException("not an object");
        foreach (var p in doc.RootElement.EnumerateObject())
        {
            string player = p.Name.Trim();
            string value = p.Value.ValueKind == JsonValueKind.String ? p.Value.GetString()!.Trim() : "";
            if (player.Length > 0 && TryResolve(value, out var entry)) map[player] = entry;
            else log($"FIXED-SOUL skipped an entry: '{value}' is neither a catalog soul nor soul_name:guid");
        }
        return map;
    }

    public static bool TryResolve(string value, out Entry entry)
    {
        if (Catalog.TryGetValue(value, out var guid))
        {
            entry = new Entry(value.ToLowerInvariant(), guid);
            return true;
        }
        var m = Custom.Match(value);
        entry = m.Success ? new Entry(m.Groups[1].Value, m.Groups[2].Value.ToLowerInvariant()) : new Entry("", "");
        return m.Success;
    }
}
