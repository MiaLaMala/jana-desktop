# Updates über die Luft

Stand 11.09.2026. Entwurf, noch nicht gebaut.

Das Display soll selbst merken, wenn eine neue Fassung da ist, und fragen,
bevor es sie nimmt. Mias Ablauf:

> Popup „Neue Version verfügbar", alte Nummer, neue Nummer, und dann ja,
> nein oder später.

---

## Das Problem: ein privates Repo braucht einen Schlüssel

Die Firmware liegt als Release im privaten Repo `MiaLaMala/jana-desktop`.
Ein Download braucht deshalb ein Token.

**Das Token darf nicht in die Firmware.** Ein ESP32 ist ein Bauteil auf dem
Schreibtisch. Wer ihn mitnimmt, liest den Flash aus, und ein
Repository-Token gilt für alle Repositories. Dazu kommt: Ein Wert, der fest
im Quelltext steht, lässt sich nicht wechseln, ohne genau das Gerät zu
flashen, das man gerade aus der Ferne erreichen wollte.

## Die Lösung: Mia OS steht dazwischen

Mia OS hat bereits einen Zugang zu GitHub, läuft ohnehin, und beide Geräte
reden sowieso mit ihm.

```
ESP32  ──fragt──>  Mia OS  ──fragt mit Token──>  GitHub
       <──liefert──        <──liefert───────────
```

Zwei neue Endpunkte:

| Pfad | Liefert |
|---|---|
| `GET /api/firmware/neueste` | Nummer, Größe, Prüfsumme, Datum |
| `GET /api/firmware/datei` | die Binärdatei, aus GitHub durchgereicht |

Das Gerät kennt damit kein Token, nur eine Adresse im Heimnetz. Wird der
Zugang gewechselt, ändert sich eine Zeile auf dem Server und kein Gerät
muss angefasst werden.

Nebeneffekt: Mia OS weiß dadurch, welche Fassung auf dem Display läuft.
Das lässt sich auf der Homelab-Seite anzeigen.

---

## Was auf dem Display passiert

**Nachsehen** beim Start und danach einmal pro Stunde. Nicht öfter: ein
Gerät, das ständig fragt, erzeugt Last ohne Nutzen.

**Fragen** in einem Kasten über der laufenden Seite:

```
        Neue Fassung da

        jetzt:  Bau 3
        neu:    Bau 7

    [ Ja ]  [ Später ]  [ Nein ]
```

- **Ja** lädt und flasht, mit Balken. Danach Neustart.
- **Später** legt es für vier Stunden weg.
- **Nein** überspringt genau diese Nummer. Erst die übernächste fragt wieder.

Die Antwort landet im `nvs`-Speicher, überlebt also den Neustart.

**Nicht fragen, während etwas läuft.** Steht gerade ein Termin an oder ist
ein Dienst unten, wartet der Kasten. Ein Update-Dialog über einer Störung
ist genau der falsche Moment.

---

## Warum das nicht alles kaputt machen kann

Der ESP32 hat zwei Speicherplätze für Programme, `app0` und `app1`. Die
Standard-Aufteilung, die dieses Projekt schon benutzt, sieht beide vor.
Geflasht wird immer in den gerade unbenutzten.

Danach gilt die neue Fassung als **auf Probe**. Sie muss sich beim ersten
Start bewähren: WLAN da, Mia OS erreichbar, eine Antwort gelesen. Erst dann
wird sie bestätigt. Bleibt die Bestätigung aus, springt das Gerät beim
nächsten Neustart von selbst auf die alte zurück.

Damit ist der schlimmste Fall kein Ziegelstein, sondern ein Neustart auf
der vorherigen Fassung.

Dasselbe Muster benutzt `deploy/pull-deploy.sh` in Mia OS schon: neu bauen,
Gesundheit prüfen, sonst zurückrollen. Was dort für den Server gilt, gilt
hier für das Gerät.

---

## Platz im Flash

OTA braucht beide App-Slots. Bei der Standard-Aufteilung ist jeder
1.310.720 Bytes groß, und der Build vom 11.09.2026 belegt davon **85,2
Prozent**. Das reicht für OTA, aber nicht mehr für viel danach.

Deshalb gehört der Wechsel auf `min_spiffs.csv` dazu: dort ist jeder Slot
1.966.080 Bytes groß, dieselbe Firmware belegt **56,8 Prozent**. Der Preis
ist ein kleinerer Dateibereich, den dieses Projekt nicht benutzt.

Eine Zeile in der `platformio.ini`:

```ini
board_build.partitions = min_spiffs.csv
```

**Dieser eine Wechsel muss über Kabel geflasht werden.** Eine geänderte
Partitionstabelle lässt sich nicht über die Luft einspielen, weil das Gerät
dabei die Karte austauschen müsste, nach der es gerade selbst arbeitet.
Danach läuft alles Weitere ohne Kabel.

---

## Was noch fehlt

Ein **Deploy-Key** für `jana-desktop` auf dem Mia-OS-Container. Für
`mia-os` gibt es so einen Schlüssel bereits, das Muster steht also. Den
legt Mia selbst an, Zugangsdaten macht sie nicht über Dritte.
