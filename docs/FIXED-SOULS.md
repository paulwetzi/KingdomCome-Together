# Change character (modified build)

This is **not** the official version of Kingdom Come: Together. Official repository:
https://github.com/DeepFriedDepp/KingdomCome-Together

Each player can choose in the game which NPC their partner sees them as. The choice is saved and used again
automatically the next time.

## Install on both PCs

1. Install the official `KingdomComeTogether-Setup-0.63.9.exe` and start the launcher once, until setup is finished.
2. Close the game and the launcher.
3. Unpack the **same** `KCT-FixedSoul-0.63.9.zip` on both PCs.
4. Run this in PowerShell in the unpacked folder:

```powershell
powershell -ExecutionPolicy Bypass -File .\Install-FixedSoul.ps1
```

Back to the official version:

```powershell
powershell -ExecutionPolicy Bypass -File .\Install-FixedSoul.ps1 -Uninstall
```

## Change character

1. Press **Insert** in the game. The first entry is **Change character**, open it with **End**.
2. **Page Up** and **Page Down** select, **End** confirms, **Insert** closes the window.
3. NPCs are sorted into women, men, story figures, guards and enemies, then by place.
4. As soon as an NPC is highlighted, a **preview** stands in front of you. Only you see it, and it goes when the window closes.
5. **End** on an NPC takes it. After a short moment your partner sees you as this figure.
6. **Default figure** at the top resets everything.

The change is locked during a fight and on a horse.

### Your figure on your own screen

With **My figure on my screen** on (menu group Display, on by default) you no longer see Henry either:

- In first person Henry's body is hidden except his arms and hands, so looking down shows no Henry.
- In dialogues and cutscenes Henry is not drawn at all. A copy of you as the chosen figure stands in his place. The DLL moves it
  every frame with your own position and walk, the same way your partner's screen moves your figure.

The copy does not speak or gesture like Henry does. `kcd.log` shows `FS-SELF spawned`, `FS-SELF view=own|other`, the native log
`FS-SELF bind`. `mp_own_figure off` turns it off.

### The menu

The menu and the character window are drawn on the game's own parchment panel. `mp_menu_plain on` brings back the plain white text.

## Test

1. One PC hosts, the other joins.
2. Choose a character on one PC. After a few seconds the figure must look like it on the other PC.
3. Change armor: the figure must stay dressed.
4. Restart the game and reconnect: the choice must still be there.

`kcd.log` shows `FIGURE chosen ...` for the player who chose and `FIGURE ghost ... look=...` for the partner. If it
says `SPAWN MISMATCH`, the game rejected this figure. Choose another one.

## Known limits

- The figure always wears the NPC's clothes. The player's armor is not visible on it.
- Combat animations can be missing or look wrong, especially on female figures.
- The original stays in the world, so the figure can appear twice there.
- Entries marked **(!)** are story figures, guards or enemies. Their own AI is paused so they do nothing by themselves.
  Because of that they may not get up by themselves after a knockout.
- NPC names in the window come from the game's German text table.
- Your own figure in dialogues and cutscenes only stands and walks: no lip movement, no gestures, no story animations.

## Fixed mapping by file (optional)

A figure can also be mapped to a Steam name. The choice in the window always takes priority:

```powershell
powershell -ExecutionPolicy Bypass -File .\Install-FixedSoul.ps1 -PlayerName "Steam-Name" -Soul ttkc_woman_9
```
