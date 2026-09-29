// Erzeugt aus farben.json. Nicht von Hand aendern.
//
// Gehoert nach jana-desktop/display/src/farben.h.
// Das Display kennt nur Dunkel: es steht auf dem Schreibtisch und
// leuchtet, ein weisser Grund waere abends eine Zumutung.
#pragma once

#define C_GRUND 0x1041  // #100b0a  Hintergrund des Fensters
#define C_FLAECHE 0x18A2  // #1c1414  Karten, Seitenleiste, Balken
#define C_ERHOBEN 0x28E3  // #281d1d  Bloecke auf einer Flaeche
#define C_ERHOBEN_2 0x3124  // #332625  gedrueckter Zustand, zweite Stufe
#define C_LINIE 0x3965  // #3d2f2f  Trenner, Raender
#define C_LINIE_WEICH 0x2904  // #2c2121  Trenner innerhalb einer Liste
#define C_TEXT 0xF79E  // #f6f0f0  Lesetext, Titel
#define C_GEDAEMPFT 0xA4B2  // #a29594  Nebensaechliches, Beschriftungen
#define C_LEISE 0x6AEB  // #6d5e5d  Dritte Ebene: Zeitstempel, Zaehler
#define C_AKZENT 0xF1EA  // #f33e52  was JETZT dran ist
#define C_AKZENT_TEXT 0xFFFF  // #ffffff  Schrift auf der Akzentflaeche
#define C_AKZENT_MATT 0x7083  // #72101f  Kante am hervorgehobenen Block
#define C_AKZENT_HAUCH 0x3082  // #371113  Flaeche hinter dem Hervorgehobenen
#define C_GUT 0x3E8D  // #3ad26a  erledigt, laeuft
#define C_ACHTUNG 0xFCE1  // #ff9f0a  faellig, bald
#define C_FEHLER 0xFB88  // #ff7043  kaputt
