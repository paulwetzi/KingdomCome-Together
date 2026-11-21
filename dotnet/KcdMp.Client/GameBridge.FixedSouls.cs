// Copyright (C) 2026 the Kingdom Come: Together contributors (AUTHORS). SPDX-License-Identifier: GPL-3.0-only
// GPLv3 section 7 additional terms: NOTICE. This project's own code only; Kingdom Come: Deliverance II and its
// content belong to Warhorse Studios and PLAION. Unofficial, free, not affiliated with or endorsed by them.
namespace KcdMp.Client;

// Modified build: a mapped player's figure is a fixed female soul (FixedSouls.cs). She keeps the soul's own outfit, because
// the partner's items are male meshes: the engine refuses them, and the converge would then strip what she wears.
public partial class GameBridge
{
    private readonly Lazy<IReadOnlyDictionary<string, FixedSouls.Entry>> _fixedSouls =
        new(() => FixedSouls.Load(Path.Combine(AppContext.BaseDirectory, FixedSouls.FileName), Console.WriteLine));

    private FixedSouls.Entry? FixedSoulOf(string? name) =>
        name is not null && _fixedSouls.Value.TryGetValue(name.Trim(), out var entry) ? entry : null;

    private bool IsFixedLook(byte ghostId) =>
        _ghostLooks.ContainsKey(ghostId) || (_ghostNames.TryGetValue(ghostId, out var name) && FixedSoulOf(name) is not null);

    /// <summary>Before the name reaches the mod, so the first spawn under that name already uses the fixed soul.</summary>
    private async Task PushFixedSoulAsync(string name)
    {
        if (FixedSoulOf(name) is { } e)
            await ExecLuaAsync($"if KCD2MP_SetFixedSoul then KCD2MP_SetFixedSoul(\"{EscapeLua(name)}\",\"{e.Soul}\",\"{e.Guid}\") end");
    }
}
