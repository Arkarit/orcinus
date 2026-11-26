# Der eingebaute Betrachter

## Bilder gucken, ohne den Faden zu verlieren

Klassischer Ablauf: Du klickst doppelt auf ein Foto, ein Bildbetrachter geht auf, Du blätterst zwanzig Bilder weiter, findest das richtige, schließt den Betrachter und stehst in Orcinus wieder beim ersten Bild. Wo war das nochmal?

In Orcinus öffnet sich ein Bild (und, wenn Orcinus mit libmpv gebaut wurde, auch ein Video) im **eingebauten Vollbild-Betrachter**. Das Besondere: **Was der Betrachter zeigt, ist gleichzeitig das aktuelle und ausgewählte Element in der Ansicht.** Drückst Du *Esc*, stehst Du also genau bei der Datei, die Du zuletzt angesehen hast. Bereit zum Kopieren, Verschieben oder Löschen.

![Der eingebaute Betrachter](images/orcinus-viewer.png)

## Blättern

Weiterblättern kannst Du mit

- dem **Mausrad**,
- den **Pfeiltasten**,
- **Bild auf** / **Bild ab**,
- **Rücktaste** (zurück),
- **Pos1** / **Ende** (erstes / letztes Bild),
- oder einem **Klick an den linken oder rechten Bildschirmrand**.

Der Betrachter geht dabei durch die anzeigbaren Dateien des Ordners, und zwar in der Reihenfolge der Ansicht, mit ihrer Sortierung und ihrem Filter. Hattest Du beim Öffnen mehrere Dateien ausgewählt, blättert er nur durch diese. Praktisch, wenn Du nur zehn Kandidaten vergleichen willst.

## Bilder

- **Strg+Mausrad** oder **`+`/`-`** zoomen, bis 3200 %. Damit siehst Du jeden einzelnen Pixel. Wirklich jeden.
- **`1`** zeigt das Bild in Originalgröße (100 %), **`0`** passt es in den Bildschirm ein.
- Mit der **mittleren Maustaste** verschiebst Du einen vergrößerten Ausschnitt.
- EXIF-Drehung und Farbprofile werden beachtet. Animierte GIFs und WebPs laufen.

## Videos

Ist Orcinus mit libmpv gebaut, spielt der Betrachter auch Videos ab:

- **Leertaste** oder ein **Klick** spielt ab und hält an.
- **Links** / **Rechts** springt 5 Sekunden zurück bzw. vor.
- **`M`** schaltet den Ton stumm.
- Die Bedienelemente erscheinen, sobald Du die Maus bewegst, und verschwinden wieder, wenn Du in Ruhe gucken willst.

## Doch lieber die richtige Anwendung?

Manchmal willst Du ein Bild ja tatsächlich bearbeiten. Dann hilft **Umschalt+Doppelklick** (oder **Umschalt+Eingabe**): Der Betrachter wird übersprungen, und die Datei öffnet sich in ihrer normalen Anwendung, zum Beispiel in GIMP oder Gwenview.

## Einstellungen

Auf der Seite *Einstellungen → Orcinus einrichten … → Betrachter* legst Du fest,

- **welche Dateitypen** der Betrachter öffnet (Bilder; mit libmpv auch Videos). Alles, was hier nicht angehakt ist, öffnet sich wie gewohnt in seiner Anwendung,
- ob er am Ende des Ordners **wieder von vorn** anfängt,
- ob **kleine Bilder** auf Bildschirmgröße **vergrößert** werden,
- ob vergrößerte Bilder **geglättet** werden (oder ob Du die Pixel lieber knackig eckig magst),
- und wie **Videos** abgespielt werden: automatisch starten, mit Ton, in Schleife.

<TODO-Bild (orcinus-viewer-settings.png): Die Einstellungsseite „Betrachter“>
