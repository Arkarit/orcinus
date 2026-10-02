# `kitemviews` – Architektur der Dolphin-Dateiansicht

Stand: Whale/Dolphin `a7b6601ea` vom 2026-09-30 (Dolphin 26.11.70-dev).
Erstellt: 2026-09-30. Die Aussagen in diesem Dokument wurden am aktuellen Sourcecode geprüft.
Pfade sind relativ zu `src/`, Zeilennummern beziehen sich auf den oben genannten Stand und können
sich bei Upstream-Merges verschieben.

Dieses Dokument vertieft die Kurzfassung in `.privatedoc/dolphin-architektur.md`. Es beschreibt das
eigene Model/View/Controller-Framework unter `kitemviews/`, seine Anbindung an `DolphinView` und die
Dolphin-spezifischen Ableitungen unter `views/`.

---

## 0. Das Wichtigste in 12 Punkten

1. `kitemviews` ist **nicht** auf `QAbstractItemModel`, `QListView`, `QTreeView` oder
   `QStyledItemDelegate` aufgebaut. Es ist ein eigenes, indexbasiertes MVC-Framework auf
   `QGraphicsView`/`QGraphicsWidget`.
2. Das Model ist eine **flache Liste**. Ein Item wird durch einen ganzzahligen Index und eine kleine
   Map aus Rollenname (`QByteArray`) und Wert (`QVariant`) beschrieben. Der Baum der Detailansicht
   wird in diese Liste hineingefaltet; Elternbeziehungen liegen in `ItemData::parent`.
3. `KFileItemModel` verbindet das Framework mit KIO: Ein `KDirLister` liefert `KFileItem`s und
   Änderungen an Verzeichnisinhalten. Das Model sortiert, filtert, gruppiert und verwaltet
   aufgeklappte Unterordner.
4. Das Model berechnet nur **billige Rollen** direkt. MIME-Ermittlung, Icons, Vorschaubilder,
   Ordnergrößen, Overlay-Icons und Baloo-Metadaten erledigt
   `KFileItemModelRolesUpdater` verzögert und sichtbarkeitsabhängig.
5. `KItemListView` erzeugt nicht für jedes Model-Item ein Widget. Sie hält nur Widgets für den
   sichtbaren Bereich, recycelt unsichtbar gewordene Instanzen und berechnet den Rest über
   `KItemListViewLayouter` und Size-Hints.
6. Symbole, Kompakt und Details sind **drei Layouts derselben View- und Widget-Klassen**, keine drei
   getrennten Ansichten.
7. `KItemListController` verarbeitet Maus, Tastatur, Touch, Gesten und Drag-and-Drop. Die Auswahl
   selbst lebt getrennt davon im `KItemListSelectionManager`.
8. `KItemListContainer` ist die QWidget-Brücke: eine `QAbstractScrollArea`, deren Viewport ein
   scrollbar-loses `QGraphicsView` mit eigener `QGraphicsScene` ist. Die eigentliche View scrollt
   über ihre Offset-Eigenschaften.
9. Besitzkette: `KItemListContainer` besitzt den Controller; der Controller besitzt Model, View,
   Selection-Manager und Keyboard-Search-Manager; die View besitzt Layouter, Rollen-Updater,
   Header, Animationen und die Fabriken für recycelte Item-Widgets.
10. Model-Indizes sind absichtlich kurzlebig. Nach Insert/Remove/Sortierung müssen Auswahl, aktuelle
    Position, sichtbare Widgets und Accessibility-Objekte anhand der Model-Signale nachgeführt
    werden. Dauerhafte Identität kommt bei Dateien von `KFileItem`/`QUrl`, nicht vom Index.
11. Die Dolphin-spezifische Schicht ist dünn: `DolphinItemListView` übersetzt ViewProperties und
    Zoomstufen; `DolphinFileItemListWidget` ergänzt VCS-Overlays. Fast die gesamte Mechanik bleibt
    im allgemeinen `kitemviews`-Framework.
12. Änderungen an diesem Framework haben eine große Reichweite. Neben der Hauptansicht benutzen es
    unter anderem der Ordnerbaum und Tests; Performance, Touch, Accessibility, Remote-URLs,
    Sortierung und laufende KIO-Jobs müssen gemeinsam betrachtet werden.

---

## 1. Einordnung und Systemgrenze

### 1.1 Wo `kitemviews` im Gesamtprogramm sitzt

```text
DolphinMainWindow
  └─ DolphinTabWidget
      └─ DolphinTabPage
          └─ DolphinViewContainer       App-Leisten um die Dateiansicht
              └─ DolphinView            fachliche Brücke zur App
                  └─ KItemListContainer QWidget/Scrolling
                      └─ KItemListController
                          ├─ KFileItemModel
                          │   └─ KDirLister (KIO)
                          ├─ DolphinItemListView
                          │   └─ sichtbare DolphinFileItemListWidgets
                          ├─ KItemListSelectionManager
                          └─ KItemListKeyboardSearchManager
```

`DolphinView` erzeugt diese Objekte in `views/dolphinview.cpp:89–242`. Sie ist die Grenze zwischen
dem generischen Item-Framework und dem restlichen Dateimanager:

- nach unten: Model-Indizes, Rollen, Controller-Signale und Graphics-Widgets;
- nach oben: `KFileItemList`, URLs, Laden/Stoppen, Dateioperationen, Kontextmenüs, Status- und
  Auswahl-Signale.

`DolphinView` kennt `DolphinMainWindow` nicht. Dadurch kann dieselbe Dateiansicht auch im KPart
verwendet werden.

### 1.2 Was zum Framework gehört

| Bereich | Hauptklassen | Aufgabe |
|---|---|---|
| Model-Vertrag | `KItemModelBase` | Index- und Rollen-API, Änderungs-, Sortier- und Gruppierungssignale |
| Datei-Model | `KFileItemModel` | KIO-Listing, `KFileItem`-Speicher, Filter, Sortierung, Gruppen, Expansion |
| Rollenauflösung | `KFileItemModelRolesUpdater` | Icons, Previews, MIME, Ordnerinhalte, Overlays, Baloo |
| View | `KItemListView` | sichtbare Items, Recycling, Layout, Header, Gruppen, Animationen |
| Standarddarstellung | `KStandardItemListView`/`Widget` | Icons-/Kompakt-/Details-Layout und Painting |
| Dateidarstellung | `KFileItemListView`/`Widget` | Dateirollen, Previews, Drag-Pixmaps, Hover-Sequenzen |
| Dolphin-Anpassung | `DolphinItemListView`, `DolphinFileItemListWidget` | Zoom, Settings, VCS-Overlays |
| Eingabe | `KItemListController` | Maus, Tastatur, Touch, Gesten, DnD, Kontextmenü |
| Auswahl | `KItemListSelectionManager`, `KItemSet` | Current Item, Anker und komprimierte Auswahlbereiche |
| QWidget-Brücke | `KItemListContainer` | Graphics-Szene, Scrollbars, Smooth Scrolling, Fokus |
| Interna | `kitemviews/private/` | Layouter, Size-Hints, Header-Widget, Rubberband, Editor usw. |
| Accessibility | `kitemviews/accessibility/` | eigene QAccessible-Hierarchie für View und Items |

Die Dateien werden in `dolphinprivate` gebaut (`src/CMakeLists.txt:64–200`). Die Klassen sind zwar
vielfach mit `DOLPHIN_EXPORT` exportiert, bilden aber keine stabile, eigenständig veröffentlichte
Plugin-API. Für Whale bleibt dies private Implementierung.

### 1.3 Zweiter konkreter Nutzer: das Ordner-Panel

Das Framework ist nicht ausschließlich die große Dateiansicht. `FoldersPanel::showEvent()` erzeugt
beim ersten Einblenden ebenfalls ein vollständiges Gespann aus `KFileItemModel`,
`KFileItemListView`, `KItemListController` und `KItemListContainer`
(`panels/folders/folderspanel.cpp:122–170`). Unterschiede zur Hauptansicht:

- das Model zeigt nur Verzeichnisse;
- die View benutzt `FoldersItemListWidget` und aktiviert Expansion immer;
- der Controller arbeitet mit Single Selection;
- Single Click ist erzwungen;
- Auto-Activation klappt nur auf bzw. zu, statt den normalen Datei-Aktivierungspfad auszulösen.

Dieser zweite Nutzer ist ein guter Architekturtest: Allgemeine Verbesserungen gehören nach
`kitemviews`; Verhalten, das nur die zentrale Dolphin-Dateiansicht betrifft, gehört eher nach
`views/`.

### 1.4 Warum kein Qt-Standard-Itemview

Die Header nennen als historischen Ausgangspunkt „Itemviews NG“ aus Trolltech Labs. Im heutigen
Code sind die praktischen Gründe direkt erkennbar:

- vollständige Kontrolle über Geometrie, Painting, Hit-Zonen und Animationen;
- nur sichtbare Item-Widgets statt einer Widget- oder Delegate-Instanz pro Model-Eintrag;
- dieselben Bausteine für Icons, kompaktes Mehrspaltenlayout und Details/Tabelle;
- eigene Baumdarstellung innerhalb eines flachen Models;
- abgestufte, sichtbarkeitsabhängige Auflösung teurer Dateieigenschaften;
- eigene Touch-, Rubberband-, Hover-, DnD- und Inline-Rename-Semantik.

Der Preis dafür ist viel selbst implementierte Infrastruktur: Auswahl, Indexverschiebungen,
Scrollbars, Fokusweitergabe, Header, Accessibility und Model/View-Synchronisierung.

---

## 2. Objektgraph, Besitz und Lebensdauer

### 2.1 Konstruktion

`DolphinView::DolphinView()` baut die Kette in dieser Reihenfolge auf
(`views/dolphinview.cpp:134–172`):

```text
new KFileItemModel
new DolphinItemListView
new KItemListController(model, view)
new KItemListContainer(controller)
```

Der Controller setzt sich als QObject-Parent von Model und View
(`kitemviews/kitemlistcontroller.cpp:91–149`). Der Container setzt sich wiederum als Parent des
Controllers und löscht ihn im Destruktor bewusst vor der `QGraphicsScene`
(`kitemviews/kitemlistcontainer.cpp:58–112`). Das verhindert, dass die Szene vor dem darin
eingetragenen `KItemListView` verschwindet.

### 2.2 Besitzmatrix

| Besitzer | Besitztes Objekt | Bemerkung |
|---|---|---|
| `DolphinView` | `KItemListContainer` | normales QWidget-Kind im Layout |
| `KItemListContainer` | `KItemListController`, `QGraphicsScene`, Smooth-Scroller, `QScroller` | Controller wird explizit zuerst gelöscht |
| `KItemListController` | Model, View, Selection-Manager, Keyboard-Search-Manager, Timer | `setModel()`/`setView()` übernehmen Ownership |
| `KItemListView` | Layouter, Size-Hint-Resolver, Animation, Rubberbands, Header, Creator | die View liegt zusätzlich als Item in der Scene |
| `KFileItemListView` | `KFileItemModelRolesUpdater`, Entprell-Timer | Updater wird beim Model-Wechsel neu gebaut |
| `KItemListWidgetCreator` | erzeugte und recycelbare Item-Widgets, gemeinsamer Informant | Fabrik und Objekt-Pool in einem |
| `KFileItemModel` | `KDirLister`, `ItemData`-Objekte, Timer, Filterzustand | Model besitzt die Dateidaten |
| Rollen-Updater | Preview-Jobs, Counter, Baloo-Monitor, Plugin-Referenzen | viele Arbeiten sind asynchron |

Wichtig: QObject-Parent und Graphics-Item-Zugehörigkeit überlagern sich. Die View gehört als QObject
zum Controller, wird aber zugleich durch `KItemListContainer::slotViewChanged()` in die
`QGraphicsScene` eingehängt (`kitemviews/kitemlistcontainer.cpp:256–287`).

### 2.3 Austausch von Model oder View

`KItemListController::setModel()` und `setView()` sind echte Austauschpunkte:

1. das alte Objekt wird per `deleteLater()` freigegeben;
2. das neue Objekt erhält den Controller als Parent;
3. Selection-Manager und View erhalten das neue Model;
4. `modelChanged` bzw. `viewChanged` wird emittiert;
5. der Container hängt die View aus bzw. in die Graphics-Szene ein;
6. `KFileItemListView::onModelChanged()` verwirft und erzeugt den Rollen-Updater neu.

In Dolphin geschieht ein solcher Austausch selten. Die API erklärt aber, warum die Verbindung der
Komponenten nicht direkt im Konstruktor fest verdrahtet ist.

---

## 3. Das Datenmodell

### 3.1 `KItemModelBase`: kleiner, eigener Model-Vertrag

`KItemModelBase` (`kitemviews/kitemmodelbase.h`) erbt nur von `QObject`. Die minimale API besteht aus:

- `count()` – Anzahl der sichtbaren Einträge;
- `data(index)` – Rollenwerte als `SmallHash`;
- optional `setData(index, values)`;
- Sortierrolle, Gruppenrolle und Sortierreihenfolge;
- optional Gruppierung, Expansion, MIME-Daten, Keyboard-Suche und DnD-Auskünfte.

Es gibt weder `QModelIndex` noch Parent/Child-Indizes. Ein Index ist schlicht `0 … count()-1`.

Die zentralen Änderungsverträge sind:

```text
itemsInserted(KItemRangeList)
itemsRemoved(KItemRangeList)
itemsMoved(KItemRange, movedToIndexes)
itemsChanged(KItemRangeList, changedRoles)
groupsChanged()
```

`KItemRange { index, count }` fasst zusammenhängende Änderungen zusammen. Bei
`itemsInserted`/`itemsRemoved` beziehen sich die Bereiche auf die Ordnung vor der Änderung.
`itemsMoved` beschreibt für jeden Index eines zusammenhängenden alten Bereichs seinen neuen Index.
Diese genaue Semantik erlaubt View und Auswahlmanager, ihre rein indexbasierten Zustände zu
reparieren.

### 3.2 `KFileItemModel`: Speicherung

Der zentrale Datensatz steht in `kitemviews/kfileitemmodel.h:385–389`:

```cpp
struct ItemData {
    KFileItem item;
    SmallHash values;
    ItemData *parent;
};
```

Die wesentlichen Container sind:

- `m_itemData`: die aktuell sichtbare, sortierte, flache Liste;
- `m_items`: URL-zu-Index-Cache für O(1)-Lookup der bereits gecachten Präfixlänge;
- `m_filteredItems`: ausgeblendete `ItemData`-Objekte, die bei geänderten Filtern wieder eingesetzt
  werden können;
- `m_pendingItemsToInsert`: während eines laufenden Listings gepufferte Einträge;
- `m_expandedDirs`: aufgeklappte Ziel-URL → sichtbare URL;
- `m_urlsToExpand`: beim Wiederherstellen noch schrittweise aufzuklappende URLs;
- `m_groups`: Cache der Gruppenanfänge und Gruppenwerte.

`data(index)` initialisiert `ItemData::values` beim ersten Zugriff lazy durch `retrieveData()` und
injiziert die URL nur in die Rückgabe. Die URL wird nicht noch einmal pro Item im Rollen-Cache
gespeichert (`kfileitemmodel.cpp:382–414`).

### 3.3 `SmallHash`: Rollen sparsam speichern

Seit 2026 benutzt jedes Item keinen `QHash<QByteArray, QVariant>` mehr, sondern `SmallHash`
(`smallhash.h`). Intern ist das ein flacher
`std::vector<std::pair<QByteArray, QVariant>>`:

- Lookup ist O(n), bei der kleinen Rollenanzahl aber günstig;
- es entfällt die große Mindestallokation eines Qt-Hashs pro Item;
- `toHash()`/`fromHash()` dienen als Brücke zu älterem Code.

Das ist eine bewusste Optimierung für große Verzeichnisse: Die Summe vieler kleiner per-Item-
Allokationen ist wichtiger als die asymptotische Lookup-Zeit einer einzelnen Rollenmap.

### 3.4 Rollen: Datenprotokoll zwischen Model und Darstellung

Rollennamen sind Byte-Strings. Es gibt keine zentrale öffentliche Enum-API. Wichtige Gruppen:

| Art | Beispiele | Herkunft |
|---|---|---|
| Identität/Text | `url`, `text`, `path`, `folder`, `extension` | `KFileItemModel` |
| Dateisystem | `size`, `modificationtime`, `permissions`, `owner`, `group` | Model bzw. Updater |
| Typ/Icon | `type`, `iconName`, `iconPixmap`, `iconOverlays` | Model + Rollen-Updater |
| Baum | `isDir`, `isExpanded`, `isExpandable`, `expandedParentsCount` | Model + Ordnerzähler |
| Preview-Sequenz | `supportsSequencing`, `hoverSequencePixmaps`, `hoverSequenceWraparoundPoint` | Rollen-Updater |
| Baloo | `tags`, `rating`, `comment`, `dimensions`, `duration`, `artist`, … | `KBalooRolesProvider` |
| Dolphin/VCS | `version` | `VersionControlObserver`, Darstellung in `DolphinFileItemListWidget` |

`KFileItemModel::rolesInfoMap()` (`kfileitemmodel.cpp:3157–3216`) beschreibt die benutzerwählbaren
Spalten mit Übersetzung, Kategorie und Baloo-/Indexer-Abhängigkeit. Interne Rollen wie `iconPixmap`
oder `version` stehen nicht in dieser Liste.

`KFileItemListView::applyRolesToModel()` vereinigt sichtbare Rollen mit intern nötigen Rollen und
übergibt die Menge sowohl dem Model als auch dem Rollen-Updater. Eine Rolle muss also nicht sichtbar
sein, um für Sortierung, Gruppierung oder Baumdarstellung benötigt zu werden.

### 3.5 Billige und teure Daten

`KFileItemModel::retrieveData()` (`kfileitemmodel.cpp:2231–2382`) liest nur Werte, die ohne
problematische I/O-Kosten verfügbar sind, beispielsweise Name, Roh-Zeitstempel, Rechte oder
UDS-Felder. Es vermeidet ausdrücklich `KFileItem::iconName()`, wenn dafür erst der MIME-Typ bestimmt
werden müsste.

Die Trennung lautet:

```text
KFileItemModel::retrieveData()
  └─ synchron, billig, für angeforderte Rollen

KFileItemModelRolesUpdater
  └─ potenziell teuer, verzögert oder asynchron:
       MIME-Typ und finales Icon
       KIO-Vorschaubild
       Overlay-Plugins
       Ordneranzahl/-größe
       Baloo-Metadaten
```

So bleibt ein großes Listing früh benutzbar, obwohl seine Detailinformationen noch nach und nach
eintreffen.

### 3.6 KIO und Verzeichnisänderungen

`KFileItemModel` legt im Konstruktor einen `KDirLister` an (`kfileitemmodel.cpp:264–341`). Der
Lebenszyklus eines normalen Listings ist:

```text
KFileItemModel::loadDirectory(url)
  → KDirLister::openUrl(url)
  → KDirLister::itemsAdded(directoryUrl, items)
  → slotItemsAdded()
  → createItemDataList()
  → m_pendingItemsToInsert
  → dispatchPendingItemsToInsert()
  → insertItems()
  → itemsInserted(ranges)
  → directoryLoadingCompleted()
```

Weitere KDirLister-Signale führen zu:

- `itemsDeleted` → `slotItemsDeleted()` → `removeItems()`;
- `refreshItems` → `slotRefreshItems()` → Rollen invalidieren/ändern und ggf. neu sortieren;
- `clear` → `slotClear()`;
- `completed` → Pending Items einfügen und Expansion fortsetzen;
- `canceled`, Fehler und Redirects → entsprechende Model-Signale an `DolphinView`.

Ein Verzeichnislisting ist daher kein einmaliges Snapshot-Laden. Das Model bleibt Beobachter des
Ortes und verarbeitet spätere Dateiänderungen.

### 3.7 Sortierung

Die Sortierlogik steckt vollständig im Model:

- `lessThan()` berücksichtigt Baumeltern, „Ordner zuerst“, „versteckte zuletzt“, Sortierrolle und
  natürliche Namenssortierung;
- `sort()` benutzt `private/kfileitemmodelsortalgorithm.h`, einen parallelen Merge-Sort;
- `m_resortAllItemsTimer` bündelt mehrere rollenbedingte Änderungen;
- `resortAllItems()` erzeugt eine URL-Liste der alten Ordnung, sortiert, berechnet daraus
  `movedToIndexes` und emittiert `itemsMoved` (`kfileitemmodel.cpp:1329–1408`);
- `sortingFinished()` bedeutet, dass die Ordnung endgültig ist und kein weiterer Resort ansteht.

Ist die Sortierrolle teuer, muss der Rollen-Updater sie zunächst für **alle** Items auflösen. Die
ersten höchstens 200 ms laufen synchron, danach stückweise über den Event-Loop. Erst dann ist eine
inhaltlich richtige Sortierung möglich.

### 3.8 Filterung

`KFileItemModelFilter` unterstützt:

- einfachen Teilstring;
- Glob-Muster;
- reguläre Ausdrücke;
- ein- und ausschließende MIME-Filter;
- optionale Groß-/Kleinschreibung.

Beim Filtern werden Items nicht sofort zerstört, sondern zwischen `m_itemData` und
`m_filteredItems` verschoben (`KFileItemModel::applyFilters()`, `kfileitemmodel.cpp:1116–1196`).
Bei aufgeklappten Ordnern bleibt die Ahnenkette eines passenden Kindes sichtbar. Deshalb ist der
Filtercode zugleich Baumcode und deutlich komplexer als ein einfacher Listenfilter.

### 3.9 Gruppierung

`groups()` liefert eine Liste aus `(erster Item-Index, Gruppenwert)`. Für Name, Größe, Zeit,
Berechtigung, Rating und generische Strings existieren eigene Gruppierungsfunktionen. Die View
erzeugt daraus `KItemListGroupHeader`-Objekte.

Gruppierung ändert nicht die Identität der Items. Bei geänderter Reihenfolge impliziert
`itemsMoved`, dass sich Gruppen ebenfalls geändert haben; `groupsChanged` wird nur gebraucht, wenn
die Reihenfolge gleich bleibt.

Aktuell können Gruppen in Icons- und Detailansicht eingeklappt werden. Der eingeklappte Zustand
liegt in der View (`m_collapsedGroups`), nicht im Model. Der Layouter überspringt dann die
Gruppenmitglieder, während der Header sichtbar bleibt.

### 3.10 Baumansicht in einer flachen Liste

Nur das Details-Layout unterstützt in Dolphins Dateiansicht aufklappbare Items. Das Model bleibt
dennoch flach:

```text
Index 0  Ordner A        parent = nullptr, expandedParentsCount = 0
Index 1    Datei A1      parent = Ordner A, expandedParentsCount = 1
Index 2    Ordner A2     parent = Ordner A, expandedParentsCount = 1
Index 3      Datei A2a   parent = Ordner A2, expandedParentsCount = 2
Index 4  Datei B         parent = nullptr, expandedParentsCount = 0
```

`setExpanded(index, true)` öffnet den Unterordner mit `KDirLister::Keep`; dessen Items werden passend
in die flache Ordnung einsortiert. Beim Einklappen stoppt/vergisst das Model die Unterlistings und
entfernt den zusammenhängenden Nachfahrenbereich (`kfileitemmodel.cpp:874–941`).

Die View zeichnet Einrückung, Expander und Verbindungslinien anhand von `isExpandable`,
`isExpanded`, `expandedParentsCount` und den berechneten Geschwisterinformationen. Es gibt keinen
rekursiven View-Baum.

---

## 4. Die View- und Widget-Hierarchie

### 4.1 Zwei parallele Vererbungsketten

Die View bestimmt Anordnung, sichtbaren Bereich und Objektverwaltung:

```text
QGraphicsWidget
  └─ KItemListView
      └─ KStandardItemListView
          └─ KFileItemListView
              └─ DolphinItemListView
```

Das einzelne sichtbare Item bestimmt Geometrien, Hit-Zonen, Caches und Painting:

```text
QGraphicsWidget
  └─ KItemListWidget
      └─ KStandardItemListWidget
          └─ KFileItemListWidget
              └─ DolphinFileItemListWidget
```

Die Trennung ist wichtig: Eine neue View-Eigenschaft gehört nicht automatisch ins Item-Widget und
eine neue visuelle Item-Rolle nicht automatisch in die View.

### 4.2 Verantwortung der vier View-Stufen

#### `KItemListView`

- hält Model und Controller-Referenzen;
- reagiert auf Insert/Remove/Move/Change und Auswahländerungen;
- besitzt Layouter, Header, Gruppenheader, Rubberband und Animationen;
- berechnet sichtbare Indizes;
- erstellt/recycelt sichtbare Widgets;
- bietet Hit-Tests (`itemAt`, `isAboveText`, `isAboveSelectionToggle`, …);
- koordiniert Inline-Editing und Drop-Indikator.

#### `KStandardItemListView`

- führt `IconsLayout`, `CompactLayout` und `DetailsLayout` ein;
- setzt Scrollrichtung und Style-Defaults;
- konfiguriert Standard-Widgets für das jeweilige Layout;
- aktiviert Expansion, Gruppen-Collapse und vollständige Zeilenhervorhebung je Layout.

Die Kompaktansicht scrollt horizontal; Icons und Details scrollen vertikal
(`kstandarditemlistview.cpp:27–47`).

#### `KFileItemListView`

- verlangt ein `KFileItemModel`;
- erzeugt den `KFileItemModelRolesUpdater`;
- synchronisiert sichtbare Rollen und Viewportbereich mit dem Updater;
- steuert Preview-Anzeige, Thumbnail-Plugins, Icongröße und Device-Pixel-Ratio;
- erzeugt Datei-Drag-Pixmaps und leitet Hover-Sequenzen weiter.

#### `DolphinItemListView`

- übersetzt Zoomlevel in Icon-, Preview- und Rastergrößen;
- liest/schreibt Dolphin-View-Einstellungen;
- entscheidet Dolphins Detailansichtsverhalten für Expansion und ganze Zeilen;
- verwendet `DolphinFileItemListWidget` als konkrete Item-Klasse.

### 4.3 Verantwortung der Widget-Stufen

#### `KItemListWidget`

Grundzustand eines sichtbaren Items:

- Index und Rollen-`SmallHash`;
- selected/current/hovered/pressed;
- sichtbare Rollen, Spaltenbreiten, Style-Option;
- Auswahl- und Expander-Hit-Rechtecke;
- Hover-Animation und Selection-Toggle;
- abstrakte Text- und Auswahlgeometrien.

#### `KStandardItemListWidget`

- berechnet die drei Layoutformen;
- cached Icon/Pixmap, Texte, Rechtecke, Rating und Overlays;
- zeichnet Auswahl, Icon, Textspalten, Baumlinien und Zusatzinformationen;
- stellt Inline-Rename über `KItemListRoleEditor` bereit;
- reagiert auf Cut-Clipboard-Zustand und Hidden-Markierung.

#### `KFileItemListWidget`

- formatiert Dateirollen in lesbare Texte;
- kennt Symlinks und versteckte Dateien;
- behandelt Erweiterungsselektion beim Umbenennen;
- startet/stoppt Hover-Preview-Sequenzen über die View.

#### `DolphinFileItemListWidget`

- wertet die Dolphin-spezifische Rolle `version` aus;
- übersetzt `KVersionControlPlugin::ItemVersion` in ein Overlay-Icon;
- ergänzt dieses beim Cache-Refresh.

### 4.4 Informants: Größen berechnen, ohne Widgets anzulegen

Die View muss die Gesamtausdehnung und bevorzugte Spaltenbreiten kennen, obwohl unsichtbare Items
kein Widget haben. Dafür gibt es `KItemListWidgetInformant` und seine Ableitungen.

Ein `KItemListWidgetCreator<T>` besitzt genau einen Informant, nutzt ihn für Size-Hints und erzeugt
oder recycelt `T` nur für wirklich sichtbare Items. So ist Layoutberechnung von Widget-Erzeugung
entkoppelt.

### 4.5 Virtualisierung und Recycling

`KItemListView::doLayout()` (`kitemlistview.cpp:1939 ff.`) ist der zentrale Ablauf:

1. der Layouter bestimmt ersten und letzten sichtbaren Index;
2. `recycleInvisibleItems()` sammelt nicht mehr sichtbare Widget-Instanzen;
3. für jeden sichtbaren, nicht eingeklappten Index wird ein vorhandenes Widget übernommen oder
   eines aus der Creator-Fabrik geholt;
4. `updateWidgetProperties()` setzt Index, Daten, Rollen, Selection/Current und Stil;
5. Geometrie und Gruppenheader werden aktualisiert;
6. je nach Änderung werden Position, Erzeugung oder Entfernung animiert;
7. Scroll- und Item-Offsets werden an den Container signalisiert.

`m_visibleItems` ist ein `QHash<int, KItemListWidget *>`; die Anzahl der Widgets wächst daher grob
mit dem Viewport, nicht mit dem gesamten Verzeichnis.

Wichtig für Erweiterungen: Ein Widget behält beim Recycling nicht dieselbe Datei. Jeder Zustand,
der von einem Item abhängt, muss bei `setIndex`/`setData`/`initializeItemListWidget` vollständig neu
gesetzt oder sauber invalidiert werden.

### 4.6 Layoutberechnung

`KItemListViewLayouter` (`private/kitemlistviewlayouter.*`) kennt:

- Viewgröße, Itemgröße und Margins;
- Scrollrichtung, Haupt-Scrolloffset und orthogonalen Item-Offset;
- Header- und Gruppenheaderhöhe;
- variable Zeilenhöhen bzw. Spaltenbreiten aus Size-Hints;
- Gruppenanfänge und eingeklappte Gruppen;
- Index ↔ Zeile/Spalte ↔ Rechteck;
- ersten/letzten sichtbaren Index und maximale Scrollausdehnung.

Änderungen markieren den Layouter zunächst nur als dirty. Die teure Neuberechnung erfolgt lazy beim
nächsten Lesezugriff. `KItemListSizeHintResolver` hält die per-Item-Size-Hints dazu passend über
Insert/Remove/Move stabil.

### 4.7 Transaktionen und Animationen

Mehrere zusammengehörige Property-Änderungen werden mit `beginTransaction()`/`endTransaction()`
gebündelt, beispielsweise beim Wechsel des Layouts. Solange eine Transaktion aktiv ist, wird kein
vollständiges Layout ausgeführt. Am Ende folgt höchstens ein Relayout; sobald eine Teiländerung
Animation ausschließt, wird die gesamte Transaktion nicht animiert.

`KItemListViewAnimation` verwaltet Erzeugen, Entfernen, Verschieben und Größenänderungen sichtbarer
Graphics-Widgets. `animateChangedItemCount()` begrenzt Animationen bei großen Mengen, damit
Masseneinfügungen nicht unnötig teuer werden.

### 4.8 Header und Gruppenheader

Der Detail-Header besteht aus:

- `KItemListHeader`: öffentliche QObject-Fassade für Spaltenbreiten, automatische Größenanpassung
  und Side-Padding;
- `KItemListHeaderWidget`: das tatsächlich gezeichnete/interaktive Graphics-Widget;
- sichtbaren Rollen als Spaltenreihenfolge;
- `preferredColumnWidths()`, berechnet aus Informant und Model-Daten.

Ein Klick auf eine Spalte ändert Sortierrolle oder -reihenfolge bereits im Model und emittiert danach
zusätzlich ein View-Signal für die Anwendung. Verschieben und Skalieren der Spalten aktualisiert die
sichtbaren Item-Widgets.

Gruppenheader werden getrennt von Item-Widgets gehalten (`m_visibleGroups`). Dadurch bleiben sie
unabhängig vom Item-Recycling. `KStandardItemListGroupHeader` übernimmt die Standarddarstellung.

---

## 5. Container und Scrolling

### 5.1 QWidget außen, Graphics View innen

`KItemListContainer` erbt von `QAbstractScrollArea`. Sein Viewport wird im Konstruktor durch ein
privates `KItemListContainerViewport : QGraphicsView` ersetzt. Dieses `QGraphicsView`:

- besitzt die `QGraphicsScene` mit dem `KItemListView`;
- zeigt keine eigenen Scrollbars;
- reicht Wheel-Events an den äußeren Container weiter.

Die äußeren `QAbstractScrollArea`-Scrollbars sind die sichtbaren Scrollbars. Sie verschieben aber
nicht den QGraphicsView-Viewport, sondern werden mit den beiden View-Eigenschaften gekoppelt:

- `scrollOffset`: Achse der Listenbewegung;
- `itemOffset`: orthogonale Achse, falls ein Item bzw. die Detailspalten breiter als die View sind.

### 5.2 Smooth Scrolling

Je Achse existiert ein `KItemListSmoothScroller`. Ein Wheel-Event wählt anhand der Delta-Richtung
und sichtbaren Scrollbars horizontal oder vertikal. Der Smooth-Scroller animiert direkt die
Offset-Property der View und hält die äußere Scrollbar synchron.

Scrollbar-Updates werden während einer laufenden Animation aufgeschoben
(`m_scrollOffsetScrollBarUpdatePending`, `m_itemOffsetScrollBarUpdatePending`), damit ein Relayout
das aktuelle Ziel nicht mitten in der Bewegung überschreibt.

### 5.3 Fokus und Event-Brücken

Tastatur- und Kontextmenüereignisse erreichen das Graphics-Widget nicht zuverlässig über Qt's
normale Fokusroute. Der Container sendet sie deshalb explizit mit `QApplication::sendEvent()` an die
View (`kitemlistcontainer.cpp:146–173`). Focus-In/Out wird ebenso weitergereicht.

Die View leitet fast alle Events in `KItemListView::event()` an
`KItemListController::processEvent()` weiter. Ausnahmen sind unter anderem Palette/Font, Fokus,
Gruppenheader-Klicks und laufendes Inline-Editing.

---

## 6. Controller und Interaktion

### 6.1 Ereignisfluss

```text
QWidget-/Graphics-Ereignis
  → KItemListContainer oder QGraphicsScene
  → KItemListView::event()
  → KItemListController::processEvent()
  → spezialisierter Handler
       keyPressEvent
       mousePress/Move/Release/DoubleClick
       contextMenuEvent
       dragEnter/Move/Leave/Drop
       hoverEnter/Move/Leave
       wheel/resize
       gesture/touch
  → SelectionManager ändern und/oder Controller-Signal
  → DolphinView-Slot
  → App-Aktion, Navigation, KIO-Job oder Signal nach außen
```

Der Controller kennt Model und View, zeichnet aber nicht. Er fragt die View nach Hit-Zonen und
Geometrien und ändert Model oder Selection-Manager.

### 6.2 Auswahlzustand

`KItemListSelectionManager` hält:

- `m_currentItem`: Tastaturfokus/current item;
- `m_anchorItem`: Ausgangspunkt für Shift-Bereiche;
- `m_selectedItems`: explizite Auswahl als `KItemSet`;
- `m_isAnchoredSelectionActive`: laufende Bereichsauswahl.

Current und Selected sind getrennte Konzepte. Ein Item kann current sein, ohne ausgewählt zu sein.

`KItemSet` speichert aufeinanderfolgende Indizes als Bereiche. Eine Auswahl `{1,2,3,4,5}` belegt
damit nur `KItemRange(1,5)`. Iteration bleibt aufsteigend, was Model- und UI-Code vereinfacht.

Die View reicht Insert/Remove/Move-Signale an den Selection-Manager weiter. Dieser verschiebt
Current, Anker und Auswahlindizes oder entfernt nicht mehr existierende Werte
(`kitemlistselectionmanager.cpp:212–370`). Ohne diese Nachführung würde jede Sortierung die Auswahl
auf andere Dateien springen lassen.

### 6.3 Maus

Die wesentliche Klicksemantik steht in `KItemListController::onPress()` und `onRelease()`:

- Single-/Double-Click-Aktivierung folgt dem Style-Hint oder einer erzwungenen Einstellung;
- Ctrl toggelt, Shift verwendet den Anker;
- Rechtsklick selektiert passend, bevor das Kontextmenü angefordert wird;
- ein Klick auf Text, ganze Zeile, Selection-Toggle und Expander kann verschieden wirken;
- Bewegung ab `QApplication::startDragDistance()` startet Drag-and-Drop;
- Klick in freie Fläche startet gegebenenfalls das Rubberband.

Die exakten Trefferflächen kommen aus dem konkreten Item-Widget. Eine Änderung an
`selectionRectFull()`, `selectionRectCore()`, `textRect()`, `selectionToggleRect()` oder
`expansionToggleRect()` ändert deshalb unmittelbar die Bedienlogik.

### 6.4 Tastatur und Type-ahead

`keyPressEvent()` behandelt Navigation, Auswahl, Aktivierung, Expansion, Page Up/Down, Home/End und
Zoom-Signale. Für ungleichmäßig gefüllte Raster merkt der Controller einen geometrischen
`m_keyboardAnchorPos`, damit wiederholtes Hoch/Runter möglichst in derselben visuellen Spalte
bleibt.

`KItemListKeyboardSearchManager` sammelt Zeichen, behandelt Wiederholung und Rückwärtssuche und
fragt das Model über `indexForKeyboardSearch()`. Der Controller setzt anschließend Current und
Auswahl; `typeAheadUsed` liefert Text und Treffer an `DolphinView` für visuelles Feedback.

### 6.5 Rubberband

`KItemListRubberBand` ist nur das Zustandsobjekt aus Start-/Endpunkt und aktiv/inaktiv. Der
Controller berechnet in `slotRubberBandChanged()` die getroffenen Items und kombiniert sie abhängig
von Ctrl/Shift mit `m_oldSelection`. Die View zeichnet das Band und übernimmt Auto-Scrolling am
Rand.

### 6.6 Touch und Gesten

Der Controller registriert Tap, Tap-and-Hold, Pinch sowie eigene Zwei-Finger-Swipe- und
Zwei-Finger-Tap-Recognizer. Unter anderem:

- Tap entspricht einer touchangepassten Klickfolge;
- langes Halten fordert den Selection Mode an;
- Pinch emittiert Zoomsignale;
- Swipe kann eine Dolphin-Aktion auslösen;
- synthetische Mausereignisse nach Touch werden gefiltert, um Doppelaktionen zu vermeiden.

### 6.7 Drag-and-Drop

Beim Drag-Start fragt der Controller `model->createMimeData(selectedIndexes)` und die View nach
einem Drag-Pixmap. Während Drag-Move:

- wird das Zielwidget per strengerem `widgetForDropPos()` bestimmt;
- ein Ordner kann gehighlightet und nach 750 ms automatisch betreten/aufgeklappt werden;
- bei Models ohne Sortierrolle ist zusätzlich ein Drop **zwischen** Items möglich;
- das Model liefert nur visuelles Vorwissen über `supportsDropping()`;
- die endgültige Operation entscheidet der Empfänger des Controller-Signals.

Für Dolphin führt `itemDropEvent` zu `DolphinView::slotItemDropEvent()` und weiter zu KIO. Das
Framework selbst kopiert oder verschiebt keine Dateien.

### 6.8 Inline-Rename

```text
DolphinView::renameSelectedItems()
  → KItemListView::editRole(index, "text")
  → sichtbares KStandardItemListWidget::setEditedRole("text")
  → KItemListRoleEditor (KTextEdit)
  → roleEditingFinished(index, "text", neuer Text)
  → DolphinView::slotRoleEditingFinished()
  → KIO::moveAs(...)
```

Der Editor ist an das sichtbare Widget gekoppelt. Die View verhindert bzw. beendet Editing passend
bei Model-/Event-Änderungen. Für Dateien bestimmt `KFileItemListWidget::selectionLength()`, dass beim
Start typischerweise der Name ohne Erweiterung markiert wird.

---

## 7. Pipeline für Icons, Rollen und Vorschaubilder

### 7.1 Erzeugung und Eingänge

`KFileItemListView::onModelChanged()` erzeugt genau einen `KFileItemModelRolesUpdater` pro Model
(`kfileitemlistview.cpp:234–249`). Die View liefert ihm:

- angeforderte Rollen;
- sichtbaren Indexbereich;
- maximale Zahl sichtbarer Items;
- Icongröße und Device-Pixel-Ratio;
- Preview an/aus und Vergrößerungsoption;
- aktivierte Thumbnail-Plugins;
- Hover-Item und Sequenzindex.

Scrollen wird um 50 ms entprellt, Zoom-/Icongrößenänderungen um 300 ms
(`kfileitemlistview.cpp:26–58`). Während View-Transaktionen oder dieser Verzögerungen wird der
Updater pausiert.

### 7.2 Zustandsmaschine

Der Updater hat fünf Zustände:

```text
Idle
Paused
ResolvingSortRole
ResolvingAllRoles
PreviewJobRunning
```

Die Pause merkt, ob sich Previews, Icongröße oder Rollen geändert haben, und holt die Arbeit beim
Fortsetzen nach. Selbst ausgelöste `model->setData()`-Änderungen werden über
`m_applyingChangesToModel` gegen eine Endlosschleife geschützt.

### 7.3 Priorisierung

`startUpdating()` arbeitet in folgender Reihenfolge:

1. bis zu 200 ms lang finale Icons der sichtbaren Items synchron bestimmen;
2. bei teurer Sortierrolle zuerst deren Werte für alle Items vervollständigen;
3. interessante Indizes mit `indexesToResolve()` aufbauen;
4. PreviewJob starten oder sonst Rollen Item für Item über 0-ms-Timer auflösen;
5. zwischenzeitlich geänderte Items erneut behandeln.

Die Prioritäten von `indexesToResolve()` sind:

1. sichtbare Dateien, dann sichtbare Ordner;
2. bis zu fünf Seiten nach dem Viewport;
3. bis zu fünf Seiten davor, in Rückwärtsreihenfolge;
4. letzte Seite;
5. erste Seite;
6. weitere Items bis zur Grenze von 500.

Damit wirken Scrollen, Home und End schnell, ohne jedes riesige Verzeichnis vollständig zu
thumbnailsieren.

### 7.4 PreviewJob

Bei aktivierten Previews bündelt ein `KIO::PreviewJob` alle anstehenden interessanten Items:

```text
KIO::PreviewJob::generated(item, image)
  → transformPreviewImage()
  → rolesData(item, index)
  → model->setData(index, { iconPixmap, supportsSequencing, ... })
  → KFileItemModel::itemsChanged(ranges, roles)
  → KItemListView::slotItemsChanged()
  → sichtbares Widget invalidiert Pixmap-/Textcache
  → nächstes paint() zeigt die Vorschau
```

Der KIO-Cache arbeitet mit 128×128 oder 256×256 Pixeln. Der Updater skaliert, rahmt und
berücksichtigt DPR sowie „kleine Vorschaubilder vergrößern“.

### 7.5 Hover-Sequenzen

Beim Hover erhöht das Item-Widget regelmäßig einen Sequenzindex. Dieser läuft über
`KFileItemListView::setHoverSequenceState()` zum Updater. Für jedes nächste Bild ist wegen der
PreviewJob-API ein eigener Job nötig. Der Updater puffert fünf Bilder voraus und hält Sequenzdaten
für höchstens 20 zuletzt verwendete Items.

Die normale Vorschau bleibt in `iconPixmap`; Index 0 der Hover-Sequenz wird nicht doppelt gespeichert.

### 7.6 Ordneranzahl und -größe

Je nach `ContentDisplaySettings::directorySizeMode()` gibt es zwei Wege:

- Content Count bzw. langsame Items: `KIO::listDir`, nicht rekursiv, direkt pro Item;
- rekursive Größenberechnung: `KDirectoryContentsCounter` mit gemeinsamem Worker in einem eigenen
  `QThread`, FIFO- und Prioritätswarteschlange sowie `KDirWatch`-Invalidierung.

Sichtbare Ordner erhalten hohe Priorität. Remote-Ordner bekommen bei Größenberechnung einen
„unbekannt“-Wert statt lokaler Rekursion.

### 7.7 Baloo und Overlay-Plugins

Mit `HAVE_BALOO` ergänzt `KBalooRolesProvider` Metadatenrollen und ein `Baloo::FileMonitor`
aktualisiert geänderte Dateien. Diese Werte sind besonders für Zusatzspalten und entsprechende
Sortierung relevant.

Zusätzlich lädt der Updater alle Plugins aus `kf6/overlayicon`. Ihre Overlay-Namen werden mit
`KFileItem::overlays()` vereinigt und als `iconOverlays` an die Darstellung gegeben. Das ist der
existierende generische Plugin-Einstieg in die Item-Dekoration.

VCS-Overlays folgen einem anderen Pfad: `VersionControlObserver` setzt die Rolle `version`, und erst
`DolphinFileItemListWidget` übersetzt sie in ein konkretes Overlay.

---

## 8. Wichtige End-to-End-Abläufe

### 8.1 Neues Verzeichnis anzeigen

```text
DolphinView::setUrl/loadDirectory
  → KFileItemModel::loadDirectory
  → KDirLister (KIO, asynchron)
  → pending ItemData
  → sortieren/filtern/einfügen
  → itemsInserted
      ├─ KItemListView passt Layout und sichtbare Widgets an
      ├─ SelectionManager verschiebt Current/Auswahl
      ├─ RolesUpdater startet Auflösung
      └─ DolphinView aktualisiert Itemzahl
  → directoryLoadingCompleted
  → DolphinView stellt Auswahl, Scrollposition und Expansion wieder her
```

### 8.2 Ein Model-Item ändert sich

```text
KDirLister::refreshItems oder Rollen-Updater
  → KFileItemModel::setData / slotRefreshItems
  → geänderte Rollen bestimmen
  → itemsChanged
  → falls sortierrelevant: Resort-Timer
      → itemsMoved
  → View aktualisiert sichtbare Widget-Daten und Size-Hints
  → Widget invalidiert nur betroffene Caches
```

Die Rollemenge in `itemsChanged` ist deshalb entscheidend für Performance. „Alles geändert“ zwingt
zu unnötiger Cache- und Größenarbeit.

### 8.3 Auswahl in die Anwendung melden

```text
Controller-Eingabe
  → KItemListSelectionManager
  → selectionChanged(KItemSet current, previous)
      ├─ KItemListView markiert sichtbare Widgets
      └─ DolphinView::slotSelectionChanged
           → 0 ms, wenn leer↔nicht leer; sonst 300 ms entprellt
           → KFileItemList erzeugen
           → DolphinView::selectionChanged
           → Statusleiste, Actions, MainWindow, Info-Panel
```

### 8.4 View-Modus wechseln

```text
DolphinView / ViewModeController
  → DolphinItemListView::setItemLayout
  → beginTransaction
  → Highlight/Expansion/Group-Collapse/Scrollrichtung anpassen
  → sichtbare Widgets neu konfigurieren
  → Zoom-/Rastergrößen anwenden
  → endTransaction
  → ein Relayout
  → sichtbaren Bereich und Preview-Größe verzögert aktualisieren
```

Es werden also weder Model noch Controller ausgetauscht. Auswahl, geladene Items und Rollen bleiben
erhalten.

---

## 9. Accessibility

Die Graphics-View-Struktur ist für Qt-Accessibility nicht automatisch eine brauchbare
Dateilisten-Hierarchie. Deshalb besitzt `kitemviews/accessibility/` eine eigene Brücke:

```text
KItemListContainerAccessible       QWidget-Welt ↔ eigene Accessibility-Welt
  └─ KItemListViewAccessible       Tabelle, Auswahl, Aktionen, View-Zustand
      └─ KItemListDelegateAccessible pro Model-Index
```

`KItemListViewAccessible` implementiert unter anderem
`QAccessibleSelectionInterface`, `QAccessibleTableInterface` und
`QAccessibleActionInterface`. Einzelne Dateien/Ordner werden durch
`KItemListDelegateAccessible` repräsentiert, nicht durch die gerade recycelten sichtbaren
`KItemListWidget`s. Dadurch bleiben auch nicht sichtbare Model-Items für Hilfstechnologien
adressierbar.

Die Accessible-Delegate-Liste muss genau wie Auswahl und View auf Inserts, Removes und Moves
reagieren. `KItemListContainer::focusInEvent()` verschiebt den zugänglichen Fokus verzögert zur
View und stößt eine vollständige Ansage an.

Konsequenz für Änderungen: Neue Interaktionszustände, Item-Texte oder Layoutsemantiken sind nicht
nur Painting-Arbeit. Accessible Name/Description, Rollen, Auswahl und Tabellenkoordinaten müssen
mitgedacht und mit einem Screenreader geprüft werden.

---

## 10. Dateilandkarte

### 10.1 Öffentliche Framework-Dateien

| Datei | Zweck |
|---|---|
| `kitemviews/kitemmodelbase.*` | abstrakter Model-Vertrag |
| `kitemviews/kfileitemmodel.*` | KIO-basiertes Datei-Model |
| `kitemviews/kfileitemmodelrolesupdater.*` | teure/asynchrone Rollen und Previews |
| `kitemviews/kitemlistview.*` | View-Kern, Virtualisierung, Layout-Koordination |
| `kitemviews/kitemlistwidget.*` | Basis eines sichtbaren Items |
| `kitemviews/kstandarditemlistview.*` | drei Standardlayouts |
| `kitemviews/kstandarditemlistwidget.*` | Standardpainting und Text-/Icon-Caches |
| `kitemviews/kfileitemlistview.*` | Datei-View und Rollen-Updater-Anbindung |
| `kitemviews/kfileitemlistwidget.*` | Dateiformatierung und Hover-Previews |
| `kitemviews/kitemlistcontroller.*` | gesamte Eingabeverarbeitung |
| `kitemviews/kitemlistselectionmanager.*` | Current/Selection/Anchor |
| `kitemviews/kitemlistcontainer.*` | QWidget-/Graphics-/Scrollbar-Brücke |
| `kitemviews/kitemlistheader.*` | öffentliche Header-Fassade |
| `kitemviews/kitemlistgroupheader.*` | Basis für Gruppenheader |
| `kitemviews/kstandarditemlistgroupheader.*` | Standard-Gruppenheader |
| `kitemviews/kitemset.*`, `kitemrange.h` | komprimierte Indexmengen/-bereiche |
| `kitemviews/kitemliststyleoption.*` | gemeinsame Darstellungsparameter |
| `kitemviews/kfileitemlisttostring.*` | textuelle Darstellung von KFileItem-Listen |

### 10.2 Interna unter `private/`

| Datei | Zweck |
|---|---|
| `kitemlistviewlayouter.*` | Index/Geometrie/Viewport/Gruppen-Layout |
| `kitemlistsizehintresolver.*` | Size-Hints über Modeländerungen verwalten |
| `kitemlistviewanimation.*` | Widget-Animationen |
| `kitemlistsmoothscroller.*` | animierte Offset-Änderungen |
| `kitemlistheaderwidget.*` | sichtbarer/interaktiver Detail-Header |
| `kitemlistrubberband.*` | Auswahlrechteck-Zustand |
| `kitemlistselectiontoggle.*` | Hover-Häkchen |
| `kitemlistkeyboardsearchmanager.*` | Type-ahead-Puffer und Suchlogik |
| `kitemlistroleeditor.*` | Inline-Texteditor |
| `kfileitemmodelfilter.*` | Name-/Glob-/Regex-/MIME-Filter |
| `kfileitemmodelsortalgorithm.h` | paralleler Merge-Sort |
| `kdirectorycontentscounter*` | asynchrone rekursive Ordnerzählung |
| `kbaloorolesprovider.*` | Baloo-Metadaten → Modelrollen |
| `kfileitemclipboard.*` | schneller Cache ausgeschnittener URLs |
| `kpixmapmodifier.*` | Preview-Skalierung und Rahmen |
| `kitemviewsutils.*` | kleine View-Hilfen, u. a. Device-Pixel-Ratio |

### 10.3 Dolphin-spezifische Dateien außerhalb von `kitemviews/`

| Datei | Bezug zu `kitemviews` |
|---|---|
| `views/dolphinview.*` | erzeugt MVC, verbindet App-Signale, Navigation und Dateioperationen |
| `views/dolphinitemlistview.*` | Zoom und ViewProperties |
| `views/dolphinfileitemlistwidget.*` | VCS-Overlay aus `version` |
| `views/viewmodecontroller.*` | Actions für Icons/Kompakt/Details |
| `views/viewproperties.*` | persistente Layout-, Rollen-, Sortier- und Preview-Einstellungen |
| `views/versioncontrol/versioncontrolobserver.*` | befüllt `version` |
| `views/draganddrophelper.*` | KIO-Drop-Hilfen |

---

## 11. Wo welche Änderung hingehört

### 11.1 Neue zusätzliche Dateirolle/Spalte

Für eine synchron billige Rolle:

1. Rolle und Beschreibung in `KFileItemModel::RoleType`/`rolesInfoMap()` ergänzen;
2. Mapping in `typeForRole()` und `roleForType()` ergänzen;
3. Wert in `retrieveData()` nur bei angeforderter Rolle setzen;
4. Textformatierung in `KFileItemListWidgetInformant::roleText()` ergänzen;
5. Sortier- und Gruppierungsverhalten in `sortRoleCompare()`/`groups()` prüfen;
6. Modeltests für Rollenwechsel, Sortierung und Filterzustand ergänzen.

Für eine teure Rolle gehört die Berechnung stattdessen in den Rollen-Updater bzw. einen klar
abgegrenzten Provider. Insbesondere keine synchrone Datei-I/O in `data()` oder `paint()` einbauen.

### 11.2 Neue Item-Dekoration

Mögliche Wege:

- standardisiertes generisches Icon-Overlay: `KOverlayIconPlugin` → `iconOverlays`;
- Whale-interner Zustand: neue Rolle, im `DolphinFileItemListWidget::refreshCache()` in Overlay oder
  Farbe übersetzen;
- vollständig neues Painting: möglichst in einer Widget-Ableitung und cachebewusst;
- Zustand mit Einfluss auf Geometrie: zusätzlich Informant, Size-Hints und
  `itemSizeHintUpdateRequired()` anpassen.

Eine Dekoration darf nicht voraussetzen, dass für jedes Item dauerhaft ein Widget existiert.

### 11.3 Neue Interaktion

- Maus-/Tastatur-/Touch-Semantik: `KItemListController`;
- neue Trefferzone: abstrakte Rechtecke in `KItemListWidget` und konkrete Berechnung im
  Standard-/Datei-Widget;
- App-Aktion als Reaktion: Controller-Signal und Verbindung in `DolphinView`;
- eigentliche Dateioperation: `DolphinView`/KIO, nicht im Controller;
- globale Action/Shortcut: `DolphinViewActionHandler` oder `DolphinMainWindow`, nicht in
  `kitemviews`.

### 11.4 Neuer View-Modus

Das ist eine große Änderung, weil derzeit mehrere `switch (ItemLayout)`-Stellen exakt drei Modi
annehmen. Betroffen wären mindestens:

- `KStandardItemListView::ItemLayout` und Default-Style;
- `KStandardItemListWidget::Layout`, Geometrie, Painting und Size-Hints;
- Layouter-/Scrollorientierung;
- `DolphinItemListView::updateGridSize()` und Zoomtabellen;
- ViewProperties, Settings, Actions und Persistenz;
- DnD-Pixmaps, Header, Gruppierung, Expansion und Accessibility;
- Tests für Navigation und Hit-Zonen.

Ein bloßes neues Widget reicht nicht.

### 11.5 Alternative Datenquelle

Ein anderes Model kann von `KItemModelBase` ableiten. Es muss jedoch mehr als `count()` und
`data()` korrekt liefern:

- präzise Änderungsbereiche;
- stabile Rollen und Keyboard-Suche;
- MIME-Daten/DnD-Auskünfte, falls gewünscht;
- Sortierung/Gruppierung oder bewusst leere Sortierrolle;
- optionale Baum-API;
- URL-/Directory-Auskünfte, wenn der bestehende Controller-DnD-Pfad genutzt wird.

`KFileItemListView` verlangt konkret `KFileItemModel`; für wirklich andere Daten ist daher eher
`KStandardItemListView` plus eigene Ableitungen der passende Einstieg.

---

## 12. Fallstricke und Invarianten

1. **Index ist keine Identität.** Nach Move/Insert/Remove niemals einen alten Index dauerhaft für
   dieselbe Datei halten. Für asynchrone Arbeit `KFileItem`/URL speichern und den Index später neu
   über das Model bestimmen.
2. **Nur sichtbare Items haben Widgets.** Logik, die alle Items betrifft, gehört ins Model oder in
   einen Provider, nicht in eine Schleife über `visibleItemListWidgets()`.
3. **Recycling löscht nicht automatisch jeden Subklassenzustand.** Bei neuen Widget-Feldern alle
   Datenwechselpfade und Cache-Invalidierung prüfen.
4. **`data()` muss billig bleiben.** MIME-Ermittlung, Stat, Verzeichnisrekursion oder Thumbnailing
   im synchronen Getter würde Scrollen und Laden blockieren.
5. **Rollenänderungen können neu sortieren.** Der Rollen-Updater darf während eigener Updates keine
   unkontrollierte Rückkopplung erzeugen; `setModelData()` schützt genau diesen Pfad.
6. **Sortierrolle und sichtbare Rollen sind verschieden.** Eine unsichtbare Sortier- oder
   Gruppenrolle muss trotzdem angefordert und aufgelöst werden.
7. **Baum und Filter sind gekoppelt.** Ein passendes Kind braucht sichtbare Ahnen; Collapse muss
   auch gefilterte Nachfahren entsorgen.
8. **Modelsignale sind Verträge.** Falsche Bereiche beschädigen Auswahl, sichtbare Widgets,
   Size-Hints und Accessibility zugleich.
9. **Graphics- und QWidget-Koordinaten unterscheiden sich.** DnD, Kontextmenü und Hit-Tests mappen
   mehrfach zwischen global, Viewport, Scene, View und Item.
10. **Layoutänderungen bündeln.** Mehrere `set…()`-Aufrufe ohne Transaktion können wiederholt
    Layout, Preview-Aktualisierung und Animation auslösen.
11. **Remote-URLs mitdenken.** `KFileItem` und KIO sind URL-basiert; lokales `QFileInfo` ist nur an
    ausdrücklich geschützten Stellen zulässig.
12. **Accessibility ist eine eigene parallele Sicht.** Ein visuell korrektes Feature kann für
    Screenreader dennoch falsch oder unsichtbar sein.
13. **Dolphin-spezifischen Code nicht unnötig nach unten ziehen.** VCS- oder Whale-Logik gehört in
    `views/`, solange das allgemeine Framework sie nicht benötigt.
14. **Private API bleibt upstream-freundlich.** Große Umbenennungen oder interne Neuordnungen machen
    KDE-Merges unnötig schwer; Erweiterungen sollten klein und an natürlichen Hooks bleiben.

---

## 13. Tests und sinnvolle Teststrategie

### 13.1 Direkte Unit-Tests

| Test | Schwerpunkt |
|---|---|
| `kfileitemmodeltest` | Listing, Rollen, Insert/Remove, Sortierung, Filter, Gruppen, Expansion, MIME-Daten |
| `kfileitemmodelbenchmark` | viele Items einfügen/entfernen, Performance |
| `kitemlistselectionmanagertest` | Current, Anker, Auswahl und Indexnachführung |
| `kitemlistcontrollertest` | Maus/Tastatur, Auswahl, DnD-Hover, Gruppen-Collapse, Inline-Rename |
| `kitemlistcontrollerexpandtest` | Ordnerexpansion |
| `kitemlistcontainertest` | Scrollbar-/Scroller-Synchronisierung |
| `kitemlistsmoothscrollertest` | Scrollziele und Abschluss-Signale |
| `kitemlistkeyboardsearchmanagertest` | Type-ahead und Rückwärtssuche |
| `kitemlistroleeditortest` | Fokusverhalten des Inline-Editors |
| `dolphinitemlistviewtest` | konkrete Datei-View, Geometrie und Darstellung |

### 13.2 Focused Runs

Der Whale-Build liegt außerhalb des Repositories und darf nicht neu konfiguriert werden. Typische
Aufrufe:

```bash
cmake --build ../build-whale --target dolphin
ctest --test-dir ../build-whale -R 'kfileitemmodeltest|kitemlist.*test' --output-on-failure
../build-whale/bin/kfileitemmodeltest testExpandItems
../build-whale/bin/kitemlistcontrollertest testMouseClickActivation
```

### 13.3 Was zusätzlich manuell geprüft werden sollte

- sehr großes lokales Verzeichnis und langsame/entfernte KIO-URL;
- Icons-, Kompakt- und Detailansicht bei mehreren Zoomstufen;
- Sortieren nach einer teuren Rolle und schnelles Umsortieren;
- gruppiert, Gruppen ein-/ausklappen;
- Detailbaum expandieren, gleichzeitig filtern, aktualisieren und einklappen;
- Preview an/aus, Zoom während laufender PreviewJobs, Hover-Sequenz;
- Ctrl/Shift/Rubberband, Single-/Double-Click und Selection Mode;
- Drag auf Ordner, Drag zwischen Items und Auto-Activation;
- Inline-Rename mit Fokuswechsel;
- HiDPI, RTL, dunkles Farbschema;
- Tastatur-only und Screenreader.

---

## 14. Empfohlene Lesereihenfolge

### Erster Durchgang: Hauptfluss

1. `views/dolphinview.cpp:89–242` – Konstruktion und Signalverdrahtung
2. `kitemviews/kitemmodelbase.h` – Model-Vertrag
3. `kitemviews/kfileitemmodel.h` – öffentliche API und interne Datenstruktur
4. `kitemviews/kitemlistcontroller.h` – Eingaben und ausgehende Signale
5. `kitemviews/kitemlistview.h` – View-Verantwortung und Creator
6. `kitemviews/kitemlistcontainer.cpp:26–112, 244–410` – Graphics-/Scroll-Brücke

### Zweiter Durchgang: Rendering und Performance

7. `kitemviews/kstandarditemlistview.cpp`
8. `kitemviews/kitemlistview.cpp:1864–2399` – Model-Anbindung, Layout, Recycling, Gruppen
9. `kitemviews/kstandarditemlistwidget.h/.cpp` – Geometrie, Caches, Painting
10. `kitemviews/private/kitemlistviewlayouter.h/.cpp`
11. `kitemviews/kfileitemlistview.cpp` – Rollen-Updater-Anbindung
12. `kitemviews/kfileitemmodelrolesupdater.h/.cpp` – asynchrone Datenpipeline

### Dritter Durchgang: schwierige Sonderfälle

13. `kitemviews/kfileitemmodel.cpp:874–1205` – Expansion plus Filter
14. `kitemviews/kfileitemmodel.cpp:1329–2102` – Sortieren, Insert/Remove
15. `kitemviews/kitemlistcontroller.cpp:238–524, 604–1010, 1649–1950` – Interaktion
16. `kitemviews/kitemlistselectionmanager.cpp`
17. `kitemviews/accessibility/`
18. die zugehörigen Tests, besonders `kfileitemmodeltest.cpp` und
    `kitemlistcontrollertest.cpp`

---

## 15. Kurze mentale Modelle

Zum Merken helfen diese fünf Sätze:

- **Das Model ist eine sortierte, flache Liste von Dateidaten, kein Qt-Baum.**
- **Die View kennt alle Geometrien, erzeugt aber nur Widgets für Sichtbares.**
- **Der Controller interpretiert Eingaben, besitzt die Auswahl aber nicht selbst.**
- **Der Rollen-Updater macht teure Daten schrittweise und viewportnah verfügbar.**
- **`DolphinView` übersetzt zwischen diesem internen Index-/Rollen-System und der übrigen
  URL-/`KFileItem`-basierten Anwendung.**
