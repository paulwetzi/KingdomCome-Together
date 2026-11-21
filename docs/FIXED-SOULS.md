# Feste Figur für einen Spieler (veränderte Version)

Dies ist **nicht** die offizielle Version von Kingdom Come: Together. Offizielles Repository:
https://github.com/DeepFriedDepp/KingdomCome-Together

Die Änderung gibt einem Spieler per Steam-Namen eine feste weibliche NPC-Soul als Figur. Der Name steht nur in
`fixed-souls.json` im Installationsordner und nie im Repository.

## Installation auf beiden PCs

1. Das offizielle `KingdomComeTogether-Setup-0.63.9.exe` installieren und den Launcher einmal starten, bis die
   Einrichtung fertig ist.
2. Spiel und Launcher schließen.
3. **Dieselbe** Datei `KCT-FixedSoul-0.63.9.zip` auf beiden PCs entpacken.
4. Im entpackten Ordner in PowerShell ausführen. Der Name muss genau dem Steam-Namen entsprechen:

```powershell
powershell -ExecutionPolicy Bypass -File .\Install-FixedSoul.ps1 -PlayerName "Steam-Name" -Soul ttkc_woman_6
```

Eine andere Figur wählst du, indem du den Befehl mit einer anderen Soul wiederholst. Zurück zur offiziellen Version:

```powershell
powershell -ExecutionPolicy Bypass -File .\Install-FixedSoul.ps1 -Uninstall
```

## Test

1. Ein PC hostet, der andere tritt bei.
2. Auf dem PC des Partners muss die Figur des eingetragenen Spielers als Frau in ihrer eigenen Kleidung erscheinen.
3. Der eingetragene Spieler sieht sich selbst weiter als Henry. Das ist richtig.
4. Rüstung wechseln: Die Figur muss angezogen bleiben.
5. Im `kcd.log` des Partners stehen `FIXED-SOUL soul=...` und `spawn verify ... resolved class=NPC_Female`.

Steht dort `SPAWN MISMATCH`, hat das Spiel die Soul abgelehnt und einen männlichen Ersatz gebaut. Dann eine andere
Soul wählen.

## Bekannte Grenzen

- Rüstung und Waffen des Spielers sind an der Figur nicht sichtbar.
- Kampfanimationen können an einem weiblichen Körper fehlen oder falsch aussehen.
- Die echte Dorfbewohnerin bleibt in der Welt. In ihrem Dorf können also zwei gleiche Frauen stehen.
- Anders als die Mod-eigenen Figuren gehört diese Soul zu einem Dorf. Dorfkämpfe oder Quests können sie deshalb
  einbeziehen.

## Kandidatinnen prüfen

```powershell
python tools\Find-FixedSoulCandidates.py "<Spielordner>\Data\Tables.pak"
```

Das Skript listet nummerierte Dorfbewohnerinnen ohne Autoritätsrolle, ohne Sprechergruppe, ohne VIP-Klasse, mit
generischer Stimme und ohne Soldaten-, Wachen- oder Feindfraktion.
