/**
 * Jana-Display: Mia OS auf einem Board, das auf dem Tisch steht.
 *
 * Mia OS beantwortet die Frage "was ist heute" auf dem Handy, und das Handy
 * liegt beim Aufstehen woanders. Dieses Geraet zeigt dieselben Daten ohne
 * Entsperren, ohne App, ohne dass jemand es anfasst.
 *
 * **Querformat, 320 x 240.** Das ist nicht nur eine gedrehte Hochkantseite:
 * die Startseite steht dadurch in zwei Spalten, links die Uhr, rechts was
 * laeuft und was kommt. Auf einem Geraet, das quer auf dem Tisch liegt, ist
 * das der natuerliche Schnitt, und nichts muss mehr untereinander gequetscht
 * werden.
 *
 * Vier Seiten, Wischen blaettert:
 *
 *   **Jetzt**    grosse Uhr, was gerade laeuft, was als naechstes kommt
 *   **Heute**    alle Termine des Tages
 *   **Morgen**   dasselbe fuer morgen
 *   **Homelab**  47 ueberwachte Dienste, und was davon nicht laeuft
 *
 * Die erste Seite ist die eigentliche Idee. Eine Terminliste beantwortet
 * "was ist heute", aber die Frage im Vorbeigehen ist eine andere: **wie
 * lange noch**. Dafuer muss man auf dem Handy rechnen, hier steht es da.
 *
 * Drei Entscheidungen, die den Rest erklaeren:
 *
 * **Kein Passwort im Quelltext.** Beim ersten Start macht das Geraet ein
 * eigenes WLAN auf und fragt nach Zugangsdaten und der Adresse von Mia OS.
 * Das ist nicht nur bequemer, es ist der Unterschied zwischen einem Projekt,
 * das man herzeigen kann, und einem, in dessen Repository Mias WLAN-Passwort
 * steht.
 *
 * **Die Adresse ist einstellbar**, weil Mia OS bewusst nicht im Internet
 * steht. Es ist ueber das Heimnetz erreichbar und sonst nirgends, und ein
 * fest eingebauter Hostname waere anderswo schlicht falsch.
 *
 * **Nur lesen.** Das Geraet schickt nichts an Mia OS zurueck. Ein Display auf
 * dem Tisch, das nur Daten abholt, kann im schlimmsten Fall nichts kaputt
 * machen, und genau das gehoert zu einem Geraet, das offen herumsteht.
 */

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <XPT2046_Touchscreen.h>
#include <Preferences.h>
#include <Update.h>
#include <esp_ota_ops.h>
#include <time.h>

// --- Farben nach DESIGN.md ------------------------------------------------
//
// Dieselbe Palette wie die Weboberflaeche, dunkel: das Geraet steht morgens
// im Halbdunkel, und ein weisses Vollbild um sieben Uhr frueh ist eine
// Zumutung. Umgerechnet von Hex auf RGB565, was das Display spricht.

#define C_GRUND 0x0841     // #0b0c0e Grund
#define C_FLAECHE 0x10A2   // #141518 Flaeche
#define C_ERHOBEN 0x18E3   // #1c1e22 Flaeche erhoeht
#define C_LINIE 0x2945     // #26282e Linie
#define C_TEXT 0xF79E      // #f2f3f5 Text
#define C_GEDAEMPFT 0x9492 // #9096a0 Text gedaempft
#define C_AKZENT 0xD967    // #d92d3c Akzent, Mias Rot
#define C_AKZENT_MATT 0x88E5 // #8c1f28 Akzent gedaempft
#define C_GUT 0x2FEB       // #30d158 in Ordnung
#define C_ACHTUNG 0xFCE1   // #ff9f0a Achtung
// Fehler muss sich vom Akzent abheben, sonst sieht man den roten Punkt
// im Raster nicht. Deshalb heller und orangestichiger als der Akzent.
#define C_FEHLER 0xFB4B    // #ff6b5e Fehler

// --- Anschluesse ----------------------------------------------------------
//
// Der Touch haengt am zweiten SPI-Bus. Das Board fuehrt ihn auf eigene Pins,
// und sie mit dem Display zu teilen ist der haeufigste Fehler bei diesem
// Modell: das Display flackert und der Touch meldet nichts.

#define TOUCH_CS 33
#define TOUCH_IRQ 36
#define TOUCH_MOSI 32
#define TOUCH_MISO 39
#define TOUCH_CLK 25

#define LED_R 4
#define LED_G 16
#define LED_B 17
#define BL_KANAL 0

// --- Masse der Flaeche ----------------------------------------------------
// Als Konstanten statt als Zahlen im Code: bei der Drehung von hoch auf quer
// war jede einzelne Koordinate anzufassen, und uebersehene Zahlen liefen
// stumm aus dem Bild.
const int BREIT = 320;
const int HOCH = 240;
const int KOPF_H = 34;   // Hoehe der Kopfzeile
const int FUSS_Y = 222;  // Oberkante der Fusszeile

const uint32_t HOLINTERVALL_MS = 120000;
const uint32_t VERALTET_MS = 600000;
const int MAX_ZEILEN = 6;

const int NACHT_AB = 22;
const int NACHT_BIS = 7;
const uint8_t HELL_TAG = 255;
const uint8_t HELL_NACHT = 28;

// Wie weit ein Finger wandern muss, damit es als Wischen gilt. Der Touch ist
// resistiv und ungenau; darunter waere jedes Tippen ein Wisch in zufaellige
// Richtung.
const int WISCH_WEG = 30;
// Darunter gilt eine Beruehrung als Tippen statt als Wisch.
const int TIPP_WEG = 14;

// XPT2046_Touchscreen::getPoint() liefert ROHWERTE von 0 bis 4095, keine
// Pixel. Wer damit gegen Bildschirmkoordinaten vergleicht, bekommt nie
// einen Treffer. Diese Grenzen bilden den nutzbaren Bereich des Panels ab
// und werden unten auf 320 x 240 umgerechnet.
const int ROH_MIN = 300;
const int ROH_MAX = 3800;

// Rohwert in Bildschirmkoordinaten. Ausserhalb liegende Werte werden
// beschnitten statt verworfen: der Rand des Panels misst ungenau.
int rohNachX(int roh) {
  const long v = map(roh, ROH_MIN, ROH_MAX, 0, BREIT);
  return constrain((int)v, 0, BREIT);
}

int rohNachY(int roh) {
  const long v = map(roh, ROH_MIN, ROH_MAX, 0, HOCH);
  return constrain((int)v, 0, HOCH);
}

TFT_eSPI tft = TFT_eSPI();
SPIClass touchSPI(VSPI);
XPT2046_Touchscreen touch(TOUCH_CS, TOUCH_IRQ);

char basisUrl[80] = "http://172.16.30.230:8080";

struct Termin {
  String titel;
  String zeit;
  String ende;
  String ort;
  // Als Minuten seit Mitternacht, damit sich damit rechnen laesst. -1 heisst
  // ganztaegig: ein Geburtstag hat keine Uhrzeit und darf im Zeitstrahl
  // nicht als "00:00 Uhr" auftauchen.
  int beginnMin = -1;
  int endeMin = -1;
};

struct Briefing {
  String datum;
  Termin heute[MAX_ZEILEN];
  int heuteAnzahl = 0;
  int heuteGesamt = 0;
  Termin morgen[MAX_ZEILEN];
  int morgenAnzahl = 0;
  int morgenGesamt = 0;
  String faellig[MAX_ZEILEN];
  int faelligAnzahl = 0;
  int offen = 0;
};

struct Homelab {
  int gesamt = 0;
  int oben = 0;
  float uptime = 0;
  String stoerung[MAX_ZEILEN];
  String meldung[MAX_ZEILEN];
  int stoerAnzahl = 0;
  String zahlLabel[4];
  String zahlWert[4];
  int zahlAnzahl = 0;
  bool gueltig = false;
};

Briefing briefing;
Homelab homelab;
bool habenDaten = false;
uint32_t letzterErfolg = 0;
uint32_t letzterVersuch = 0;
String letzterFehler = "";

int ansicht = 0;
const int ANSICHTEN = 4;

// --- Updates ueber die Luft -----------------------------------------------
//
// Das Geraet fragt Mia OS, nicht GitHub: das Repo ist privat, und ein
// Schluessel im Flash waere auslesbar. Gefragt wird beim ohnehin laufenden
// Termin-Abruf mit, ein eigener Takt waere Last ohne Gewinn.

#ifndef FIRMWARE_VERSION
#define FIRMWARE_VERSION "0.0.0"
#endif

// Wie lange "Spaeter" gilt. Vier Stunden sind lang genug, um in Ruhe zu
// arbeiten, und kurz genug, dass ein Update nicht vergessen wird.
const uint32_t SPAETER_MS = 4UL * 60 * 60 * 1000;

struct Update_t {
  String version;      // was bereitsteht
  String abgelehnt;    // diese Nummer hat Mia weggeklickt
  uint32_t spaeterBis = 0;
  bool gefragt = false;  // Dialog steht gerade auf dem Schirm
  bool laeuft = false;   // wird gerade geflasht
  int prozent = 0;
  String fehler;
};
Update_t neuling;

Preferences merker;

void updateBalkenZeichnen();
void updateDialogZeichnen();

/** Versionen wie 0.1.7 vergleichen. Gibt >0, wenn a neuer als b ist. */
int versionVergleich(const String &a, const String &b) {
  int ai = 0, bi = 0;
  for (int teil = 0; teil < 3; teil++) {
    int az = 0, bz = 0;
    while (ai < (int)a.length() && a[ai] >= '0' && a[ai] <= '9')
      az = az * 10 + (a[ai++] - '0');
    while (bi < (int)b.length() && b[bi] >= '0' && b[bi] <= '9')
      bz = bz * 10 + (b[bi++] - '0');
    if (az != bz)
      return az - bz;
    if (ai < (int)a.length())
      ai++;  // Punkt ueberspringen
    if (bi < (int)b.length())
      bi++;
  }
  return 0;
}
bool neuZeichnen = true;
uint8_t helligkeit = HELL_TAG;

// Wann zuletzt jemand das Geraet angefasst hat. Nach einer Beruehrung geht
// es nachts kurz auf volle Helligkeit, damit man im Dunkeln etwas erkennt,
// ohne dass es die ganze Nacht leuchtet.
uint32_t letzteBeruehrung = 0;
const uint32_t WACH_MS = 20000;

void led(bool r, bool g, bool b) {
  digitalWrite(LED_R, r ? LOW : HIGH);
  digitalWrite(LED_G, g ? LOW : HIGH);
  digitalWrite(LED_B, b ? LOW : HIGH);
}

/**
 * Umlaute auf das umsetzen, was die eingebaute Schrift kann.
 *
 * Mia OS liefert UTF-8, die Schriften von TFT_eSPI sind eine Bytetabelle.
 * Ein "ä" kommt dort als zwei Zeichen an und wird zu Kaesekaestchen. Bei
 * deutschen Terminen ist das keine Randerscheinung: "Frühstück", "Prüfung",
 * "Ärztin". Also wird umgeschrieben, bevor gezeichnet wird.
 */
String entumlauten(const String &roh) {
  String aus;
  aus.reserve(roh.length());
  for (size_t i = 0; i < roh.length(); i++) {
    uint8_t c = (uint8_t)roh[i];
    if (c == 0xC3 && i + 1 < roh.length()) {
      uint8_t n = (uint8_t)roh[++i];
      switch (n) {
      case 0xA4: aus += "ae"; break; // ä
      case 0xB6: aus += "oe"; break; // ö
      case 0xBC: aus += "ue"; break; // ü
      case 0x84: aus += "Ae"; break; // Ä
      case 0x96: aus += "Oe"; break; // Ö
      case 0x9C: aus += "Ue"; break; // Ü
      case 0x9F: aus += "ss"; break; // ß
      default: aus += '?'; break;
      }
    } else if (c == 0xE2 && i + 2 < roh.length()) {
      i += 2;
      aus += '-';
    } else if (c < 0x80) {
      aus += (char)c;
    }
  }
  return aus;
}

String kuerzen(const String &text, int maxZeichen) {
  if ((int)text.length() <= maxZeichen)
    return text;
  return text.substring(0, maxZeichen - 1) + ".";
}

/** "07:30" als Minuten seit Mitternacht. -1, wenn da keine Uhrzeit steht. */
int alsMinuten(const String &hhmm) {
  if (hhmm.length() < 4)
    return -1;
  const int punkt = hhmm.indexOf(':');
  if (punkt < 1)
    return -1;
  const int h = hhmm.substring(0, punkt).toInt();
  const int m = hhmm.substring(punkt + 1).toInt();
  if (h < 0 || h > 23 || m < 0 || m > 59)
    return -1;
  return h * 60 + m;
}

/** Eine Dauer, wie ein Mensch sie sagen wuerde: "3 min", "1:45 h". */
String alsDauer(int minuten) {
  if (minuten < 0)
    minuten = 0;
  if (minuten < 60)
    return String(minuten) + " min";
  char puffer[12];
  snprintf(puffer, sizeof(puffer), "%d:%02d h", minuten / 60, minuten % 60);
  return String(puffer);
}

/** Wie viele Minuten seit Mitternacht es gerade ist. -1 ohne gestellte Uhr. */
int jetztMinuten() {
  struct tm jetzt;
  if (!getLocalTime(&jetzt, 20))
    return -1;
  return jetzt.tm_hour * 60 + jetzt.tm_min;
}

/** Eine Anfrage an Mia OS. Gibt ``false`` zurueck und setzt ``letzterFehler``. */
bool holen(const char *pfad, JsonDocument &doc, JsonDocument &filter) {
  if (WiFi.status() != WL_CONNECTED) {
    letzterFehler = "kein WLAN";
    return false;
  }
  HTTPClient http;
  http.setTimeout(8000);
  http.setConnectTimeout(5000);
  if (!http.begin(String(basisUrl) + pfad)) {
    letzterFehler = "Adresse ungueltig";
    return false;
  }
  const int code = http.GET();
  if (code != 200) {
    letzterFehler = code > 0 ? ("HTTP " + String(code)) : "keine Antwort";
    http.end();
    return false;
  }
  const DeserializationError fehler =
      deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));
  http.end();
  if (fehler) {
    letzterFehler = String("JSON: ") + fehler.c_str();
    return false;
  }
  return true;
}

/**
 * Nachsehen, ob eine neuere Fassung bereitsteht.
 *
 * Meldet dabei gleich, wer fragt: Mia OS fuehrt daraus seine Geraeteliste,
 * ohne dass es dafuer einen eigenen Abruf braucht.
 */
void updatePruefen() {
  if (WiFi.status() != WL_CONNECTED || neuling.laeuft)
    return;

  HTTPClient http;
  http.setTimeout(8000);
  http.setConnectTimeout(5000);

  String pfad = String(basisUrl) + "/api/firmware/neueste?kennung=" +
                WiFi.macAddress() + "&name=Jana-Display&version=" + FIRMWARE_VERSION;
  if (!http.begin(pfad))
    return;

  if (http.GET() != 200) {
    http.end();
    return;
  }

  JsonDocument doc;
  JsonDocument filter;
  filter["version"] = true;
  const DeserializationError fehler =
      deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));
  http.end();
  if (fehler)
    return;

  const String draussen = doc["version"].as<String>();
  if (draussen.isEmpty())
    return;

  // Nur fragen, wenn es wirklich neuer ist. Gleich oder aelter heisst:
  // nichts zu tun. Ein Dialog fuer dieselbe Nummer waere Gehupe.
  if (versionVergleich(draussen, FIRMWARE_VERSION) <= 0) {
    neuling.version = "";
    return;
  }

  // Diese Nummer hat Mia bereits weggeklickt.
  if (draussen == neuling.abgelehnt)
    return;

  neuling.version = draussen;
}

/**
 * Die neue Fassung holen und in den freien Speicherplatz schreiben.
 *
 * Der ESP32 hat zwei Plaetze fuer Programme. Geschrieben wird immer in den
 * gerade unbenutzten, der laufende bleibt unangetastet. Geht dabei etwas
 * schief, startet das Geraet einfach wieder mit dem alten.
 */
bool updateHolen() {
  if (WiFi.status() != WL_CONNECTED)
    return false;

  neuling.laeuft = true;
  neuling.prozent = 0;
  neuling.fehler = "";

  HTTPClient http;
  http.setTimeout(30000);
  http.setConnectTimeout(8000);
  if (!http.begin(String(basisUrl) + "/api/firmware/datei")) {
    neuling.fehler = "Adresse ungueltig";
    neuling.laeuft = false;
    return false;
  }

  if (http.GET() != 200) {
    neuling.fehler = "Server antwortet nicht";
    http.end();
    neuling.laeuft = false;
    return false;
  }

  const int groesse = http.getSize();
  if (groesse <= 0) {
    neuling.fehler = "Groesse unbekannt";
    http.end();
    neuling.laeuft = false;
    return false;
  }

  if (!Update.begin(groesse)) {
    // Haeufigster Grund: der freie Platz ist kleiner als die Datei.
    neuling.fehler = "Kein Platz";
    http.end();
    neuling.laeuft = false;
    return false;
  }

  // In Stuecken lesen und dabei zeichnen. Ein Balken, der sich nicht
  // bewegt, sieht nach Absturz aus, und 1,1 MB dauern ueber WLAN spuerbar.
  WiFiClient *strom = http.getStreamPtr();
  uint8_t puffer[1024];
  int gelesen = 0;
  uint32_t letztesZeichnen = 0;

  while (http.connected() && gelesen < groesse) {
    const size_t da = strom->available();
    if (da) {
      const int n = strom->readBytes(puffer, min(da, sizeof(puffer)));
      if (Update.write(puffer, n) != (size_t)n) {
        neuling.fehler = "Schreibfehler";
        Update.abort();
        http.end();
        neuling.laeuft = false;
        return false;
      }
      gelesen += n;
      neuling.prozent = (gelesen * 100) / groesse;
      if (millis() - letztesZeichnen > 200) {
        letztesZeichnen = millis();
        updateBalkenZeichnen();
      }
    }
    delay(1);
  }

  http.end();

  if (!Update.end(true)) {
    neuling.fehler = "Pruefung fehlgeschlagen";
    neuling.laeuft = false;
    return false;
  }

  // Die neue Fassung gilt als auf Probe: sie muss sich beim ersten Start
  // bewaehren, sonst faellt das Geraet von selbst zurueck.
  merker.begin("jana", false);
  merker.putString("probe", neuling.version);
  merker.end();
  return true;
}

void terminUebernehmen(Termin &z, JsonObject t) {
  z.titel = entumlauten(t["titel"].as<String>());
  z.zeit = entumlauten(t["zeit"].as<String>());
  z.ende = entumlauten(t["ende"].as<String>());
  z.ort = entumlauten(t["ort"].as<String>());
  z.beginnMin = alsMinuten(z.zeit);
  z.endeMin = alsMinuten(z.ende);
}

/**
 * Das Briefing holen und auseinandernehmen.
 *
 * **Wirft nie und blockiert die Anzeige nicht.** Faellt der Server aus, bleibt
 * das letzte Briefing stehen und bekommt einen Hinweis, seit wann es alt ist.
 * Ein leeres Display waere die schlechtere Antwort: die Termine von vor zehn
 * Minuten stimmen mit hoher Wahrscheinlichkeit immer noch.
 */
bool briefingHolen() {
  // Filter statt alles einlesen: das meiste der Antwort braucht dieses Geraet
  // nicht. Auf einem ESP32 mit knappem Heap ist "nur die Felder, die
  // gezeichnet werden" kein Geiz, sondern der Grund, warum es nach Stunden
  // noch laeuft.
  JsonDocument filter;
  filter["datum"] = true;
  filter["offen"] = true;
  for (const char *feld : {"heute", "morgen"}) {
    filter[feld][0]["titel"] = true;
    filter[feld][0]["zeit"] = true;
    filter[feld][0]["ende"] = true;
    filter[feld][0]["ort"] = true;
  }
  filter["faellig"][0]["titel"] = true;

  JsonDocument doc;
  if (!holen("/api/briefing", doc, filter))
    return false;

  Briefing frisch;
  frisch.datum = doc["datum"].as<String>();
  frisch.offen = doc["offen"] | 0;

  JsonArray heute = doc["heute"].as<JsonArray>();
  frisch.heuteGesamt = heute.size();
  for (JsonObject t : heute) {
    if (frisch.heuteAnzahl >= MAX_ZEILEN)
      break;
    terminUebernehmen(frisch.heute[frisch.heuteAnzahl++], t);
  }

  JsonArray morgen = doc["morgen"].as<JsonArray>();
  frisch.morgenGesamt = morgen.size();
  for (JsonObject t : morgen) {
    if (frisch.morgenAnzahl >= MAX_ZEILEN)
      break;
    terminUebernehmen(frisch.morgen[frisch.morgenAnzahl++], t);
  }

  for (JsonObject f : doc["faellig"].as<JsonArray>()) {
    if (frisch.faelligAnzahl >= MAX_ZEILEN)
      break;
    frisch.faellig[frisch.faelligAnzahl++] = entumlauten(f["titel"].as<String>());
  }

  // Erst ganz am Ende uebernehmen. Ein Abbruch mittendrin wuerde sonst eine
  // halb gefuellte Anzeige hinterlassen, in der die Termine von heute schon
  // weg und die neuen noch nicht da sind.
  briefing = frisch;
  habenDaten = true;
  letzterErfolg = millis();
  letzterFehler = "";
  return true;
}

/**
 * Die Lage im Homelab holen.
 *
 * Scheitert eigenstaendig: ein Ausfall hier laesst die Termine stehen. Die
 * beiden Abrufe haengen nicht voneinander ab, und ein halbes Display ist
 * besser als ein leeres.
 */
bool homelabHolen() {
  JsonDocument filter;
  filter["lage"]["gesamt"] = true;
  filter["lage"]["oben"] = true;
  filter["lage"]["uptime"] = true;
  filter["stoerungen"][0]["name"] = true;
  filter["stoerungen"][0]["meldung"] = true;
  filter["kennzahlen"][0]["label"] = true;
  filter["kennzahlen"][0]["wert"] = true;

  JsonDocument doc;
  if (!holen("/api/homelab", doc, filter)) {
    homelab.gueltig = false;
    return false;
  }

  Homelab frisch;
  JsonObject lage = doc["lage"].as<JsonObject>();
  frisch.gesamt = lage["gesamt"] | 0;
  frisch.oben = lage["oben"] | 0;
  frisch.uptime = lage["uptime"] | 0.0f;

  for (JsonObject s : doc["stoerungen"].as<JsonArray>()) {
    if (frisch.stoerAnzahl >= MAX_ZEILEN)
      break;
    frisch.stoerung[frisch.stoerAnzahl] = entumlauten(s["name"].as<String>());
    frisch.meldung[frisch.stoerAnzahl] = entumlauten(s["meldung"].as<String>());
    frisch.stoerAnzahl++;
  }

  // Nur die vier Kennzahlen, die auf ein Blatt passen und etwas aussagen.
  // "Speicher: main" ist ein Name und keine Zahl, sowas faellt raus.
  const char *gewollt[] = {"Arbeitsspeicher", "Prozessor", "Antwortzeit", "Vollster Speicher"};
  for (JsonObject k : doc["kennzahlen"].as<JsonArray>()) {
    if (frisch.zahlAnzahl >= 4)
      break;
    const String label = k["label"].as<String>();
    for (const char *g : gewollt) {
      if (label == g) {
        frisch.zahlLabel[frisch.zahlAnzahl] = entumlauten(label);
        frisch.zahlWert[frisch.zahlAnzahl] = entumlauten(k["wert"].as<String>());
        frisch.zahlAnzahl++;
        break;
      }
    }
  }

  frisch.gueltig = true;
  homelab = frisch;
  return true;
}

bool stoerungAktiv() {
  return homelab.gueltig && homelab.oben < homelab.gesamt;
}

/** Der Termin, der gerade laeuft. ``nullptr``, wenn keiner laeuft. */
const Termin *laeuftGerade() {
  const int jetzt = jetztMinuten();
  if (jetzt < 0)
    return nullptr;
  for (int i = 0; i < briefing.heuteAnzahl; i++) {
    const Termin &t = briefing.heute[i];
    if (t.beginnMin >= 0 && t.endeMin > t.beginnMin &&
        jetzt >= t.beginnMin && jetzt < t.endeMin)
      return &t;
  }
  return nullptr;
}

/** Der naechste Termin, der noch kommt. ``nullptr``, wenn heute nichts mehr ist. */
const Termin *kommtAlsNaechstes() {
  const int jetzt = jetztMinuten();
  if (jetzt < 0)
    return nullptr;
  const Termin *beste = nullptr;
  for (int i = 0; i < briefing.heuteAnzahl; i++) {
    const Termin &t = briefing.heute[i];
    if (t.beginnMin > jetzt && (!beste || t.beginnMin < beste->beginnMin))
      beste = &t;
  }
  return beste;
}

/** Ein Balken, der einen Anteil zeigt. Grundlage fuer Fortschritt und Tag. */
void balken(int x, int y, int breite, int hoehe, float anteil, uint16_t farbe) {
  if (anteil < 0)
    anteil = 0;
  if (anteil > 1)
    anteil = 1;
  tft.fillRoundRect(x, y, breite, hoehe, hoehe / 2, C_LINIE);
  const int voll = (int)(breite * anteil);
  if (voll > hoehe)
    tft.fillRoundRect(x, y, voll, hoehe, hoehe / 2, farbe);
}

/**
 * Die Startseite: links die Uhr, rechts was ansteht.
 *
 * Das Querformat macht hier den Unterschied. Hochkant standen Uhr, laufender
 * Termin und naechster Termin untereinander und drueckten sich gegenseitig
 * aus dem Bild. Nebeneinander hat die Uhr Platz, gross genug, um sie vom
 * anderen Ende des Zimmers zu lesen, und die Termine haben eine eigene
 * Spalte.
 */
void jetztZeichnen() {
  struct tm jetzt;
  const bool zeitDa = getLocalTime(&jetzt, 50);
  const int jetztMin = jetztMinuten();

  // Font 7 braucht fuer "09:47" rund 142 px. Bei 132 war die letzte Ziffer
  // abgeschnitten, auf dem Display stand "09:4".
  const int spalte = 152;
  // Die Trennlinie laeuft oben und unten aus, statt hart abzubrechen.
  {
    const int oben = KOPF_H + 8;
    const int hoehe = HOCH - KOPF_H - 30;
    for (int i = 0; i < hoehe; i++) {
      const float rand = min(i, hoehe - i) / 12.0f;
      if (rand >= 1.0f)
        tft.drawPixel(spalte - 10, oben + i, C_LINIE);
      else if (rand > 0.45f)
        tft.drawPixel(spalte - 10, oben + i, C_ERHOBEN);
    }
  }

  // --- Linke Spalte: die Uhr -------------------------------------------
  char uhr[6] = "--:--";
  if (zeitDa)
    strftime(uhr, sizeof(uhr), "%H:%M", &jetzt);

  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(C_TEXT, C_GRUND);
  // Breite zur Laufzeit messen statt zu hoffen: passt die 7-Segment-Schrift
  // nicht, wird die kleinere genommen, bevor etwas abgeschnitten wird.
  const int platz = spalte - 16;
  const uint8_t uhrFont = tft.textWidth(uhr, 7) <= platz ? 7 : 6;
  tft.drawString(uhr, 8, 52, uhrFont);

  if (zeitDa) {
    // Die Wochentage kommen aus der Systemsprache und waeren englisch. Ein
    // Geraet, das auf Deutsch beschriftet ist und "Thursday" sagt, wirkt
    // unfertig, deshalb eine eigene Tabelle.
    static const char *tage[] = {"Sonntag", "Montag", "Dienstag", "Mittwoch",
                                 "Donnerstag", "Freitag", "Samstag"};
    char tag[28];
    snprintf(tag, sizeof(tag), "%s", tage[jetzt.tm_wday]);
    tft.setTextColor(C_GEDAEMPFT, C_GRUND);
    tft.drawString(tag, 12, 106, 2);
    snprintf(tag, sizeof(tag), "%d. %d.", jetzt.tm_mday, jetzt.tm_mon + 1);
    tft.drawString(tag, 12, 122, 2);
  }

  // Der Tag als Strich, von sieben bis zweiundzwanzig Uhr, also der Zeit, in
  // der etwas passiert. Eine Linie ueber volle vierundzwanzig Stunden steht
  // morgens fast ganz links und abends fast ganz rechts, und dazwischen sagt
  // sie nichts.
  if (jetztMin >= 0) {
    const float anteil = (jetztMin - 7 * 60) / (float)((22 - 7) * 60);
    // Der verstrichene Teil bekommt Farbe, statt nur ein Punkt zu wandern.
    // Man sieht dadurch auf einen Blick, wie viel Tag noch uebrig ist.
    const int bBreite = spalte - 32;
    tft.fillRoundRect(12, 152, bBreite, 5, 2, C_LINIE);
    if (anteil > 0)
      tft.fillRoundRect(12, 152, (int)(bBreite * min(anteil, 1.0f)), 5, 2,
                        C_AKZENT);
    if (anteil >= 0 && anteil <= 1) {
      const int px = 12 + (int)(bBreite * anteil);
      tft.fillCircle(px, 154, 5, C_GRUND);
      tft.fillCircle(px, 154, 3, C_TEXT);
    }
  }

  // Faelliges nur als Zahl. Die Titel stehen auf der Heute-Seite; hier
  // zaehlt, ob ueberhaupt etwas offen ist, sonst wird die ruhigste Seite
  // zur vollsten.
  if (briefing.faelligAnzahl > 0) {
    tft.fillCircle(16, 181, 3, C_ACHTUNG);
    tft.setTextColor(C_ACHTUNG, C_GRUND);
    tft.drawString(String(briefing.faelligAnzahl) + " faellig", 24, 174, 2);
  }

  // --- Rechte Spalte: was laeuft, was kommt -----------------------------
  int y = KOPF_H + 10;
  const Termin *laeuft = laeuftGerade();
  const Termin *naechste = kommtAlsNaechstes();
  const int rechts_breite = BREIT - spalte - 10;

  if (laeuft) {
    const int dauer = laeuft->endeMin - laeuft->beginnMin;
    const int weg = jetztMin - laeuft->beginnMin;
    const int rest = laeuft->endeMin - jetztMin;

    tft.fillRoundRect(spalte, y, rechts_breite, 74, 8, C_ERHOBEN);
    tft.fillRoundRect(spalte, y, 3, 74, 1, C_AKZENT);

    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(C_GEDAEMPFT, C_ERHOBEN);
    tft.drawString("laeuft", spalte + 12, y + 8, 1);
    tft.setTextColor(C_TEXT, C_ERHOBEN);
    tft.drawString(kuerzen(laeuft->titel, 18), spalte + 12, y + 20, 4);

    balken(spalte + 12, y + 50, rechts_breite - 24, 6,
           dauer > 0 ? weg / (float)dauer : 0, C_AKZENT);
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(C_GEDAEMPFT, C_ERHOBEN);
    tft.drawString(laeuft->zeit + " - " + laeuft->ende, spalte + 12, y + 60, 1);
    tft.setTextDatum(TR_DATUM);
    tft.setTextColor(C_AKZENT, C_ERHOBEN);
    tft.drawString("noch " + alsDauer(rest), BREIT - 12, y + 58, 2);
    y += 82;
  }

  if (naechste) {
    const int bis = naechste->beginnMin - jetztMin;
    tft.fillRoundRect(spalte, y, rechts_breite, 62, 8, C_FLAECHE);
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(C_GEDAEMPFT, C_FLAECHE);
    tft.drawString("danach", spalte + 12, y + 8, 1);
    tft.setTextColor(C_TEXT, C_FLAECHE);
    tft.drawString(kuerzen(naechste->titel, 18), spalte + 12, y + 20, 4);

    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(C_GEDAEMPFT, C_FLAECHE);
    tft.drawString(naechste->zeit, spalte + 12, y + 46, 1);
    tft.setTextDatum(TR_DATUM);
    // Unter einer Viertelstunde wird die Zahl farbig: das ist der Moment,
    // in dem man losgehen muesste.
    tft.setTextColor(bis <= 15 ? C_ACHTUNG : C_GEDAEMPFT, C_FLAECHE);
    tft.drawString("in " + alsDauer(bis), BREIT - 12, y + 42, 2);
    y += 70;
  }

  if (!laeuft && !naechste) {
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(C_GEDAEMPFT, C_GRUND);
    tft.drawString(zeitDa ? "Nichts mehr heute" : "warte auf die Uhrzeit",
                   spalte, KOPF_H + 40, 4);
  }
}

/** Die Kopfzeile: welche Seite, und wie frisch die Daten sind. */
uint16_t seitenFarbe() {
  // Jede Seite hat ihren eigenen Ton. Man sieht am Rand, wo man ist,
  // ohne die Ueberschrift zu lesen.
  switch (ansicht) {
  case 0: return C_AKZENT;
  case 1: return C_AKZENT;
  case 2: return 0xB59F;  // gedaempftes Lila fuer Morgen
  default: return stoerungAktiv() ? C_ACHTUNG : C_GUT;
  }
}

void kopfZeichnen() {
  static const char *namen[] = {"Jetzt", "Heute", "Morgen", "Homelab"};
  const uint16_t ton = seitenFarbe();
  tft.fillRect(0, 0, BREIT, KOPF_H, C_FLAECHE);
  // Farbiger Balken statt grauer Linie: der Seitenton zieht sich durch.
  tft.fillRect(0, KOPF_H, BREIT, 2, ton);

  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(C_TEXT, C_FLAECHE);
  tft.drawString(namen[ansicht], 12, 7, 4);

  // Der Punkt rechts oben sagt ohne Worte, woran man ist: gruen frisch,
  // gelb aelter als zehn Minuten, rot gar keine Verbindung.
  uint16_t farbe = C_FEHLER;
  if (habenDaten)
    farbe = (millis() - letzterErfolg) < VERALTET_MS ? C_GUT : C_ACHTUNG;
  tft.fillCircle(BREIT - 14, 17, 4, farbe);

  // Die Punkte zeigen, auf welcher Seite man ist. Ohne sie weiss niemand,
  // dass es ueberhaupt mehr als eine gibt.
  for (int i = 0; i < ANSICHTEN; i++) {
    const int x = BREIT - 100 + i * 16;
    if (i == ansicht) {
      tft.fillRoundRect(x - 6, 14, 16, 6, 3, ton);
    } else {
      tft.fillCircle(x, 17, 3, C_LINIE);
    }
  }
}

/**
 * Die Terminseiten, heute und morgen, in zwei Spalten.
 *
 * Sieben Termine passen quer nicht untereinander. Zwei Spalten zu drei
 * Zeilen schon, und die Uhrzeiten stehen dabei immer noch untereinander,
 * so dass das Auge sie der Reihe nach findet.
 */
void termineZeichnen() {
  const bool istHeute = ansicht == 1;
  const Termin *liste = istHeute ? briefing.heute : briefing.morgen;
  const int anzahl = istHeute ? briefing.heuteAnzahl : briefing.morgenAnzahl;
  const int gesamt = istHeute ? briefing.heuteGesamt : briefing.morgenGesamt;
  const Termin *laeuft = istHeute ? laeuftGerade() : nullptr;

  if (anzahl == 0) {
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(C_GEDAEMPFT, C_GRUND);
    tft.drawString(istHeute ? "Nichts mehr heute" : "Morgen nichts", BREIT / 2, 120, 4);
    return;
  }

  const int spalten = anzahl > 3 ? 2 : 1;
  const int sp_breite = (BREIT - 16 - (spalten - 1) * 8) / spalten;
  const int zeilen = (anzahl + spalten - 1) / spalten;
  const int z_hoehe = min(46, (FUSS_Y - KOPF_H - 16) / max(zeilen, 1));

  for (int i = 0; i < anzahl; i++) {
    const Termin &t = liste[i];
    const bool aktiv = &t == laeuft;
    const int sp = i / zeilen;
    const int ze = i % zeilen;
    const int x = 8 + sp * (sp_breite + 8);
    const int y = KOPF_H + 8 + ze * z_hoehe;

    tft.fillRoundRect(x, y, sp_breite, z_hoehe - 4, 6, aktiv ? C_ERHOBEN : C_FLAECHE);
    // Ein Strich links markiert, was gerade laeuft. Farbe statt Text: auf
    // einer Liste findet das Auge den Balken schneller als ein Wort.
    if (aktiv)
      tft.fillRoundRect(x, y, 3, z_hoehe - 4, 1, C_AKZENT);

    const uint16_t grund = aktiv ? C_ERHOBEN : C_FLAECHE;
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(aktiv ? C_AKZENT : C_GEDAEMPFT, grund);
    tft.drawString(t.zeit.length() ? t.zeit : "ganztags", x + 10, y + 5, 2);

    tft.setTextColor(C_TEXT, grund);
    tft.drawString(kuerzen(t.titel, sp_breite > 200 ? 30 : 16), x + 10, y + 22, 2);

    if (t.ende.length() && z_hoehe >= 44) {
      tft.setTextDatum(TR_DATUM);
      tft.setTextColor(C_GEDAEMPFT, grund);
      tft.drawString("bis " + t.ende, x + sp_breite - 10, y + 6, 1);
    }
  }

  if (gesamt > anzahl) {
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(C_GEDAEMPFT, C_GRUND);
    tft.drawString("+ " + String(gesamt - anzahl) + " weitere", 10, FUSS_Y - 14, 1);
  }
}

/**
 * Die Homelab-Seite: links die Zahl, rechts was kaputt ist.
 *
 * Die Reihenfolge ist Absicht. "46 von 47" beantwortet die Frage in einem
 * Blick, und nur wer stehen bleibt, liest daneben, welcher Dienst fehlt.
 */
void homelabZeichnen() {
  if (!homelab.gueltig) {
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(C_GEDAEMPFT, C_GRUND);
    tft.drawString("Homelab nicht erreichbar", BREIT / 2, 120, 2);
    return;
  }

  const bool stoerung = stoerungAktiv();
  const int spalte = 124;
  tft.drawFastVLine(spalte - 10, KOPF_H + 8, HOCH - KOPF_H - 30, C_LINIE);

  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(stoerung ? C_ACHTUNG : C_GUT, C_GRUND);
  tft.drawString(String(homelab.oben), 12, 46, 7);

  // "von 47" stand vorher rechts neben der Zahl und lief in die Trennlinie.
  // Unter die Zahl gesetzt hat es Platz, egal wie breit sie wird.
  tft.setTextColor(C_GEDAEMPFT, C_GRUND);
  tft.drawString("von " + String(homelab.gesamt) + " Diensten", 12, 96, 2);

  // Ein Raster aus Punkten, einer je Dienst. Das macht aus einer Zahl ein
  // Bild: 46 gruene Punkte und ein roter sagen dasselbe wie "46 von 47",
  // aber man sieht den roten sofort.
  int px = 14, py = 122;
  for (int i = 0; i < homelab.gesamt && py < 168; i++) {
    tft.fillCircle(px, py, 2, i < homelab.oben ? C_GUT : C_FEHLER);
    px += 9;
    if (px > spalte - 20) {
      px = 14;
      py += 10;
    }
  }

  tft.setTextColor(C_GEDAEMPFT, C_GRUND);
  tft.drawString(String(homelab.uptime, 1) + " % / 24 h", 12, 186, 2);

  int y = KOPF_H + 8;
  if (stoerung) {
    for (int i = 0; i < homelab.stoerAnzahl && y < FUSS_Y - 30; i++) {
      tft.fillRoundRect(spalte, y, BREIT - spalte - 10, 34, 6, C_ERHOBEN);
      tft.fillRoundRect(spalte, y, 3, 34, 1, C_FEHLER);
      tft.setTextDatum(TL_DATUM);
      tft.setTextColor(C_TEXT, C_ERHOBEN);
      tft.drawString(kuerzen(homelab.stoerung[i], 22), spalte + 10, y + 4, 2);
      tft.setTextColor(C_GEDAEMPFT, C_ERHOBEN);
      // 32 Zeichen liefen ueber den Kastenrand hinaus. Die Meldung ist ohnehin
      // nur ein Hinweis, der Dienstname darueber ist die Information.
      tft.drawString(kuerzen(homelab.meldung[i], 26), spalte + 10, y + 21, 1);
      y += 40;
    }
  } else {
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(C_GUT, C_GRUND);
    tft.drawString("Alles laeuft", spalte, y + 4, 4);
    y += 34;
  }

  // Die Kennzahlen in zwei Spalten. Label klein darueber, Wert darunter:
  // beim Ueberfliegen sucht das Auge die Zahl, nicht das Wort.
  const int k_breite = (BREIT - spalte - 10) / 2;
  for (int i = 0; i < homelab.zahlAnzahl && y < FUSS_Y - 24; i++) {
    const int x = spalte + (i % 2) * k_breite;
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(C_GEDAEMPFT, C_GRUND);
    tft.drawString(kuerzen(homelab.zahlLabel[i], 14), x, y, 1);
    tft.setTextColor(C_TEXT, C_GRUND);
    tft.drawString(homelab.zahlWert[i], x, y + 11, 2);
    if (i % 2)
      y += 32;
  }
}

/** Die ganze Anzeige. Wird nur bei Aenderung gezeichnet, nicht im Takt. */
/**
 * Der Fortschrittsbalken beim Flashen.
 *
 * Bewusst ein eigenes Vollbild statt eines Kastens ueber der Seite: waehrend
 * geschrieben wird, darf nichts anderes passieren, und das soll man sehen.
 */
void updateBalkenZeichnen() {
  static int letzterProzent = -1;
  if (neuling.prozent == letzterProzent)
    return;

  if (letzterProzent < 0) {
    tft.fillScreen(C_GRUND);
    tft.setTextDatum(TC_DATUM);
    tft.setTextColor(C_TEXT, C_GRUND);
    tft.drawString("Wird geladen", BREIT / 2, 72, 4);
    tft.setTextColor(C_GEDAEMPFT, C_GRUND);
    tft.drawString("Version " + neuling.version, BREIT / 2, 104, 2);
    tft.drawString("Strom nicht trennen", BREIT / 2, 190, 2);
  }
  letzterProzent = neuling.prozent;

  const int x = 40, y = 136, breit = BREIT - 80, hoch = 14;
  tft.drawRoundRect(x, y, breit, hoch, 4, C_LINIE);
  tft.fillRoundRect(x + 2, y + 2, ((breit - 4) * neuling.prozent) / 100, hoch - 4, 3,
                    C_AKZENT);

  tft.setTextDatum(TC_DATUM);
  tft.setTextColor(C_TEXT, C_GRUND);
  tft.drawString(String(neuling.prozent) + " %", BREIT / 2, y + 24, 2);
}

/**
 * Der Dialog: was laeuft, was kaeme, und drei Knoepfe.
 *
 * Liegt ueber der Seite statt sie zu ersetzen. Mia soll sehen, was das
 * Geraet gerade anzeigt, waehrend sie entscheidet.
 */
void updateDialogZeichnen() {
  const int x = 26, y = 40, breit = BREIT - 52, hoch = 164;

  // Schatten als Andeutung von Hoehe, damit der Kasten nicht wie ein
  // Teil der Seite aussieht.
  tft.fillRoundRect(x + 3, y + 3, breit, hoch, 10, C_GRUND);
  tft.fillRoundRect(x, y, breit, hoch, 10, C_ERHOBEN);
  tft.drawRoundRect(x, y, breit, hoch, 10, C_AKZENT);

  tft.setTextDatum(TC_DATUM);
  tft.setTextColor(C_TEXT, C_ERHOBEN);
  tft.drawString("Neue Version da", BREIT / 2, y + 14, 4);

  // Alt und neu untereinander, Werte ausgerichtet: so sieht man den
  // Unterschied, ohne zu lesen.
  tft.setTextDatum(TR_DATUM);
  tft.setTextColor(C_GEDAEMPFT, C_ERHOBEN);
  tft.drawString("jetzt", BREIT / 2 - 12, y + 54, 2);
  tft.drawString("neu", BREIT / 2 - 12, y + 78, 2);

  tft.setTextDatum(TL_DATUM);
  tft.drawString(FIRMWARE_VERSION, BREIT / 2 + 4, y + 54, 2);
  tft.setTextColor(C_AKZENT, C_ERHOBEN);
  tft.drawString(neuling.version, BREIT / 2 + 4, y + 78, 2);

  // Drei Knoepfe nebeneinander. Ja hat Farbe, die anderen nicht: die
  // haeufigste Antwort soll am leichtesten zu treffen sein.
  const int ky = y + 112, kh = 36, abstand = 8;
  const int kb = (breit - 2 * 14 - 2 * abstand) / 3;
  const int k1 = x + 14, k2 = k1 + kb + abstand, k3 = k2 + kb + abstand;

  tft.fillRoundRect(k1, ky, kb, kh, 6, C_AKZENT);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(C_TEXT, C_AKZENT);
  tft.drawString("Ja", k1 + kb / 2, ky + kh / 2, 2);

  for (int i = 0; i < 2; i++) {
    const int kx = i == 0 ? k2 : k3;
    tft.fillRoundRect(kx, ky, kb, kh, 6, C_FLAECHE);
    tft.drawRoundRect(kx, ky, kb, kh, 6, C_LINIE);
    tft.setTextColor(C_GEDAEMPFT, C_FLAECHE);
    tft.drawString(i == 0 ? "Spaeter" : "Nein", kx + kb / 2, ky + kh / 2, 2);
  }
}

void anzeigeZeichnen() {
  tft.fillScreen(C_GRUND);
  kopfZeichnen();

  if (!habenDaten && ansicht != 3) {
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(C_GEDAEMPFT, C_GRUND);
    tft.drawString("Keine Verbindung", BREIT / 2, 100, 4);
    tft.drawString(kuerzen(letzterFehler, 34), BREIT / 2, 128, 2);
    tft.drawString(kuerzen(String(basisUrl), 44), BREIT / 2, 150, 1);
    return;
  }

  switch (ansicht) {
  case 0: jetztZeichnen(); break;
  case 3: homelabZeichnen(); break;
  default: termineZeichnen(); break;
  }

  // Fusszeile: die eine Zahl, die zaehlt.
  tft.fillRect(0, FUSS_Y, BREIT, HOCH - FUSS_Y, C_FLAECHE);
  tft.drawFastHLine(0, FUSS_Y, BREIT, C_LINIE);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(C_GEDAEMPFT, C_FLAECHE);
  tft.drawString("offen", 12, FUSS_Y + 6, 1);
  tft.setTextColor(briefing.offen > 0 ? C_AKZENT : C_GEDAEMPFT, C_FLAECHE);
  tft.drawString(String(briefing.offen), 46, FUSS_Y + 4, 2);

  tft.setTextDatum(TC_DATUM);
  tft.setTextColor(C_GEDAEMPFT, C_FLAECHE);
  tft.drawString(briefing.datum, BREIT / 2, FUSS_Y + 6, 1);

  // Der Hinweis nennt beide Wege, seit Tippen dazugekommen ist.
  tft.setTextDatum(TR_DATUM);
  tft.setTextColor(C_GEDAEMPFT, C_FLAECHE);
  tft.drawString("< tippen >", BREIT - 12, FUSS_Y + 6, 1);
}

/**
 * Nur die Uhr neu zeichnen, nicht die ganze Seite.
 *
 * Ein ``fillScreen`` jede Minute waere ein sichtbares Zucken. Hier wird
 * genau das Rechteck der Uhr ueberschrieben, den Rest sieht man nicht
 * flackern.
 */
void uhrAuffrischen() {
  if (ansicht != 0 || !habenDaten)
    return;
  struct tm jetzt;
  if (!getLocalTime(&jetzt, 20))
    return;
  char uhr[6];
  strftime(uhr, sizeof(uhr), "%H:%M", &jetzt);
  // Muss denselben Bereich raeumen, den jetztZeichnen() beschreibt, sonst
  // bleiben Reste der alten Ziffern stehen.
  tft.fillRect(6, 50, 140, 50, C_GRUND);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(C_TEXT, C_GRUND);
  const uint8_t uhrFont = tft.textWidth(uhr, 7) <= 136 ? 7 : 6;
  tft.drawString(uhr, 8, 52, uhrFont);
}

/**
 * Helligkeit nach Tageszeit, mit Aufwachen bei Beruehrung.
 *
 * Ohne gestellte Uhr bleibt es hell: lieber ein zu helles Display als eines,
 * das mitten am Tag auf Nachtstufe steht, weil die Zeit noch nicht kam.
 */
void helligkeitPruefen() {
  struct tm jetzt;
  uint8_t soll = HELL_TAG;
  if (getLocalTime(&jetzt, 50)) {
    const bool nacht = jetzt.tm_hour >= NACHT_AB || jetzt.tm_hour < NACHT_BIS;
    const bool angefasst = millis() - letzteBeruehrung < WACH_MS;
    soll = (nacht && !angefasst) ? HELL_NACHT : HELL_TAG;
  }
  if (soll != helligkeit) {
    helligkeit = soll;
    ledcWrite(BL_KANAL, helligkeit);
  }
}

/**
 * Die LED als stille Alarmleuchte.
 *
 * **Dunkel heisst in Ordnung.** Eine LED, die dauernd leuchtet, wird nach
 * zwei Tagen nicht mehr wahrgenommen, und dann taugt sie auch als Warnung
 * nichts mehr. Sie geht deshalb nur an, wenn etwas nicht stimmt: rot bei
 * fehlender Verbindung, gelb wenn ein Dienst im Homelab unten ist.
 */
void ledPruefen() {
  if (!habenDaten)
    led((millis() / 700) % 2 == 0, false, false);
  else if (stoerungAktiv())
    led(true, true, false);
  else
    led(false, false, false);
}

/**
 * Wischen erkennen.
 *
 * Gibt -1 und +1 fuer die beiden Richtungen zurueck, 0 fuer nichts. Der
 * Touch ist resistiv und meldet einen Druck vielfach; ausgewertet wird
 * deshalb erst beim Loslassen, aus Anfangs- und Endpunkt.
 *
 * **Gewischt wird in X**, seit das Geraet quer steht. Im Hochformat war es
 * Y, und wer nur ``setRotation`` aendert, wischt danach ins Leere.
 */
// Auf welche Seite oben getippt wurde, -1 fuer keine.
int tippZiel = -1;
// Was im Update-Dialog getippt wurde: 1 Ja, 2 Spaeter, 3 Nein, 0 nichts.
int antwort = 0;

int wischen() {
  static bool lag_an = false;
  static int startX = 0, startY = 0;
  static int letzteX = 0, letzteY = 0;
  static uint32_t startZeit = 0;
  static int punkte = 0;

  tippZiel = -1;
  antwort = 0;
  const bool an = touch.tirqTouched() && touch.touched();

  if (an) {
    const TS_Point roh = touch.getPoint();
    // Zu schwacher Druck ist Rauschen, kein Finger.
    if (roh.z < 300)
      return 0;
    // Erst umrechnen, dann vergleichen. Vorher wurde gegen Pixel geprueft,
    // waehrend hier Werte bis 4095 ankamen: jede Beruehrung fiel durch.
    const TS_Point p(rohNachX(roh.x), rohNachY(roh.y), roh.z);
    if (!lag_an) {
      lag_an = true;
      startX = p.x;
      startY = p.y;
      startZeit = millis();
      punkte = 0;
    }
    // Der entscheidende Teil: die letzte gueltige Lage mitschreiben,
    // solange der Finger noch aufliegt.
    letzteX = p.x;
    letzteY = p.y;
    punkte++;
    letzteBeruehrung = millis();
    return 0;
  }

  if (!lag_an)
    return 0;
  lag_an = false;

  // Damit sich die Kalibrierung pruefen laesst, statt sie zu raten.
  Serial.printf("[touch] von (%d,%d) nach (%d,%d), %d Punkte, %lu ms\n",
                startX, startY, letzteX, letzteY, punkte,
                (unsigned long)(millis() - startZeit));

  // Ein einzelner Messpunkt ist Rauschen, kein Finger.
  if (punkte < 2)
    return 0;
  if (millis() - startZeit > 1500)
    return 0;

  const int wegX = letzteX - startX;
  const int wegY = letzteY - startY;

  // Kurz und fast ohne Weg heisst: getippt.
  if (abs(wegX) < TIPP_WEG && abs(wegY) < TIPP_WEG) {
    // Steht der Update-Dialog, gehoert jeder Tipp ihm. Ein Seitenwechsel
    // unter einem offenen Dialog waere verwirrend.
    if (neuling.gefragt) {
      const int x = 26, y = 40, breit = BREIT - 52;
      const int ky = y + 112, kh = 36, abstand = 8;
      const int kb = (breit - 2 * 14 - 2 * abstand) / 3;
      const int k1 = x + 14, k2 = k1 + kb + abstand, k3 = k2 + kb + abstand;

      if (startY >= ky && startY <= ky + kh) {
        if (startX >= k1 && startX <= k1 + kb) {
          antwort = 1;  // Ja
        } else if (startX >= k2 && startX <= k2 + kb) {
          antwort = 2;  // Spaeter
        } else if (startX >= k3 && startX <= k3 + kb) {
          antwort = 3;  // Nein
        }
      }
      return 0;
    }

    // Oben auf einen der Punkte: direkt auf diese Seite springen.
    if (startY < KOPF_H + 8) {
      for (int i = 0; i < ANSICHTEN; i++) {
        const int x = BREIT - 100 + i * 16;
        if (abs(startX - x) < 10) {
          tippZiel = i;
          return 0;
        }
      }
      return 0;
    }
    // Linkes Viertel zurueck, rechtes Viertel vor. Auf einem resistiven
    // Panel trifft ein Tippen zuverlaessiger als ein Wisch.
    if (startX < BREIT / 4)
      return -1;
    if (startX > BREIT * 3 / 4)
      return 1;
    return 0;
  }

  // Ein Wisch ist waagerecht. Diagonal gezogen meint meistens nichts.
  if (abs(wegX) < WISCH_WEG || abs(wegY) > abs(wegX))
    return 0;
  return wegX > 0 ? 1 : -1;
}

/**
 * Seitenwechsel sichtbar machen.
 *
 * Ein hartes ``fillScreen`` sieht aus wie ein Absturz. Hier laeuft ein
 * Vorhang in Wischrichtung ueber das Bild. Ein Vollbild-Sprite waere mit
 * 150 KB zu gross fuer den ESP32, deshalb Streifen statt Puffer.
 */
void uebergang(int richtung) {
  const int schritt = 20;
  if (richtung >= 0) {
    for (int x = 0; x < BREIT; x += schritt) {
      tft.fillRect(x, 0, schritt, HOCH, C_FLAECHE);
      tft.fillRect(x - schritt, 0, schritt, HOCH, C_GRUND);
      delay(4);
    }
    tft.fillRect(BREIT - schritt, 0, schritt, HOCH, C_GRUND);
  } else {
    for (int x = BREIT - schritt; x > -schritt * 2; x -= schritt) {
      tft.fillRect(x, 0, schritt, HOCH, C_FLAECHE);
      tft.fillRect(x + schritt, 0, schritt, HOCH, C_GRUND);
      delay(4);
    }
    tft.fillRect(0, 0, schritt, HOCH, C_GRUND);
  }
}

void portalHinweis(WiFiManager *wm) {
  tft.fillScreen(C_GRUND);
  tft.setTextDatum(TC_DATUM);
  tft.setTextColor(C_TEXT, C_GRUND);
  tft.drawString("Einrichten", BREIT / 2, 40, 4);
  tft.setTextColor(C_GEDAEMPFT, C_GRUND);
  tft.drawString("WLAN verbinden mit", BREIT / 2, 84, 2);
  tft.setTextColor(C_AKZENT, C_GRUND);
  tft.drawString(wm->getConfigPortalSSID(), BREIT / 2, 106, 4);
  tft.setTextColor(C_GEDAEMPFT, C_GRUND);
  tft.drawString("dann Adresse von Mia OS eintragen", BREIT / 2, 152, 2);
  led(false, false, true);
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\n[jana-display] Start");

  pinMode(LED_R, OUTPUT);
  pinMode(LED_G, OUTPUT);
  pinMode(LED_B, OUTPUT);
  led(false, false, true);

  tft.init();
  // Quer, um 180 Grad gegen die andere Moeglichkeit. Ob 1 oder 3 richtig
  // ist, haengt daran, auf welcher Seite die USB-Buchse herauskommen soll,
  // und das entscheidet erst der Standfuss. Der Touch bekommt unten
  // dieselbe Zahl: er ist ein eigener Baustein an einem eigenen Bus und
  // dreht sich nicht mit dem Display mit.
  tft.setRotation(3);

  // Die Beleuchtung uebernimmt der PWM-Kanal, nachdem TFT_eSPI den Pin beim
  // init auf HIGH gesetzt hat. Andersherum ueberschreibt die Bibliothek die
  // Einstellung wieder und das Dimmen bleibt wirkungslos.
  ledcSetup(BL_KANAL, 5000, 8);
  ledcAttachPin(TFT_BL, BL_KANAL);
  ledcWrite(BL_KANAL, HELL_TAG);

  tft.fillScreen(C_GRUND);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(C_GEDAEMPFT, C_GRUND);
  tft.drawString("Mia OS", BREIT / 2, HOCH / 2, 4);

  touchSPI.begin(TOUCH_CLK, TOUCH_MISO, TOUCH_MOSI, TOUCH_CS);
  touch.begin(touchSPI);
  // Der Touch dreht sich nicht mit dem Display: er ist ein eigener Baustein
  // an einem eigenen Bus und braucht dieselbe Drehung noch einmal.
  touch.setRotation(3);

  WiFiManager wm;
  WiFiManagerParameter feldUrl("url", "Mia OS Adresse", basisUrl, sizeof(basisUrl) - 1);
  wm.addParameter(&feldUrl);
  wm.setAPCallback(portalHinweis);
  wm.setConfigPortalTimeout(180);
  // Hartnaeckig statt schnell aufgeben. Ohne diese beiden Zeilen wartet
  // WiFiManager nur den Standardzeitraum und faellt danach sofort ins
  // Einrichtungsportal: gemessen am 10.09. gab es nach einem harten Reset
  // schon nach 9,2 s auf, obwohl die Zugangsdaten gespeichert waren und der
  // Access Point sendete. Am Display sah das aus, als muesste man das WLAN
  // jedes Mal neu einrichten.
  wm.setConnectTimeout(20);
  wm.setConnectRetries(3);
  wm.setSaveParamsCallback([&feldUrl]() {
    strncpy(basisUrl, feldUrl.getValue(), sizeof(basisUrl) - 1);
    basisUrl[sizeof(basisUrl) - 1] = '\0';
  });

  if (!wm.autoConnect("Jana-Display")) {
    Serial.println("[jana-display] WLAN fehlgeschlagen, laufe ohne");
    letzterFehler = "kein WLAN";
  } else {
    Serial.print("[jana-display] WLAN ok, IP ");
    Serial.println(WiFi.localIP());
    strncpy(basisUrl, feldUrl.getValue(), sizeof(basisUrl) - 1);
    basisUrl[sizeof(basisUrl) - 1] = '\0';
    // Sommerzeit steht in der Regel selbst drin, deshalb die volle
    // Zonenangabe statt fester Stundenverschiebung: sonst dimmt das Geraet
    // ab Ende Oktober eine Stunde zu spaet.
    configTzTime("CET-1CEST,M3.5.0,M10.5.0/3", "pool.ntp.org", "time.nist.gov");
  }
  WiFi.setAutoReconnect(true);
  Serial.printf("[jana-display] Mia OS: %s\n", basisUrl);

  briefingHolen();
  homelabHolen();

  // Die neue Fassung hat sich bewaehrt: WLAN steht und Mia OS hat
  // geantwortet. Ohne diese Bestaetigung faellt das Geraet beim naechsten
  // Neustart von selbst auf die vorherige zurueck. Genau das soll es auch,
  // wenn eine Fassung hier nicht ankommt.
  if (habenDaten) {
    const esp_partition_t *laeuft = esp_ota_get_running_partition();
    esp_ota_img_states_t zustand;
    if (esp_ota_get_state_partition(laeuft, &zustand) == ESP_OK &&
        zustand == ESP_OTA_IMG_PENDING_VERIFY) {
      esp_ota_mark_app_valid_cancel_rollback();
      Serial.printf("[jana-display] Version %s bestaetigt\n", FIRMWARE_VERSION);
      merker.begin("jana", false);
      merker.remove("probe");
      merker.end();
    }
  }

  // Was Mia zuletzt abgelehnt hat, gilt weiter.
  merker.begin("jana", true);
  neuling.abgelehnt = merker.getString("abgelehnt", "");
  merker.end();

  updatePruefen();
  neuZeichnen = true;
}

void loop() {
  const int wisch = wischen();

  // Der Dialog hat Vorrang: solange er steht, wird nicht geblaettert.
  if (antwort != 0) {
    if (antwort == 1) {
      if (updateHolen()) {
        tft.fillScreen(C_GRUND);
        tft.setTextDatum(MC_DATUM);
        tft.setTextColor(C_TEXT, C_GRUND);
        tft.drawString("Neustart", BREIT / 2, 120, 4);
        delay(800);
        ESP.restart();
      }
      // Fehlgeschlagen: kurz zeigen, warum, dann weitermachen wie bisher.
      tft.fillScreen(C_GRUND);
      tft.setTextDatum(MC_DATUM);
      tft.setTextColor(C_FEHLER, C_GRUND);
      tft.drawString("Update fehlgeschlagen", BREIT / 2, 108, 2);
      tft.setTextColor(C_GEDAEMPFT, C_GRUND);
      tft.drawString(neuling.fehler, BREIT / 2, 134, 2);
      delay(3000);
      neuling.spaeterBis = millis() + SPAETER_MS;
    } else if (antwort == 2) {
      neuling.spaeterBis = millis() + SPAETER_MS;
    } else if (antwort == 3) {
      // Diese Nummer nie wieder anbieten. Ueberlebt den Neustart, sonst
      // steht der Dialog nach jedem Stromausfall wieder da.
      neuling.abgelehnt = neuling.version;
      merker.begin("jana", false);
      merker.putString("abgelehnt", neuling.abgelehnt);
      merker.end();
      neuling.version = "";
    }
    neuling.gefragt = false;
    neuling.laeuft = false;
    antwort = 0;
    neuZeichnen = true;
    return;
  }

  if (wisch != 0) {
    ansicht = (ansicht + wisch + ANSICHTEN) % ANSICHTEN;
    uebergang(wisch);
    neuZeichnen = true;
  } else if (tippZiel >= 0 && tippZiel != ansicht) {
    uebergang(tippZiel > ansicht ? 1 : -1);
    ansicht = tippZiel;
    neuZeichnen = true;
  }

  if (millis() - letzterVersuch > HOLINTERVALL_MS || letzterVersuch == 0) {
    letzterVersuch = millis();
    const bool stoerungVorher = stoerungAktiv();
    briefingHolen();
    homelabHolen();
    // Im selben Takt mitgefragt: Mia OS erfaehrt dabei, dass es dieses
    // Geraet gibt und welche Fassung laeuft.
    updatePruefen();
    // Auch bei Misserfolg neu zeichnen: der Punkt oben rechts und das
    // "vor X min" sind dann die eigentliche Information.
    neuZeichnen = true;
    if (stoerungAktiv() != stoerungVorher)
      Serial.printf("[jana-display] Homelab: %d von %d\n", homelab.oben, homelab.gesamt);
  }

  // Die Startseite lebt: die Uhr laeuft weiter, und die Restzeit stimmt nur,
  // wenn sie neu gerechnet wird. Einmal je Minute reicht, denn genauer als
  // eine Minute ist keine der Zahlen darauf.
  static int letzteMinute = -1;
  const int jetzt = jetztMinuten();
  if (jetzt != letzteMinute) {
    letzteMinute = jetzt;
    if (ansicht == 0)
      neuZeichnen = true;
  }

  static uint32_t letztePruefung = 0;
  if (millis() - letztePruefung > 5000) {
    letztePruefung = millis();
    helligkeitPruefen();
    // Von selbst zurueckkommen, wenn das WLAN wieder da ist. Der Aufruf
    // blockiert nicht, das Ergebnis zeigt sich beim naechsten Abruf.
    if (WiFi.status() != WL_CONNECTED)
      WiFi.reconnect();
  }
  ledPruefen();

  if (neuZeichnen) {
    neuZeichnen = false;
    anzeigeZeichnen();
    if (neuling.gefragt)
      updateDialogZeichnen();
  }

  // Fragen, wenn etwas bereitsteht und gerade nichts dagegen spricht.
  const bool ruhe = !stoerungAktiv() && laeuftGerade() == nullptr;
  if (!neuling.gefragt && !neuling.version.isEmpty() && ruhe &&
      millis() > neuling.spaeterBis) {
    neuling.gefragt = true;
    updateDialogZeichnen();
  }

  delay(20);
}
