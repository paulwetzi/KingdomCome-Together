// Copyright (C) 2026 the Kingdom Come: Together contributors (AUTHORS). SPDX-License-Identifier: GPL-3.0-only
// GPLv3 section 7 additional terms: NOTICE. This project's own code only; Kingdom Come: Deliverance II and its
// content belong to Warhorse Studios and PLAION. Unofficial, free, not affiliated with or endorsed by them.
using System.Collections.Concurrent;
using System.Text.Json.Nodes;
using KcdMp.Wire;

namespace KcdMp.Client;

// Modified build: the character window. The mod draws it; this half owns the catalog, remembers the player's choice
// (FigureLook in mod-settings.json) and tells the partner on the action channel: on a change, when a partner arrives, every 15 s.
public partial class GameBridge
{
    private const string FigureLookSetting = "FigureLook";
    private const long FigureResendMs = 15_000;

    private Task<FigureCatalog?>? _figureCatalog;
    private readonly ConcurrentDictionary<byte, Figure> _ghostLooks = new();
    private volatile Figure? _myFigure;
    private bool _figureRestored;
    private long _figureSentMs;

    /// <summary>Read once; a failed read is tried again the next time the list is needed.</summary>
    private async Task<FigureCatalog?> FigureCatalogAsync()
    {
        var task = _figureCatalog ??= Task.Run(LoadFigureCatalog);
        var cat = await task;
        if (cat is null) Interlocked.CompareExchange(ref _figureCatalog, null, task);
        return cat;
    }

    private FigureCatalog? LoadFigureCatalog()
    {
        try
        {
            string? pak = TablesPakPath() ?? WeaponSwingCatalog.FindTablesPak();
            if (pak is null) { Console.WriteLine("MP-FIGURE Tables.pak not found -- no character list yet"); return null; }
            var cat = FigureCatalog.Load(pak);
            Console.WriteLine($"MP-FIGURE {cat.Count} characters read from the game's tables");
            return cat;
        }
        catch (Exception ex)
        {
            Console.WriteLine($"MP-FIGURE the character list failed: {ex.GetType().Name}");
            return null;
        }
    }

    private void FigureOnEvent(string name, string? arg)
    {
        string a = (arg ?? "").Trim();
        if (name == "fig_list") _ = FigureListAsync(a);
        else if (name == "fig_pick") _ = FigurePickAsync(a);
    }

    private async Task FigureListAsync(string node)
    {
        try
        {
            var cat = await FigureCatalogAsync();
            if (cat is null || cat.Count == 0) { await FigureErrorAsync("The NPC list could not be read."); return; }
            var page = cat.PageFor(node) ?? cat.PageFor("root")!;
            await ExecLuaAsync($"if KCD2MP_FigurePageBegin then KCD2MP_FigurePageBegin(\"{EscapeLua(node)}\",\"{EscapeLua(page.Title)}\",\"{EscapeLua(_myFigure?.Soul ?? "")}\",{page.Rows.Count}) end");
            foreach (var r in page.Rows)
                await ExecLuaAsync($"if KCD2MP_FigureRow then KCD2MP_FigureRow(\"{EscapeLua(node)}\",\"{r.Type}\",\"{EscapeLua(r.Key)}\",\"{EscapeLua(r.Label)}\",\"{r.Guid}\",\"{r.Class}\") end");
        }
        catch (Exception ex) { Console.WriteLine($"MP-FIGURE list failed: {ex.GetType().Name}"); }
    }

    private async Task FigurePickAsync(string soul)
    {
        try
        {
            var cat = await FigureCatalogAsync();
            Figure? f = null;
            if (soul != "none")
            {
                f = cat?.ByName(soul);
                if (f is null) { await FigureErrorAsync("This figure does not exist in your game."); return; }
            }
            _myFigure = f;
            var saved = SettingsJson.Update(W154mModSettings, new[] { SettingsJson.Edit.Set(FigureLookSetting, JsonValue.Create(f?.Soul ?? "")) }, createIfMissing: true);
            Console.WriteLine($"MP-FIGURE chosen: {f?.Soul ?? "standard"} (saved: {saved.Outcome.ToString().ToLowerInvariant()})");
            await FigureSendAsync("changed");
            await ExecLuaAsync($"if KCD2MP_FigureCurrent then KCD2MP_FigureCurrent(\"{EscapeLua(f?.Soul ?? "")}\",\"{EscapeLua(f?.Label ?? "Default figure")}\",{(saved.Ok ? "true" : "false")}) end");
        }
        catch (Exception ex) { Console.WriteLine($"MP-FIGURE pick failed: {ex.GetType().Name}"); }
    }

    private Task FigureErrorAsync(string text) =>
        ExecLuaAsync($"if KCD2MP_FigureError then KCD2MP_FigureError(\"{EscapeLua(text)}\") end");

    /// <summary>Once a second from the menu loop: the saved choice comes back, and the partner hears it again.</summary>
    private async Task FigureTickAsync()
    {
        if (!_figureRestored)
        {
            var node = SettingsJson.Read(W154mModSettings)?[FigureLookSetting];
            string saved = node?.GetValueKind() == System.Text.Json.JsonValueKind.String ? node.GetValue<string>() : "";
            if (saved.Length == 0) _figureRestored = true;
            else if (await FigureCatalogAsync() is { } cat)   // no tables yet: tried again next second
            {
                _figureRestored = true;
                _myFigure = cat.ByName(saved);
                Console.WriteLine($"MP-FIGURE saved choice: {(_myFigure is null ? "not in this game" : _myFigure.Soul)}");
            }
        }
        if (_myFigure is not null && Environment.TickCount64 - Interlocked.Read(ref _figureSentMs) >= FigureResendMs)
            await FigureSendAsync("heartbeat");
    }

    private async Task FigureSendAsync(string why)
    {
        if (_wo121Stream is not Stream s) return;
        try
        {
            await WritePacketAsync(s, _actionOut.Build(ActionKind.FigureLook, ActionPhase.Commit, FigureWire.Build(_myFigure)), _wo121Ct);
            Interlocked.Exchange(ref _figureSentMs, Environment.TickCount64);
            if (why != "heartbeat") Console.WriteLine($"MP-FIGURE sent to the partner ({why})");
        }
        catch (Exception ex) { Console.WriteLine($"MP-FIGURE send failed: {ex.GetType().Name}"); }
    }

    private async Task FigureOnActionAsync(InboundAction a)
    {
        if (!FigureWire.TryParse(a.Payload, out string cls, out Guid id)) return;
        byte src = a.SourceGhostId;
        if (cls.Length == 0)
        {
            if (_ghostLooks.TryRemove(src, out _)) await PushGhostLookAsync(src);
            return;
        }
        var f = (await FigureCatalogAsync())?.ById(id);
        if (f is null || f.Class != cls)
        {
            Console.WriteLine($"MP-FIGURE ghost {src} chose a soul this game does not list -- it keeps its figure");
            return;
        }
        if (_ghostLooks.TryGetValue(src, out var cur) && cur.Id == f.Id) return;
        _ghostLooks[src] = f;
        Console.WriteLine($"MP-FIGURE ghost {src} is now {f.Soul}");
        await PushGhostLookAsync(src);
    }

    private async Task PushGhostLookAsync(byte ghostId)
    {
        _ghostLooks.TryGetValue(ghostId, out var f);
        await ExecLuaAsync($"if KCD2MP_SetFigureLook then KCD2MP_SetFigureLook(\"{ghostId}\",\"{f?.Soul ?? ""}\",\"{f?.Id.ToString() ?? ""}\",\"{f?.Class ?? ""}\",{(f?.Calm == true ? "true" : "false")}) end");
    }

    private void FigureOnPeerConnected() => Interlocked.Exchange(ref _figureSentMs, 0);

    private void FigureOnPeerGone(byte id)
    {
        if (_ghostLooks.TryRemove(id, out _)) _ = PushGhostLookAsync(id);
    }

    /// <summary>The relay is gone: its ghost ids mean nothing any more.</summary>
    private void FigureOnRelayLost()
    {
        foreach (byte id in _ghostLooks.Keys.ToArray()) FigureOnPeerGone(id);
    }
}
