# Jana Desktop

Zwei Geräte, die dieselben Daten zeigen, und ein Server, der sie hat.

| Teil | Was | Wo |
|---|---|---|
| `display/` | Tischdisplay auf dem ESP32-2432S028, zeigt Termine und Homelab | läuft |
| `assistent/` | Sprachassistent auf dem Pi Zero 2 W, BBW-Projektarbeit | im Bau |

Beide reden mit **Mia OS** auf `172.16.30.230:8080`. Mia OS ist bewusst nicht
aus dem Internet erreichbar, deshalb funktioniert hier nichts ohne Heimnetz
oder Tunnel.

---

## Display

ESP32-2432S028, im Netz als „Cheap Yellow Display" bekannt. 2,8 Zoll,
320 x 240 im Querformat, resistiver Touch.

Vier Seiten: Jetzt, Heute, Morgen, Homelab. Gewechselt wird durch Tippen
links oder rechts, durch Tippen auf die Punkte oben rechts, oder durch
Wischen.

### Bauen und flashen

```bash
cd display
pio run              # bauen
pio run -t upload    # flashen, Board muss per USB dranhängen
```

Auf Mias Mac liegt das Projekt unter `~/projekte/jana-desktop/`, der Port
ist `/dev/cu.usbserial-110`. PlatformIO liegt dort in
`~/Library/Python/3.9/bin` und ist nicht im Standard-PATH.

### Fallstricke, die Zeit gekostet haben

**Display und Touch liegen auf getrennten SPI-Bussen.** Wer beide auf
denselben legt, bekommt ein flackerndes Display und einen Touch, der nichts
meldet. Panel an HSPI, XPT2046 an VSPI.

**`XPT2046_Touchscreen` liegt nicht mehr in der PlatformIO-Registry.** Der
Build bricht mit `UnknownPackageError` ab, obwohl fast jede Anleitung zu
diesem Board die Bibliothek so einbindet. Lösung: direkt aus dem Git-Baum
von PaulStoffregen, steht so in der `platformio.ini`.

**921600 Baud sind zu schnell** für den USB-Wandler dieses Boards. Der
Upload bricht mit „Unable to verify flash chip connection" ab, obwohl der
Chip vorher sauber erkannt wurde. 460800 läuft zuverlässig.

**`touch.getPoint()` liefert Rohwerte von 0 bis 4095, keine Pixel.** Wer
damit gegen Bildschirmkoordinaten vergleicht, bekommt nie einen Treffer.
Umgerechnet wird in `rohNachX()` und `rohNachY()`.

**Die Wischrichtung nur beim Loslassen zu messen geht schief.** Ohne Druck
liefert ein resistives Panel Rauschen. Die letzte gültige Lage wird
deshalb mitgeschrieben, solange der Finger noch aufliegt.

**Der Touch dreht sich nicht mit dem Display.** Er ist ein eigener Baustein
an einem eigenen Bus. Wer beim Wechsel auf Querformat nur `tft.setRotation()`
ändert, wischt danach in die falsche Richtung.

**Umlaute werden zu Kästchen.** Die Schriften von TFT_eSPI sind eine
Bytetabelle, Mia OS liefert UTF-8. Bei deutschen Terminen ist das kein
Randfall („Frühstück", „Prüfung"), deshalb `entumlauten()` vor dem Zeichnen.

**Schriftbreite messen statt schätzen.** Font 7 braucht für „09:47" rund
142 Pixel. In einer 112 Pixel breiten Spalte stand auf dem Display „09:4".
`tft.textWidth()` sagt vorher, ob es passt.

**WiFiManager gibt zu früh auf.** Gemessen: nach 9,2 Sekunden fiel es ins
Einrichtungsportal, obwohl die Zugangsdaten gespeichert waren.
`setConnectTimeout(20)` plus `setConnectRetries(3)` behebt es.

---

## Assistent

Noch nicht gebaut. Abgabe der Projektarbeit ist der **24.09.2026**,
Showtime um 13:00.

Die Architektur steht und beruht auf einer eigenen Messung vom 10.09.2026:

| Schritt | Wo | Warum dort |
|---|---|---|
| Wakeword „Jana" | auf dem Gerät | läuft in Millisekunden, ohne Netz |
| Spracherkennung | Groq, Whisper large-v3 | das Modell, das kein SBC schafft |
| Antwort | Mia OS | steht ohnehin |
| Vorlesen | Piper, lokal | Sprachausgabe ist leicht |
| Text und Bild | das Display | schon fertig |
| Zustand | E-Paper | still, wenn alles läuft |

**Warum die Spracherkennung nicht lokal läuft.** Auf einem Raspberry Pi 4
gemessen, ganze Wartezeit von „Datei da" bis „Text da":

| Modell | Audio | Wartezeit | Faktor |
|---|---|---|---|
| tiny | 4,4 s | 7,2 s | 1,63x |
| base | 4,4 s | 17,2 s | 3,91x |
| small | 4,4 s | 57,3 s | 13,05x |

Größer wurde dabei nicht besser: `small` rechnete 13-mal so lange wie
`tiny` und lieferte ein schlechteres Ergebnis. Bei diesen Modellgrößen liegt
Deutsch außerhalb dessen, was sie können. Damit war ein Pi 5 für 184,90 €
erledigt, bevor er gekauft war.

---

## CI

`.github/workflows/firmware.yml` baut die Firmware bei jedem Push, prüft wie
voll der Flash ist und hängt das Ergebnis an einen Release.

**Geflasht wird nicht automatisch.** Der Runner steht in der Cloud, das
Board hängt per USB an Mias Mac, und ein Runner kann kein Kabel. Aus dem CI
kommt eine gebaute, nummerierte Datei, das Aufspielen bleibt ein Handgriff.

Der Flash war am 11.09.2026 zu 84,7 Prozent belegt. Der Workflow warnt ab
90 Prozent und bricht ab 95 Prozent ab, damit das nicht erst beim Flashen
auffällt.
