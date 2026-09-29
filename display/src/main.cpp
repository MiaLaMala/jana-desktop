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
#include "gesicht.h"
#include <ESP32Ping.h>

// --- Farben aus Mia OS -----------------------------------------------------
//
// farben.h wird in Mia OS aus farben.json erzeugt (scripts/farben_bauen.py) und
// hierher kopiert. Nicht von Hand aendern: bis 0.2 hatte das Display ein eigenes,
// kaeltere Palette, und genau das sah nicht nach Mia OS aus.
#include "farben.h"

// Kalenderfarben auf dem Zeitlineal. Gedaempft, damit sie nicht mit Rot
// (jetzt), Orange (faellig) und Gruen (laeuft) verwechselt werden.
#define C_KAL_ARBEIT 0x6C76  // #6f8db3, ruhiges Blau
#define C_KAL_PRIVAT 0x9436  // #9687b5, gedaempftes Violett
#define C_KAL_WOHNEN C_LEISE
#define C_KAL_SONST C_LEISE

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
  // Alle faelligen, nicht nur die sechs, die als Zeile Platz haben.
  int faelligGesamt = 0;
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

// --- Das Gesicht ----------------------------------------------------------
//
// Seit 0.2.0 gibt es zwei Themes in einer Firmware. "Seiten" ist der Stand
// 0.1.10: Kopfzeile, Uhr, Kaesten. "Gesicht" ersetzt die erste Seite durch
// zwei Augen und einen Satz; Heute, Morgen und Homelab bleiben per Wischen
// erreichbar. Die Einstellungen liegen im NVS und kommen ab 0.2.x von Mia OS.

bool themeGesicht = true;
GesichtEinstellung gesichtEinst = {0x5E5F, true, true};  // #5ac8fa Hellblau
// Feierabend-Zeitraum in vollen Stunden, von Mia in Mia OS gesetzt.
int feierabendAb = 16;
int feierabendBis = 7;
// Ob die Augen gerade auf dem Panel stehen. Alles, was darueber malt
// (Dialoge, Uebergaenge, andere Seiten), setzt das auf false, dann wird
// beim naechsten Zeichnen der Grund geraeumt und das Gesicht neu gesetzt.
bool gesichtSteht = false;
// Gemessen beim Start: Mikrosekunden fuer ein Bild beider Augen.
uint32_t bildUs = 0;
uint32_t startZeitpunkt = 0;
Preferences merker;
extern bool neuZeichnen;

bool gesichtAktiv() { return themeGesicht && ansicht == 0; }

// Der Stand der Einstellungen, den Mia OS zuletzt gemeldet hat. Ein
// eigener Task haelt dauerhaft eine Anfrage offen; der Server antwortet in
// dem Moment, in dem Mia etwas speichert. So wirkt eine Aenderung in unter
// einer Sekunde statt beim naechsten Zwei-Minuten-Abruf. Der Server kann
// das Geraet nicht selbst anrufen: es steht hinter dem NAT des Pi.
volatile int einstellungsStand = 0;
// Uebergabe vom Warte-Task an loop(): der Task parst, loop() uebernimmt.
// Beide fassen die Anzeige an, deshalb laeuft das Uebernehmen in loop().
JsonDocument wartendeEinstellung;
volatile bool einstellungWartet = false;

void geraetEinstellen(JsonObject g);

/** Name der Augenfarbe aus Mia OS in RGB565. */
uint16_t augenfarbe(const String &name) {
  if (name == "gruen") return C_GUT;
  if (name == "lila") return 0xB59F;
  if (name == "rot") return C_AKZENT;
  return 0x5E5F;  // hellblau
}

/**
 * Einstellungen aus dem Briefing uebernehmen. Was sich geaendert hat, wird
 * ins NVS geschrieben, damit es den naechsten Start ohne Server uebersteht.
 * Bleibt alles gleich, wird nichts geschrieben: der Flash mag keine
 * Schreibzugriffe im Zwei-Minuten-Takt.
 */
void geraetEinstellen(JsonObject g) {
  if (g.isNull())
    return;
  const bool theme = String(g["theme"] | "gesicht") != "seiten";
  GesichtEinstellung neu;
  neu.farbe = augenfarbe(String(g["augenfarbe"] | "hellblau"));
  neu.blinzeln = String(g["blinzeln"] | "1") == "1";
  neu.umherschauen = String(g["umherschauen"] | "1") == "1";
  const int ab = String(g["feierabend_ab"] | "16").toInt();
  const int bis = String(g["feierabend_bis"] | "7").toInt();

  const bool anders = theme != themeGesicht || neu.farbe != gesichtEinst.farbe ||
                      neu.blinzeln != gesichtEinst.blinzeln ||
                      neu.umherschauen != gesichtEinst.umherschauen || ab != feierabendAb ||
                      bis != feierabendBis;
  if (!anders)
    return;
  if (theme != themeGesicht) {
    gesichtSteht = false;
    ansicht = 0;
  }
  themeGesicht = theme;
  gesichtEinst = neu;
  feierabendAb = ab;
  feierabendBis = bis;
  gesichtEinstellen(gesichtEinst);
  merker.begin("jana", false);
  merker.putUChar("theme", themeGesicht ? 1 : 0);
  merker.putUShort("augfarbe", gesichtEinst.farbe);
  merker.putUChar("blinzeln", gesichtEinst.blinzeln ? 1 : 0);
  merker.putUChar("schauen", gesichtEinst.umherschauen ? 1 : 0);
  merker.putUChar("feierab", feierabendAb);
  merker.putUChar("feierbis", feierabendBis);
  merker.end();
  Serial.println("[jana-display] Einstellungen von Mia OS uebernommen");
  neuZeichnen = true;
}

/**
 * Vor einem Abruf: Pupillen nach oben rechts, wie jemand, der nachdenkt.
 * Ersetzt den Ladebalken. Die 360 ms sind die Zeit, die die Pupille fuer
 * den Weg braucht; der Abruf selbst blockiert danach ohnehin.
 */
void gesichtDenken() {
  if (!gesichtAktiv() || !gesichtSteht)
    return;
  gesichtZustand(G_DENKEN);
  const uint32_t bis = millis() + 360;
  while (millis() < bis) {
    gesichtTakt();
    delay(15);
  }
}

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

void einstellungenWarten(void *) {
  for (;;) {
    if (WiFi.status() != WL_CONNECTED || neuling.laeuft || einstellungWartet) {
      delay(1000);
      continue;
    }
    HTTPClient http;
    http.setTimeout(70000);
    http.setConnectTimeout(5000);
    const String pfad = String(basisUrl) + "/api/geraete/einstellungen?warten=55&seit=" +
                        String(einstellungsStand);
    if (!http.begin(pfad)) {
      delay(5000);
      continue;
    }
    const int code = http.GET();
    if (code == 200) {
      JsonDocument doc;
      if (!deserializeJson(doc, http.getStream())) {
        const int stand = doc["stand"] | 0;
        if (stand != einstellungsStand) {
          einstellungsStand = stand;
          wartendeEinstellung = doc["geraet"];
          einstellungWartet = true;
        }
      }
      http.end();
    } else {
      http.end();
      delay(5000);  // Server weg: nicht im Kreis haemmern
    }
  }
}


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
                WiFi.macAddress() + "&name=Jana-Display&version=" + FIRMWARE_VERSION +
                // Die gemessene Bildzeit der Augen faehrt mit: so laesst sie
                // sich ohne Kabel in /api/geraete ablesen.
                "&bild_us=" + String(bildUs) + "&heap=" + String(ESP.getFreeHeap());
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
  filter["geraet"] = true;

  JsonDocument doc;
  if (!holen("/api/briefing", doc, filter))
    return false;

  geraetEinstellen(doc["geraet"].as<JsonObject>());

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
    // Ueberfaellig heisst: Datum liegt vor heute. Beide sind ISO-Daten,
    // also reicht ein Zeichenvergleich, ohne Kalenderrechnung.
    const String datum = f["datum"].as<String>();
    const bool ueber =
        datum.length() == 10 && frisch.datum.length() == 10 && datum < frisch.datum;
    frisch.faelligGesamt++;
    if (frisch.faelligAnzahl >= MAX_ZEILEN)
      continue;
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
  const char *kurz[] = {"RAM", "CPU", "Antwort", "Platte"};
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


/**
 * Die Startseite (0.3, Entwurf C2 vom 29.09.2026): oben links die Uhr, rechts
 * drei kurze Fakten, unten der Arbeitstag als Lineal mit roter Jetzt-Linie.
 *
 * Keine Kaesten, keine Kopfzeile. Rot steht nur an dem, was jetzt dran ist
 * (laufender Termin, Jetzt-Linie), Orange nur an Faelligem. Alles andere ist
 * warmes Neutral aus Mia OS.
 */
const int LINEAL_Y = 176;             // Grundlinie der Skala
const int LINEAL_L = 14, LINEAL_R = BREIT - 14;

/** Minute des Tages auf die x-Achse des Lineals. */
int linealX(int minute, int von, int bis) {
  if (minute < von) minute = von;
  if (minute > bis) minute = bis;
  return LINEAL_L + (int)((long)(minute - von) * (LINEAL_R - LINEAL_L) / (bis - von));
}

/** Label klein und leise, Wert darunter, optional eine kleine Zeile dazu. Ein Fakt, keine Kachel. */
void fakt(int x, int y, int breite, const String &label, const String &wert, uint16_t farbe,
          const String &dazu = "") {
  tft.setTextDatum(TL_DATUM);
  tft.setFreeFont(S_KLEIN);
  tft.setTextColor(C_LEISE, C_GRUND);
  tft.drawString(passend(label, breite), x, y);
  // Passt der Wert nicht, erst die kleinere Schrift, dann kuerzen: ein ganzes
  // "Netzwerk-Doku" klein ist besser als ein grosses "Netzwerk-Do.".
  tft.setFreeFont(S_NORMAL);
  if (tft.textWidth(wert) > breite) {
    tft.setFreeFont(S_KLEIN);
    y += 3;
  }
  tft.setTextColor(farbe, C_GRUND);
  tft.drawString(passend(wert, breite), x, y + 13);
  if (dazu.length()) {
    tft.setFreeFont(S_KLEIN);
    tft.drawString(passend(dazu, breite), x, y + 32);
  }
}

void jetztZeichnen() {
  struct tm jetzt;
  const bool zeitDa = getLocalTime(&jetzt, 50);
  const int jetztMin = jetztMinuten();
  const Termin *laeuft = laeuftGerade();
  const Termin *naechste = kommtAlsNaechstes();

  // --- Uhr und Datum, links oben ---------------------------------------
  char uhr[6] = "--:--";
  if (zeitDa)
    strftime(uhr, sizeof(uhr), "%H:%M", &jetzt);
  if (eier.stein) {
    // Zehn Sekunden auf die Uhr gehalten: die Uhr ist jetzt ein Stein.
    tft.setSwapBytes(true);
    tft.pushImage(40, 2, SPRITE_B, SPRITE_B, SPRITE_STEIN);
  } else {
    tft.setFreeFont(S_RIESIG);
    tft.setTextDatum(TL_DATUM);
    // Regel 67. Um 06:07 und 16:07 ist die Uhr kurz rot.
    tft.setTextColor(istSiebenundsechzig(uhr) ? C_AKZENT : C_TEXT, C_GRUND);
    tft.drawString(uhr, 8, 12);
  }
  if (zeitDa) {
    static const char *tage[] = {"Sonntag", "Montag", "Dienstag", "Mittwoch",
                                 "Donnerstag", "Freitag", "Samstag"};
    char zeile[32];
    snprintf(zeile, sizeof(zeile), "%s, %d. %d.", tage[jetzt.tm_wday], jetzt.tm_mday,
             jetzt.tm_mon + 1);
    tft.setFreeFont(S_NORMAL);
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(C_GEDAEMPFT, C_GRUND);
    tft.drawString(zeile, 11, 66);
  }

  // --- Drei Fakten, rechts oben ----------------------------------------
  // Die Uhr ist hoechstens 173 px breit ("04:44"), die Fakten beginnen danach.
  const int fx = 196, fb = BREIT - fx - 6;
  if (millis() < eier.schereBis)
    fakt(fx, 8, fb, "jetzt", "ey schere", C_AKZENT);
  else if (laeuft)
    fakt(fx, 8, fb, "jetzt", laeuft->titel, C_AKZENT,
         "noch " + alsDauer(laeuft->endeMin - jetztMin));
  else if (naechste)
    // Unter einer Viertelstunde ist der naechste Termin das, was jetzt dran
    // ist: Rot. Orange bleibt Faelligem vorbehalten.
    fakt(fx, 8, fb, "gleich", naechste->titel,
         naechste->beginnMin - jetztMin <= 15 ? C_AKZENT : C_TEXT,
         "in " + alsDauer(naechste->beginnMin - jetztMin));
  else
    fakt(fx, 8, fb, "heute", "Nichts mehr.", C_GEDAEMPFT);

  if (briefing.faelligGesamt > 0) {
    const int n = briefing.faelligGesamt;
    // Nur die Zahl. Wie viel davon ueberfaellig ist, steht auf der
    // Heute-Seite; hier waere es eine Mahnung bei jedem Blick.
    fakt(fx, 58, fb, "fällig", String(n) + (n == 1 ? " Aufgabe" : " Aufgaben"), C_ACHTUNG);
  } else {
    fakt(fx, 58, fb, "fällig", "nichts", C_GEDAEMPFT);
  }

  if (!homelab.gueltig)
    fakt(fx, 104, fb, "Homelab", "keine Daten", C_LEISE);
  else if (stoerungAktiv() && homelab.stoerAnzahl > 0)
    fakt(fx, 104, fb, "Homelab, weg:",
         homelab.stoerAnzahl > 1 ? homelab.stoerung[0] + " +" + String(homelab.stoerAnzahl - 1)
                                 : homelab.stoerung[0],
         C_FEHLER);
  else
    fakt(fx, 104, fb, "Homelab", "alles läuft", C_GEDAEMPFT);

  // --- Das Lineal ------------------------------------------------------
  // Spanne: vom ersten bis zum letzten Termin des Tages, mindestens 07:30
  // bis 16:45 (ein Arbeitstag), auf volle Stunden gerundet.
  int von = 7 * 60 + 30, bis = 16 * 60 + 45;
  for (int i = 0; i < briefing.heuteAnzahl; i++) {
    const Termin &t = briefing.heute[i];
    if (t.beginnMin >= 0 && t.beginnMin < von) von = t.beginnMin;
    if (t.endeMin > bis) bis = t.endeMin;
  }
  von = von / 60 * 60;
  bis = (bis + 59) / 60 * 60;
  const bool linealDa = jetztMin >= 0;
  const int jx = linealDa ? linealX(jetztMin, von, bis) : -1;

  // Termine als flache Balken ueber der Skala. Ueberschneidungen rutschen
  // eine Spur hoeher, damit keiner den anderen verdeckt.
  int spurEnde[3] = {-1, -1, -1};
  for (int i = 0; i < briefing.heuteAnzahl; i++) {
    const Termin &t = briefing.heute[i];
    if (t.beginnMin < 0)
      continue;  // ganztaegig: gehoert nicht auf eine Uhrzeit
    const int ende = t.endeMin > t.beginnMin ? t.endeMin : t.beginnMin + 30;
    int spur = 0;
    while (spur < 2 && spurEnde[spur] > t.beginnMin)
      spur++;
    spurEnde[spur] = ende;
    const int x1 = linealX(t.beginnMin, von, bis), x2 = linealX(ende, von, bis);
    const bool aktiv = &t == laeuft;
    const bool vorbei = ende <= jetztMin;
    tft.fillRect(x1, LINEAL_Y - 16 - spur * 8, max(x2 - x1 - 1, 2), 5,
                 aktiv ? C_AKZENT : vorbei ? C_LINIE : t.farbe);
  }

  // Skala: Grundlinie, kleine Striche je Viertelstunde, grosse je Stunde.
  // Links der Jetzt-Linie leiser: der Teil des Tages ist gelaufen.
  for (int m = von; m <= bis; m += 15) {
    const int x = linealX(m, von, bis);
    const bool stunde = m % 60 == 0;
    const uint16_t f = linealDa && x < jx ? C_LINIE_WEICH : (stunde ? C_LEISE : C_LINIE);
    tft.drawFastVLine(x, LINEAL_Y - (stunde ? 6 : 3), stunde ? 6 : 3, f);
    // Beschriftung nur jede zweite Stunde, sonst wird es auf 2,8 Zoll Brei.
    if (stunde && (m / 60) % 2 == 0) {
      tft.setFreeFont(S_KLEIN);
      tft.setTextDatum(TC_DATUM);
      tft.setTextColor(C_LEISE, C_GRUND);
      tft.drawString(String(m / 60), x, LINEAL_Y + 5);
    }
  }
  tft.drawFastHLine(LINEAL_L, LINEAL_Y, LINEAL_R - LINEAL_L, C_LINIE);
  if (linealDa && jx > LINEAL_L)
    tft.drawFastHLine(LINEAL_L, LINEAL_Y, jx - LINEAL_L, C_LINIE_WEICH);

  // Die Jetzt-Linie, das einzige kraeftige Rot auf dem Bildschirm, wenn
  // gerade nichts laeuft.
  if (linealDa && jetztMin >= von && jetztMin <= bis) {
    // Oben endet sie unter den Fakten (Homelab-Wert bis y 133), sonst
    // schneidet sie nachmittags durch den Text.
    tft.fillRect(jx - 1, LINEAL_Y - 30, 2, 34, C_AKZENT);
    tft.fillTriangle(jx - 4, LINEAL_Y - 34, jx + 3, LINEAL_Y - 34, jx, LINEAL_Y - 30, C_AKZENT);
  }

  // Unten eine leise Zeile: was morgen als erstes kommt. Wer nachmittags
  // rueberschaut, will genau das wissen.
  const Termin *morgen = nullptr;
  for (int i = 0; i < briefing.morgenAnzahl; i++) {
    const Termin &t = briefing.morgen[i];
    if (t.beginnMin >= 0 && (!morgen || t.beginnMin < morgen->beginnMin))
      morgen = &t;
  }
  tft.setFreeFont(S_KLEIN);
  tft.setTextDatum(TL_DATUM);
  // Sind die Daten alt, sagt die Zeile, woran es liegt. Die Startseite hat
  // keinen Kopf mit Punkt, das hier ist ihr einziger Hinweis.
  const bool alt = millis() - letzterErfolg >= VERALTET_MS;
  if (lage != LAGE_OK || alt) {
    const char *grund = lage == LAGE_KEIN_WLAN     ? "Kein WLAN, Daten sind alt."
                        : lage == LAGE_KEIN_TUNNEL ? "Tunnel weg, Daten sind alt."
                        : lage == LAGE_KEIN_SERVER ? "Mia OS antwortet nicht, Daten sind alt."
                                                   : "Daten älter als zehn Minuten.";
    tft.setTextColor(C_ACHTUNG, C_GRUND);
    tft.drawString(grund, 12, 220);
  } else if (morgen) {
    tft.setTextColor(C_LEISE, C_GRUND);
    tft.drawString(passend("morgen " + morgen->zeit + "  " + morgen->titel, BREIT - 24), 12, 220);
  }
}

/** Die Kopfzeile: welche Seite, und wie frisch die Daten sind. */

void kopfZeichnen() {
  static const char *namen[] = {"Jetzt", "Heute", "Morgen", "Homelab"};
  tft.setFreeFont(S_FETT);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(C_TEXT, C_GRUND);
  tft.drawString(namen[ansicht], 12, 8);

  // Die Punkte zeigen, auf welcher Seite man ist. Gleiche x-Werte wie beim
  // Antippen (BREIT - 100 + i * 16), sonst trifft der Finger daneben.
  for (int i = 0; i < ANSICHTEN; i++) {
    const int x = BREIT - 100 + i * 16;
    if (i == ansicht)
      tft.fillRoundRect(x - 5, 15, 12, 5, 2, C_TEXT);
    else
      tft.fillCircle(x, 17, 2, C_LINIE);
  }

  // Dunkel heisst in Ordnung: der Punkt erscheint nur, wenn die Daten alt
  // sind (orange) oder gar keine Verbindung besteht (Fehlerfarbe).
  if (!habenDaten)
    tft.fillCircle(BREIT - 14, 17, 3, C_FEHLER);
  else if (millis() - letzterErfolg >= VERALTET_MS)
    tft.fillCircle(BREIT - 14, 17, 3, C_ACHTUNG);
}

/**
 * Die Terminseiten, heute und morgen, als Liste ohne Kaesten.
 *
 * Eine Zeile je Termin: Kalenderfarbe als Punkt, Uhrzeit, Titel. Vorbei ist
 * leise, was laeuft ist rot und sagt rechts, wie lange noch. Darunter, durch
 * eine Haarlinie getrennt, die faelligen Aufgaben: so viele, wie Platz haben,
 * der Rest als Zahl. Faellig ist orange, auch wenn es ueberfaellig ist: die
 * Liste soll erinnern, nicht schimpfen.
 */
// Wo die Faellig-Zeilen auf der Heute-Seite stehen, fuer das Halten.
const int FAELLIG_H = 17;
int faelligY[MAX_ZEILEN];
int faelligZeilen = 0;

void termineZeichnen() {
  const bool istHeute = ansicht == 1;
  const Termin *liste = istHeute ? briefing.heute : briefing.morgen;
  const int anzahl = istHeute ? briefing.heuteAnzahl : briefing.morgenAnzahl;
  const int gesamt = istHeute ? briefing.heuteGesamt : briefing.morgenGesamt;
  const Termin *laeuft = istHeute ? laeuftGerade() : nullptr;
  const int jetztMin = istHeute ? jetztMinuten() : -1;
  const int ZEILE = 22;

  int y = 40;
  tft.setTextDatum(TL_DATUM);
  if (anzahl == 0) {
    tft.setFreeFont(S_NORMAL);
    tft.setTextColor(C_GEDAEMPFT, C_GRUND);
    tft.drawString(istHeute ? "Keine Termine heute." : "Morgen ist nichts eingetragen.", 12, y);
    y += ZEILE;
  }
  for (int i = 0; i < anzahl; i++) {
    const Termin &t = liste[i];
    const bool aktiv = &t == laeuft;
    const bool vorbei = istHeute && t.endeMin >= 0 && jetztMin >= t.endeMin;
    const uint16_t zeitFarbe = aktiv ? C_AKZENT : vorbei ? C_LEISE : C_GEDAEMPFT;
    const uint16_t titelFarbe = vorbei ? C_LEISE : C_TEXT;

    tft.fillCircle(15, y + 8, 2, vorbei ? C_LINIE : t.farbe);
    tft.setFreeFont(S_NORMAL);
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(zeitFarbe, C_GRUND);
    tft.drawString(t.zeit.length() ? t.zeit : "ganz", 26, y);

    int rechts = BREIT - 12;
    if (aktiv) {
      const String rest = "noch " + alsDauer(t.endeMin - jetztMin);
      tft.setFreeFont(S_KLEIN);
      tft.setTextDatum(TR_DATUM);
      tft.setTextColor(C_AKZENT, C_GRUND);
      tft.drawString(rest, rechts, y + 3);
      rechts -= tft.textWidth(rest) + 10;
    } else if (!vorbei && t.ende.length()) {
      tft.setFreeFont(S_KLEIN);
      tft.setTextDatum(TR_DATUM);
      tft.setTextColor(C_LEISE, C_GRUND);
      tft.drawString("bis " + t.ende, rechts, y + 3);
      rechts -= tft.textWidth("bis " + t.ende) + 10;
    }
    tft.setFreeFont(S_NORMAL);
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(titelFarbe, C_GRUND);
    tft.drawString(passend(t.titel, rechts - 84), 84, y);
    y += ZEILE;
  }
  if (gesamt > anzahl) {
    tft.setFreeFont(S_KLEIN);
    tft.setTextColor(C_LEISE, C_GRUND);
    tft.drawString("und " + String(gesamt - anzahl) + " weitere", 84, y);
    y += 16;
  }

  // Faellige Aufgaben. Nur auf der Heute-Seite: was morgen faellig ist,
  // weiss Mia OS heute noch nicht.
  faelligZeilen = 0;
  if (!istHeute || briefing.faelligGesamt == 0)
    return;
  y += 4;
  tft.drawFastHLine(12, y, BREIT - 24, C_LINIE_WEICH);
  y += 8;
  const int platz = (HOCH - 6 - y) / FAELLIG_H;
  const bool rest = briefing.faelligGesamt > platz;
  const int zeigen = min(briefing.faelligAnzahl, rest ? platz - 1 : platz);
  tft.setFreeFont(S_KLEIN);
  tft.setTextDatum(TL_DATUM);
  for (int i = 0; i < zeigen; i++) {
    faelligY[i] = y;
    faelligZeilen = i + 1;
    tft.fillCircle(15, y + 7, 2, C_ACHTUNG);
    // Regel 21: was ueber 30 Tage liegt, wurde exportiert. Halten holt es
    // zurueck, das ist die Funktion hinter dem Witz.
    if (briefing.begraben[i]) {
      tft.setTextColor(C_LEISE, C_GRUND);
      tft.drawString(passend("als .zip in die Friedhofsgärtnerei exportiert", BREIT - 38), 26, y);
    } else {
      tft.setTextColor(C_TEXT, C_GRUND);
      tft.drawString(passend(briefing.faellig[i], BREIT - 38), 26, y);
    }
    y += FAELLIG_H;
  }
  if (rest) {
    tft.setTextColor(C_ACHTUNG, C_GRUND);
    tft.drawString("und " + String(briefing.faelligGesamt - zeigen) + " weitere fällig", 26, y);
  }
}

/**
 * Die Homelab-Seite: erst der Satz, dann was kaputt ist, dann vier Zahlen.
 *
 * Bis 0.2 stand hier eine riesige Zahl mit einem Punkteraster. Das war die
 * Dashboard-Schablone, die nach KI aussieht; "46 von 48" sagt der Satz
 * darunter genauso, und welcher Dienst fehlt, steht direkt dabei.
 */
void homelabZeichnen() {
  tft.setTextDatum(TL_DATUM);
  if (!homelab.gueltig) {
    tft.setFreeFont(S_GROSS);
    tft.setTextColor(C_GEDAEMPFT, C_GRUND);
    tft.drawString("Keine Homelab-Daten.", 12, 44);
    return;
  }
  const bool stoerung = stoerungAktiv();
  String satz = "Alles läuft.";
  if (stoerung && homelab.stoerAnzahl == 1)
    satz = homelab.stoerung[0] + " ist weg.";
  else if (stoerung && homelab.stoerAnzahl > 1)
    satz = String(homelab.gesamt - homelab.oben) + " Dienste sind weg.";
  tft.setFreeFont(S_GROSS);
  tft.setTextColor(stoerung ? C_FEHLER : C_TEXT, C_GRUND);
  tft.drawString(passend(satz, BREIT - 24), 12, 42);

  tft.setFreeFont(S_KLEIN);
  tft.setTextColor(C_GEDAEMPFT, C_GRUND);
  String unter = String(homelab.oben) + " von " + String(homelab.gesamt) + " Diensten oben, " +
                 String(homelab.uptime, homelab.uptime < 99.995f ? 2 : 0) + " % in 24 h";
  unter.replace(".", ",");
  tft.drawString(passend(unter, BREIT - 24), 12, 70);

  int y = 94;
  if (stoerung) {
    for (int i = 0; i < homelab.stoerAnzahl && y < 150; i++) {
      tft.fillCircle(15, y + 8, 2, C_FEHLER);
      tft.setFreeFont(S_NORMAL);
      tft.setTextColor(C_TEXT, C_GRUND);
      tft.drawString(passend(homelab.stoerung[i], 110), 26, y);
      tft.setFreeFont(S_KLEIN);
      tft.setTextColor(C_GEDAEMPFT, C_GRUND);
      tft.drawString(passend(homelab.meldung[i], BREIT - 150), 138, y + 3);
      y += 22;
    }
  }

  // Vier Zahlen in einer Reihe, unten. Label leise darueber, Wert darunter.
  const int reihe = HOCH - 44;
  tft.drawFastHLine(12, reihe - 10, BREIT - 24, C_LINIE_WEICH);
  const int spalte = (BREIT - 24) / 4;
  for (int i = 0; i < homelab.zahlAnzahl; i++) {
    const int x = 12 + i * spalte;
    tft.setFreeFont(S_KLEIN);
    tft.setTextColor(C_LEISE, C_GRUND);
    tft.drawString(passend(homelab.zahlLabel[i], spalte - 6), x, reihe);
    String wert = homelab.zahlWert[i];
    wert.replace(".", ",");
    tft.setFreeFont(S_NORMAL);
    tft.setTextColor(C_TEXT, C_GRUND);
    tft.drawString(passend(wert, spalte - 6), x, reihe + 14);
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
                    C_TEXT);

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
  gesichtSteht = false;
  tft.fillRoundRect(DLG_X, DLG_Y, DLG_B, DLG_H, 10, C_ERHOBEN);
  tft.drawRoundRect(DLG_X, DLG_Y, DLG_B, DLG_H, 10, C_LINIE);

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
  tft.setTextColor(C_TEXT, C_ERHOBEN);
  tft.drawString(neuling.version, BREIT / 2 + 4, DLG_Y + 78);

  // Drei Knoepfe nebeneinander. Ja ist hell gefuellt, die anderen nur
  // umrandet: die haeufigste Antwort soll am leichtesten zu treffen sein.
  // Kein Rot, das gehoert dem, was jetzt dran ist.
  tft.fillRoundRect(DLG_K1, DLG_KY, DLG_KB, DLG_KH, 6, C_TEXT);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(C_GRUND, C_TEXT);
  tft.drawString("Ja", DLG_K1 + DLG_KB / 2, DLG_KY + DLG_KH / 2);

  for (int i = 0; i < 2; i++) {
    const int kx = i == 0 ? DLG_K2 : DLG_K3;
    tft.drawRoundRect(kx, DLG_KY, DLG_KB, DLG_KH, 6, C_LINIE);
    tft.setTextColor(C_GEDAEMPFT, C_ERHOBEN);
    tft.drawString(i == 0 ? "Später" : "Nein", kx + DLG_KB / 2, DLG_KY + DLG_KH / 2);
  }
}

/** Regel 67: 06:07, 16:07, oder die Zahl 67 irgendwo in der Uhr. */
bool istSiebenundsechzig(const char *uhr) {
  return strstr(uhr, "6:07") != nullptr || strstr(uhr, "67") != nullptr;
}

// Masse des Hinweis-Kastens. Zeichnen und Treffer teilen sich die Zahlen.
const int HW_X = 20, HW_Y = 36, HW_B = BREIT - 40, HW_H = 156;
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
  gesichtSteht = false;
  tft.fillRoundRect(HW_X, HW_Y, HW_B, HW_H, 10, C_ERHOBEN);
  tft.drawRoundRect(HW_X, HW_Y, HW_B, HW_H, 10, C_LINIE);

  tft.setFreeFont(S_KLEIN);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(C_GEDAEMPFT, C_ERHOBEN);
  tft.drawString(briefing.hinweisVon + (briefing.hinweiseAnzahl > 1
                                            ? "  (+" + String(briefing.hinweiseAnzahl - 1) + ")"
                                            : ""),
                 HW_X + 12, HW_Y + 8);

  // Drei Zeilen Text, an Wortgrenzen umgebrochen. Was dann noch uebrig
  // ist, wird abgeschnitten. Zwei Zeilen reichten nicht: "Soll ich neu
  // starten?" endete als "Soll ich neu start.".
  tft.setFreeFont(S_FETT);
  tft.setTextColor(C_TEXT, C_ERHOBEN);
  String rest = briefing.hinweisText;
  const int breite = HW_B - 24;
  for (int zeile = 0; zeile < 3 && rest.length(); zeile++) {
    String teil = rest;
    while (tft.textWidth(teil) > breite) {
      const int leer = teil.lastIndexOf(' ');
      if (leer <= 0) {
        teil = passend(teil, breite);
        break;
      }
      teil = teil.substring(0, leer);
    }
    if (zeile == 2 && teil.length() < rest.length())
      teil = passend(rest, breite);
    tft.drawString(teil, HW_X + 12, HW_Y + 26 + zeile * 22);
    rest = teil.length() < rest.length() ? rest.substring(teil.length() + 1) : "";
  }

  tft.setFreeFont(S_NORMAL);
  tft.fillRoundRect(HW_K1, HW_KY, HW_KB, HW_KH, 6, C_TEXT);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(C_GRUND, C_TEXT);
  tft.drawString(hinweisKnopf == 1 && hinweisTipps > 1 ? String(hinweisTipps) + "x ok" : "ok",
                 HW_K1 + HW_KB / 2, HW_KY + HW_KH / 2);
  tft.drawRoundRect(HW_K2, HW_KY, HW_KB, HW_KH, 6, C_LINIE);
  tft.setTextColor(C_GEDAEMPFT, C_ERHOBEN);
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
  gesichtSteht = false;
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
  // Im Gesicht-Theme faehrt er unter den Augen durch, nicht durch die Uhr.
  const int y = gesichtAktiv() ? AUGE_UNTEN + 1 : 200;
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

/**
 * Die Gesichtsseite: Augen oben, darunter ein Satz, eine Zeile Kleingedrucktes
 * und die Uhr. Keine Kopfzeile, keine Kaesten. Das Gesicht ist Jana auf dem
 * Tisch; die Zahlen stehen auf den anderen Seiten.
 *
 * Die Augen werden nur neu gesetzt, wenn sie nicht mehr stehen (nach einem
 * Dialog, einer anderen Seite, einem Uebergang). Sonst wird nur der Text
 * darunter neu geschrieben, und die Augen bewegen sich ungestoert weiter.
 */
const int SATZ_Y = 138;

void gesichtSeiteZeichnen() {
  struct tm jetzt;
  const bool zeitDa = getLocalTime(&jetzt, 50);
  const int jetztMin = jetztMinuten();
  const Termin *laeuft = laeuftGerade();
  const Termin *naechste = kommtAlsNaechstes();

  // Stimmung aus der Lage. Schreck und Zwinkern kommen von aussen, hier
  // wird nur der Grundzustand gesetzt.
  //
  // Muede im Feierabend-Zeitraum, den Mia in Mia OS setzt. Der Zeitraum
  // geht ueber Mitternacht, deshalb "oder" statt "und".
  const bool tagVorbei =
      zeitDa && (jetzt.tm_hour >= feierabendAb || jetzt.tm_hour < feierabendBis);
  gesichtZustand(tagVorbei ? G_MUEDE : G_WACH);

  if (!gesichtSteht) {
    tft.fillScreen(C_GRUND);
    gesichtZeichnen();
    gesichtSteht = true;
  } else {
    tft.fillRect(0, AUGE_UNTEN + 2, BREIT, HOCH - AUGE_UNTEN - 2, C_GRUND);
  }

  if (eier.stein) {
    // Gesteinigt: der Stein liegt auf beiden Augen. Bleibt bis zur naechsten
    // Beruehrung, wie auf der Uhr im Seiten-Theme.
    tft.setSwapBytes(true);
    tft.pushImage(AUGE_L_X + (AUGE_B - SPRITE_B) / 2, AUGE_Y + 16, SPRITE_B, SPRITE_B, SPRITE_STEIN);
    tft.pushImage(AUGE_R_X + (AUGE_B - SPRITE_B) / 2, AUGE_Y + 16, SPRITE_B, SPRITE_B, SPRITE_STEIN);
    // Faellt der Stein ab, muessen die Augen darunter neu gesetzt werden.
    gesichtSteht = false;
  }

  String satz, klein;
  uint16_t satzFarbe = C_TEXT;
  if (!habenDaten) {
    switch (lage) {
    case LAGE_KEIN_WLAN: satz = "Kein WLAN."; klein = "Pi aus oder zu weit weg"; break;
    case LAGE_KEIN_TUNNEL: satz = "Tunnel weg."; klein = "Firmennetz. Warten."; break;
    case LAGE_KEIN_SERVER: satz = "Mia OS antwortet nicht."; klein = "Tunnel steht, Server nicht"; break;
    default: satz = "Keine Verbindung."; klein = letzterFehler; break;
    }
    satzFarbe = C_GEDAEMPFT;
  } else if (stoerungAktiv() && homelab.stoerAnzahl > 0) {
    // Eine Stoerung ist wichtig genug fuer den Satz. Der Rest steht auf
    // der Homelab-Seite.
    satz = homelab.stoerung[0] + (homelab.stoerAnzahl > 1 ? " und mehr sind weg." : " ist weg.");
    klein = String(homelab.oben) + " von " + String(homelab.gesamt);
    satzFarbe = C_ACHTUNG;
  } else if (laeuft) {
    satz = laeuft->titel + ". Noch " + alsDauer(laeuft->endeMin - jetztMin) + ".";
    klein = "bis " + laeuft->ende;
    if (naechste)
      klein += "  ·  dann " + naechste->titel;
  } else if (naechste) {
    satz = naechste->titel + " in " + alsDauer(naechste->beginnMin - jetztMin) + ".";
    klein = "um " + naechste->zeit;
  } else if (zeitDa) {
    // Nichts laeuft, nichts kommt mehr. Im Feierabend-Zeitraum heisst das
    // Feierabend, davor einfach ein freier Rest des Tages.
    satz = tagVorbei ? "Feierabend." : "Nichts mehr heute.";
    const Termin *morgen = nullptr;
    for (int i = 0; i < briefing.morgenAnzahl; i++) {
      const Termin &t = briefing.morgen[i];
      if (t.beginnMin >= 0 && (!morgen || t.beginnMin < morgen->beginnMin))
        morgen = &t;
    }
    if (morgen)
      klein = "morgen " + morgen->zeit + "  " + morgen->titel;
  } else {
    satz = "Warte auf die Uhrzeit.";
  }
  // Faelliges steht unten links, nicht mehr in der kleinen Zeile: dort
  // schnitt es den Satz ab ("... dann Mittagspause · 6.").
  if (millis() < eier.schereBis)
    satz = "ey schere.";

  // Passt der Satz nicht, wandert die Dauer in die kleine Zeile. Sonst
  // stand da "Mittagspause. Noch 58." und das "min" fehlte.
  tft.setFreeFont(S_GROSS);
  if (tft.textWidth(satz) > BREIT - 24 && millis() >= eier.schereBis && habenDaten) {
    if (laeuft) {
      satz = laeuft->titel + ".";
      klein = "noch " + alsDauer(laeuft->endeMin - jetztMin) + ", bis " + laeuft->ende;
    } else if (naechste && !stoerungAktiv()) {
      satz = naechste->titel + ".";
      klein = "in " + alsDauer(naechste->beginnMin - jetztMin) + ", um " + naechste->zeit;
    }
  }

  tft.setTextDatum(TC_DATUM);
  tft.setTextColor(satzFarbe, C_GRUND);
  tft.drawString(passend(satz, BREIT - 24), BREIT / 2, SATZ_Y);
  tft.setFreeFont(S_NORMAL);
  tft.setTextColor(C_GEDAEMPFT, C_GRUND);
  tft.drawString(passend(klein, BREIT - 24), BREIT / 2, SATZ_Y + 30);

  // Unten klein die Uhr, links der Daten-Punkt, rechts der Zaehler.
  char uhr[6] = "--:--";
  if (zeitDa)
    strftime(uhr, sizeof(uhr), "%H:%M", &jetzt);
  tft.setFreeFont(S_KLEIN);
  tft.setTextColor(istSiebenundsechzig(uhr) ? C_AKZENT : C_LEISE, C_GRUND);
  tft.drawString(uhr, BREIT / 2, 216);
  if (briefing.faelligGesamt > 0 && habenDaten) {
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(C_ACHTUNG, C_GRUND);
    tft.drawString(String(briefing.faelligGesamt) + " fällig", 12, 216);
  }
  // Dunkel heisst in Ordnung: der Punkt oben links nur bei alten Daten.
  if (!habenDaten || millis() - letzterErfolg >= VERALTET_MS)
    tft.fillCircle(10, 10, 3, habenDaten ? C_ACHTUNG : C_FEHLER);
  tft.setTextDatum(TR_DATUM);
  tft.setTextColor(C_LEISE, C_GRUND);
  tft.drawString(doah > 0 ? "DOAH " + String(doah) : "", BREIT - 12, 216);
}

/** Die ganze Anzeige. Wird nur bei Aenderung gezeichnet, nicht im Takt. */
void anzeigeZeichnen() {
  if (ansicht == KAMMER) {
    gesichtSteht = false;
    kammerZeichnen();
    return;
  }
  if (gesichtAktiv()) {
    gesichtSeiteZeichnen();
    return;
  }
  gesichtSteht = false;
  tft.fillScreen(C_GRUND);
  // Die Startseite traegt ihre Uhr selbst und braucht keinen Rahmen.
  if (ansicht != 0)
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
  case 0: jetztZeichnen(); return;
  case 3: homelabZeichnen(); break;
  default: termineZeichnen(); break;
  }

  // Keine Fusszeile mehr (0.3): "offen 20" in Rot und das Datum als ISO-
  // Zeichenkette waren Rauschen. Der Lage-Hinweis steht im Kopf als Punkt,
  // der Zaehler fuer nichts (Regel 30) klein daneben.
  if (doah > 0) {
    tft.setFreeFont(S_KLEIN);
    tft.setTextDatum(TR_DATUM);
    tft.setTextColor(C_LEISE, C_GRUND);
    tft.drawString("DOAH " + String(doah), BREIT - 112, 11);
  }
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
  // Im Gesicht-Theme liegt der Stein auf den Augen statt auf der Uhr.
  const bool aufDerUhr =
      ansicht == 0 && (gesichtAktiv() ? (startY >= AUGE_Y && startY < AUGE_UNTEN &&
                                         startX >= AUGE_L_X && startX < AUGE_R_X + AUGE_B)
                                      : (startX < 190 && startY < 84));
  if (lag_an && aufDerUhr && !neuling.gefragt && millis() - startZeit > 10000) {
    lag_an = false;
    halteZiel = 1;
    return 0;
  }
  if (lag_an && ansicht == 1 && !neuling.gefragt && briefing.hinweiseAnzahl == 0 &&
      millis() - startZeit > 2000) {
    for (int i = 0; i < faelligZeilen; i++) {
      if (startY >= faelligY[i] - 2 && startY < faelligY[i] + FAELLIG_H) {
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
          if (!themeGesicht)
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
    if (startY < KOPF_H + 8 && !gesichtAktiv()) {
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
        if (startY >= faelligY[i] - 2 && startY < faelligY[i] + FAELLIG_H) {
          faelligTipp = i;
          return 0;
        }
      }
    }
    // Linkes Viertel zurueck, rechtes Viertel vor. Auf einem resistiven
    // Panel trifft ein Tippen zuverlaessiger als ein Wisch.
    if (themeGesicht)
      return 0;
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
  gesichtSteht = false;
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

  // Das Gesicht: Sprites anlegen und einmal messen, wie lange ein Bild
  // beider Augen braucht. Gerechnet waren 6 ms, und "gerechnet" war der
  // Grund, es hier zu messen. Die Zahl steht kurz auf dem Startbild und im
  // Log, damit sie ohne Kabel ablesbar ist.
  merker.begin("jana", true);
  themeGesicht = merker.getUChar("theme", 1) == 1;
  gesichtEinst.farbe = merker.getUShort("augfarbe", 0x5E5F);
  gesichtEinst.blinzeln = merker.getUChar("blinzeln", 1) == 1;
  gesichtEinst.umherschauen = merker.getUChar("schauen", 1) == 1;
  feierabendAb = merker.getUChar("feierab", 16);
  feierabendBis = merker.getUChar("feierbis", 7);
  merker.end();
  if (gesichtStart(gesichtEinst)) {
    bildUs = gesichtMessen(60);
    Serial.printf("[jana-display] Augen: %lu us je Bild, Heap frei %u\n",
                  (unsigned long)bildUs, ESP.getFreeHeap());
    tft.fillRect(0, AUGE_Y, BREIT, AUGE_H, C_GRUND);
    tft.setFreeFont(S_GROSS);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(C_GEDAEMPFT, C_GRUND);
    tft.drawString("Mia OS", BREIT / 2, HOCH / 2);
    tft.setFreeFont(S_KLEIN);
    tft.setTextColor(C_LINIE, C_GRUND);
    char zeile[48];
    snprintf(zeile, sizeof(zeile), "Augen %lu.%lu ms je Bild  ·  %s",
             (unsigned long)(bildUs / 1000), (unsigned long)((bildUs % 1000) / 100),
             FIRMWARE_VERSION);
    tft.drawString(zeile, BREIT / 2, HOCH / 2 + 28);
  } else {
    Serial.println("[jana-display] Augen: kein Speicher fuer Sprites, Seiten-Theme");
    themeGesicht = false;
  }

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

  // Der Warte-Task fuer Einstellungen, auf dem zweiten Kern, damit er
  // die Augen nicht bremst. 8 KB Stapel reichen fuer HTTP plus JSON.
  xTaskCreatePinnedToCore(einstellungenWarten, "einst", 8192, nullptr, 1, nullptr, 0);

  // Gleich beim Start nachsehen, nicht erst beim naechsten Abruf.
  updatePruefen();
  neuZeichnen = true;
}

void loop() {
  const int wisch = wischen();

  // Was der Warte-Task hereingeholt hat, sofort uebernehmen.
  if (einstellungWartet) {
    geraetEinstellen(wartendeEinstellung.as<JsonObject>());
    einstellungWartet = false;
  }

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
      gesichtZwinkern();
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
    if (hinweisTipps >= 4 && hinweisKnopf == 1)
      gesichtZwinkern();
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
  } else if (wisch != 0 && themeGesicht) {
    // Im Gesicht gibt es keine Seiten. Jana steht auf dem Tisch, und wer
    // an ihr wischt, wischt an ihr. Die Schere bleibt (Regel bleibt Regel).
    static uint32_t letzterWischG = 0;
    static int wischSerieG = 0;
    wischSerieG = (millis() - letzterWischG < 900) ? wischSerieG + 1 : 1;
    letzterWischG = millis();
    if (wischSerieG >= 3 && laeuftGerade()) {
      eier.schereBis = millis() + 5000;
      wischSerieG = 0;
      neuZeichnen = true;
    }
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
  } else if (tippZiel >= 0 && tippZiel != ansicht && (!themeGesicht || tippZiel == KAMMER)) {
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
    const bool datenVorher = habenDaten;
    const Lage lageVorher = lage;
    const String updateVorher = neuling.version;
    gesichtDenken();
    briefingHolen();
    homelabHolen();
    // Im selben Takt mitgefragt: Mia OS erfaehrt dabei, dass es dieses
    // Geraet gibt und welche Fassung laeuft.
    updatePruefen();
    // Auch bei Misserfolg neu zeichnen: der Punkt oben rechts und das
    // "vor X min" sind dann die eigentliche Information. Nur nicht, solange
    // der Update-Dialog steht, sonst ist er nach zwei Minuten weg.
    neuZeichnen = !neuling.gefragt;
    if (stoerungAktiv() != stoerungVorher) {
      Serial.printf("[jana-display] Homelab: %d von %d\n", homelab.oben, homelab.gesamt);
      // Neue Stoerung: Schreck. Entwarnung: Zwinkern.
      if (stoerungAktiv())
        gesichtSchreck();
      else
        gesichtZwinkern();
    }
    // Verbindung gerade verloren: das ist auch ein Schreck.
    if (datenVorher && lageVorher == LAGE_OK && lage != LAGE_OK)
      gesichtSchreck();
    if (updateVorher.isEmpty() && !neuling.version.isEmpty())
      gesichtZwinkern();
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

  // Die Augen leben zwischen den Bildern. Nur wenn nichts darueber liegt.
  if (gesichtAktiv() && gesichtSteht && !neuling.gefragt && briefing.hinweiseAnzahl == 0 &&
      !eier.stein)
    gesichtTakt();

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
