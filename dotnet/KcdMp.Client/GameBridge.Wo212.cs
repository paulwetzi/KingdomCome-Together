// Copyright (C) 2026 the Kingdom Come: Together contributors (AUTHORS). SPDX-License-Identifier: GPL-3.0-only
// GPLv3 section 7 additional terms: NOTICE. This project's own code only; Kingdom Come: Deliverance II and its
// content belong to Warhorse Studios and PLAION. Unofficial, free, not affiliated with or endorsed by them.
using System.Collections.Concurrent;
using KcdMp.Wire;

namespace KcdMp.Client;

// WO-212: the agent's half of the stuck inventory keys and of each player's own crime switch (kdcmp.lua, KCD2MP.w212).
public partial class GameBridge
{
    /// <summary>Host: a peer whose crimes are off, until this tick. The peer repeats it every 10 s, so a lost peer's entry runs out.</summary>
    private readonly ConcurrentDictionary<byte, long> _w212NoCrimeUntil = new();
    private const long Wo212NoCrimeHoldMs = 30_000;

    private bool Wo212NoCrime(byte peer) => _w212NoCrimeUntil.TryGetValue(peer, out long until) && Environment.TickCount64 < until;

    /// <summary><c>w212_keys free|set &lt;why&gt;</c>: the DLL makes the combat controller's own filter call.</summary>
    private async Task Wo212OnKeysAsync(string? arg)
    {
        var f = (arg ?? "").Split(' ', StringSplitOptions.RemoveEmptyEntries);
        if (f.Length != 2 || f[0] is not ("free" or "set") || !Wo139Text.IsWord(f[1])) return;
        int result = -1;
        try
        {
            var r = await _combat.Wo163Async(Protocol.Wo212OpInputFilter, [(byte)(f[0] == "set" ? 1 : 0)]);
            if (r is { Ok: true, Payload.Length: >= 1 } x) result = x.Payload[0];
        }
        catch (Exception ex) when (ex is not OperationCanceledException) { Console.WriteLine($"MP-W212 keys: {ex.Message}"); }
        Console.WriteLine($"MP-W212 keys {f[0]} ({f[1]}): result {result}");
        await ExecLuaAsync($"if KCD2MP_W212KeysDone then KCD2MP_W212KeysDone({result}, \"{f[1]}\") end");
    }

    /// <summary><c>w212_shop &lt;keeper&gt; &lt;0 close|1 open|2 ask&gt; &lt;shop entity hex&gt;</c>: the shop's own call for a joiner's copy.</summary>
    private async Task Wo212OnShopAsync(string? arg)
    {
        var f = (arg ?? "").Split(' ', StringSplitOptions.RemoveEmptyEntries);
        if (f.Length != 3 || !Wo139Text.IsName(f[0]) || f[1] is not ("0" or "1" or "2")
            || !uint.TryParse(f[2], System.Globalization.NumberStyles.HexNumber, System.Globalization.CultureInfo.InvariantCulture, out uint shop)) return;
        byte[] name = System.Text.Encoding.ASCII.GetBytes(f[0]);
        if (name.Length > 63) return;
        var a = new byte[6 + name.Length];
        a[0] = (byte)(f[1][0] - '0');
        System.Buffers.Binary.BinaryPrimitives.WriteUInt32LittleEndian(a.AsSpan(1), shop);
        a[5] = (byte)name.Length;
        name.CopyTo(a, 6);
        int result = -1, before = 0, after = 0;
        try
        {
            var r = await _combat.Wo163Async(Protocol.Wo212OpShopKeeper, a);
            if (r is { Ok: true, Payload.Length: >= 3 } x) { result = x.Payload[0]; before = x.Payload[1]; after = x.Payload[2]; }
        }
        catch (Exception ex) when (ex is not OperationCanceledException) { Console.WriteLine($"MP-W212 shop: {ex.Message}"); }
        Console.WriteLine($"MP-W212 shop keeper={f[0]} mode={f[1]}: result {result}, open {before} -> {after}");
        await ExecLuaAsync($"if KCD2MP_W212ShopDone then KCD2MP_W212ShopDone(\"{f[0]}\", {f[1]}, {result}, {before}, {after}) end");
    }

    /// <summary><c>w212_crime clear|off|on</c>: a joiner asks its host; a host applies it to the partners only under the joint mode.</summary>
    private async Task Wo212OnCrimeAsync(string? arg)
    {
        string word = (arg ?? "").Trim();
        if (word is not ("clear" or "off" or "on")) return;
        if (W139Joiner)
        {
            await Wo139SendAsync(Protocol.CrimeAskUp, Protocol.JoinTargetHost, Protocol.CrimeAskClear, 0, word);
            return;
        }
        if (word != "clear" || !W139Host || !_w151CrimeJoint) return;
        foreach (byte peer in Wo134Peers()) await Wo212ClearRecordAsync(peer, "the host cleared the shared crimes");
    }

    /// <summary>Host: a joiner's menu choice.</summary>
    private async Task Wo212HostOnAskAsync(byte src, string? word)
    {
        switch (word)
        {
            case "off":
                _w212NoCrimeUntil[src] = Environment.TickCount64 + Wo212NoCrimeHoldMs;
                return;
            case "on":
                _w212NoCrimeUntil.TryRemove(src, out _);
                Console.WriteLine($"MP-W212 host: ghost {src}'s crimes count again");
                return;
            case "clear":
                await Wo212ClearRecordAsync(src, "their menu");
                await Wo151OnJoinerClearedAsync(src, "persuaded");   // joint only: this world forgets the host's Henry too
                return;
        }
    }

    private async Task Wo212ClearRecordAsync(byte peer, string why)
    {
        int n = Wo139Record(peer).Clear(null);
        foreach (var (g, p) in _w139Pursuits.ToArray())
            if (p.Peer == peer) await Wo139EndPursuitAsync(g, "the record is cleared (menu)");
        await Wo139SendAsync(Protocol.CrimeHostUp, peer, Protocol.CrimeHostCleared, 0, Wo139Rules.ClearedText("partner-persuaded", null));
        Console.WriteLine($"MP-W212 host: ghost {peer}'s record cleared ({n} crime(s)) -- {why}");
    }
}
