# Updates über die Luft

Stand 11.09.2026. Entwurf, noch nicht gebaut.

Das Display soll selbst merken, wenn eine neue Fassung da ist, und fragen,
bevor es sie nimmt. Mias Ablauf:

> Popup „Neue Version da", alte Nummer, neue Nummer, und dann ja, nein
> oder später.

## Die Nummer

Dieselbe Regel wie Mia OS: **`MAJOR.MINOR.<Anzahl Commits>`**. Major und
Minor stehen in `display/version.txt` und werden von Hand gesetzt, der
dritte Teil zählt sich selbst hoch.

Warum nicht einfach durchnummerierte Bauten: Die Geräte und der Server
gehören zusammen. Wer `0.1.7` auf dem Display liest, soll das ohne
Umrechnen neben `0.2.108` in der Oberfläche halten können. Zwei Schemata
für dasselbe System wären eine Quelle für Missverständnisse.

Die Nummer kommt beim Bauen als Compilerschalter in die Firmware
(`display/version_bauen.py`), das Gerät weiß also selbst, welche Fassung
es ist. Der Release-Tag heißt `v0.1.7`, genau wie bei Mia OS.

---

## Warum der Server dazwischen steht

Naheliegend wäre, das Gerät direkt bei GitHub nachfragen zu lassen. Solange
das Repository privat ist, braucht das ein Token, und **ein Token gehört
nicht in die Firmware**: Ein ESP32 ist ein Bauteil auf dem Schreibtisch, wer
ihn mitnimmt liest den Flash aus. Dazu kommt, dass sich ein fest
eingebrannter Wert nur wechseln lässt, indem man genau das Gerät flasht, das
man gerade aus der Ferne erreichen wollte.

Auch bei einem öffentlichen Repository bleibt der Umweg sinnvoll: Das Gerät
kennt dann nur eine Adresse im eigenen Netz und muss nicht ins Internet.

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
        Neue Version da

        jetzt:  0.1.3
        neu:    0.1.7

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

## Stand

Gebaut und in Betrieb seit dem 11.09.2026. Die Kette läuft vom Push bis zum
Gerät durch: CI baut, legt die Firmware in den Zweig `firmware`, der Server
holt sie, das Display fragt beim Termin-Abruf mit und meldet sich dabei
selbst an.

Vom Push bis zum Dialog vergehen bis zu zwölf Minuten: neunzig Sekunden für
den Bau, dazu der Zwischenspeicher des Servers von zehn Minuten und der
Abruf des Geräts alle zwei Minuten.

**Noch nicht bewiesen:** der Rückfall auf die vorherige Fassung. Der Code
ist da und die Probezeit greift, aber es wurde nie absichtlich eine kaputte
Fassung eingespielt, um zuzusehen, ob das Gerät zurückspringt. Bis das
einmal passiert ist, gilt der Rückfall als ungetestet.
