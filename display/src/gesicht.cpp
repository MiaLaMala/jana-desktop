/**
 * Der Augen-Motor. Siehe gesicht.h fuer das Warum.
 *
 * Alles hier ist zeitgesteuert, nicht bildgesteuert: ein Blinzeln dauert
 * auf dem Panel gleich lang, egal ob ein Bild 6 oder 20 ms braucht. Das
 * war der Grund, es vor dem Einbau einmal zu messen (gesichtMessen).
 */

#include "gesicht.h"

#include <TFT_eSPI.h>

extern TFT_eSPI tft;

// Dieselbe Grundfarbe wie in main.cpp (#0b0c0e). Pupille und Lid sind
// Grundfarbe, deshalb ist es egal, ob die Pupille an einer Ecke ueber den
// Rand der Augenform hinausragt: dort ist ohnehin Grund.
static const uint16_t GRUND = 0x0841;
static const int ECKE = 40;         // Rundung der Augenform
static const int PUP_B = 26, PUP_H = 30;
static const int PUP_X = 29, PUP_Y = 33;  // Pupille in Ruhe, linke obere Ecke

static TFT_eSprite links(&tft);
static TFT_eSprite rechts(&tft);
static bool bereit = false;
static bool pause = false;

static GesichtEinstellung einst = {0x5E5F, true, true};
static GesichtZustand grund = G_WACH;

// Zeitmarken. 0 heisst: laeuft nicht.
static uint32_t schreckBis = 0;
static uint32_t zwinkernBis = 0;
static uint32_t blinzelStart = 0;
static uint32_t naechstesBlinzeln = 0;
static uint32_t naechsterBlick = 0;
static uint32_t blickStart = 0;
static uint32_t letztesZittern = 0;

// Pupille: wo sie ist, wo sie war, wo sie hin will (in Pixeln um die Ruhelage).
static int pupX = 0, pupY = 0;
static int vonX = 0, vonY = 0;
static int zielX = 0, zielY = 0;
static int zitterX = 0, zitterY = 0;

// Was zuletzt gezeichnet wurde, damit nur bei Aenderung gezeichnet wird.
static int gezPupX = -99, gezPupY = -99;
static int gezLidL = -1, gezLidR = -1, gezUnten = -1;
static uint16_t gezFarbe = 0;
static int gezGroesse = -1;

static const uint32_t BLICK_DAUER_MS = 350;
static const uint32_t BLINZEL_ZU_MS = 70;
static const uint32_t BLINZEL_HALT_MS = 50;
static const uint32_t BLINZEL_AUF_MS = 90;

/** Eine RGB565-Farbe auf etwa die Haelfte abdunkeln, fuer muede. */
static uint16_t gedaempft(uint16_t c) {
  const int r = ((c >> 11) & 31) * 5 / 10;
  const int g = ((c >> 5) & 63) * 5 / 10;
  const int b = (c & 31) * 5 / 10;
  return (uint16_t)((r << 11) | (g << 5) | b);
}

/**
 * Ein Auge in seinen Sprite zeichnen.
 *
 * ``lid`` und ``unten`` in Prozent: wie weit das obere Lid herunter und das
 * untere herauf ist. ``groesse`` in Prozent skaliert die Pupille.
 */
static void augeMalen(TFT_eSprite &s, uint16_t farbe, int dx, int dy, int groesse, int lid,
                      int unten) {
  s.fillSprite(GRUND);
  s.fillRoundRect(0, 0, AUGE_B, AUGE_H, ECKE, farbe);

  const int pb = PUP_B * groesse / 100;
  const int ph = PUP_H * groesse / 100;
  const int px = PUP_X + (PUP_B - pb) / 2 + dx;
  const int py = PUP_Y + (PUP_H - ph) / 2 + dy;
  s.fillRoundRect(px, py, pb, ph, min(pb, ph) / 2, GRUND);

  // Das Lid ist die Augenform in Grundfarbe, von oben hereingeschoben. Bei
  // 100 liegt sie genau auf dem Auge: dann ist nichts mehr zu sehen, auch
  // keine Kante.
  if (lid > 0) {
    const int weg = AUGE_H * lid / 100;
    s.fillRoundRect(0, weg - AUGE_H, AUGE_B, AUGE_H, ECKE, GRUND);
  }
  if (unten > 0) {
    const int weg = AUGE_H * unten / 100;
    s.fillRoundRect(0, AUGE_H - weg, AUGE_B, AUGE_H, ECKE, GRUND);
  }
}

static void beideMalen(uint16_t farbe, int dx, int dy, int groesse, int lidL, int lidR,
                       int unten) {
  augeMalen(links, farbe, dx, dy, groesse, lidL, unten);
  augeMalen(rechts, farbe, dx, dy, groesse, lidR, unten);
  links.pushSprite(AUGE_L_X, AUGE_Y);
  rechts.pushSprite(AUGE_R_X, AUGE_Y);
}

bool gesichtStart(const GesichtEinstellung &e) {
  einst = e;
  if (!bereit) {
    links.setColorDepth(16);
    rechts.setColorDepth(16);
    if (!links.createSprite(AUGE_B, AUGE_H) || !rechts.createSprite(AUGE_B, AUGE_H)) {
      links.deleteSprite();
      rechts.deleteSprite();
      return false;
    }
    bereit = true;
  }
  randomSeed(micros());
  naechstesBlinzeln = millis() + 2000;
  naechsterBlick = millis() + 1500;
  return true;
}

void gesichtEinstellen(const GesichtEinstellung &e) {
  einst = e;
  gezFarbe = 0;  // erzwingt Neuzeichnen
}

void gesichtZustand(GesichtZustand z) {
  if (z == G_SCHRECK || z == G_ZWINKERN)
    return;  // die beiden laufen ueber ihre eigenen Aufrufe
  if (z == grund)
    return;
  grund = z;
  // Ruhelage der Pupille je Zustand. Von dort aus wandert sie, wenn sie darf.
  vonX = pupX;
  vonY = pupY;
  switch (z) {
  case G_MUEDE: zielX = 0; zielY = 7; break;
  case G_DENKEN: zielX = 9; zielY = -7; break;
  default: zielX = 0; zielY = 0; break;
  }
  blickStart = millis();
  naechsterBlick = millis() + 2600;
}

GesichtZustand gesichtGrundzustand() { return grund; }

void gesichtSchreck(uint32_t dauerMs) {
  schreckBis = millis() + dauerMs;
  zwinkernBis = 0;
  blinzelStart = 0;
}

void gesichtZwinkern(uint32_t dauerMs) {
  zwinkernBis = millis() + dauerMs;
  schreckBis = 0;
  blinzelStart = 0;
}

void gesichtPause(bool an) { pause = an; }

/** Lidstellung 0..100 im Verlauf eines Blinzelns, aus der Zeit seit Beginn. */
static int blinzelLid(uint32_t seit) {
  if (seit < BLINZEL_ZU_MS)
    return (int)(seit * 100 / BLINZEL_ZU_MS);
  seit -= BLINZEL_ZU_MS;
  if (seit < BLINZEL_HALT_MS)
    return 100;
  seit -= BLINZEL_HALT_MS;
  if (seit < BLINZEL_AUF_MS)
    return 100 - (int)(seit * 100 / BLINZEL_AUF_MS);
  return -1;  // fertig
}

/** Weich von 0 nach 100 in der Zeit, mit Bremse am Ende (ease-out). */
static int weich(uint32_t seit, uint32_t dauer) {
  if (seit >= dauer)
    return 100;
  const float t = 1.0f - (float)seit / dauer;
  return (int)(100.0f * (1.0f - t * t));
}

static void zeichnen(bool erzwingen) {
  if (!bereit)
    return;
  const uint32_t jetzt = millis();
  const bool schreck = jetzt < schreckBis;
  const bool zwinkert = jetzt < zwinkernBis;

  uint16_t farbe = einst.farbe;
  if (schreck)
    farbe = 0xFCE1;  // C_ACHTUNG, orange
  else if (grund == G_MUEDE)
    farbe = gedaempft(einst.farbe);

  int groesse = 100;
  if (schreck)
    groesse = 60;
  else if (grund == G_HOERT_ZU)
    groesse = 130;

  int dx = pupX, dy = pupY;
  if (schreck) {
    dx = zitterX;
    dy = zitterY;
  }

  int lidL = 0, lidR = 0, unten = 0;
  if (grund == G_MUEDE)
    lidL = lidR = 50;
  if (blinzelStart) {
    const int b = blinzelLid(jetzt - blinzelStart);
    if (b < 0)
      blinzelStart = 0;
    else
      lidL = lidR = max(lidL, b);
  }
  if (zwinkert) {
    lidR = 100;
    unten = 30;
  }

  if (!erzwingen && dx == gezPupX && dy == gezPupY && lidL == gezLidL && lidR == gezLidR &&
      unten == gezUnten && farbe == gezFarbe && groesse == gezGroesse)
    return;

  beideMalen(farbe, dx, dy, groesse, lidL, lidR, unten);
  gezPupX = dx;
  gezPupY = dy;
  gezLidL = lidL;
  gezLidR = lidR;
  gezUnten = unten;
  gezFarbe = farbe;
  gezGroesse = groesse;
}

void gesichtZeichnen() { zeichnen(true); }

bool gesichtTakt() {
  if (!bereit || pause)
    return false;
  const uint32_t jetzt = millis();
  const bool schreck = jetzt < schreckBis;

  // Zittern: alle 40 ms ein neuer Versatz um bis zu 3 Pixel.
  if (schreck && jetzt - letztesZittern >= 40) {
    letztesZittern = jetzt;
    zitterX = random(-3, 4);
    zitterY = random(-2, 3);
  }

  // Blinzeln, nur wach und muede. Beim Denken und Zuhoeren starrt sie.
  const bool darfBlinzeln = einst.blinzeln && !schreck && jetzt >= zwinkernBis &&
                            (grund == G_WACH || grund == G_MUEDE || grund == G_DENKEN);
  if (darfBlinzeln && !blinzelStart && jetzt >= naechstesBlinzeln) {
    blinzelStart = jetzt;
    naechstesBlinzeln = jetzt + (grund == G_MUEDE ? 6000 : 4200) + random(0, 1200);
  }

  // Umherschauen: alle 2,6 s ein neues Ziel, nur wach. Dorthin in 350 ms.
  if (einst.umherschauen && grund == G_WACH && !schreck && jetzt >= naechsterBlick) {
    naechsterBlick = jetzt + 2600 + random(0, 800);
    vonX = pupX;
    vonY = pupY;
    zielX = random(-9, 10);
    zielY = random(-5, 6);
    blickStart = jetzt;
  }
  if (blickStart) {
    const int p = weich(jetzt - blickStart, BLICK_DAUER_MS);
    pupX = vonX + (zielX - vonX) * p / 100;
    pupY = vonY + (zielY - vonY) * p / 100;
    if (p >= 100)
      blickStart = 0;
  }

  const int vorX = gezPupX, vorY = gezPupY, vorL = gezLidL, vorR = gezLidR, vorU = gezUnten,
            vorG = gezGroesse;
  const uint16_t vorF = gezFarbe;
  zeichnen(false);
  return vorX != gezPupX || vorY != gezPupY || vorL != gezLidL || vorR != gezLidR ||
         vorU != gezUnten || vorG != gezGroesse || vorF != gezFarbe;
}

uint32_t gesichtMessen(int bilder) {
  if (!bereit || bilder <= 0)
    return 0;
  const uint32_t start = micros();
  for (int i = 0; i < bilder; i++) {
    const int dx = (i % 19) - 9;
    const int dy = (i % 11) - 5;
    beideMalen(einst.farbe, dx, dy, 100, (i % 7 == 0) ? 100 : 0, 0, 0);
  }
  const uint32_t dauer = micros() - start;
  gezPupX = -99;  // danach einmal frisch zeichnen
  return dauer / bilder;
}
