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
 * **Fast nur lesen.** Bis 0.1.9 schickte das Geraet nichts zurueck. Seit
 * 0.1.10 kann es genau drei Dinge: eine faellige Aufgabe abhaken (zwei
 * Sekunden halten), einen Hinweis von Mia OS beantworten (ok oder spaeter)
 * und eine begrabene Aufgabe zurueckholen. Alles davon ist umkehrbar, und
 * nichts davon loescht. Ein Display, das offen herumsteht, darf nicht mehr
 * koennen als das.
 *
 * **Der Tunnel-Watchdog.** Antwortet Mia OS nicht, pingt das Geraet den
 * Rand des Heimnetzes. Antwortet der, ist der Server das Problem; wenn
 * nicht, der Tunnel oder die Firmenleitung. Drei verschiedene Saetze auf
 * dem Display, weil nur einer davon etwas ist, das Mia beheben kann.
 *
 * **Und dann gibt es Dinge, die nicht dokumentiert sind.** Regel 62.
 *
 * **Eigene Schrift.** Seit 0.1.9 zeichnet das Geraet mit Inter statt mit den
 * eingebauten Schriften von TFT_eSPI. Der Grund ist banal: die eingebauten
 * kennen keine Umlaute, und ein Geraet, das "Fruehstueck" schreibt, sieht
 * nach Bastelkeller aus. Die Glyphen liegen in schriften.h.
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

#include "schriften.h"
#include "sprites.h"
#include <ESP32Ping.h>

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

// Farben der Kalender. Jeder Termin traegt sein Feld ``kalender`` mit, und
// ein Streifen am Kasten sagt, ob das Arbeit oder Freizeit ist, bevor man
// den Titel liest.
#define C_KAL_ARBEIT C_AKZENT
#define C_KAL_PRIVAT 0x3D9F  // #3a9fff, ein kuehles Blau
#define C_KAL_WOHNEN 0xB59F  // gedaempftes Lila, wie die Morgen-Seite
#define C_KAL_SONST C_GEDAEMPFT

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

// Vorgabe, falls noch nichts eingerichtet wurde. Wird beim ersten Start im
// Einrichtungsportal gesetzt und liegt danach im NVS.
//
// Achtung, teuer gelernt am 11.09.2026: WiFiManager speichert eigene Felder
// NICHT von selbst. Ohne das Sichern unten stand hier bei jedem Start wieder
// die einkompilierte Vorgabe, und als die von der echten Adresse auf einen
// Beispielnamen geaendert wurde, war das Geraet stumm: im WLAN, aber ohne
// Server. Deshalb wird der Wert jetzt ausdruecklich abgelegt und gelesen.
char basisUrl[80] = "http://192.168.1.10:8080";

struct Termin {
  String titel;
  String zeit;
  String ende;
  String ort;
  // Farbe des Kalenders, aus dem der Termin stammt.
  uint16_t farbe = C_KAL_SONST;
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
  // Ob die Aufgabe schon vor heute faellig war. Die steht dann in Achtung-
  // Farbe, weil sie sonst in der Liste untergeht.
  bool ueberfaellig[MAX_ZEILEN];
  // Ob die Aufgabe seit mehr als 30 Tagen faellig ist. Die steht dann nicht
  // mehr mit Titel da, sondern "als .zip in die Friedhofsgaertnerei
  // exportiert" (Regel 21). Antippen holt sie zurueck.
  bool begraben[MAX_ZEILEN];
  int faelligId[MAX_ZEILEN];
  int faelligAnzahl = 0;
  int offen = 0;
  // Der aelteste offene Hinweis, den Mia OS auf den Tisch legen will.
  int hinweisId = 0;
  String hinweisText;
  String hinweisVon;
  int hinweiseAnzahl = 0;
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
// Die fuenfte Seite. Erscheint nirgends in der Navigation, hat keinen
// Punkt oben und kein Wischen hin. Regel 62.
const int KAMMER = 4;

// --- Was das Geraet sonst noch weiss --------------------------------------

// Wo die Verbindung haengt, wenn sie haengt. Drei Stufen, drei Saetze:
// der rote Punkt allein sagt nicht, ob man etwas tun kann.
enum Lage { LAGE_OK, LAGE_KEIN_WLAN, LAGE_KEIN_TUNNEL, LAGE_KEIN_SERVER };
Lage lage = LAGE_OK;
// Der Rand des Heimnetzes hinter dem Tunnel. Antwortet der, steht der
// Tunnel; antwortet nur der Pi, ist der Tunnel weg.
const IPAddress TUNNEL_PRUEF(172, 16, 50, 1);

// Was gerade an Sondersachen auf dem Schirm liegt.
struct Eier {
  bool stein = false;           // Uhr als Steinblock, bis zur naechsten Beruehrung
  uint32_t schereBis = 0;       // "ey schere" im laufenden Kasten
  uint32_t raphBis = 0;         // Raphmoment steht
  int raphStufe = 0;            // wie oft hintereinander geschnipst
  uint32_t letzterSchnips = 0;
  int kopfTipps = 0;            // Tipps auf die Kopfzeile fuer die Kammer
  uint32_t letzterKopfTipp = 0;
  int wischWand = 0;            // Wische gegen die Wand fuer DOAH
  uint32_t zugStart = 0;        // wann der Zug losfaehrt, 0 wenn nicht
  int zugTerminMin = -1;        // fuer welchen Terminbeginn er schon fuhr
  int regelNr = 0;              // welche Regel die Kammer gerade zeigt
};
Eier eier;
uint32_t doah = 0;            // Der Zaehler fuer nichts. Regel 30.
String regeln[8];
int regelnAnzahl = 0;
// Antwort auf einen Hinweis, wird nach dem Tippen gesendet.
int hinweisTipps = 0;         // wie oft auf denselben Knopf getippt (Regel 39)
int hinweisKnopf = 0;         // 1 ok, 2 spaeter
uint32_t hinweisTippZeit = 0;

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
void lageEinordnen();
bool istSiebenundsechzig(const char *uhr);
int tageDazwischen(const String &von, const String &bis);
String saeubern(const String &roh);
bool holen(const char *pfad, JsonDocument &doc, JsonDocument &filter);

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
 * Text auf das bringen, was die Schriften koennen.
 *
 * Mia OS liefert UTF-8. Die Inter-Schriften in schriften.h decken Latin-1
 * ab, also alles von "Frühstück" bis "Ärztin". Was darueber hinausgeht
 * (Gedankenstrich, Pfeile, Emoji) wuerde TFT_eSPI stumm ueberspringen, was
 * Woerter zusammenklebt. Hier wird es durch ein sichtbares Zeichen ersetzt.
 *
 * Bis 0.1.7 hiess diese Funktion ``entumlauten`` und machte aus "ä" ein
 * "ae", weil die eingebauten Schriften nur ASCII kennen.
 */
String saeubern(const String &roh) {
  String aus;
  aus.reserve(roh.length());
  for (size_t i = 0; i < roh.length(); i++) {
    const uint8_t c = (uint8_t)roh[i];
    if (c < 0x80) {
      aus += (char)c;
    } else if (c == 0xC2 || c == 0xC3) {
      // Zweibytezeichen aus Latin-1: unveraendert durchreichen.
      aus += (char)c;
      if (i + 1 < roh.length())
        aus += roh[++i];
    } else if ((c & 0xE0) == 0xC0) {
      i += 1;
      aus += '?';
    } else if ((c & 0xF0) == 0xE0) {
      i += 2;
      // Die haeufigsten Dreibyter sind Striche und Anfuehrungszeichen
      // (U+2013, U+2014, U+201E, U+201C). Ein Bindestrich passt meistens.
      aus += '-';
    } else if ((c & 0xF8) == 0xF0) {
      i += 3;  // Emoji: weglassen, das Wort davor bleibt lesbar.
    }
  }
  return aus;
}

/**
 * Text so kuerzen, dass er in ``breite`` Pixel passt, in der gerade
 * gesetzten Schrift. Gemessen statt gezaehlt: "Mittagspause" und "iiiiiiiiii"
 * haben gleich viele Zeichen und sehr verschiedene Breiten. Genau daran ist
 * die alte Zeichenzaehlung gescheitert, Titel liefen ueber den Kastenrand.
 *
 * Geschnitten wird nur an Zeichengrenzen, nie mitten in einem Umlaut.
 */
String passend(const String &text, int breite) {
  if (tft.textWidth(text) <= breite)
    return text;
  String aus = text;
  while (aus.length() > 0) {
    int schnitt = aus.length() - 1;
    while (schnitt > 0 && ((uint8_t)aus[schnitt] & 0xC0) == 0x80)
      schnitt--;
    aus = aus.substring(0, schnitt);
    // Abschliessende Leerzeichen vor dem Punkt sehen schlampig aus.
    while (aus.length() && aus[aus.length() - 1] == ' ')
      aus.remove(aus.length() - 1);
    if (tft.textWidth(aus + ".") <= breite)
      return aus + ".";
  }
  return "";
}

/** Tage zwischen zwei ISO-Daten, grob ueber den Tag des Jahres. Reicht fuer "aelter als 30". */
int tageDazwischen(const String &von, const String &bis) {
  auto tag = [](const String &d) {
    struct tm t = {};
    t.tm_year = d.substring(0, 4).toInt() - 1900;
    t.tm_mon = d.substring(5, 7).toInt() - 1;
    t.tm_mday = d.substring(8, 10).toInt();
    t.tm_hour = 12;
    return mktime(&t) / 86400;
  };
  return (int)(tag(bis) - tag(von));
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

/**
 * Herausfinden, wo es haengt, wenn Mia OS nicht antwortet.
 *
 * Das Display haengt am Pi, der Pi am Firmennetz, das Firmennetz am
 * Tunnel, der Tunnel am Heimnetz. Ein Ping auf den Heimnetz-Rand sagt,
 * ob der Tunnel steht. Faellt der, ist es die Firmenleitung, und daran
 * kann Mia nichts aendern. Antwortet er, ist Mia OS selbst das Problem.
 */
void lageEinordnen() {
  if (WiFi.status() != WL_CONNECTED) {
    lage = LAGE_KEIN_WLAN;
    return;
  }
  lage = Ping.ping(TUNNEL_PRUEF, 2) ? LAGE_KEIN_SERVER : LAGE_KEIN_TUNNEL;
}

/** Etwas an Mia OS schicken. Antwort interessiert nur als Statuscode. */
bool senden(const String &pfad, const String &json, const char *methode = "POST") {
  if (WiFi.status() != WL_CONNECTED)
    return false;
  HTTPClient http;
  http.setTimeout(6000);
  http.setConnectTimeout(4000);
  if (!http.begin(String(basisUrl) + pfad))
    return false;
  http.addHeader("Content-Type", "application/json");
  const int code = http.sendRequest(methode, json);
  http.end();
  Serial.printf("[jana-display] %s %s -> %d\n", methode, pfad.c_str(), code);
  return code >= 200 && code < 300;
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
    lageEinordnen();
    return false;
  }
  lage = LAGE_OK;
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
  z.titel = saeubern(t["titel"].as<String>());
  z.zeit = saeubern(t["zeit"].as<String>());
  z.ende = saeubern(t["ende"].as<String>());
  z.ort = saeubern(t["ort"].as<String>());
  z.beginnMin = alsMinuten(z.zeit);
  z.endeMin = alsMinuten(z.ende);

  const String kalender = t["kalender"].as<String>();
  if (kalender == "Arbeit")
    z.farbe = C_KAL_ARBEIT;
  else if (kalender == "Privat")
    z.farbe = C_KAL_PRIVAT;
  else if (kalender.startsWith("Lernort"))
    z.farbe = C_KAL_WOHNEN;
  else
    z.farbe = C_KAL_SONST;
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
    filter[feld][0]["kalender"] = true;
  }
  filter["faellig"][0]["titel"] = true;
  filter["faellig"][0]["datum"] = true;
  filter["faellig"][0]["id"] = true;
  filter["hinweise"][0]["id"] = true;
  filter["hinweise"][0]["text"] = true;
  filter["hinweise"][0]["von"] = true;

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
    // Ueberfaellig heisst: Datum liegt vor heute. Beide sind ISO-Daten,
    // also reicht ein Zeichenvergleich, ohne Kalenderrechnung.
    const String datum = f["datum"].as<String>();
    const bool ueber =
        datum.length() == 10 && frisch.datum.length() == 10 && datum < frisch.datum;
    frisch.ueberfaellig[frisch.faelligAnzahl] = ueber;
    frisch.begraben[frisch.faelligAnzahl] = ueber && tageDazwischen(datum, frisch.datum) > 30;
    frisch.faelligId[frisch.faelligAnzahl] = f["id"] | 0;
    frisch.faellig[frisch.faelligAnzahl++] = saeubern(f["titel"].as<String>());
  }

  JsonArray hinweise = doc["hinweise"].as<JsonArray>();
  frisch.hinweiseAnzahl = hinweise.size();
  if (frisch.hinweiseAnzahl > 0) {
    JsonObject h = hinweise[0];
    frisch.hinweisId = h["id"] | 0;
    frisch.hinweisText = saeubern(h["text"].as<String>());
    frisch.hinweisVon = saeubern(h["von"].as<String>());
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
    frisch.stoerung[frisch.stoerAnzahl] = saeubern(s["name"].as<String>());
    frisch.meldung[frisch.stoerAnzahl] = saeubern(s["meldung"].as<String>());
    frisch.stoerAnzahl++;
  }

  // Nur die vier Kennzahlen, die auf ein Blatt passen und etwas aussagen.
  // "Speicher: main" ist ein Name und keine Zahl, sowas faellt raus. Die
  // Namen werden gekuerzt: "Arbeitsspeicher" passt nicht in eine 90 Pixel
  // breite Spalte, "RAM" schon.
  const char *gewollt[] = {"Arbeitsspeicher", "Prozessor", "Antwortzeit", "Vollster Speicher"};
  const char *kurz[] = {"RAM", "CPU", "Antwort", "vollste Platte"};
  for (JsonObject k : doc["kennzahlen"].as<JsonArray>()) {
    if (frisch.zahlAnzahl >= 4)
      break;
    const String label = k["label"].as<String>();
    for (int g = 0; g < 4; g++) {
      if (label == gewollt[g]) {
        frisch.zahlLabel[frisch.zahlAnzahl] = kurz[g];
        frisch.zahlWert[frisch.zahlAnzahl] = saeubern(k["wert"].as<String>());
        frisch.zahlAnzahl++;
        break;
      }
    }
  }

  frisch.gueltig = true;
  homelab = frisch;
  return true;
}

/** Die Regeln fuer die Kammer. Einmal beim Start, acht Stueck reichen. */
void regelnHolen() {
  JsonDocument filter;
  filter["regeln"] = true;
  JsonDocument doc;
  if (!holen("/api/regeln", doc, filter))
    return;
  regelnAnzahl = 0;
  JsonArray alle = doc["regeln"].as<JsonArray>();
  // Zufaellig auswaehlen, damit nicht jeden Tag dieselben acht kommen.
  const int n = alle.size();
  if (n == 0)
    return;
  int start = (int)(esp_random() % n);
  for (int i = 0; i < n && regelnAnzahl < 8; i++)
    regeln[regelnAnzahl++] = saeubern(alle[(start + i * 7) % n].as<String>());
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
/** Kleiner Farbpunkt mit Text daneben, fuer Hinweise wie "2 fällig". */
void punktZeile(int x, int y, const String &text, uint16_t farbe, uint16_t grund) {
  tft.setFreeFont(S_NORMAL);
  tft.fillCircle(x + 3, y + 9, 3, farbe);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(farbe, grund);
  tft.drawString(text, x + 12, y);
}

/**
 * Die Leerlauf-Ansicht: nichts laeuft, nichts kommt mehr.
 *
 * Statt einer halb leeren Zweispalter steht dann eine Uhr ueber die volle
 * Breite. Abends und am Wochenende ist das der Zustand, in dem das Geraet
 * die meiste Zeit verbringt, und er sollte nicht aussehen wie "Daten
 * fehlen".
 */
void leerlaufZeichnen(const struct tm &jetzt, bool zeitDa) {
  char uhr[6] = "--:--";
  if (zeitDa)
    strftime(uhr, sizeof(uhr), "%H:%M", &jetzt);

  if (eier.stein) {
    tft.setSwapBytes(true);
    tft.pushImage(BREIT / 2 - SPRITE_B / 2, 56, SPRITE_B, SPRITE_B, SPRITE_STEIN);
  } else {
    tft.setFreeFont(S_RIESIG);
    tft.setTextDatum(TC_DATUM);
    tft.setTextColor(istSiebenundsechzig(uhr) ? C_AKZENT : C_TEXT, C_GRUND);
    tft.drawString(uhr, BREIT / 2, 62);
  }

  if (zeitDa) {
    static const char *tage[] = {"Sonntag", "Montag", "Dienstag", "Mittwoch",
                                 "Donnerstag", "Freitag", "Samstag"};
    char zeile[40];
    snprintf(zeile, sizeof(zeile), "%s, %d. %d.", tage[jetzt.tm_wday],
             jetzt.tm_mday, jetzt.tm_mon + 1);
    tft.setFreeFont(S_NORMAL);
    tft.setTextColor(C_GEDAEMPFT, C_GRUND);
    tft.drawString(zeile, BREIT / 2, 140);
  }

  // Was morgen als erstes kommt, als Fussnote. Wer abends auf das Geraet
  // schaut, will genau das wissen.
  const Termin *morgen = nullptr;
  for (int i = 0; i < briefing.morgenAnzahl; i++) {
    const Termin &t = briefing.morgen[i];
    if (t.beginnMin >= 0 && (!morgen || t.beginnMin < morgen->beginnMin))
      morgen = &t;
  }
  tft.setFreeFont(S_NORMAL);
  tft.setTextDatum(TC_DATUM);
  if (morgen) {
    tft.setTextColor(C_GEDAEMPFT, C_GRUND);
    tft.drawString(passend("morgen " + morgen->zeit + "  " + morgen->titel, BREIT - 40),
                   BREIT / 2, 176);
  } else if (briefing.faelligAnzahl > 0) {
    tft.setTextColor(C_ACHTUNG, C_GRUND);
    tft.drawString(String(briefing.faelligAnzahl) + " fällig", BREIT / 2, 176);
  }
}

void jetztZeichnen() {
  struct tm jetzt;
  const bool zeitDa = getLocalTime(&jetzt, 50);
  const int jetztMin = jetztMinuten();
  const Termin *laeuft = laeuftGerade();
  const Termin *naechste = kommtAlsNaechstes();

  if (!laeuft && !naechste && zeitDa) {
    leerlaufZeichnen(jetzt, zeitDa);
    return;
  }

  // Die Uhr in 24 Punkt braucht fuer "09:47" 134 Pixel. Die Spalte ist
  // darauf gemessen, nicht geschaetzt: bei 0.1.5 stand "09:4" auf dem
  // Display, weil die Schrift breiter war als der Platz.
  const int spalte = 150;
  {
    const int oben = KOPF_H + 8;
    const int hoehe = FUSS_Y - oben - 8;
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

  if (eier.stein) {
    // Zehn Sekunden auf die Uhr gehalten: die Uhr ist jetzt ein Stein.
    // Bleibt, bis jemand das Geraet wieder anfasst. Wird nicht erklaert.
    tft.setSwapBytes(true);
    tft.pushImage(24, 44, SPRITE_B, SPRITE_B, SPRITE_STEIN);
  } else {
    tft.setFreeFont(S_UHR);
    tft.setTextDatum(TL_DATUM);
    // Regel 67. Um 06:07 und 16:07 ist die Uhr kurz rot.
    tft.setTextColor(istSiebenundsechzig(uhr) ? C_AKZENT : C_TEXT, C_GRUND);
    tft.drawString(uhr, 8, 46);
  }

  if (zeitDa) {
    static const char *tage[] = {"Sonntag", "Montag", "Dienstag", "Mittwoch",
                                 "Donnerstag", "Freitag", "Samstag"};
    char zeile[40];
    snprintf(zeile, sizeof(zeile), "%s, %d. %d.", tage[jetzt.tm_wday],
             jetzt.tm_mday, jetzt.tm_mon + 1);
    tft.setFreeFont(S_NORMAL);
    tft.setTextColor(C_GEDAEMPFT, C_GRUND);
    tft.drawString(passend(zeile, spalte - 24), 10, eier.stein ? 112 : 90);
  }

  // Der Tag als Strich, von sieben bis zweiundzwanzig Uhr.
  if (jetztMin >= 0) {
    const float anteil = (jetztMin - 7 * 60) / (float)((22 - 7) * 60);
    const int bBreite = spalte - 32;
    tft.fillRoundRect(12, 126, bBreite, 5, 2, C_LINIE);
    if (anteil > 0)
      tft.fillRoundRect(12, 126, (int)(bBreite * min(anteil, 1.0f)), 5, 2,
                        C_AKZENT);
    if (anteil >= 0 && anteil <= 1) {
      const int px = 12 + (int)(bBreite * anteil);
      tft.fillCircle(px, 128, 5, C_GRUND);
      tft.fillCircle(px, 128, 3, C_TEXT);
    }
  }

  // Faelliges als Zahl. Die Titel stehen auf der Heute-Seite; hier zaehlt,
  // ob ueberhaupt etwas offen ist, sonst wird die ruhigste Seite zur
  // vollsten.
  if (briefing.faelligAnzahl > 0) {
    int ueber = 0;
    for (int i = 0; i < briefing.faelligAnzahl; i++)
      if (briefing.ueberfaellig[i])
        ueber++;
    punktZeile(12, 148, String(briefing.faelligAnzahl) + " fällig", C_ACHTUNG, C_GRUND);
    if (ueber > 0) {
      tft.setFreeFont(S_KLEIN);
      tft.setTextColor(C_FEHLER, C_GRUND);
      tft.drawString(String(ueber) + " davon überfällig", 24, 170);
    }
  }

  // --- Rechte Spalte: was laeuft, was kommt -----------------------------
  int y = KOPF_H + 10;
  const int rb = BREIT - spalte - 10;  // Breite der Kaesten

  if (laeuft) {
    const int dauer = laeuft->endeMin - laeuft->beginnMin;
    const int weg = jetztMin - laeuft->beginnMin;
    const int rest = laeuft->endeMin - jetztMin;

    tft.fillRoundRect(spalte, y, rb, 78, 8, C_ERHOBEN);
    tft.fillRoundRect(spalte, y, 4, 78, 2, laeuft->farbe);

    // Oben die Zeile "läuft ... noch 40 min", dann der Titel gross, dann
    // Balken und Uhrzeiten. Restzeit und Uhrzeiten standen vorher in einer
    // Zeile und liefen ineinander.
    tft.setFreeFont(S_KLEIN);
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(C_GEDAEMPFT, C_ERHOBEN);
    tft.drawString(millis() < eier.schereBis ? "ey schere" : "läuft", spalte + 12, y + 6);
    tft.setFreeFont(S_NORMAL);
    tft.setTextDatum(TR_DATUM);
    tft.setTextColor(C_AKZENT, C_ERHOBEN);
    tft.drawString("noch " + alsDauer(rest), BREIT - 12, y + 3);
    tft.setFreeFont(S_FETT);
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(C_TEXT, C_ERHOBEN);
    tft.drawString(passend(laeuft->titel, rb - 22), spalte + 12, y + 26);

    balken(spalte + 12, y + 54, rb - 24, 5,
           dauer > 0 ? weg / (float)dauer : 0, laeuft->farbe);
    tft.setFreeFont(S_KLEIN);
    tft.setTextColor(C_GEDAEMPFT, C_ERHOBEN);
    tft.drawString(laeuft->zeit + " - " + laeuft->ende, spalte + 12, y + 63);
    y += 86;
  }

  if (naechste) {
    const int bis = naechste->beginnMin - jetztMin;
    const int hoch = laeuft ? 66 : 78;
    tft.fillRoundRect(spalte, y, rb, hoch, 8, C_FLAECHE);
    tft.fillRoundRect(spalte, y, 4, hoch, 2, naechste->farbe);
    tft.setFreeFont(S_KLEIN);
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(C_GEDAEMPFT, C_FLAECHE);
    tft.drawString(laeuft ? "danach" : "als nächstes", spalte + 12, y + 6);
    tft.setFreeFont(S_NORMAL);
    tft.setTextDatum(TR_DATUM);
    // Unter einer Viertelstunde wird die Zahl farbig: das ist der Moment,
    // in dem man losgehen muesste.
    tft.setTextColor(bis <= 15 ? C_ACHTUNG : C_GEDAEMPFT, C_FLAECHE);
    tft.drawString("in " + alsDauer(bis), BREIT - 12, y + 3);
    tft.setFreeFont(S_FETT);
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(C_TEXT, C_FLAECHE);
    tft.drawString(passend(naechste->titel, rb - 22), spalte + 12, y + 26);

    tft.setFreeFont(S_KLEIN);
    tft.setTextColor(C_GEDAEMPFT, C_FLAECHE);
    tft.drawString(naechste->zeit + (naechste->ende.length() ? " - " + naechste->ende : ""),
                   spalte + 12, y + hoch - 16);
    y += hoch + 8;
  }

  if (!laeuft && !naechste) {
    tft.setFreeFont(S_GROSS);
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(C_GEDAEMPFT, C_GRUND);
    tft.drawString("warte auf", spalte, KOPF_H + 30);
    tft.drawString("die Uhrzeit", spalte, KOPF_H + 62);
  }
}

/** Die Kopfzeile: welche Seite, und wie frisch die Daten sind. */
uint16_t seitenFarbe() {
  // Jede Seite hat ihren eigenen Ton. Man sieht am Rand, wo man ist,
  // ohne die Ueberschrift zu lesen.
  switch (ansicht) {
  case 0: return C_AKZENT;
  case 1: return C_AKZENT;
  case 2: return C_KAL_WOHNEN;
  default: return stoerungAktiv() ? C_ACHTUNG : C_GUT;
  }
}

void kopfZeichnen() {
  static const char *namen[] = {"Jetzt", "Heute", "Morgen", "Homelab"};
  const uint16_t ton = seitenFarbe();
  tft.fillRect(0, 0, BREIT, KOPF_H, C_FLAECHE);
  // Farbiger Balken statt grauer Linie: der Seitenton zieht sich durch.
  tft.fillRect(0, KOPF_H, BREIT, 2, ton);

  tft.setFreeFont(S_FETT);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(C_TEXT, C_FLAECHE);
  tft.drawString(namen[ansicht], 12, 6);

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
 * so dass das Auge sie der Reihe nach findet. Unter den Terminen, wenn
 * Platz ist: die faelligen Aufgaben mit Titel.
 */
// Wo die Faellig-Zeilen auf der Heute-Seite stehen, fuer das Antippen.
int faelligY[MAX_ZEILEN];
int faelligZeilen = 0;

void termineZeichnen() {
  const bool istHeute = ansicht == 1;
  const Termin *liste = istHeute ? briefing.heute : briefing.morgen;
  const int anzahl = istHeute ? briefing.heuteAnzahl : briefing.morgenAnzahl;
  const int gesamt = istHeute ? briefing.heuteGesamt : briefing.morgenGesamt;
  const Termin *laeuft = istHeute ? laeuftGerade() : nullptr;

  int y = KOPF_H + 8;

  if (anzahl == 0) {
    tft.setFreeFont(S_GROSS);
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(C_GEDAEMPFT, C_GRUND);
    tft.drawString(istHeute ? "Keine Termine heute" : "Morgen nichts", 12, y + 6);
    y += 44;
  } else {
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
      const int ky = y + ze * z_hoehe;
      const int kh = z_hoehe - 4;
      const uint16_t grund = aktiv ? C_ERHOBEN : C_FLAECHE;

      tft.fillRoundRect(x, ky, sp_breite, kh, 6, grund);
      // Der Streifen links traegt die Kalenderfarbe. Was gerade laeuft, hat
      // zusaetzlich den helleren Grund.
      tft.fillRoundRect(x, ky, 4, kh, 2, t.farbe);

      tft.setFreeFont(S_KLEIN);
      tft.setTextDatum(TL_DATUM);
      tft.setTextColor(aktiv ? C_AKZENT : C_GEDAEMPFT, grund);
      tft.drawString(t.zeit.length() ? t.zeit : "ganztags", x + 12, ky + 5);

      if (t.ende.length() && kh >= 40) {
        tft.setTextDatum(TR_DATUM);
        tft.drawString("bis " + t.ende, x + sp_breite - 10, ky + 5);
      }

      tft.setFreeFont(spalten == 1 ? S_FETT : S_NORMAL);
      tft.setTextDatum(TL_DATUM);
      tft.setTextColor(C_TEXT, grund);
      tft.drawString(passend(t.titel, sp_breite - 22), x + 12, ky + (spalten == 1 ? 18 : 20));
    }
    y += zeilen * z_hoehe + 2;

    if (gesamt > anzahl) {
      tft.setFreeFont(S_KLEIN);
      tft.setTextDatum(TL_DATUM);
      tft.setTextColor(C_GEDAEMPFT, C_GRUND);
      tft.drawString("+ " + String(gesamt - anzahl) + " weitere", 12, y);
      y += 16;
    }
  }

  // Faellige Aufgaben, so viele wie noch Platz haben. Auf der Heute-Seite,
  // weil sie dorthin gehoeren, wo der Tag geplant wird. Morgen zeigt sie
  // nicht: was morgen faellig ist, weiss Mia OS heute noch nicht.
  faelligZeilen = 0;
  if (istHeute && briefing.faelligAnzahl > 0) {
    tft.setFreeFont(S_KLEIN);
    for (int i = 0; i < briefing.faelligAnzahl && y + 15 <= FUSS_Y - 2; i++) {
      faelligY[i] = y;
      faelligZeilen = i + 1;
      const uint16_t farbe = briefing.ueberfaellig[i] ? C_FEHLER : C_ACHTUNG;
      tft.fillCircle(15, y + 7, 3, farbe);
      tft.setTextDatum(TL_DATUM);
      // Regel 21: was ueber 30 Tage liegt, wurde exportiert. Antippen holt
      // es zurueck, das ist die Funktion hinter dem Witz.
      if (briefing.begraben[i]) {
        tft.setTextColor(C_GEDAEMPFT, C_GRUND);
        tft.drawString("als .zip in die Friedhofsgärtnerei exportiert", 24, y);
      } else {
        tft.setTextColor(C_TEXT, C_GRUND);
        tft.drawString(passend(briefing.faellig[i], BREIT - 36), 24, y);
      }
      y += 15;
    }
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
    tft.setFreeFont(S_NORMAL);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(C_GEDAEMPFT, C_GRUND);
    tft.drawString("Homelab nicht erreichbar", BREIT / 2, 120);
    return;
  }

  const bool stoerung = stoerungAktiv();
  const int spalte = 124;
  tft.drawFastVLine(spalte - 10, KOPF_H + 8, FUSS_Y - KOPF_H - 16, C_LINIE);

  tft.setFreeFont(S_UHR);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(stoerung ? C_ACHTUNG : C_GUT, C_GRUND);
  tft.drawString(String(homelab.oben), 12, 46);

  tft.setFreeFont(S_KLEIN);
  tft.setTextColor(C_GEDAEMPFT, C_GRUND);
  tft.drawString("von " + String(homelab.gesamt) + " Diensten", 12, 88);

  // Ein Raster aus Punkten, einer je Dienst. Das macht aus einer Zahl ein
  // Bild: 46 gruene Punkte und ein roter sagen dasselbe wie "46 von 47",
  // aber man sieht den roten sofort.
  int px = 14, py = 114;
  for (int i = 0; i < homelab.gesamt && py < 170; i++) {
    tft.fillCircle(px, py, 2, i < homelab.oben ? C_GUT : C_FEHLER);
    px += 9;
    if (px > spalte - 20) {
      px = 14;
      py += 10;
    }
  }

  tft.setFreeFont(S_KLEIN);
  tft.setTextColor(C_GEDAEMPFT, C_GRUND);
  tft.drawString(String(homelab.uptime, 1) + " % in 24 h", 12, 184);

  int y = KOPF_H + 8;
  if (stoerung) {
    for (int i = 0; i < homelab.stoerAnzahl && y < FUSS_Y - 36; i++) {
      tft.fillRoundRect(spalte, y, BREIT - spalte - 10, 36, 6, C_ERHOBEN);
      tft.fillRoundRect(spalte, y, 4, 36, 2, C_FEHLER);
      tft.setFreeFont(S_NORMAL);
      tft.setTextDatum(TL_DATUM);
      tft.setTextColor(C_TEXT, C_ERHOBEN);
      tft.drawString(passend(homelab.stoerung[i], BREIT - spalte - 32), spalte + 12, y + 3);
      tft.setFreeFont(S_KLEIN);
      tft.setTextColor(C_GEDAEMPFT, C_ERHOBEN);
      tft.drawString(passend(homelab.meldung[i], BREIT - spalte - 32), spalte + 12, y + 21);
      y += 42;
    }
  } else {
    tft.setFreeFont(S_GROSS);
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(C_GUT, C_GRUND);
    tft.drawString("Alles läuft", spalte, y + 2);
    y += 40;
  }

  // Die Kennzahlen in zwei Spalten. Label klein darueber, Wert darunter:
  // beim Ueberfliegen sucht das Auge die Zahl, nicht das Wort.
  const int k_breite = (BREIT - spalte - 10) / 2;
  for (int i = 0; i < homelab.zahlAnzahl && y + 30 <= FUSS_Y; i++) {
    const int x = spalte + (i % 2) * k_breite;
    tft.setFreeFont(S_KLEIN);
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(C_GEDAEMPFT, C_GRUND);
    tft.drawString(passend(homelab.zahlLabel[i], k_breite - 8), x, y);
    tft.setFreeFont(S_NORMAL);
    tft.setTextColor(C_TEXT, C_GRUND);
    tft.drawString(homelab.zahlWert[i], x, y + 13);
    if (i % 2)
      y += 36;
  }
}

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
    tft.setFreeFont(S_GROSS);
    tft.setTextDatum(TC_DATUM);
    tft.setTextColor(C_TEXT, C_GRUND);
    tft.drawString("Wird geladen", BREIT / 2, 66);
    tft.setFreeFont(S_NORMAL);
    tft.setTextColor(C_GEDAEMPFT, C_GRUND);
    tft.drawString("Version " + neuling.version, BREIT / 2, 102);
    tft.drawString("Strom nicht trennen", BREIT / 2, 186);
  }
  letzterProzent = neuling.prozent;

  const int x = 40, y = 136, breit = BREIT - 80, hoch = 14;
  tft.drawRoundRect(x, y, breit, hoch, 4, C_LINIE);
  tft.fillRoundRect(x + 2, y + 2, ((breit - 4) * neuling.prozent) / 100, hoch - 4, 3,
                    C_AKZENT);

  tft.setFreeFont(S_NORMAL);
  tft.setTextDatum(TC_DATUM);
  tft.setTextColor(C_TEXT, C_GRUND);
  tft.drawString(String(neuling.prozent) + " %", BREIT / 2, y + 22);
}

// Masse des Update-Dialogs, an einer Stelle: das Zeichnen und die
// Trefferpruefung beim Tippen muessen dieselben Zahlen benutzen.
const int DLG_X = 26, DLG_Y = 40, DLG_B = BREIT - 52, DLG_H = 164;
const int DLG_KY = DLG_Y + 112, DLG_KH = 36, DLG_ABSTAND = 8;
const int DLG_KB = (DLG_B - 2 * 14 - 2 * DLG_ABSTAND) / 3;
const int DLG_K1 = DLG_X + 14, DLG_K2 = DLG_K1 + DLG_KB + DLG_ABSTAND,
          DLG_K3 = DLG_K2 + DLG_KB + DLG_ABSTAND;

/**
 * Der Dialog: was laeuft, was kaeme, und drei Knoepfe.
 *
 * Liegt ueber der Seite statt sie zu ersetzen. Mia soll sehen, was das
 * Geraet gerade anzeigt, waehrend sie entscheidet.
 */
void updateDialogZeichnen() {
  // Schatten als Andeutung von Hoehe, damit der Kasten nicht wie ein
  // Teil der Seite aussieht.
  tft.fillRoundRect(DLG_X + 3, DLG_Y + 3, DLG_B, DLG_H, 10, C_GRUND);
  tft.fillRoundRect(DLG_X, DLG_Y, DLG_B, DLG_H, 10, C_ERHOBEN);
  tft.drawRoundRect(DLG_X, DLG_Y, DLG_B, DLG_H, 10, C_AKZENT);

  tft.setFreeFont(S_GROSS);
  tft.setTextDatum(TC_DATUM);
  tft.setTextColor(C_TEXT, C_ERHOBEN);
  tft.drawString("Neue Version da", BREIT / 2, DLG_Y + 12);

  // Alt und neu untereinander, Werte ausgerichtet: so sieht man den
  // Unterschied, ohne zu lesen.
  tft.setFreeFont(S_NORMAL);
  tft.setTextDatum(TR_DATUM);
  tft.setTextColor(C_GEDAEMPFT, C_ERHOBEN);
  tft.drawString("jetzt", BREIT / 2 - 12, DLG_Y + 54);
  tft.drawString("neu", BREIT / 2 - 12, DLG_Y + 78);

  tft.setTextDatum(TL_DATUM);
  tft.drawString(FIRMWARE_VERSION, BREIT / 2 + 4, DLG_Y + 54);
  tft.setTextColor(C_AKZENT, C_ERHOBEN);
  tft.drawString(neuling.version, BREIT / 2 + 4, DLG_Y + 78);

  // Drei Knoepfe nebeneinander. Ja hat Farbe, die anderen nicht: die
  // haeufigste Antwort soll am leichtesten zu treffen sein.
  tft.fillRoundRect(DLG_K1, DLG_KY, DLG_KB, DLG_KH, 6, C_AKZENT);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(C_TEXT, C_AKZENT);
  tft.drawString("Ja", DLG_K1 + DLG_KB / 2, DLG_KY + DLG_KH / 2);

  for (int i = 0; i < 2; i++) {
    const int kx = i == 0 ? DLG_K2 : DLG_K3;
    tft.fillRoundRect(kx, DLG_KY, DLG_KB, DLG_KH, 6, C_FLAECHE);
    tft.drawRoundRect(kx, DLG_KY, DLG_KB, DLG_KH, 6, C_LINIE);
    tft.setTextColor(C_GEDAEMPFT, C_FLAECHE);
    tft.drawString(i == 0 ? "Später" : "Nein", kx + DLG_KB / 2, DLG_KY + DLG_KH / 2);
  }
}

/** Regel 67: 06:07, 16:07, oder die Zahl 67 irgendwo in der Uhr. */
bool istSiebenundsechzig(const char *uhr) {
  return strstr(uhr, "6:07") != nullptr || strstr(uhr, "67") != nullptr;
}

// Masse des Hinweis-Kastens. Zeichnen und Treffer teilen sich die Zahlen.
const int HW_X = 20, HW_Y = 48, HW_B = BREIT - 40, HW_H = 128;
const int HW_KY = HW_Y + HW_H - 46, HW_KH = 34;
const int HW_KB = (HW_B - 3 * 12) / 2;
const int HW_K1 = HW_X + 12, HW_K2 = HW_K1 + HW_KB + 12;

/**
 * Ein Zettel von Mia OS: Text, Absender, zwei Knoepfe.
 *
 * Liegt ueber der Seite wie der Update-Dialog. "ok" heisst gesehen und
 * weg, "später" schiebt ihn eine Stunde. Viermal auf denselben Knopf ist
 * Regel 39: Hall of Fame oder Hall of Shame.
 */
void hinweisZeichnen() {
  tft.fillRoundRect(HW_X + 3, HW_Y + 3, HW_B, HW_H, 10, C_GRUND);
  tft.fillRoundRect(HW_X, HW_Y, HW_B, HW_H, 10, C_ERHOBEN);
  tft.drawRoundRect(HW_X, HW_Y, HW_B, HW_H, 10, C_KAL_PRIVAT);

  tft.setFreeFont(S_KLEIN);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(C_GEDAEMPFT, C_ERHOBEN);
  tft.drawString(briefing.hinweisVon + (briefing.hinweiseAnzahl > 1
                                            ? "  (+" + String(briefing.hinweiseAnzahl - 1) + ")"
                                            : ""),
                 HW_X + 12, HW_Y + 8);

  // Zwei Zeilen Text, an Wortgrenzen umgebrochen. Was dann noch uebrig
  // ist, wird abgeschnitten: 120 Zeichen passen fast immer.
  tft.setFreeFont(S_FETT);
  tft.setTextColor(C_TEXT, C_ERHOBEN);
  String rest = briefing.hinweisText;
  const int breite = HW_B - 24;
  for (int zeile = 0; zeile < 2 && rest.length(); zeile++) {
    String teil = rest;
    while (tft.textWidth(teil) > breite) {
      const int leer = teil.lastIndexOf(' ');
      if (leer <= 0) {
        teil = passend(teil, breite);
        break;
      }
      teil = teil.substring(0, leer);
    }
    if (zeile == 1 && teil.length() < rest.length())
      teil = passend(rest, breite);
    tft.drawString(teil, HW_X + 12, HW_Y + 26 + zeile * 24);
    rest = teil.length() < rest.length() ? rest.substring(teil.length() + 1) : "";
  }

  tft.setFreeFont(S_NORMAL);
  tft.fillRoundRect(HW_K1, HW_KY, HW_KB, HW_KH, 6, C_KAL_PRIVAT);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(C_TEXT, C_KAL_PRIVAT);
  tft.drawString(hinweisKnopf == 1 && hinweisTipps > 1 ? String(hinweisTipps) + "x ok" : "ok",
                 HW_K1 + HW_KB / 2, HW_KY + HW_KH / 2);
  tft.fillRoundRect(HW_K2, HW_KY, HW_KB, HW_KH, 6, C_FLAECHE);
  tft.drawRoundRect(HW_K2, HW_KY, HW_KB, HW_KH, 6, C_LINIE);
  tft.setTextColor(C_GEDAEMPFT, C_FLAECHE);
  tft.drawString(hinweisKnopf == 2 && hinweisTipps > 1 ? String(hinweisTipps) + "x später"
                                                       : "später",
                 HW_K2 + HW_KB / 2, HW_KY + HW_KH / 2);
}

/**
 * Die Kammer des Korsaren. Regel 62.
 *
 * Fuenfmal auf die Kopfzeile getippt. Kein Punkt oben, keine Seite in der
 * Reihenfolge, Wischen fuehrt zurueck. Zeigt eine Regel, Tippen die
 * naechste.
 */
void kammerZeichnen() {
  tft.fillScreen(TFT_BLACK);
  tft.setFreeFont(S_FETT);
  tft.setTextDatum(TC_DATUM);
  tft.setTextColor(C_ACHTUNG, TFT_BLACK);
  tft.drawString("!  KAMMER DES KORSAREN  !", BREIT / 2, 18);
  tft.drawFastHLine(24, 48, BREIT - 48, C_LINIE);

  if (regelnAnzahl == 0) {
    tft.setFreeFont(S_NORMAL);
    tft.setTextColor(C_GEDAEMPFT, TFT_BLACK);
    tft.drawString("Es gibt keine Kammer.", BREIT / 2, 110);
    return;
  }
  // Regeltext auf bis zu vier Zeilen umbrechen.
  const String &r = regeln[eier.regelNr % regelnAnzahl];
  tft.setFreeFont(S_NORMAL);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(C_TEXT, TFT_BLACK);
  String rest = r;
  int y = 66;
  for (int zeile = 0; zeile < 5 && rest.length(); zeile++) {
    String teil = rest;
    while (tft.textWidth(teil) > BREIT - 40) {
      const int leer = teil.lastIndexOf(' ');
      if (leer <= 0) {
        teil = passend(teil, BREIT - 40);
        break;
      }
      teil = teil.substring(0, leer);
    }
    tft.drawString(teil, 20, y);
    y += 24;
    rest = teil.length() < rest.length() ? rest.substring(teil.length() + 1) : "";
  }

  tft.setFreeFont(S_KLEIN);
  tft.setTextDatum(BC_DATUM);
  tft.setTextColor(C_GEDAEMPFT, TFT_BLACK);
  tft.drawString("Es wird nicht laut darüber geredet.", BREIT / 2, HOCH - 8);
}

/**
 * Raphmoment. Regel 61: wer mehr als dreimal schnipst, wird extracted.
 *
 * Dreimal schnell auf dieselbe Stelle getippt ist das Display-Aequivalent
 * zum Schnipsen. Beim dritten Mal steht Fridolinatis Satz in der
 * Fusszeile, beim vierten wird auf die Homelab-Seite verlegt.
 */
void raphZeichnen() {
  tft.fillRect(0, FUSS_Y, BREIT, HOCH - FUSS_Y, C_FLAECHE);
  tft.drawFastHLine(0, FUSS_Y, BREIT, C_LINIE);
  tft.setFreeFont(S_KLEIN);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(eier.raphStufe >= 4 ? C_FEHLER : C_ACHTUNG, C_FLAECHE);
  tft.drawString(eier.raphStufe >= 4 ? "extracted." : "Leute, könnt ihr damit aufhören",
                 12, FUSS_Y + 5);
  // Raphael, klein, unten rechts ueber der Fusszeile.
  tft.setSwapBytes(true);
  tft.pushImage(BREIT - SPRITE_B - 8, FUSS_Y - SPRITE_B - 2, SPRITE_B, SPRITE_B, SPRITE_RAPH);
}

/**
 * Die Zugentgleisung. Ein 8-Pixel-Zug faehrt unten von rechts nach links
 * aus dem Bild, wenn ein Termin anfaengt und niemand das Geraet anfasst.
 * Kein Text, kein Ton. Wer es kennt, kennt es.
 */
void zugZeichnen() {
  if (!eier.zugStart)
    return;
  const uint32_t weg = millis() - eier.zugStart;
  const int x = BREIT - (int)(weg / 12);  // 12 ms je Pixel, ~4 s ueber das Bild
  const int y = FUSS_Y - 10;
  // Spur freimachen, wo der Zug gerade war.
  tft.fillRect(x + 22, y, 6, 8, C_GRUND);
  if (x < -30) {
    eier.zugStart = 0;
    return;
  }
  // Lok mit Schornstein und zwei Wagen.
  tft.fillRect(x, y + 2, 10, 6, C_FEHLER);
  tft.fillRect(x + 7, y - 1, 3, 3, C_FEHLER);
  tft.fillRect(x + 12, y + 3, 7, 5, C_GEDAEMPFT);
  tft.fillRect(x + 21, y + 3, 7, 5, C_GEDAEMPFT);
  tft.drawPixel(x + 2, y + 8, C_TEXT);
  tft.drawPixel(x + 8, y + 8, C_TEXT);
  tft.drawPixel(x + 15, y + 8, C_TEXT);
  tft.drawPixel(x + 24, y + 8, C_TEXT);
}

/** Die ganze Anzeige. Wird nur bei Aenderung gezeichnet, nicht im Takt. */
void anzeigeZeichnen() {
  if (ansicht == KAMMER) {
    kammerZeichnen();
    return;
  }
  tft.fillScreen(C_GRUND);
  kopfZeichnen();

  if (!habenDaten && ansicht != 3) {
    // Drei verschiedene Saetze, weil drei verschiedene Dinge kaputt sein
    // koennen und nur eines davon Mia betrifft.
    const char *titel = "Keine Verbindung";
    const char *satz = "";
    switch (lage) {
    case LAGE_KEIN_WLAN: titel = "Kein WLAN"; satz = "Pi aus oder zu weit weg"; break;
    case LAGE_KEIN_TUNNEL: titel = "Tunnel weg"; satz = "Firmennetz. Nichts zu tun, warten."; break;
    case LAGE_KEIN_SERVER: titel = "Mia OS antwortet nicht"; satz = "Tunnel steht, Server nicht"; break;
    default: break;
    }
    tft.setFreeFont(S_GROSS);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(C_GEDAEMPFT, C_GRUND);
    tft.drawString(titel, BREIT / 2, 92);
    tft.setFreeFont(S_NORMAL);
    tft.drawString(satz, BREIT / 2, 124);
    tft.setFreeFont(S_KLEIN);
    tft.drawString(passend(letzterFehler + "  " + String(basisUrl), BREIT - 24), BREIT / 2, 152);
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
  tft.setFreeFont(S_KLEIN);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(C_GEDAEMPFT, C_FLAECHE);
  tft.drawString("offen", 12, FUSS_Y + 5);
  tft.setFreeFont(S_NORMAL);
  tft.setTextColor(briefing.offen > 0 ? C_AKZENT : C_GEDAEMPFT, C_FLAECHE);
  tft.drawString(String(briefing.offen), 46, FUSS_Y);

  tft.setFreeFont(S_KLEIN);
  tft.setTextDatum(TC_DATUM);
  // Sind die Daten alt, steht hier statt des Datums, woran es liegt.
  if (lage == LAGE_KEIN_TUNNEL) {
    tft.setTextColor(C_ACHTUNG, C_FLAECHE);
    tft.drawString("Tunnel weg, Daten alt", BREIT / 2, FUSS_Y + 5);
  } else if (lage == LAGE_KEIN_SERVER) {
    tft.setTextColor(C_ACHTUNG, C_FLAECHE);
    tft.drawString("Mia OS antwortet nicht", BREIT / 2, FUSS_Y + 5);
  } else if (lage == LAGE_KEIN_WLAN) {
    tft.setTextColor(C_FEHLER, C_FLAECHE);
    tft.drawString("kein WLAN", BREIT / 2, FUSS_Y + 5);
  } else {
    tft.setTextColor(C_GEDAEMPFT, C_FLAECHE);
    tft.drawString(briefing.datum, BREIT / 2, FUSS_Y + 5);
  }

  // Rechts: der Zaehler fuer nichts. Regel 30. Waechst mit jedem Wisch
  // gegen die Wand und wird nie erklaert.
  tft.setTextDatum(TR_DATUM);
  tft.setTextColor(C_GEDAEMPFT, C_FLAECHE);
  tft.drawString(doah > 0 ? "DOAH " + String(doah) : "halten: Setup", BREIT - 12, FUSS_Y + 5);
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
// Welche Faellig-Zeile angetippt wurde (Index), -1 fuer keine.
int faelligTipp = -1;
// Ob gerade ein Hinweis-Knopf getippt wurde: 1 ok, 2 spaeter.
int hinweisAntwort = 0;
// Halten auf die Uhr (Stein), auf eine Faellig-Zeile (erledigt).
int halteZiel = 0;  // 1 Uhr, 2 Faellig-Zeile
int halteIndex = -1;

int wischen() {
  static bool lag_an = false;
  static int startX = 0, startY = 0;
  static int letzteX = 0, letzteY = 0;
  static uint32_t startZeit = 0;
  static int punkte = 0;

  tippZiel = -1;
  antwort = 0;
  faelligTipp = -1;
  hinweisAntwort = 0;
  halteZiel = 0;
  const bool an = touch.tirqTouched() && touch.touched();

  // Langes Halten an bestimmten Stellen, ausgewertet solange der Finger
  // noch liegt. Zehn Sekunden auf die Uhr: Stein. Zwei Sekunden auf eine
  // faellige Aufgabe: erledigt. Beides laenger als ein Wisch, kuerzer als
  // die sechs Sekunden fuer das Setup, damit sich nichts ueberschneidet.
  if (lag_an && ansicht == 0 && !neuling.gefragt && startX < 150 && startY > KOPF_H &&
      startY < 120 && millis() - startZeit > 10000) {
    lag_an = false;
    halteZiel = 1;
    return 0;
  }
  if (lag_an && ansicht == 1 && !neuling.gefragt && briefing.hinweiseAnzahl == 0 &&
      millis() - startZeit > 2000) {
    for (int i = 0; i < faelligZeilen; i++) {
      if (startY >= faelligY[i] - 3 && startY < faelligY[i] + 15) {
        lag_an = false;
        halteZiel = 2;
        halteIndex = i;
        return 0;
      }
    }
  }

  // Langer Druck oeffnet die Einrichtung. Ohne das kommt man an die
  // Serveradresse nur ueber ein USB-Kabel oder indem man das WLAN abschaltet:
  // beides schlecht, wenn das Geraet am Arbeitsplatz steht.
  const bool aufDerUhr = ansicht == 0 && startX < 150 && startY > KOPF_H && startY < 120;
  if (lag_an && !aufDerUhr && millis() - startZeit > 6000) {
    lag_an = false;
    tft.fillScreen(C_GRUND);
    tft.setFreeFont(S_GROSS);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(C_TEXT, C_GRUND);
    tft.drawString("Einrichtung", BREIT / 2, 104);
    tft.setFreeFont(S_NORMAL);
    tft.setTextColor(C_GEDAEMPFT, C_GRUND);
    tft.drawString("WLAN verbinden mit Jana-Display", BREIT / 2, 140);
    WiFiManager wm;
    WiFiManagerParameter feldUrl("url", "Mia OS Adresse", basisUrl, sizeof(basisUrl) - 1);
    wm.addParameter(&feldUrl);
    wm.setConfigPortalTimeout(300);
    wm.startConfigPortal("Jana-Display");
    strncpy(basisUrl, feldUrl.getValue(), sizeof(basisUrl) - 1);
    basisUrl[sizeof(basisUrl) - 1] = '\0';
    merker.begin("jana", false);
    merker.putString("url", basisUrl);
    merker.end();
    ESP.restart();
  }

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
    // Raphmoment: dreimal in unter einer Sekunde auf dieselbe Stelle ist
    // das Display-Aequivalent zum Schnipsen. Regel 61.
    {
      static int schnipsX = -100, schnipsY = -100;
      const uint32_t jetztMs = millis();
      if (jetztMs - eier.letzterSchnips < 700 && abs(startX - schnipsX) < 20 &&
          abs(startY - schnipsY) < 20) {
        eier.raphStufe++;
      } else {
        eier.raphStufe = 1;
      }
      eier.letzterSchnips = jetztMs;
      schnipsX = startX;
      schnipsY = startY;
      if (eier.raphStufe >= 3) {
        eier.raphBis = jetztMs + 4000;
        neuZeichnen = true;
        if (eier.raphStufe >= 4) {
          // Extracted: auf die Homelab-Seite verlegt, egal wo man war.
          tippZiel = 3;
          eier.raphStufe = 0;
        }
        return 0;
      }
    }

    // In der Kammer: Tippen zeigt die naechste Regel.
    if (ansicht == KAMMER) {
      eier.regelNr++;
      neuZeichnen = true;
      return 0;
    }

    // Steht der Update-Dialog, gehoert jeder Tipp ihm. Ein Seitenwechsel
    // unter einem offenen Dialog waere verwirrend.
    if (neuling.gefragt) {
      // Dieselben Masse wie beim Zeichnen, mit etwas Luft nach oben und
      // unten: der resistive Touch trifft selten pixelgenau.
      if (startY >= DLG_KY - 6 && startY <= DLG_KY + DLG_KH + 6) {
        if (startX >= DLG_K1 && startX <= DLG_K1 + DLG_KB) {
          antwort = 1;  // Ja
        } else if (startX >= DLG_K2 && startX <= DLG_K2 + DLG_KB) {
          antwort = 2;  // Spaeter
        } else if (startX >= DLG_K3 && startX <= DLG_K3 + DLG_KB) {
          antwort = 3;  // Nein
        }
      }
      return 0;
    }

    // Liegt ein Hinweis auf dem Tisch, gehoeren die Knoepfe ihm.
    if (briefing.hinweiseAnzahl > 0 && startY >= HW_KY - 6 && startY <= HW_KY + HW_KH + 6) {
      if (startX >= HW_K1 && startX <= HW_K1 + HW_KB)
        hinweisAntwort = 1;
      else if (startX >= HW_K2 && startX <= HW_K2 + HW_KB)
        hinweisAntwort = 2;
      if (hinweisAntwort)
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
      // Fuenfmal auf den Seitennamen links oben, in unter drei Sekunden:
      // die Kammer. Regel 62.
      if (startX < 120) {
        const uint32_t jetztMs = millis();
        eier.kopfTipps = (jetztMs - eier.letzterKopfTipp < 700) ? eier.kopfTipps + 1 : 1;
        eier.letzterKopfTipp = jetztMs;
        if (eier.kopfTipps >= 5) {
          eier.kopfTipps = 0;
          tippZiel = KAMMER;
        }
      }
      return 0;
    }

    // Auf der Heute-Seite auf eine Faellig-Zeile: Begrabenes zurueckholen.
    if (ansicht == 1 && briefing.hinweiseAnzahl == 0) {
      for (int i = 0; i < faelligZeilen; i++) {
        if (startY >= faelligY[i] - 3 && startY < faelligY[i] + 15) {
          faelligTipp = i;
          return 0;
        }
      }
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
  tft.setFreeFont(S_GROSS);
  tft.setTextDatum(TC_DATUM);
  tft.setTextColor(C_TEXT, C_GRUND);
  tft.drawString("Einrichten", BREIT / 2, 40);
  tft.setFreeFont(S_NORMAL);
  tft.setTextColor(C_GEDAEMPFT, C_GRUND);
  tft.drawString("WLAN verbinden mit", BREIT / 2, 84);
  tft.setFreeFont(S_GROSS);
  tft.setTextColor(C_AKZENT, C_GRUND);
  tft.drawString(wm->getConfigPortalSSID(), BREIT / 2, 106);
  tft.setFreeFont(S_NORMAL);
  tft.setTextColor(C_GEDAEMPFT, C_GRUND);
  tft.drawString("dann Adresse von Mia OS eintragen", BREIT / 2, 152);
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
  // 0,8 Sekunden lang, in der kleinsten Schrift, nur beim Neustart. Dann
  // "Mia OS" drueber. Wer nicht hinsieht, sieht es nicht.
  tft.setFreeFont(S_KLEIN);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(C_LINIE, C_GRUND);
  tft.drawString("Arbeitszeitbetrug Central", BREIT / 2, HOCH / 2);
  delay(800);
  tft.fillRect(0, HOCH / 2 - 20, BREIT, 40, C_GRUND);
  tft.setFreeFont(S_GROSS);
  tft.setTextColor(C_GEDAEMPFT, C_GRUND);
  tft.drawString("Mia OS", BREIT / 2, HOCH / 2);

  touchSPI.begin(TOUCH_CLK, TOUCH_MISO, TOUCH_MOSI, TOUCH_CS);
  touch.begin(touchSPI);
  // Der Touch dreht sich nicht mit dem Display: er ist ein eigener Baustein
  // an einem eigenen Bus und braucht dieselbe Drehung noch einmal.
  touch.setRotation(3);

  // Was zuletzt eingerichtet wurde, schlaegt die einkompilierte Vorgabe.
  merker.begin("jana", true);
  const String gemerkteUrl = merker.getString("url", "");
  merker.end();
  if (!gemerkteUrl.isEmpty()) {
    strncpy(basisUrl, gemerkteUrl.c_str(), sizeof(basisUrl) - 1);
    basisUrl[sizeof(basisUrl) - 1] = '\0';
  }

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
    // Dauerhaft ablegen, sonst ist die Adresse nach dem naechsten Start weg.
    merker.begin("jana", false);
    merker.putString("url", basisUrl);
    merker.end();
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

  // Was Mia zuletzt abgelehnt hat, gilt weiter. Und der Zaehler.
  merker.begin("jana", true);
  neuling.abgelehnt = merker.getString("abgelehnt", "");
  doah = merker.getUInt("doah", 0);
  merker.end();

  regelnHolen();

  // Gleich beim Start nachsehen, nicht erst beim naechsten Abruf.
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
        tft.setFreeFont(S_GROSS);
        tft.setTextDatum(MC_DATUM);
        tft.setTextColor(C_TEXT, C_GRUND);
        tft.drawString("Neustart", BREIT / 2, 120);
        delay(800);
        ESP.restart();
      }
      // Fehlgeschlagen: kurz zeigen, warum, dann weitermachen wie bisher.
      tft.fillScreen(C_GRUND);
      tft.setFreeFont(S_NORMAL);
      tft.setTextDatum(MC_DATUM);
      tft.setTextColor(C_FEHLER, C_GRUND);
      tft.drawString("Update fehlgeschlagen", BREIT / 2, 108);
      tft.setTextColor(C_GEDAEMPFT, C_GRUND);
      tft.drawString(passend(neuling.fehler, BREIT - 24), BREIT / 2, 134);
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

  // Der Stein faellt bei jeder Beruehrung wieder ab.
  if (eier.stein && (wisch != 0 || tippZiel >= 0 || touch.touched())) {
    eier.stein = false;
    neuZeichnen = true;
  }

  if (halteZiel == 1) {
    eier.stein = true;
    neuZeichnen = true;
    letzteBeruehrung = 0;  // sonst faellt er sofort wieder ab
  } else if (halteZiel == 2 && halteIndex >= 0 && halteIndex < briefing.faelligAnzahl) {
    // Erledigt: PATCH an Mia OS, dann sofort neu holen, damit die Zeile
    // verschwindet und "offen" runterzaehlt.
    const int id = briefing.faelligId[halteIndex];
    if (id > 0 && senden("/api/sammlung/" + String(id),
                         "{\"eigenschaft\":\"status\",\"wert\":\"fertig\"}", "PATCH")) {
      led(false, true, false);
      delay(150);
      led(false, false, false);
      briefingHolen();
    }
    neuZeichnen = true;
  }

  if (faelligTipp >= 0 && briefing.begraben[faelligTipp]) {
    // Aus der Friedhofsgaertnerei zurueckholen: Datum auf heute setzen.
    const int id = briefing.faelligId[faelligTipp];
    if (id > 0 && senden("/api/sammlung/" + String(id),
                         "{\"datum\":\"" + briefing.datum + "\"}", "PATCH"))
      briefingHolen();
    neuZeichnen = true;
  }

  if (hinweisAntwort != 0 && briefing.hinweisId > 0) {
    // Regel 39: viermal derselbe Knopf in Folge ist Fame oder Shame. Der
    // erste Tipp zaehlt schon, gesendet wird beim Loslassen der Serie,
    // also nach 1,2 s ohne weiteren Tipp.
    if (hinweisAntwort == hinweisKnopf && millis() - hinweisTippZeit < 1200)
      hinweisTipps++;
    else
      hinweisTipps = 1;
    hinweisKnopf = hinweisAntwort;
    hinweisTippZeit = millis();
    neuZeichnen = true;
  }
  if (hinweisKnopf != 0 && millis() - hinweisTippZeit > 1200) {
    String antwortText = hinweisKnopf == 1 ? "ok" : "spaeter";
    if (hinweisTipps >= 4)
      antwortText = hinweisKnopf == 1 ? "fame" : "shame";
    senden("/api/hinweise/" + String(briefing.hinweisId) + "/" + antwortText, "{}");
    hinweisKnopf = 0;
    hinweisTipps = 0;
    briefingHolen();
    neuZeichnen = true;
  }

  if (ansicht == KAMMER && wisch != 0) {
    // Aus der Kammer fuehrt jeder Wisch zurueck auf die erste Seite, als
    // waere nichts gewesen.
    ansicht = 0;
    uebergang(wisch);
    neuZeichnen = true;
  } else if (wisch != 0 && ansicht == 0 && wisch < 0) {
    // Wisch gegen die Wand: DOAH. Zaehlt hoch, blaettert nicht.
    doah++;
    eier.wischWand++;
    merker.begin("jana", false);
    merker.putUInt("doah", doah);
    merker.end();
    neuZeichnen = true;
  } else if (wisch != 0) {
    // Dreimal schnell hin und her waehrend ein Termin laeuft: Schere.
    static uint32_t letzterWisch = 0;
    static int wischSerie = 0;
    wischSerie = (millis() - letzterWisch < 900) ? wischSerie + 1 : 1;
    letzterWisch = millis();
    if (wischSerie >= 3 && laeuftGerade()) {
      eier.schereBis = millis() + 5000;
      wischSerie = 0;
      ansicht = 0;
    } else {
      ansicht = (ansicht + wisch + ANSICHTEN) % ANSICHTEN;
    }
    uebergang(wisch);
    neuZeichnen = true;
  } else if (tippZiel >= 0 && tippZiel != ansicht) {
    uebergang(tippZiel > ansicht ? 1 : -1);
    ansicht = tippZiel;
    neuZeichnen = true;
  }

  // Schere und Raph laufen ab, dann wird normal weitergezeichnet.
  static bool schereStand = false, raphStand = false;
  const bool schereJetzt = millis() < eier.schereBis;
  const bool raphJetzt = millis() < eier.raphBis;
  if (schereJetzt != schereStand || raphJetzt != raphStand) {
    schereStand = schereJetzt;
    raphStand = raphJetzt;
    neuZeichnen = true;
  }

  // Zugentgleisung: ein Termin hat vor unter drei Minuten angefangen und
  // seitdem hat niemand das Geraet angefasst.
  {
    const Termin *l = laeuftGerade();
    const int jm = jetztMinuten();
    if (l && jm >= 0 && jm - l->beginnMin >= 2 && jm - l->beginnMin < 3 &&
        eier.zugTerminMin != l->beginnMin && ansicht == 0 && !neuling.gefragt &&
        briefing.hinweiseAnzahl == 0 && millis() - letzteBeruehrung > 180000) {
      eier.zugTerminMin = l->beginnMin;
      eier.zugStart = millis();
    }
    if (eier.zugStart && ansicht == 0)
      zugZeichnen();
    else if (eier.zugStart)
      eier.zugStart = 0;
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
    // "vor X min" sind dann die eigentliche Information. Nur nicht, solange
    // der Update-Dialog steht, sonst ist er nach zwei Minuten weg.
    neuZeichnen = !neuling.gefragt;
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
    // Nicht neu zeichnen, solange der Dialog steht: die tickende Uhr haette
    // ihn sonst jede Minute uebermalt.
    if (ansicht == 0 && !neuling.gefragt)
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
    else if (briefing.hinweiseAnzahl > 0 && ansicht != KAMMER)
      hinweisZeichnen();
    if (millis() < eier.raphBis && ansicht != KAMMER)
      raphZeichnen();
  }

  // Fragen, sobald etwas bereitsteht.
  //
  // Hier stand einmal eine Bedingung "nur wenn gerade nichts laeuft": keine
  // Stoerung im Homelab und kein laufender Termin. Gut gemeint, in der Praxis
  // eine Sperre, die nie aufgeht. Mias Arbeitstag ist von 7:30 bis 16:45
  // lueckenlos mit Terminen belegt, und ein Dienst ist fast immer unten.
  // Der Dialog erschien deshalb kein einziges Mal.
  //
  // Die Unterbrechung regelt ohnehin der Knopf "Spaeter". Wer entscheidet,
  // ob gerade ein guter Moment ist, ist Mia und nicht das Geraet.
  if (!neuling.gefragt && !neuling.version.isEmpty() && briefing.hinweiseAnzahl == 0 &&
      ansicht != KAMMER && millis() > neuling.spaeterBis) {
    neuling.gefragt = true;
    updateDialogZeichnen();
  }

  delay(20);
}
