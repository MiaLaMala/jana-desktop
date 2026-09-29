# Product

<!-- impeccable:product-schema 1 -->

## Platform

embedded

ESP32-2432S028: 2,8-Zoll-TFT, 320 x 240 im Querformat, resistiver Touch, RGB565. Zeichnet mit TFT_eSPI und
Bitmap-Schriften (GFX), ohne Kantenglättung. Keine Web- oder Apple-Regeln anwenden.

## Users

Eine Person: Mia. Das Gerät steht dauerhaft neben ihr am Arbeitsplatz, auf Armlänge. Sie schaut im Laufe
des Arbeitstags kurz rüber, meist zwischen zwei Dingen, und will in einer Sekunde wissen, was heute noch
ansteht. Die Heute-Ansicht ist die, die sie wirklich benutzt; die Homelab-Ansicht selten.

## Product Purpose

Das Display ist der Teil von Mia OS, der auf dem Tisch steht, und zugleich Jana auf dem Tisch: es zeigt den
Tag und meldet sich, wenn eine Antwort von Mia etwas ändert. Erfolg heißt: Mia muss für „was ist heute noch“
nicht zum Handy greifen.

## Positioning

Kein Dashboard aus Kacheln. Es kennt Mias Tag aus Mia OS (Termine mit Kalenderfarbe, fällige Aufgaben,
Homelab-Lage) und spricht in ganzen Sätzen statt Nullen.

## Operating Context

Tagsüber im Büro, Kunstlicht, Blick von der Seite. Daten kommen alle zwei Minuten aus Mia OS
(`/api/briefing`, `/api/homelab`). Updates über die Luft. Einstellungen (Theme, Augenfarbe, Feierabend)
kommen aus Mia OS.

## Capabilities and Constraints

- Termine heute und morgen (Titel, Beginn, Ende, Kalenderfarbe), fällige und überfällige Aufgaben, offene
  Zahl, Homelab (Dienste oben/gesamt, Störungen, Kennzahlen), Hinweise mit bis zu drei Antwortknöpfen.
- Gesicht (zwei Augen, nur die Pupillen bewegen sich) als eigenes Theme, dazu Easter Eggs aus dem
  Discord-Slang. Beides bleibt.
- Heap reicht nicht für ein Vollbild-Sprite; ein Bild zeichnen dauert gemessen rund 8 ms.
- Das Repo ist öffentlich: keine Namen, Adressen oder echten Termine in Code oder Entwürfen.

## Brand Commitments

Folgt Mia OS (`MiaLaMala/mia-os`, DESIGN.md und farben.json): warmes Rot auf OKLCH-Farbton 20, Neutral mit
einer Spur desselben Rots statt kaltem Grau, dunkel als Standard, Inter, Tabellenziffern. Rot markiert nur,
was jetzt dran ist; Fälliges orange, Fehler zum Orange verschoben. Ganze Sätze statt nackter Nullen, kein
Lob, keine Emojis. Mias Urteil über den Stand 0.2.15: „sieht sehr AI generated aus und verfolgt nicht
wirklich mein Branding“.

## Product Principles

- Heute zuerst. Alles andere ist eine Geste entfernt.
- Je kleiner der Schirm, desto weniger steht drauf.
- Das Gerät redet nur, wenn es etwas zu sagen hat.
