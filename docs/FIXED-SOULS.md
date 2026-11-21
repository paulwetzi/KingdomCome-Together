# Charakter wechseln (veränderte Version)

Dies ist **nicht** die offizielle Version von Kingdom Come: Together. Offizielles Repository:
https://github.com/DeepFriedDepp/KingdomCome-Together

Jeder Spieler kann im Spiel selbst wählen, als welcher NPC ihn sein Partner sieht. Die Wahl wird gespeichert und beim
nächsten Spiel automatisch wieder benutzt.

## Installation auf beiden PCs

1. Das offizielle `KingdomComeTogether-Setup-0.63.9.exe` installieren und den Launcher einmal starten, bis die
   Einrichtung fertig ist.
2. Spiel und Launcher schließen.
3. **Dieselbe** Datei `KCT-FixedSoul-0.63.9.zip` auf beiden PCs entpacken.
4. Im entpackten Ordner in PowerShell ausführen:

```powershell
powershell -ExecutionPolicy Bypass -File .\Install-FixedSoul.ps1
```

Zurück zur offiziellen Version:

```powershell
powershell -ExecutionPolicy Bypass -File .\Install-FixedSoul.ps1 -Uninstall
```

## Charakter wechseln

1. Im Spiel **Einfg** (Insert) drücken. Der erste Eintrag ist **Charakter wechseln**, mit **Ende** öffnen.
2. **Bild auf** und **Bild ab** wählen, **Ende** bestätigt, **Einfg** schließt das Fenster.
3. Die NPCs sind sortiert nach Frauen, Männern, Story-Figuren, Wachen und Feinden, darunter nach Ort.
4. Sobald ein NPC markiert ist, steht er als **Vorschau** vor dir. Nur du siehst sie, und sie verschwindet mit dem Fenster.
5. **Ende** auf einem NPC übernimmt ihn. Dein Partner sieht dich nach einem kurzen Moment als diese Figur.
6. **Standardfigur** ganz oben stellt alles zurück.

Du selbst bleibst auf deinem Bildschirm Henry. Im Kampf und auf dem Pferd ist der Wechsel gesperrt.

## Test

1. Ein PC hostet, der andere tritt bei.
2. Auf einem PC einen Charakter wählen. Auf dem anderen muss die Figur nach wenigen Sekunden so aussehen.
3. Rüstung wechseln: Die Figur muss angezogen bleiben.
4. Spiel neu starten und wieder verbinden: Die Wahl muss noch da sein.

Im `kcd.log` stehen dazu `FIGURE chosen ...` beim Wählenden und `FIGURE ghost ... look=...` beim Partner. Steht dort
`SPAWN MISMATCH`, hat das Spiel diese Figur abgelehnt. Dann einfach eine andere wählen.

## Bekannte Grenzen

- Die Figur trägt immer die Kleidung des NPCs. Rüstung des Spielers ist an ihr nicht sichtbar.
- Kampfanimationen können besonders an weiblichen Figuren fehlen oder falsch aussehen.
- Das Original bleibt in der Welt, die Figur kann dort also doppelt stehen.
- Einträge mit **(!)** sind Story-Figuren, Wachen oder Feinde. Ihre eigene KI wird angehalten, damit sie nichts von
  selbst tun. Dadurch stehen sie nach einem K.o. eventuell nicht von allein wieder auf.

## Feste Zuordnung per Datei (optional)

Eine Figur lässt sich auch fest einem Steam-Namen zuordnen. Die Wahl im Fenster hat aber immer Vorrang:

```powershell
powershell -ExecutionPolicy Bypass -File .\Install-FixedSoul.ps1 -PlayerName "Steam-Name" -Soul ttkc_woman_6
```
