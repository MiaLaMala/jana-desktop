/**
 * Das Gesicht: zwei Augen, die auf dem Tisch stehen.
 *
 * Jedes Auge ist ein abgerundetes Rechteck in der Augenfarbe, darin eine
 * Pupille in Grundfarbe. Das Auge selbst steht still, **nur die Pupille
 * bewegt sich** (Mias Entscheidung vom 11.09.2026, kein Schweben). Das Lid
 * ist die Augenform selbst in Grundfarbe und faehrt von oben innerhalb der
 * Augenform herunter: ist das Auge zu, bleibt keine Kante stehen.
 *
 * Jedes Auge wird in einem eigenen Sprite (84 x 96 x 2 Byte = 16 KB)
 * gezeichnet und dann in einem Stueck auf das Panel geschoben. Direkt auf
 * dem Panel zu zeichnen wuerde flackern: die Pupille waere fuer einen Moment
 * weg, bevor sie an der neuen Stelle steht. Ein Vollbild-Sprite (150 KB)
 * passt nicht in den Heap eines ESP32 mit laufendem WLAN.
 *
 * Kein Anti-Aliasing, keine Ueberblendung. Uebergaenge werden geschnitten,
 * das Panel ist dafuer zu langsam.
 */

#pragma once

#include <Arduino.h>

enum GesichtZustand {
  G_WACH,      // Pupillen wandern, Blinzeln alle 4 bis 5 s
  G_SCHRECK,   // Pupillen zittern, Auge orange. Laeuft von selbst ab.
  G_ZWINKERN,  // rechts zu, untere Lider hoch. Laeuft von selbst ab.
  G_MUEDE,     // Lid halb, Pupillen unten, Farbe gedaempft
  G_DENKEN,    // Pupillen oben rechts, beim Datenholen
  G_HOERT_ZU,  // Pupillen gross, ruhig. Noch ohne Ausloeser.
};

struct GesichtEinstellung {
  uint16_t farbe;      // Augenfarbe, RGB565
  bool blinzeln;
  bool umherschauen;
};

// Wo die Augen stehen. Das Gesicht braucht keine Kopfzeile, deshalb Y = 30
// wie im Entwurf, und darunter bleibt ab Y = 130 Platz fuer zwei Zeilen.
const int AUGE_B = 84;
const int AUGE_H = 96;
const int AUGE_Y = 30;
const int AUGE_L_X = 60;
const int AUGE_R_X = 176;
const int AUGE_UNTEN = AUGE_Y + AUGE_H;  // 126

/** Sprites anlegen. Gibt ``false`` zurueck, wenn der Heap nicht reicht. */
bool gesichtStart(const GesichtEinstellung &e);
void gesichtEinstellen(const GesichtEinstellung &e);

/** Den Grundzustand setzen (wach, muede, denken, hoert zu). */
void gesichtZustand(GesichtZustand z);
GesichtZustand gesichtGrundzustand();

/** Kurze Reaktionen. Beide laufen von selbst in den Grundzustand zurueck. */
void gesichtSchreck(uint32_t dauerMs = 6000);
void gesichtZwinkern(uint32_t dauerMs = 1400);

/** Beide Augen sofort zeichnen, etwa nach einem fillScreen. */
void gesichtZeichnen();

/**
 * Ein Takt der Animation. Oft aufrufen (alle 20 ms reicht). Zeichnet nur,
 * wenn sich etwas bewegt hat, und gibt dann ``true`` zurueck.
 */
bool gesichtTakt();

/** Animation anhalten, etwa solange ein Dialog auf den Augen liegt. */
void gesichtPause(bool an);

/**
 * Messen statt schaetzen: ``bilder`` Vollbilder beider Augen mit wandernder
 * Pupille zeichnen und die Mikrosekunden je Bild zurueckgeben. Ein Bild
 * heisst: zwei Sprites fuellen und beide auf das Panel schieben.
 */
uint32_t gesichtMessen(int bilder);
