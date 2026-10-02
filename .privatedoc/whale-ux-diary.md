# Whale UX Diary

Stand: 2026-10-02

Dieses Dokument hält UX-Entscheidungen für Whale fest. Es soll nicht nur beschreiben, **was**
geändert wurde, sondern vor allem, **warum** eine Änderung sinnvoll erschien, welche Nachteile
bewusst akzeptiert wurden und unter welchen Beobachtungen die Entscheidung wieder geändert werden
sollte.

Das Diary ist vorerst interne Projektdokumentation. Geeignete, für Anwender relevante Teile können
später in ein Changelog, Release Notes oder eine öffentliche Design-Dokumentation übernommen werden.

## Arbeitsweise

Für jede UX-relevante Änderung soll ein eigener, nummerierter Eintrag entstehen. Ein Eintrag sollte
mindestens enthalten:

- das beobachtete Problem und den Nutzungskontext;
- die betroffenen Nutzergruppen und ihre miteinander konkurrierenden Erwartungen;
- das gewünschte Verhalten einschließlich Default;
- bewusst nicht verfolgte Alternativen und deren Gründe;
- Wechselwirkungen mit bestehenden Konventionen, Einstellungen und Shortcuts;
- technische Randbedingungen, soweit sie die UX-Entscheidung beeinflussen;
- überprüfbare Akzeptanzkriterien und eine Testmatrix;
- offene Fragen, Risiken und Bedingungen für eine spätere Neubewertung;
- einen kurzen Textentwurf für ein späteres Changelog.

Entscheidungen sind keine unumstößlichen Wahrheiten. Wenn neue Erkenntnisse eine Entscheidung
ändern, soll der alte Stand nachvollziehbar bleiben und durch ein datiertes Addendum ergänzt werden.
So bleibt sichtbar, auf welcher Informationsbasis Whale sich entwickelt hat.

Begrenzte Features und Fehlerkorrekturen sollen, soweit praktisch möglich, Dolphin-kompatibel
entstehen. Ihr fachlicher Kern wird klein, eigenständig und ohne unnötige Whale-Abhängigkeiten
gehalten, damit er auf einen unveränderten Dolphin-Fork übertragen und KDE als Merge Request
angeboten werden kann. Unvermeidbare Whale-Produktintegration – etwa eine abweichende
Konfigurationsdatei – bleibt von diesem übertragbaren Kern getrennt.

---

# UX-001 – Ziehen aus der freien Fläche einer Detailzeile

## Metadaten

- **Datum:** 2026-10-02
- **Status:** Implementiert und automatisiert getestet; praktische Erprobung steht aus
- **Bereich:** Dateiansicht, Detailansicht, Mausinteraktion, Drag-and-drop, Rubberband-Selektion
- **Upstream-Bezug:** Dolphin-kompatibles Verhalten bleibt zunächst der Default
- **Betroffene Schicht:** `KItemListController`, `KItemListView`, Detailansicht-Einstellungen

## Ausgangslage

In Dolphins Detailansicht ist die sichtbare Zeile eines Datei- oder Ordnereintrags breiter als ihr
eigentlich belegter Inhalt. Zwischen Dateiname und weiteren Spalten sowie innerhalb breiter Spalten
kann eine optisch freie Fläche liegen.

Diese Fläche sieht zwar wie ein Teil der hervorgehobenen Zeile aus, wird beim Beginn einer
Mausbewegung aber abhängig vom Selektionszustand unterschiedlich behandelt:

- Bei einer **nicht selektierten** Zeile startet Ziehen aus der freien Zeilenfläche eine
  Rubberband-Selektion.
- Bei einer **bereits selektierten** Zeile startet dieselbe Geste einen Drag der ausgewählten
  Datei beziehungsweise der gesamten Auswahl.
- Ziehen direkt auf Icon oder Text startet auch bei einer nicht selektierten Zeile einen Dateidrag.

Das Verhalten ist nicht von der Geschwindigkeit der Maus abhängig. Entscheidend ist, welche
Interaktion bereits beim Drücken der Maustaste vorbereitet wurde.

## Technische Ursache im Upstream-Verhalten

Die Item-View unterscheidet zwei relevante Geometrien:

- `selectionRectFull()` umfasst in der Detailansicht die vollständige hervorgehobene Zeile.
- `selectionRectCore()` umfasst bei einer nicht selektierten Zeile nur Icon und Text, bei einer
  selektierten Zeile hingegen `selectionRectFull()`.

`KItemListView::itemAt()` erkennt über die volle Fläche weiterhin, zu welcher Zeile ein Mausklick
gehört. `KItemListController::onPress()` prüft anschließend jedoch `selectionRectCore()`. Eine freie
Stelle in einer noch nicht selektierten Zeile wird dadurch als `hitTargetIsRowEmptyRegion`
klassifiziert. Ist keine Auswahl vorhanden, wird `createRubberBand` gesetzt.

Die Zeile wird im weiteren Verlauf des Press-Handlers zwar ausgewählt, die zuvor getroffene
Entscheidung für das Rubberband wird aber nicht neu bewertet. `mousePressEvent()` aktiviert das
Rubberband unmittelbar. `mouseMoveEvent()` startet den eigentlichen Dateidrag nur dann, wenn kein
Rubberband aktiv ist. Damit ist der Drag-Pfad für diese Geste bereits gesperrt.

Die maßgeblichen Stellen sind:

- `src/kitemviews/kstandarditemlistwidget.cpp`, `selectionRectFull()` und
  `selectionRectCore()`;
- `src/kitemviews/kitemlistview.cpp`, `itemAt()`;
- `src/kitemviews/kitemlistcontroller.cpp`, `mousePressEvent()`, `mouseMoveEvent()`,
  `onPress()` und `startRubberBand()`.

Die heutige Form der zustandsabhängigen Geometrie stammt aus dem Upstream-Commit `c1e712890`
„New selection effects“ vom 20. Juni 2025. Die vollständige Zeilenhervorhebung und die bewusste
Behandlung der restlichen Zeilenfläche als möglicher View-Hintergrund reichen bis zum Commit
`d383961719` „Full row highlight implementation“ vom 16. Januar 2022 zurück.

## Das UX-Problem

Das bestehende Verhalten optimiert zwei verschiedene Aufgaben, ohne dass die Oberfläche die
Grenze zwischen ihnen sichtbar erklärt:

1. **Dateien schnell und fehlertolerant ziehen**

   Eine sichtbare Zeile wird als zusammenhängendes Item wahrgenommen. Wer in die Zeile greift,
   erwartet, dass die Datei gezogen wird. Ein großes Drag-Ziel reduziert Präzisionsanforderungen,
   vermeidet den Umweg über eine vorherige Auswahl und ist besonders bei langen Mauswegen nützlich.

2. **Eine Rubberband-Selektion auch in einer gefüllten Ansicht beginnen**

   In einem vollständig mit Zeilen gefüllten Fenster ist unter Umständen kaum echter
   View-Hintergrund sichtbar. Die freien Bereiche innerhalb der Zeilen bieten dann große und gut
   erreichbare Startflächen für ein Auswahlrechteck.

### Kein Dolphin-spezifischer Zielkonflikt

Das Problem entsteht nicht durch eine Besonderheit Dolphins. Jeder Dateimanager, der sowohl
direktes Ziehen von Items als auch eine Rubberband-Selektion anbietet, muss dieselbe Zeigergeste
auflösen: Bedeutet Press-and-drag an dieser Stelle „dieses Item ziehen“ oder „hier ein
Auswahlrechteck beginnen“?

Windows Explorer dient als bekanntes Beispiel für eine Gewichtung zugunsten eines leicht
erreichbaren direkten Item-Drags. Rubberband-Selektion bleibt über Bereiche möglich, die in der
jeweiligen Ansicht als Hintergrund gelten. Die genaue Hit-Geometrie ist dabei von Ansicht und
Windows-Version abhängig; für Whale ist deshalb nicht eine bestimmte Explorer-Geometrie das
Vorbild, sondern die grundsätzliche Aufteilung in Item- und Hintergrundflächen.

Total Commander macht denselben Zielkonflikt ausdrücklich konfigurierbar. Bei aktivierter Option
„Rubber band selection“ kann Ziehen aus der freien Zeilenfläche ein Auswahlrechteck beginnen;
direkter Drag bleibt auf dem Dateinamen oder einer bereits selektierten Zeile möglich. Wird die
Option abgeschaltet, steht die Zeilenfläche wieder für den direkten Drag zur Verfügung. Der Autor
von Total Commander bestätigt beide Pfade im offiziellen Supportforum:

- [Classic Drag and Drop Option?](https://www.ghisler.ch/board/viewtopic.php?t=46839)
- [How can I use rubber band selection?](https://www.ghisler.ch/board/viewtopic.php?t=44617)
- [How to disable selection rectangle in file list?](https://www.ghisler.ch/board/viewtopic.php?t=52967)

Damit sind beide Interaktionsmodelle nicht nur theoretisch plausibel, sondern in etablierten
Dateimanagern praktisch anzutreffen. Whale behandelt deshalb keines von beiden als allgemein
„richtig“. Die Konfigurierbarkeit ist Teil der UX-Lösung und kein bloßer Kompromiss aus technischer
Unentschlossenheit.

Beide Erwartungen sind nachvollziehbar. Eine von ihnen pauschal als falsch zu behandeln würde eine
gut funktionierende Arbeitsweise zugunsten einer anderen entfernen.

## Betroffene Nutzerperspektiven

### Nutzer mit drag-orientiertem Arbeitsablauf

- ziehen Dateien häufig zwischen Fenstern, Tabs, Split Views oder anderen Anwendungen;
- bewegen den Mauszeiger schnell und zielen eher auf die Zeile als exakt auf Icon oder Dateiname;
- interpretieren die vollflächige Hervorhebung als Zusage, dass die ganze Zeile zum Item gehört;
- erleben ein unerwartetes Rubberband als abgebrochenen Arbeitsvorgang.

### Nutzer mit auswahl-orientiertem Arbeitsablauf

- stellen häufig Mehrfachauswahlen mit einem Auswahlrechteck zusammen;
- arbeiten auch in dicht gefüllten Verzeichnissen ohne große freie Fläche unter den Einträgen;
- profitieren davon, ein Rubberband zwischen den sichtbaren Inhalten einer Zeile beginnen zu
  können;
- könnten bei einer pauschalen Full-row-Lösung eine wichtige und eingeübte Startfläche verlieren.

### Upstream-orientierte Nutzer

- erwarten beim Wechsel von Dolphin zu Whale zunächst möglichst wenig überraschende
  Verhaltensänderungen;
- profitieren von einem Dolphin-kompatiblen Default, können sich aber bewusst für Whales
  alternatives Verhalten entscheiden.

## Entscheidung

Whale soll das bestehende Dolphin-Verhalten nicht pauschal ersetzen. Das Verhalten wird für die
Detailansicht konfigurierbar.

Vorgesehen sind zwei Modi:

### 1. Dolphin-kompatibel – Default

- Ziehen auf Icon oder Text startet einen Dateidrag.
- Ziehen aus der freien Fläche einer nicht selektierten Zeile startet ein Rubberband.
- Eine bereits selektierte Zeile bleibt wie bisher über ihre vollständige Zeilenfläche ziehbar.
- Bestehende Modifier-Semantik bleibt vollständig erhalten.

### 2. Full-row Drag – explizit aktivierbar

- Ziehen aus der freien Fläche einer Zeile startet einen Drag des betreffenden Items.
- Bei bestehender Mehrfachauswahl gelten weiterhin Dolphins Regeln dafür, ob die Auswahl gemeinsam
  gezogen oder vor dem Drag ersetzt wird.
- Die Bedeutung eines normalen Klicks ohne relevante Mausbewegung bleibt unverändert.
- `Shift` plus Ziehen aus der freien Zeilenfläche dient als temporärer Ausweg und startet dort ein
  Rubberband.
- Ein bloßer `Shift`-Klick ohne Drag bleibt eine Bereichsauswahl.

Der `Shift`-Ausweg ist bewusst **einseitig**: Er steht im Full-row-Modus für den seltener benötigten
Rubberband-Start zur Verfügung. Im Dolphin-kompatiblen Modus wird `Shift` nicht umgekehrt zum
Erzwingen eines Dateidrags verwendet.

## Warum der Default Dolphin-kompatibel bleibt

- Whale bleibt für neue Nutzer und beim Parallelbetrieb zunächst vorhersehbar.
- Die Änderung nimmt niemandem ungefragt eine vorhandene Rubberband-Startfläche.
- Die alternative Bedienung kann gezielt erprobt werden, bevor über einen späteren Default-Wechsel
  entschieden wird.
- Upstream-Merges bleiben leichter nachvollziehbar, weil die Abweichung klar konfiguriert und
  standardmäßig inaktiv ist.

Ein zukünftiger Default-Wechsel ist nicht ausgeschlossen. Er benötigt jedoch Beobachtungen aus der
praktischen Nutzung und sollte als eigene UX-Entscheidung dokumentiert werden.

## Platzierung in den Einstellungen

Die Option gehört in:

`Configure Whale → View → Details`

Sie soll nahe bei der vorhandenen Einstellung
„Open files and folders: By clicking anywhere on the row“ erscheinen. Beide Optionen betreffen die
Wahrnehmung und Interaktion der vollständigen Detailzeile, sind aber technisch und semantisch
voneinander unabhängig.

Arbeitstitel für das Häkchen:

> **Drag files and folders from anywhere on the row**

Vorgesehener Hilfetext:

> When enabled, dragging from empty space in a row drags that item. Hold Shift while dragging there
> to start a selection rectangle instead.

Die endgültige Formulierung wird während der Implementierung auf Kürze, Übersetzbarkeit und die
KDE-Terminologie für selection rectangle/rubberband geprüft.

Ein neues horizontales Tab „Details“ oder „Interaction“ unter „Interface“ wird hierfür nicht
angelegt. Eine einzelne, ausschließlich die Detailansicht betreffende Option wäre dort schlechter
auffindbar und würde mit der bereits vorhandenen Konfiguration der Detailansicht konkurrieren. Eine
allgemeine Interaction-Seite kann später sinnvoll werden, wenn Whale mehrere ansichtsübergreifende
Eingabeoptionen besitzt.

## Modifier und bestehende Konventionen

Die vorhandenen Modifier sind nicht frei verfügbar:

- `Shift` plus Klick auf ein Item wählt den Bereich zwischen Auswahlanker und angeklicktem Item.
- `Shift` während einer bereits gestarteten Rubberband-Selektion behält die bisherige Auswahl bei.
- `Ctrl` plus Klick invertiert den Auswahlzustand eines Items.
- `Ctrl` während einer Rubberband-Selektion invertiert die erfassten Auswahlzustände.
- Modifier können während eines laufenden Qt/KIO-Drags außerdem die gewünschte Drop-Aktion
  Copy/Move/Link beeinflussen.
- `Alt` und `Meta` sind keine verlässlichen Alternativen, weil Window-Manager Kombinationen aus
  Modifier und Mausbewegung global abfangen können.

### Warum kein symmetrischer Shift-Inverter

Ein symmetrischer Inverter würde im Dolphin-kompatiblen Modus `Shift` plus Ziehen aus einer freien
Zeilenfläche in einen Dateidrag verwandeln. Das kollidiert mit der dokumentierten Funktion, eine
bestehende Auswahl beim Rubberband beizubehalten, sowie mit der Shift-Bereichsauswahl auf Items.

Der heutige Dolphin-Pfad ist dabei selbst kontextabhängig: Wenn noch keine Auswahl besteht, kann
`Shift` plus Ziehen aus der freien Fläche einer nicht selektierten Zeile ein Rubberband starten. Bei
einer bestehenden Auswahl verhindert diese Auswahl an derselben Stelle den Rubberband-Start; der
Shift-Anker wird erweitert und eine anschließende Bewegung kann die so entstandene Auswahl ziehen.
Auch dieses Verhalten gehört zum Dolphin-kompatiblen Default und darf nicht unbemerkt vereinfacht
werden.

Die einseitige Lösung verändert den Dolphin-kompatiblen Modus nicht. Nur wer Full-row Drag bewusst
aktiviert, erhält mit `Shift` einen temporären Weg zurück zum Rubberband. Dadurch ist der Modifier
eine Ergänzung der neuen Option statt eine globale Umdefinition einer etablierten Geste.

## Abgrenzung: Was ausdrücklich nicht geändert wird

- Das Verhalten der Symbol- und Kompaktansicht bleibt unverändert.
- Das Verhalten beim **Ablegen** auf einer Zeile wird nicht geändert. Diese Entscheidung betrifft
  ausschließlich den Beginn eines ausgehenden Drags.
- Direktes Ziehen auf Icon oder Text bleibt unverändert.
- Das Ziehen bereits ausgewählter Items bleibt ohne gedrücktes `Shift` unverändert.
- Die Semantik von Copy, Move und Link nach Beginn des Drags bleibt bei Qt/KIO.
- Touch-, Long-Press- und Auswahlmodus-Verhalten werden nicht beiläufig neu definiert.
- Die Option wird nicht an „Highlight entire row“ gekoppelt. Darstellung, Klickziel und Dragziel
  bleiben getrennte UX-Eigenschaften.
- Es wird kein allgemeines internes Rewrite des Item-View-Frameworks angestrebt.

## Erwogene und verworfene Alternativen

### Dolphin-Verhalten pauschal durch Full-row Drag ersetzen

**Verworfen**, weil dadurch Rubberband-Selektion in vollständig gefüllten Ansichten deutlich
schwerer erreichbar werden kann und bestehende Nutzer ungefragt eine eingeübte Interaktion
verlieren.

### Status quo unverändert lassen

**Verworfen**, weil die sichtbare vollständige Zeile ein großes Dragziel suggeriert und der
zustandsabhängige Unterschied zwischen selektierten und nicht selektierten Zeilen weder sichtbar
noch leicht erlernbar ist.

### Full-row Drag an „Highlight entire row“ koppeln

**Verworfen**, weil eine visuelle Hervorhebung nicht automatisch eine Festlegung sämtlicher
Mausgesten sein sollte. Nutzer können die volle Zeile gern hervorheben und trotzdem freie Bereiche
für Rubberband verwenden wollen.

### Neues Interface-Tab „Details“ oder „Interaction“

**Vorerst verworfen**, weil die bestehende Seite `View → Details` fachlich genauer ist und genügend
Platz bietet. Eine Interaction-Seite bleibt eine mögliche spätere Strukturentscheidung.

### `Shift` als symmetrischen Inverter verwenden

**Verworfen**, weil dies im Dolphin-kompatiblen Modus etablierte Shift-Auswahlgesten überschreiben
würde.

### `Ctrl`, `Alt` oder `Meta` als Inverter verwenden

**Verworfen**:

- `Ctrl` ist zentral für toggelnde Auswahl und wirkt in Drag-and-drop auf die Drop-Aktion.
- `Alt` und `Meta` können durch den Window-Manager belegt sein.
- komplexere Modifier-Kombinationen wären schlecht auffindbar und motorisch unnötig aufwendig.

### Nur eine größere echte Hintergrundfläche am Fensterrand schaffen

**Nicht ausreichend**. Zusätzliche Ränder können Rubberband helfen, lösen aber nicht die Erwartung,
dass die sichtbare Item-Zeile selbst ein bequemes Dragziel ist. Außerdem steht die verfügbare Fläche
auf kleinen Fenstern und bei vielen Spalten unter Druck.

## Technische Leitplanken für die Umsetzung

### Trennung von Whale-Einstellung und generischem Controller

`KItemListController` gehört zum generischen `kitemviews`-Framework und soll nicht direkt von
`DetailsModeSettings` abhängen. Die Einstellung sollte in `DolphinItemListView` gelesen und als klar
benannte View-Eigenschaft an `KItemListView` beziehungsweise den Controller weitergegeben werden.

Mögliche interne Formen sind:

- ein boolesches `dragFromAnywhereOnRow`; oder
- ein kleines Enum für die Interaktion mit der freien Zeilenfläche.

Ein Enum wäre erweiterbarer und semantisch klarer; ein boolescher Wert wäre für genau zwei Modi
einfacher. Die endgültige Wahl wird bei der Implementierung anhand der bestehenden API-Konventionen
getroffen.

### Stabile Definition der freien Zeilenfläche

`selectionRectCore()` ist für die neue Shift-Regel allein nicht ausreichend, weil sein Ergebnis vom
Selektionszustand abhängt: Bei ausgewählten Zeilen umfasst es die vollständige Zeile. Für die
Interaktion wird eine selektionsunabhängige Unterscheidung benötigt zwischen:

- dem tatsächlichen Inhaltsbereich einer Zeile, insbesondere Icon und Text; und
- der übrigen sichtbaren Zeilenfläche.

Die Implementierung soll diese Geometrie ausdrücklich benennen, statt den Selektionszustand als
indirekten Geometrieschalter zu verwenden. Dadurch kann `Shift` auch auf der freien Fläche einer
bereits selektierten Zeile zuverlässig erkannt werden.

### Klick und Drag unterscheiden

Ein `Shift`-Klick muss seine bestehende Bereichsauswahl behalten; erst eine hinreichende
Mausbewegung darf als Rubberband-Geste gelten. Dolphins bestehender Release-Pfad unterscheidet
bereits anhand von `QApplication::startDragDistance()` zwischen Klick und tatsächlichem
Rubberband-Drag. Die Umsetzung muss prüfen, ob dieser Mechanismus alle neuen Zustände korrekt
abdeckt oder ob die Entscheidung bis zum Mouse-Move aufgeschoben werden muss.

### Auswahl erst nach eindeutiger Gestenerkennung verändern

Die neue Logik darf keine sichtbaren Zwischenzustände erzeugen, die anschließend wieder
zurückgenommen werden müssen. Besonders zu prüfen sind:

- keine vorhandene Auswahl;
- genau ein anderes ausgewähltes Item;
- eine Mehrfachauswahl;
- das angeklickte Item ist bereits Teil der Auswahl;
- aktive Shift-Ankerauswahl;
- `Ctrl` oder zusätzliche Modifier;
- Single-Click- und Double-Click-Aktivierung.

### Keine unbeabsichtigte Änderung des Drop-Ziels

`widgetForDropPos()` entscheidet, ob etwas auf ein Item beziehungsweise einen Ordner oder auf den
aktuellen View-Ordner abgelegt wird. Die neue Einstellung darf diese Zielbestimmung nicht verändern.
Initiieren und Empfangen eines Drags sind zwei getrennte UX-Fragen.

## Erwartete Interaktionsmatrix

„Inhalt“ bezeichnet Icon/Text, „freie Zeile“ den übrigen Bereich innerhalb der sichtbaren
Zeilenhervorhebung. „Bewegen“ bedeutet eine Mausbewegung mindestens über der systemweiten
Drag-Schwelle.

| Modus | Ausgangslage | Startfläche | Modifier | Bewegung | Erwartung |
|---|---|---|---|---|---|
| Dolphin | nicht selektiert | Inhalt | keiner | bewegen | Item auswählen und ziehen |
| Dolphin | nicht selektiert | freie Zeile | keiner | bewegen | neues Rubberband |
| Dolphin | selektiert | freie Zeile | keiner | bewegen | bestehende Auswahl ziehen |
| Dolphin | keine Auswahl | freie Zeile | Shift | bewegen | Rubberband |
| Dolphin | Auswahl vorhanden | freie Zeile | Shift | bewegen | bestehenden Shift-Bereich beziehungsweise Auswahl ziehen |
| Full-row | nicht selektiert | Inhalt | keiner | bewegen | Item auswählen und ziehen |
| Full-row | nicht selektiert | freie Zeile | keiner | bewegen | Item auswählen und ziehen |
| Full-row | selektiert | freie Zeile | keiner | bewegen | bestehende Auswahl ziehen |
| Full-row | beliebig | freie Zeile | Shift | bewegen | Rubberband; bisherige Auswahl behalten |
| beide | beliebig | beliebig | Shift | nicht bewegen | bestehende Shift-Bereichsauswahl |
| beide | beliebig | echter View-Hintergrund | keiner | bewegen | neues Rubberband |
| beide | beliebig | echter View-Hintergrund | Shift | bewegen | Rubberband; bisherige Auswahl behalten |

Die Matrix muss während der Implementierung für `Ctrl`, `Ctrl+Shift`, Einzelauswahl,
Mehrfachauswahl und unterschiedliche Aktivierungsmodi ergänzt werden.

## Akzeptanzkriterien

Die Änderung ist UX-seitig fertig, wenn folgende Punkte erfüllt sind:

1. Ohne Änderung der Einstellung verhält sich Whale wie Dolphin vor der Änderung.
2. Die neue Option ist ausschließlich unter `View → Details` sichtbar und standardmäßig aus.
3. Bei aktivierter Option lässt sich eine nicht selektierte Datei aus der freien Fläche ihrer Zeile
   mit einer einzigen Press-and-drag-Geste ziehen.
4. Bei aktivierter Option startet `Shift` plus Ziehen aus derselben Fläche ein Rubberband.
5. Ein `Shift`-Klick ohne Drag führt weiterhin eine Bereichsauswahl aus.
6. Direktes Ziehen auf Icon/Text bleibt in beiden Modi unverändert.
7. Das Ziehen einer bestehenden Mehrfachauswahl bleibt möglich und zieht keine unerwarteten Items.
8. Das Drop-Ziel einer eingehenden Drag-and-drop-Operation ändert sich nicht.
9. Symbol- und Kompaktansicht verhalten sich unverändert.
10. Single-Click- und Double-Click-Aktivierung bleiben funktionsfähig.
11. Rechts-nach-links-Layouts, horizontale Ränder und unterschiedliche Spaltenbreiten führen nicht zu
    abweichenden Hit-Targets.
12. Die entscheidenden Gesten sind durch automatisierte Controller-Tests abgesichert.

## Vorgesehene Tests

Im bestehenden `kitemlistcontrollertest` fehlen derzeit Mausbewegungstests für den Beginn eines
Dateidrags beziehungsweise Rubberbands aus der freien Zeilenfläche. Die Umsetzung soll mindestens
folgende Regressionstests ergänzen:

- Dolphin-Modus: unselektierte freie Zeile aktiviert Rubberband;
- Dolphin-Modus mit bestehender Auswahl und `Shift`: bisheriger Bereichs-/Drag-Pfad bleibt erhalten;
- Full-row-Modus: unselektierte freie Zeile aktiviert keinen Rubberband-Startpfad;
- Full-row-Modus mit `Shift`: freie Zeile aktiviert Rubberband;
- Full-row-Modus mit `Shift`, Bewegung unterhalb der Schwelle: bleibt Shift-Klick/Bereichsauswahl;
- ausgewählte Zeile ohne Modifier: gemeinsamer Drag bleibt möglich;
- Mehrfachauswahl plus Klick/Drag auf ein anderes Item;
- echter View-Hintergrund startet in beiden Modi Rubberband;
- eingehendes Drop-Hit-Testing bleibt unabhängig von der neuen Einstellung.

Falls ein echter `QDrag::exec()` im Unit-Test unpraktisch ist, soll die Entscheidung zwischen den
beiden Gesten über einen testbaren Zustand, ein Signal oder eine eng begrenzte Test-Naht geprüft
werden. Produktionslogik darf nicht nur für den Test dupliziert werden.

## Umsetzung vom 2. Oktober 2026

Die Umsetzung folgt der vorgesehenen Trennung zwischen übertragbarem Dolphin-Kern und
Whale-Integration:

- `KItemListView` besitzt die standardmäßig ausgeschaltete boolesche Eigenschaft
  `dragFromAnywhereOnRow`. Der Controller kennt weder KConfig noch Whale-spezifische Klassen.
- `KItemListWidget::selectionRectContent()` benennt den selektionsunabhängigen Inhaltsbereich. Die
  Basisklasse fällt kompatibel auf `selectionRectCore()` zurück;
  `KStandardItemListWidget` liefert in der Detailansicht ausdrücklich Icon und Text. Bestehende
  Bedeutungen von `selectionRectCore()` und `selectionRectFull()` bleiben erhalten.
- Bei aktivierter Eigenschaft verwendet nur der linke Mauspfad die volle Zeile als mögliches
  Dragziel. Rechtsklick, eingehende Drops und andere Ansichten werden nicht umdefiniert.
- `Shift` auf der freien Zeilenfläche startet den vorhandenen Rubberband-Pfad, ohne vorher die
  Auswahl oder den Auswahlanker zu verändern. Bleibt die Bewegung unter
  `QApplication::startDragDistance()`, wird die Geste beim Loslassen weiterhin als normaler
  Shift-Klick und damit als Bereichsauswahl abgeschlossen.
- Auswahl- und Expansionstoggles sind von diesem Shift-Sonderpfad ausgenommen.
- Die KConfigXT-Einstellung heißt `DragFromAnywhereOnRow`, ist standardmäßig `false` und wird von
  `DolphinItemListView` nur in der Detailansicht auf die generische View-Eigenschaft abgebildet.
- Die Checkbox „Drag files and folders from anywhere on the row“ steht unter
  `Configure Whale → View → Details`; ihr Hilfetext nennt den Shift-Ausweg.

Der Regressionstest `KItemListControllerTest::testDragFromAnywhereOnRow()` prüft:

- den unveränderten Dolphin-Default auf einer freien, nicht selektierten Zeilenfläche;
- den Item-Pfad bei eingeschaltetem Full-row Drag;
- den Shift-Rubberband-Start ohne vorzeitige Auswahländerung; und
- die erhaltene Shift-Bereichsauswahl bei einer Bewegung unterhalb der Drag-Schwelle;
- die selektionsunabhängige Erkennung der freien Fläche auch auf einer ausgewählten Zeile.

Der Test vermeidet absichtlich ein blockierendes natives `QDrag::exec()`. Er prüft stattdessen die
entscheidende Verzweigung: Ist kein Rubberband aktiv und das Item ausgewählt, erreicht eine weitere
Bewegung den bereits bestehenden und separat verwendeten Item-Drag-Pfad. Zusätzlich lief die
vollständige `kitemlistcontrollertest`-Suite mit 95 bestandenen Tests sowie der Build des
Anwendungsziels `whale` erfolgreich durch.

Vor einer abschließenden UX-Bewertung bleiben praktische Mausproben sinnvoll, insbesondere mit
Mehrfachauswahl, `Ctrl`, Rechts-nach-links-Layout und echten ausgehenden sowie eingehenden
Drag-and-drop-Vorgängen. Diese Restprüfung betrifft nicht die dokumentierte Kernentscheidung,
sondern mögliche Randwirkungen im realen Desktop-Stack.

## Risiken und Beobachtungspunkte

### Auffindbarkeit

Ein Checkbox-Text muss verständlich machen, dass es um den Beginn eines Drags auf der leeren
Zeilenfläche geht und nicht um das Ablegen auf Ordnern. Der Hilfetext muss den Shift-Ausweg nennen.

### Unbeabsichtigte Auswahländerungen

Maus-Press, Auswahl, Rubberband und Drag sind im Controller eng gekoppelt. Eine lokal wirkende
Bedingungsänderung kann Mehrfachauswahlen löschen oder erweitern, bevor die endgültige Geste bekannt
ist. Tests müssen deshalb den kompletten Press–Move–Release-Ablauf abdecken.

### Modifier während des Dateidrags

Wenn aus einer Geste tatsächlich ein Dateidrag wird, bleiben Modifier Teil des Qt/KIO-Drag-and-drop-
Protokolls. Die Implementierung darf sie nicht global konsumieren oder maskieren.

### Touch und alternative Eingabegeräte

Die Option ist zunächst für Mausinteraktion begründet. Touch-Ereignisse werden teilweise als
synthetische Mausereignisse geliefert und im Controller gesondert behandelt. Die neue Option darf
Long-Press und Auswahlmodus nicht versehentlich verändern.

### Upstream-Merges

Die Abweichung soll klein, lokal und durch Tests beschrieben bleiben. Falls Upstream sein
Hit-Testing oder Drag-Verhalten ändert, muss UX-001 beim Merge ausdrücklich erneut geprüft werden.

## Bedingungen für eine Neubewertung

Die Entscheidung sollte überprüft werden, wenn mindestens einer der folgenden Fälle eintritt:

- praktische Nutzung zeigt, dass der Dolphin-kompatible Default weiterhin häufig zu Fehlgesten
  führt;
- Full-row-Nutzer finden den Shift-Ausweg nicht oder lösen ihn versehentlich aus;
- Upstream führt selbst eine entsprechende Option oder ein anderes Zeilen-Hit-Testing ein;
- Telemetrie, strukturierte Tests oder belastbares Nutzerfeedback sprechen für einen Default-Wechsel;
- eine zukünftige allgemeine Interaction-Seite macht eine andere Platzierung sinnvoll;
- Qt oder KIO ändern die Modifier- beziehungsweise Drag-and-drop-Konventionen.

Ein Default-Wechsel oder eine andere Modifier-Belegung benötigt einen eigenen Diary-Nachtrag.

## Entwurf für ein späteres Changelog

> **Optional full-row dragging in Details view:** Whale can treat the entire visible row as a drag
> target, making files and folders easier to grab without aiming precisely at their icon or name.
> Dolphin's existing selection behavior remains the default. When full-row dragging is enabled,
> hold Shift while dragging from empty space in a row to start a selection rectangle instead.

## Noch offene Fragen nach der Umsetzung

- Bewährt sich die englische Beschriftung im realen Einstellungsdialog auch bei schmalen Fenstern
  und nach Übersetzungen?
- Ist der Shift-Ausweg ohne weitere sichtbare Erklärung hinreichend auffindbar?
- Zeigt die praktische Nutzung Gründe für einen späteren Default-Wechsel?
- Sollte eine spätere Upstream-Einreichung Einstellung, generischen Kern und UI-Anbindung in
  getrennte, einzeln prüfbare Commits aufteilen?

---

# UX-002 – Dateinamen nach Typ und Dateiflags einfärben

## Metadaten

- **Datum:** 2026-10-02
- **Status:** Evaluation; noch nicht entschieden und nicht implementiert
- **Bereich:** Dateiansicht (alle Ansichtsmodi), Darstellung, Einstellungsdialog
- **Upstream-Bezug:** Default soll Dolphin-kompatibel bleiben (keine Einfärbung)
- **Betroffene Schicht:** `DolphinFileItemListWidget`, `KStandardItemListWidget`,
  `View`-Einstellungsseite

## Ausgangslage

Fast alle grafischen Dateimanager stellen Dateinamen einheitlich in der Textfarbe des Farbschemas
dar. Typ und Eigenschaften einer Datei sind nur über das Icon, eine zusätzliche Spalte oder das
Kontextmenü erkennbar. Im Terminal ist dagegen seit Jahrzehnten etabliert, dass `ls --color` Namen
nach Dateiart, Rechten und Endung einfärbt (`LS_COLORS`, `dircolors`).

Besonders vermisst wird die Hervorhebung **ausführbarer Dateien** (gesetztes X-Bit). Das Icon zeigt
das nicht zuverlässig: Ein Shell-Skript mit und ohne X-Bit sieht in Dolphin gleich aus, und die
Spalte „Permissions“ muss eigens eingeblendet und gelesen werden. Schon die Einfärbung nach X allein
wäre ein deutlicher Gewinn.

Gewünscht sind daher zwei Arten von Regeln:

1. **Dateiflags:** zuallererst ausführbar (X), perspektivisch weitere Eigenschaften wie Symlink,
   defekter Symlink, setuid/setgid, Sticky-Bit, Sonderdateien (FIFO, Socket, Gerät).
2. **Dateityp:** MIME-Typ beziehungsweise Dateiendung, etwa Bilder, Archive, Quellcode.

## Vorhandene technische Grundlage

Die Analyse des Quellcodes zeigt, dass das Einfärben von Dateinamen bereits einen fertigen
Mechanismus besitzt, der heute für den Versionskontrollstatus verwendet wird:

- `src/views/dolphinfileitemlistwidget.cpp`, `refreshCache()`: berechnet für VCS-Zustände eine
  Tönung, mischt sie 50/50 mit der Textfarbe und übergibt sie mit `setTextColor()`.
- `src/kitemviews/kstandarditemlistwidget.cpp`, `textColor()`: verwendet die eigene Farbe nur für
  **nicht selektierte** Einträge. Selektierte Einträge erhalten weiterhin `HighlightedText`;
  versteckte Einträge bleiben abgeblendet und haben Vorrang.
- `updateAdditionalInfoTextColor()`: leitet die Farbe der übrigen Spalten (Größe, Datum …) aus der
  eigenen Farbe ab, indem sie zu 70 % mit dem Hintergrund gemischt wird.

Eine weitere Farbquelle in `DolphinFileItemListWidget::refreshCache()` wirkt damit automatisch in
Symbol-, Kompakt- und Detailansicht, ohne den generischen Zeichenpfad zu verändern.

### Zugriff auf Typ und Flags

- Die Modellrolle `"type"` ist ungeeignet: Sie enthält nur den übersetzten Anzeigenamen
  (`mimeComment()`, `kfileitemmodel.cpp`) und wird nur bei Bedarf befüllt.
- Das Widget erreicht das `KFileItem` über das Modell, wie es `kfileitemlistwidget.cpp` bereits tut
  (`fileItemModel->fileItem(index)`).
- **Flags** sind sofort und billig verfügbar, weil sie aus dem `UDSEntry` stammen:
  `KFileItem::isExecutable()` (seit KF 6.0), `permissions()`, `mode()`, `isLink()`, `linkDest()`,
  `isDir()`, `isRegularFile()`. Für X ist keine asynchrone Nachbestimmung nötig.
- **MIME-Typ:** `currentMimeType()` ist zunächst nur aus dem Namen geraten. Der
  `KFileItemModelRolesUpdater` bestimmt den Typ für sichtbare Einträge später inhaltsbasiert. Bei
  Dateien ohne eindeutige Endung kann eine Typfarbe deshalb kurz nach dem Erscheinen wechseln –
  genau wie heute das Icon.

### Performance

Die Zuordnung MIME-Typ → Farbe soll pro MIME-Typname zwischengespeichert werden, inklusive der
Auflösung der MIME-Vererbung. Flag-Prüfungen sind einfache Bitoperationen. Ein spürbares Risiko für
große Verzeichnisse ist nicht erkennbar; es sollte dennoch mit einem Verzeichnis mit mehreren
zehntausend Einträgen gemessen werden.

## Betroffene Nutzerperspektiven

### Power-User („Simon“)

- arbeiten parallel im Terminal und kennen `ls --color`;
- wollen Skripte, Binärdateien und Symlinks ohne zusätzliche Spalte erkennen;
- profitieren von frei konfigurierbaren Regeln, gegebenenfalls aus `LS_COLORS` übernommen.

### Office-Nutzer („Lisa“)

- benötigen das Feature in der Regel nicht und dürfen davon nicht gestört werden;
- könnten von dezenten Typfarben (Dokumente, Bilder, Archive) profitieren, wenn sie es bewusst
  einschalten.

### Upstream-orientierte Nutzer

- erwarten zunächst unveränderte Darstellung; daher ist die Funktion standardmäßig aus.

## Entwurf der Entscheidung

Noch nicht bestätigt. Vorgeschlagen wird:

1. **Default aus.** Ohne Einschalten verhält sich Whale wie Dolphin.
2. **Zwei Regelarten**, in dieser Reihenfolge ausgewertet:
   1. Flag-Regeln (X, Symlink, …);
   2. Typ-Regeln (MIME-Typ mit Platzhalter und Vererbung, alternativ Dateiendung).

   Innerhalb einer Regelart gewinnt die erste passende Regel. Die Rangfolge Flag vor Typ entspricht
   der Konvention von GNU `ls`, bei der eine ausführbare Datei als „ausführbar“ und nicht nach ihrer
   Endung gefärbt wird.
3. **Rangfolge gegenüber vorhandenen Zuständen:**
   - Selektion: Markierungsfarbe gewinnt (bestehendes Verhalten, sichert Lesbarkeit).
   - Versteckt: bleibt abgeblendet (bestehendes Verhalten). Zu prüfen ist, ob eine abgeblendete
     Variante der Regelfarbe besser wäre.
   - VCS-Status: gewinnt vor Typ- und Flagfarbe, damit Statusinformationen nicht verloren gehen.
   - Ausgeschnitten: bleibt wie bisher erkennbar.
4. **Nur der Dateiname wird eingefärbt.** Die übrigen Spalten erhalten über den vorhandenen
   Mechanismus automatisch eine blasse Variante; ob das erwünscht ist, wird bei der Erprobung
   bewertet.
5. **Farbe ist zusätzliche, nie alleinige Information.** Alle Eigenschaften bleiben über Icon,
   Spalten oder Eigenschaften-Dialog erkennbar (Barrierefreiheit, Farbsehschwäche).

### Ausführbar (X) im Detail

X ist der wichtigste Einzelfall und wird als erster Ausbauschritt empfohlen. Folgende Fragen sind
dafür zu klären:

- **Ordner ausschließen.** Ordner tragen fast immer ein X-Bit (Durchsuchbarkeit). Die Regel soll nur
  für reguläre Dateien gelten (`isRegularFile()` beziehungsweise `isFile() && !isDir()`).
- **Welche Bits zählen?** `isExecutable()` ist im Header nur als „has executable permission“
  dokumentiert. Vor der Umsetzung ist in den KIO-Quellen zu prüfen, ob Benutzer-, Gruppen- oder
  beliebiges X-Bit gemeint ist. `ls` färbt bei jedem gesetzten X-Bit (`S_IXUGO`); dieser Konvention
  zu folgen ist naheliegend.
- **Dateisysteme ohne echte Unix-Rechte.** Auf FAT-, exFAT- und häufig NTFS-Mounts melden alle
  Dateien X. Ohne Gegenmaßnahme wäre ein ganzer USB-Stick eingefärbt. Optionen:
  - (a) hinnehmen, wie `ls` es tut;
  - (b) Dateisysteme ohne POSIX-Rechte erkennen und dort keine X-Färbung anwenden;
  - (c) per Einstellung abschaltbar machen.

  Empfohlen wird (b), falls die Erkennung mit vertretbarem Aufwand zuverlässig möglich ist
  (etwa über `KMountPoint` beziehungsweise den Dateisystemtyp), sonst (a).
- **`noexec`-Mounts.** Dort ist das X-Bit gesetzt, die Datei aber nicht ausführbar. `ls` ignoriert
  das; Whale sollte es zunächst ebenso tun, weil die Farbe das Recht und nicht die tatsächliche
  Ausführbarkeit beschreibt.
- **Nicht lokale URLs.** Viele KIO-Worker (z. B. SFTP) liefern Rechte. Die Färbung soll auch dort
  gelten, sofern Rechte bekannt sind; fehlen sie (`KFileItem::Unknown`), wird nicht gefärbt.
- **`.desktop`-Dateien und Typ „ausführbar“ ohne X-Bit** sind keine Flag-Frage. Sie können über eine
  Typ-Regel (`application/x-executable`, `application/x-desktop`) abgedeckt werden.

### Weitere Flags (später)

In Anlehnung an `LS_COLORS` kommen in Frage: Symlink (`ln`), defekter Symlink (`or`), setuid (`su`),
setgid (`sg`), Sticky-Ordner (`st`), für andere beschreibbare Ordner (`ow`, `tw`), FIFO (`pi`),
Socket (`so`), Block-/Zeichengerät (`bd`, `cd`). Sie sind technisch ähnlich billig wie X, aber
seltener relevant. Defekte Symlinks erfordern gegebenenfalls einen zusätzlichen `stat` und sind
deshalb gesondert zu bewerten.

## Farbschema-Verträglichkeit

Feste Farben sind der heikelste Punkt, weil Whale helle und dunkle Farbschemata unterstützen muss.
Erwogen werden:

- **(a) Eine Farbe je Regel mit automatischer Kontrastkorrektur** gegenüber `QPalette::Base`. Die
  Farbe wird, falls nötig, in der Helligkeit angepasst, bis ein Mindestkontrast erreicht ist.
- **(b) Getrennte Farben für helles und dunkles Schema.** Präzise, aber doppelte Pflege.
- **(c) Tönung wie beim VCS-Status**, 50/50 mit der Textfarbe gemischt. Immer lesbar, aber blass und
  für eine bewusste Farbkodierung oft zu schwach.

Empfohlen wird (a). Die Standardregeln sollten so gewählt werden, dass sie in Breeze und Breeze Dark
ohne Korrektur gut funktionieren. Bei einem Wechsel des Farbschemas zur Laufzeit muss die Korrektur
neu berechnet werden; der vorhandene Pfad über `styleOption().palette` sollte das abdecken, was bei
der Umsetzung zu verifizieren ist.

## Platzierung in den Einstellungen

Vorgeschlagen wird ein neuer **horizontaler Reiter** unter:

`Configure Whale → View → <neuer Reiter>` (Arbeitstitel „Colors“ oder „Highlighting“)

Begründung:

- Die Funktion betrifft die Darstellung und gilt für alle Ansichtsmodi, genau wie der Reiter
  „Content Display“.
- Ein eigener Eintrag in der vertikalen Seitenliste wäre prominenter, als es eine optionale
  Darstellungsfunktion rechtfertigt, und widerspräche dem Grundsatz unaufdringlicher Optionen.
- Die Einbindung erfolgt in `src/settings/viewmodes/viewsettingspage.cpp` nach dem Muster von
  `ContentDisplayTab`.

Risiko: Die Reiterleiste ist mit fünf Einträgen bereits gut gefüllt. Mit sechs Reitern und
Übersetzungen ist die Breite bei kleinen Dialoggrößen zu prüfen.

Vorgesehener Inhalt:

- Häkchen „Color file names by type and permissions“ (Arbeitstitel), standardmäßig aus;
- Abschnitt **Flags**: zunächst nur „Executable files“ mit Farbwähler;
- Abschnitt **Types**: Regeltabelle (Muster, Farbe, Vorschau), Hinzufügen, Entfernen, Hoch, Runter;
- „Defaults“ stellt einen vorbereiteten Regelsatz wieder her.

### Speicherung

KConfigXT eignet sich gut für das Häkchen und einzelne Flag-Farben, aber schlecht für Listen
strukturierter Regeln. Vorgeschlagen wird eine eigene Gruppe in der Whale-Konfiguration (z. B.
`[FileNameColors]`), gelesen von einer kleinen Regelklasse. Die Regeln gelten global, nicht pro
Ordner.

## Abgrenzung: Was ausdrücklich nicht geändert wird

- Kein Einfärben von Icons, Zeilenhintergründen oder Markierungen.
- Keine Änderung der Selektionsdarstellung.
- Keine Abhängigkeit von Baloo oder zusätzlichen Dateisystemzugriffen für den Basisumfang.
- Keine Ordner-spezifischen Regeln über die View-Properties.

## Erwogene Alternativen

### Nur ein fester, nicht konfigurierbarer Regelsatz

**Zu prüfen.** Einfacher umzusetzen und für X allein ausreichend, aber für Typfarben zu
unflexibel. Ein fester X-Regelsatz könnte ein sinnvoller erster Schritt sein.

### `LS_COLORS` direkt übernehmen

**Als Zusatz interessant, nicht als Grundlage.** `LS_COLORS` verwendet Terminalfarben und
Attribute (fett, Hintergrund), die sich nicht eins zu eins auf ein grafisches Farbschema abbilden
lassen. Ein späterer Import ist denkbar.

### Färbung über eine neue Spalte oder ein Overlay-Emblem

**Verworfen** als Ersatz: Emblem-Ecken sind bereits durch VCS und Plugins belegt, und eine Spalte
existiert in Form von „Permissions“ schon. Ziel ist gerade das Erkennen auf einen Blick ohne
zusätzliche Elemente.

### Eigener Eintrag in der linken Seitenliste

**Vorerst verworfen**, siehe Platzierung.

## Vorgeschlagener Ausbau in Schritten

1. **X-Färbung:** generische Farbquelle im Widget, Flag-Regel „ausführbar“ für reguläre Dateien,
   Kontrastkorrektur, Häkchen und Farbwähler im neuen Reiter, Tests.
2. **Typ-Regeln:** Regelklasse mit MIME-Platzhaltern, Vererbung und Endungsmustern, Regeltabelle,
   Standardregeln.
3. **Weitere Flags** und gegebenenfalls `LS_COLORS`-Import.

Jeder Schritt kann für sich erprobt und bewertet werden.

## Upstream-Übertragbarkeit

Wie bei UX-001 wird zwischen übertragbarem Kern und Whale-Integration getrennt:

- **Kern:** Regelauswertung, Farbquelle in `DolphinFileItemListWidget::refreshCache()`,
  Rangfolge gegenüber VCS, Kontrastkorrektur, Tests.
- **Integration:** Einstellungsreiter, Speicherung, Standardregeln.

Ob KDE die Funktion annehmen würde, ist offen. Sie ist optional und standardmäßig aus und erfüllt
damit das README-Kriterium unaufdringlicher Optionen. Die X-Färbung allein ist vermutlich der am
leichtesten vermittelbare Teil.

## Vorläufige Akzeptanzkriterien

1. Ohne Einschalten ist die Darstellung identisch mit Dolphin.
2. Eingeschaltet erscheinen reguläre Dateien mit X-Bit in der konfigurierten Farbe, Ordner nicht.
3. Selektierte Einträge bleiben in der Markierungsfarbe lesbar.
4. VCS-Farben haben Vorrang vor Flag- und Typfarben.
5. In Breeze und Breeze Dark sind alle Standardfarben gut lesbar; ein Schemawechsel zur Laufzeit wird
   ohne Neustart übernommen.
6. Ändern der Rechte (`chmod +x`) wird ohne manuelles Neuladen sichtbar.
7. Symbol-, Kompakt- und Detailansicht verhalten sich gleich.
8. Ein Verzeichnis mit mehreren zehntausend Einträgen lässt sich nicht spürbar langsamer anzeigen und
   scrollen.

## Offene Fragen

- Welche Bits prüft `KFileItem::isExecutable()` genau?
- Wie wird mit FAT/exFAT/NTFS umgegangen (Optionen a–c oben)?
- Sollen Typ-Regeln MIME-Typen, Endungen oder beides erlauben?
- Welche Farbstrategie (a–c) wird gewählt?
- Wie heißt der neue Reiter, und passt er in die Reiterleiste?
- Sollen versteckte Dateien ihre Regelfarbe abgeblendet behalten?
- Sollen die übrigen Spalten den Farbstich übernehmen?

## Entwurf für ein späteres Changelog

> **Colored file names:** Whale can color file names by permissions and type. Executable files stand
> out at a glance, and custom rules can highlight images, archives, source code or any other file
> type. Colors adapt to light and dark color schemes. The feature is off by default.
