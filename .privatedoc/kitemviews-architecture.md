# `kitemviews` – architecture of Dolphin's file view

State: `9449d3da9` of 2026-09-30 (Dolphin 26.11.70-dev, still under the working title Whale).
Written 2026-09-30, translated to English on 2026-10-26. The statements were checked against the
source code of that state. Paths are relative to `src/`; line numbers refer to that state and can
shift with upstream merges. The fork's own changes to the framework since then (expansion in
Compact view, expansion tints, filter scope, recursive content, linked selection, …) are described
in `design-notes.md`.

This document deepens the overview in `.privatedoc/dolphin-architecture.md`. It describes the own
model/view/controller framework under `kitemviews/`, its connection to `DolphinView` and Dolphin's
subclasses under `views/`.

---

## 0. The essentials in 12 points

1. `kitemviews` is **not** built on `QAbstractItemModel`, `QListView`, `QTreeView` or
   `QStyledItemDelegate`. It is an own, index-based MVC framework on
   `QGraphicsView`/`QGraphicsWidget`.
2. The model is a **flat list**. An item is described by an integer index and a small map from role
   name (`QByteArray`) to value (`QVariant`). The tree of the Details view is folded into this list;
   parent relations live in `ItemData::parent`.
3. `KFileItemModel` connects the framework to KIO: a `KDirLister` delivers `KFileItem`s and changes of
   folder contents. The model sorts, filters, groups and manages expanded subfolders.
4. The model computes only **cheap roles** directly. MIME detection, icons, previews, folder sizes,
   overlay icons and Baloo metadata are handled by `KFileItemModelRolesUpdater`, delayed and
   depending on visibility.
5. `KItemListView` does not create a widget for every model item. It keeps widgets only for the
   visible range, recycles instances that became invisible and computes the rest through
   `KItemListViewLayouter` and size hints.
6. Icons, Compact and Details are **three layouts of the same view and widget classes**, not three
   separate views.
7. `KItemListController` handles mouse, keyboard, touch, gestures and drag and drop. The selection
   itself lives separately in the `KItemListSelectionManager`.
8. `KItemListContainer` is the QWidget bridge: a `QAbstractScrollArea` whose viewport is a
   `QGraphicsView` without scroll bars, with its own `QGraphicsScene`. The actual view scrolls
   through its offset properties.
9. Ownership chain: `KItemListContainer` owns the controller; the controller owns model, view,
   selection manager and keyboard search manager; the view owns layouter, roles updater, header,
   animations and the factories for recycled item widgets.
10. Model indexes are deliberately short-lived. After insert/remove/sorting, selection, current
    position, visible widgets and accessibility objects must be updated from the model signals.
    Lasting identity for files comes from `KFileItem`/`QUrl`, not from the index.
11. The Dolphin-specific layer is thin: `DolphinItemListView` translates view properties and zoom
    levels; `DolphinFileItemListWidget` adds VCS overlays. Almost all mechanics stay in the general
    `kitemviews` framework.
12. Changes to this framework have a wide reach. Besides the main view it is used by the folder tree
    and tests, among others; performance, touch, accessibility, remote URLs, sorting and running KIO
    jobs must be considered together.

---

## 1. Placement and system boundary

### 1.1 Where `kitemviews` sits in the program

```text
DolphinMainWindow
  └─ DolphinTabWidget
      └─ DolphinTabPage
          └─ DolphinViewContainer       application bars around the file view
              └─ DolphinView            functional bridge to the application
                  └─ KItemListContainer QWidget/scrolling
                      └─ KItemListController
                          ├─ KFileItemModel
                          │   └─ KDirLister (KIO)
                          ├─ DolphinItemListView
                          │   └─ visible DolphinFileItemListWidgets
                          ├─ KItemListSelectionManager
                          └─ KItemListKeyboardSearchManager
```

`DolphinView` creates these objects in `views/dolphinview.cpp:89–242`. It is the boundary between the
generic item framework and the rest of the file manager:

- downwards: model indexes, roles, controller signals and graphics widgets;
- upwards: `KFileItemList`, URLs, loading/stopping, file operations, context menus, status and
  selection signals.

`DolphinView` does not know `DolphinMainWindow`, so the same file view can also be used in the KPart.

### 1.2 What belongs to the framework

| Area | Main classes | Task |
|---|---|---|
| Model contract | `KItemModelBase` | index and role API, change, sort and grouping signals |
| File model | `KFileItemModel` | KIO listing, `KFileItem` storage, filter, sorting, groups, expansion |
| Role resolution | `KFileItemModelRolesUpdater` | icons, previews, MIME, folder contents, overlays, Baloo |
| View | `KItemListView` | visible items, recycling, layout, header, groups, animations |
| Standard presentation | `KStandardItemListView`/`Widget` | Icons/Compact/Details layouts and painting |
| File presentation | `KFileItemListView`/`Widget` | file roles, previews, drag pixmaps, hover sequences |
| Dolphin adaptation | `DolphinItemListView`, `DolphinFileItemListWidget` | zoom, settings, VCS overlays |
| Input | `KItemListController` | mouse, keyboard, touch, gestures, DnD, context menu |
| Selection | `KItemListSelectionManager`, `KItemSet` | current item, anchor and compressed selection ranges |
| QWidget bridge | `KItemListContainer` | graphics scene, scroll bars, smooth scrolling, focus |
| Internals | `kitemviews/private/` | layouter, size hints, header widget, rubber band, editor and so on |
| Accessibility | `kitemviews/accessibility/` | own QAccessible hierarchy for view and items |

The files are built in `dolphinprivate` (`src/CMakeLists.txt:64–200`). Many classes are exported with
`DOLPHIN_EXPORT`, but they form no stable, separately published plugin API. For the fork this stays
a private implementation.

### 1.3 A second concrete user: the Folders panel

The framework is not only the big file view. `FoldersPanel::showEvent()` also creates a complete set
of `KFileItemModel`, `KFileItemListView`, `KItemListController` and `KItemListContainer` when first
shown (`panels/folders/folderspanel.cpp:122–170`). Differences to the main view:

- the model shows only folders;
- the view uses `FoldersItemListWidget` and always enables expansion;
- the controller works with single selection;
- single click is enforced;
- auto activation only expands or collapses instead of taking the normal file activation path.

This second user is a good architecture test: general improvements belong in `kitemviews`; behavior
that only concerns Dolphin's central file view rather belongs in `views/`.

### 1.4 Why not a Qt standard item view

The headers name "Itemviews NG" from Trolltech Labs as the historical starting point. In today's code
the practical reasons are visible directly:

- full control over geometry, painting, hit zones and animations;
- only visible item widgets instead of one widget or delegate instance per model entry;
- the same building blocks for icons, a compact multi-column layout and details/table;
- an own tree presentation within a flat model;
- graded, visibility-dependent resolution of expensive file properties;
- own touch, rubber band, hover, DnD and inline rename semantics.

The price is a lot of self-implemented infrastructure: selection, index shifting, scroll bars, focus
forwarding, header, accessibility and model/view synchronization.

---

## 2. Object graph, ownership and lifetime

### 2.1 Construction

`DolphinView::DolphinView()` builds the chain in this order (`views/dolphinview.cpp:134–172`):

```text
new KFileItemModel
new DolphinItemListView
new KItemListController(model, view)
new KItemListContainer(controller)
```

The controller makes itself the QObject parent of model and view
(`kitemviews/kitemlistcontroller.cpp:91–149`). The container in turn becomes the parent of the
controller and deliberately deletes it before the `QGraphicsScene` in its destructor
(`kitemviews/kitemlistcontainer.cpp:58–112`), so the scene does not disappear before the
`KItemListView` registered in it.

### 2.2 Ownership matrix

| Owner | Owned object | Remark |
|---|---|---|
| `DolphinView` | `KItemListContainer` | normal QWidget child in the layout |
| `KItemListContainer` | `KItemListController`, `QGraphicsScene`, smooth scroller, `QScroller` | the controller is deleted first explicitly |
| `KItemListController` | model, view, selection manager, keyboard search manager, timers | `setModel()`/`setView()` take ownership |
| `KItemListView` | layouter, size hint resolver, animation, rubber bands, header, creator | the view is also an item in the scene |
| `KFileItemListView` | `KFileItemModelRolesUpdater`, debounce timers | the updater is rebuilt when the model changes |
| `KItemListWidgetCreator` | created and recyclable item widgets, shared informant | factory and object pool in one |
| `KFileItemModel` | `KDirLister`, `ItemData` objects, timers, filter state | the model owns the file data |
| Roles updater | preview jobs, counter, Baloo monitor, plugin references | much of the work is asynchronous |

Important: QObject parenthood and graphics item membership overlap. As a QObject the view belongs to
the controller, but `KItemListContainer::slotViewChanged()` also puts it into the `QGraphicsScene`
(`kitemviews/kitemlistcontainer.cpp:256–287`).

### 2.3 Replacing model or view

`KItemListController::setModel()` and `setView()` are real replacement points:

1. the old object is released with `deleteLater()`;
2. the new object gets the controller as parent;
3. selection manager and view get the new model;
4. `modelChanged` or `viewChanged` is emitted;
5. the container takes the view out of or puts it into the graphics scene;
6. `KFileItemListView::onModelChanged()` discards and recreates the roles updater.

Dolphin rarely does such a replacement, but the API explains why the components are not wired
together firmly in the constructor.

---

## 3. The data model

### 3.1 `KItemModelBase`: a small, own model contract

`KItemModelBase` (`kitemviews/kitemmodelbase.h`) only inherits `QObject`. The minimal API consists of:

- `count()` – number of visible entries;
- `data(index)` – role values as a `SmallHash`;
- optionally `setData(index, values)`;
- sort role, group role and sort order;
- optionally grouping, expansion, MIME data, keyboard search and DnD information.

There is no `QModelIndex` and no parent/child indexes. An index is simply `0 … count()-1`.

The central change contracts are:

```text
itemsInserted(KItemRangeList)
itemsRemoved(KItemRangeList)
itemsMoved(KItemRange, movedToIndexes)
itemsChanged(KItemRangeList, changedRoles)
groupsChanged()
```

`KItemRange { index, count }` combines contiguous changes. For `itemsInserted`/`itemsRemoved` the
ranges refer to the order before the change. `itemsMoved` gives the new index for each index of a
contiguous old range. This exact semantics lets view and selection manager repair their purely
index-based states.

### 3.2 `KFileItemModel`: storage

The central record is in `kitemviews/kfileitemmodel.h:385–389`:

```cpp
struct ItemData {
    KFileItem item;
    SmallHash values;
    ItemData *parent;
};
```

The essential containers are:

- `m_itemData`: the currently visible, sorted, flat list;
- `m_items`: URL-to-index cache for O(1) lookup of the prefix already cached;
- `m_filteredItems`: hidden `ItemData` objects that can be reinserted when filters change;
- `m_pendingItemsToInsert`: entries buffered during a running listing;
- `m_expandedDirs`: expanded target URL → visible URL;
- `m_urlsToExpand`: URLs still to be expanded step by step during a restore;
- `m_groups`: cache of group starts and group values.

`data(index)` initializes `ItemData::values` lazily on first access through `retrieveData()` and
injects the URL only into the returned value; the URL is not stored a second time per item in the
role cache (`kfileitemmodel.cpp:382–414`).

### 3.3 `SmallHash`: storing roles sparingly

Since 2026 each item no longer uses a `QHash<QByteArray, QVariant>` but `SmallHash` (`smallhash.h`).
Internally it is a flat `std::vector<std::pair<QByteArray, QVariant>>`:

- lookup is O(n), but cheap for the small number of roles;
- the large minimum allocation of a Qt hash per item disappears;
- `toHash()`/`fromHash()` bridge to older code.

A deliberate optimization for big folders: the sum of many small per-item allocations matters more
than the asymptotic lookup time of a single role map.

### 3.4 Roles: the data protocol between model and presentation

Role names are byte strings. There is no central public enum API. Important groups:

| Kind | Examples | Origin |
|---|---|---|
| Identity/text | `url`, `text`, `path`, `folder`, `extension` | `KFileItemModel` |
| File system | `size`, `modificationtime`, `permissions`, `owner`, `group` | model or updater |
| Type/icon | `type`, `iconName`, `iconPixmap`, `iconOverlays` | model + roles updater |
| Tree | `isDir`, `isExpanded`, `isExpandable`, `expandedParentsCount` | model + folder counter |
| Preview sequence | `supportsSequencing`, `hoverSequencePixmaps`, `hoverSequenceWraparoundPoint` | roles updater |
| Baloo | `tags`, `rating`, `comment`, `dimensions`, `duration`, `artist`, … | `KBalooRolesProvider` |
| Dolphin/VCS | `version` | `VersionControlObserver`, presented in `DolphinFileItemListWidget` |

`KFileItemModel::rolesInfoMap()` (`kfileitemmodel.cpp:3157–3216`) describes the user-selectable
columns with translation, category and Baloo/indexer dependency. Internal roles such as `iconPixmap`
or `version` are not in this list.

`KFileItemListView::applyRolesToModel()` merges visible roles with internally needed roles and passes
the set to both the model and the roles updater. A role does not have to be visible to be needed for
sorting, grouping or the tree presentation.

### 3.5 Cheap and expensive data

`KFileItemModel::retrieveData()` (`kfileitemmodel.cpp:2231–2382`) reads only values available
without problematic I/O, for example name, raw timestamps, permissions or UDS fields. It explicitly
avoids `KFileItem::iconName()` when that would first require determining the MIME type.

The split is:

```text
KFileItemModel::retrieveData()
  └─ synchronous, cheap, for requested roles

KFileItemModelRolesUpdater
  └─ potentially expensive, delayed or asynchronous:
       MIME type and final icon
       KIO preview
       overlay plugins
       folder count/size
       Baloo metadata
```

So a big listing is usable early, although its detail information keeps arriving bit by bit.

### 3.6 KIO and folder changes

`KFileItemModel` creates a `KDirLister` in its constructor (`kfileitemmodel.cpp:264–341`). The life
cycle of a normal listing:

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

Further KDirLister signals lead to:

- `itemsDeleted` → `slotItemsDeleted()` → `removeItems()`;
- `refreshItems` → `slotRefreshItems()` → invalidate/change roles and resort if needed;
- `clear` → `slotClear()`;
- `completed` → insert pending items and continue expansion;
- `canceled`, errors and redirects → corresponding model signals to `DolphinView`.

A folder listing is therefore not a one-time snapshot: the model keeps observing the location and
processes later file changes.

### 3.7 Sorting

The sort logic is entirely in the model:

- `lessThan()` considers tree parents, "folders first", "hidden last", the sort role and natural name
  sorting;
- `sort()` uses `private/kfileitemmodelsortalgorithm.h`, a parallel merge sort;
- `m_resortAllItemsTimer` bundles several role-caused changes;
- `resortAllItems()` builds a URL list of the old order, sorts, computes `movedToIndexes` from it and
  emits `itemsMoved` (`kfileitemmodel.cpp:1329–1408`);
- `sortingFinished()` means the order is final and no further resort is pending.

If the sort role is expensive, the roles updater first has to resolve it for **all** items. The first
200 ms at most run synchronously, the rest piece by piece through the event loop. Only then is a
correct sort possible.

### 3.8 Filtering

`KFileItemModelFilter` supports:

- plain substring;
- glob patterns;
- regular expressions;
- including and excluding MIME filters;
- optional case sensitivity.

When filtering, items are not destroyed but moved between `m_itemData` and `m_filteredItems`
(`KFileItemModel::applyFilters()`, `kfileitemmodel.cpp:1116–1196`). With expanded folders, the
ancestor chain of a matching child stays visible, which makes the filter code tree code too and much
more complex than a plain list filter.

### 3.9 Grouping

`groups()` returns a list of `(first item index, group value)`. There are own grouping functions for
name, size, time, permissions, rating and generic strings. The view creates `KItemListGroupHeader`
objects from them.

Grouping does not change the identity of items. When the order changes, `itemsMoved` implies that
groups changed as well; `groupsChanged` is only needed when the order stays the same.

Groups can currently be collapsed in Icons and Details. The collapsed state lives in the view
(`m_collapsedGroups`), not in the model; the layouter then skips the group members while the header
stays visible.

### 3.10 A tree view in a flat list

Only the Details layout supports expandable items in Dolphin's file view (the fork added Compact,
CX1). The model still stays flat:

```text
Index 0  Folder A        parent = nullptr, expandedParentsCount = 0
Index 1    File A1       parent = Folder A, expandedParentsCount = 1
Index 2    Folder A2     parent = Folder A, expandedParentsCount = 1
Index 3      File A2a    parent = Folder A2, expandedParentsCount = 2
Index 4  File B          parent = nullptr, expandedParentsCount = 0
```

`setExpanded(index, true)` opens the subfolder with `KDirLister::Keep`; its items are sorted into the
flat order. On collapse the model stops/forgets the sub-listings and removes the contiguous range of
descendants (`kfileitemmodel.cpp:874–941`).

The view draws indentation, expanders and connecting lines from `isExpandable`, `isExpanded`,
`expandedParentsCount` and the computed sibling information. There is no recursive view tree.

---

## 4. The view and widget hierarchy

### 4.1 Two parallel inheritance chains

The view determines arrangement, visible range and object management:

```text
QGraphicsWidget
  └─ KItemListView
      └─ KStandardItemListView
          └─ KFileItemListView
              └─ DolphinItemListView
```

The single visible item determines geometries, hit zones, caches and painting:

```text
QGraphicsWidget
  └─ KItemListWidget
      └─ KStandardItemListWidget
          └─ KFileItemListWidget
              └─ DolphinFileItemListWidget
```

The separation matters: a new view property does not automatically belong in the item widget, and a
new visual item role not automatically in the view.

### 4.2 Responsibilities of the four view levels

#### `KItemListView`

- holds model and controller references;
- reacts to insert/remove/move/change and selection changes;
- owns layouter, header, group headers, rubber band and animations;
- computes visible indexes;
- creates/recycles visible widgets;
- offers hit tests (`itemAt`, `isAboveText`, `isAboveSelectionToggle`, …);
- coordinates inline editing and the drop indicator.

#### `KStandardItemListView`

- introduces `IconsLayout`, `CompactLayout` and `DetailsLayout`;
- sets scroll direction and style defaults;
- configures standard widgets for each layout;
- enables expansion, group collapse and full row highlighting per layout.

Compact scrolls horizontally; Icons and Details scroll vertically (`kstandarditemlistview.cpp:27–47`).

#### `KFileItemListView`

- requires a `KFileItemModel`;
- creates the `KFileItemModelRolesUpdater`;
- keeps visible roles and viewport range in sync with the updater;
- controls preview display, thumbnail plugins, icon size and device pixel ratio;
- creates file drag pixmaps and forwards hover sequences.

#### `DolphinItemListView`

- translates zoom levels into icon, preview and grid sizes;
- reads/writes Dolphin's view settings;
- decides Dolphin's Details behavior for expansion and full rows;
- uses `DolphinFileItemListWidget` as the concrete item class.

### 4.3 Responsibilities of the widget levels

#### `KItemListWidget`

The basic state of a visible item:

- index and role `SmallHash`;
- selected/current/hovered/pressed;
- visible roles, column widths, style option;
- selection and expander hit rectangles;
- hover animation and selection toggle;
- abstract text and selection geometries.

#### `KStandardItemListWidget`

- computes the three layout forms;
- caches icon/pixmap, texts, rectangles, rating and overlays;
- paints selection, icon, text columns, tree lines and additional information;
- provides inline rename through `KItemListRoleEditor`;
- reacts to the cut clipboard state and the hidden marker.

#### `KFileItemListWidget`

- formats file roles into readable texts;
- knows symbolic links and hidden files;
- handles selecting the name without extension when renaming;
- starts/stops hover preview sequences through the view.

#### `DolphinFileItemListWidget`

- evaluates the Dolphin-specific role `version`;
- translates `KVersionControlPlugin::ItemVersion` into an overlay icon;
- adds it when refreshing the cache.

### 4.4 Informants: computing sizes without creating widgets

The view has to know the total extent and preferred column widths, although invisible items have no
widget. `KItemListWidgetInformant` and its subclasses serve this purpose.

A `KItemListWidgetCreator<T>` owns exactly one informant, uses it for size hints and creates or
recycles `T` only for really visible items. Layout computation is thus decoupled from widget
creation.

### 4.5 Virtualization and recycling

`KItemListView::doLayout()` (`kitemlistview.cpp:1939 ff.`) is the central flow:

1. the layouter determines the first and last visible index;
2. `recycleInvisibleItems()` collects widget instances that are no longer visible;
3. for each visible, non-collapsed index an existing widget is taken over or one is fetched from the
   creator factory;
4. `updateWidgetProperties()` sets index, data, roles, selection/current and style;
5. geometry and group headers are updated;
6. depending on the change, position, creation or removal is animated;
7. scroll and item offsets are signaled to the container.

`m_visibleItems` is a `QHash<int, KItemListWidget *>`; the number of widgets grows roughly with the
viewport, not with the whole folder.

Important for extensions: a widget does not keep the same file when recycled. Every state that
depends on an item must be fully reset or cleanly invalidated in `setIndex`/`setData`/
`initializeItemListWidget`.

### 4.6 Layout computation

`KItemListViewLayouter` (`private/kitemlistviewlayouter.*`) knows:

- view size, item size and margins;
- scroll direction, main scroll offset and orthogonal item offset;
- header and group header height;
- variable row heights or column widths from size hints;
- group starts and collapsed groups;
- index ↔ row/column ↔ rectangle;
- first/last visible index and maximum scroll extent.

Changes only mark the layouter as dirty; the expensive recomputation happens lazily on the next
read. `KItemListSizeHintResolver` keeps the per-item size hints stable across insert/remove/move.

### 4.7 Transactions and animations

Several related property changes are bundled with `beginTransaction()`/`endTransaction()`, for
example when switching the layout. While a transaction is active, no full layout runs; at the end at
most one relayout follows, and as soon as one partial change rules out animation, the whole
transaction is not animated.

`KItemListViewAnimation` manages creating, removing, moving and resizing visible graphics widgets.
`animateChangedItemCount()` limits animations for large amounts so mass insertions do not get
needlessly expensive.

### 4.8 Header and group headers

The Details header consists of:

- `KItemListHeader`: public QObject facade for column widths, automatic resizing and side padding;
- `KItemListHeaderWidget`: the graphics widget actually painted and interacted with;
- the visible roles as column order;
- `preferredColumnWidths()`, computed from informant and model data.

A click on a column changes sort role or order already in the model and then also emits a view signal
for the application. Moving and resizing columns updates the visible item widgets.

Group headers are kept separately from item widgets (`m_visibleGroups`), independent of item
recycling. `KStandardItemListGroupHeader` provides the standard presentation.

---

## 5. Container and scrolling

### 5.1 QWidget outside, graphics view inside

`KItemListContainer` inherits `QAbstractScrollArea`. In the constructor its viewport is replaced by a
private `KItemListContainerViewport : QGraphicsView`. This `QGraphicsView`:

- owns the `QGraphicsScene` with the `KItemListView`;
- shows no scroll bars of its own;
- forwards wheel events to the outer container.

The outer `QAbstractScrollArea` scroll bars are the visible ones. They do not move the
QGraphicsView's viewport but are coupled to two view properties:

- `scrollOffset`: the axis along which the list moves;
- `itemOffset`: the orthogonal axis, when an item or the Details columns are wider than the view.

### 5.2 Smooth scrolling

There is one `KItemListSmoothScroller` per axis. A wheel event chooses horizontal or vertical by
delta direction and visible scroll bars. The smooth scroller animates the view's offset property
directly and keeps the outer scroll bar in sync.

Scroll bar updates are postponed during a running animation
(`m_scrollOffsetScrollBarUpdatePending`, `m_itemOffsetScrollBarUpdatePending`), so a relayout does
not overwrite the current target in the middle of the movement.

### 5.3 Focus and event bridges

Keyboard and context menu events do not reliably reach the graphics widget through Qt's normal focus
route. The container therefore sends them explicitly with `QApplication::sendEvent()` to the view
(`kitemlistcontainer.cpp:146–173`); focus in/out is forwarded the same way.

The view forwards almost all events in `KItemListView::event()` to
`KItemListController::processEvent()`. Exceptions include palette/font, focus, group header clicks
and running inline editing.

---

## 6. Controller and interaction

### 6.1 Event flow

```text
QWidget/graphics event
  → KItemListContainer or QGraphicsScene
  → KItemListView::event()
  → KItemListController::processEvent()
  → specialized handler
       keyPressEvent
       mousePress/Move/Release/DoubleClick
       contextMenuEvent
       dragEnter/Move/Leave/Drop
       hoverEnter/Move/Leave
       wheel/resize
       gesture/touch
  → change the selection manager and/or emit a controller signal
  → DolphinView slot
  → application action, navigation, KIO job or outgoing signal
```

The controller knows model and view but does not paint. It asks the view for hit zones and geometries
and changes the model or the selection manager.

### 6.2 Selection state

`KItemListSelectionManager` holds:

- `m_currentItem`: keyboard focus/current item;
- `m_anchorItem`: starting point for Shift ranges;
- `m_selectedItems`: explicit selection as a `KItemSet`;
- `m_isAnchoredSelectionActive`: a range selection in progress.

Current and selected are separate concepts. An item can be current without being selected.

`KItemSet` stores consecutive indexes as ranges: a selection `{1,2,3,4,5}` occupies only
`KItemRange(1,5)`. Iteration stays ascending, which simplifies model and UI code.

The view forwards insert/remove/move signals to the selection manager, which shifts current item,
anchor and selection indexes or removes values that no longer exist
(`kitemlistselectionmanager.cpp:212–370`). Without this, every sort would make the selection jump to
other files.

### 6.3 Mouse

The essential click semantics are in `KItemListController::onPress()` and `onRelease()`:

- single/double click activation follows the style hint or an enforced setting;
- Ctrl toggles, Shift uses the anchor;
- a right click selects appropriately before requesting the context menu;
- a click on text, full row, selection toggle and expander can act differently;
- movement beyond `QApplication::startDragDistance()` starts drag and drop;
- a click into empty space may start the rubber band.

The exact hit areas come from the concrete item widget. A change to `selectionRectFull()`,
`selectionRectCore()`, `textRect()`, `selectionToggleRect()` or `expansionToggleRect()` therefore
directly changes the interaction logic.

### 6.4 Keyboard and type-ahead

`keyPressEvent()` handles navigation, selection, activation, expansion, Page Up/Down, Home/End and
zoom signals. For unevenly filled grids the controller remembers a geometric `m_keyboardAnchorPos`,
so repeated Up/Down stays in the same visual column where possible.

`KItemListKeyboardSearchManager` collects characters, handles repetition and backward search and asks
the model through `indexForKeyboardSearch()`. The controller then sets current item and selection;
`typeAheadUsed` delivers text and match to `DolphinView` for visual feedback.

### 6.5 Rubber band

`KItemListRubberBand` is only the state object of start/end point and active/inactive. The controller
computes the hit items in `slotRubberBandChanged()` and combines them with `m_oldSelection` depending
on Ctrl/Shift. The view paints the band and auto-scrolls at the edge.

### 6.6 Touch and gestures

The controller registers tap, tap-and-hold, pinch and own two-finger swipe and two-finger tap
recognizers. Among others:

- a tap is a touch-adapted click sequence;
- holding long requests the selection mode;
- pinch emits zoom signals;
- a swipe can trigger a Dolphin action;
- synthetic mouse events after touch are filtered to avoid double actions.

### 6.7 Drag and drop

At drag start the controller asks `model->createMimeData(selectedIndexes)` and the view for a drag
pixmap. During drag move:

- the target widget is determined by the stricter `widgetForDropPos()`;
- a folder can be highlighted and entered/expanded automatically after 750 ms;
- for models without a sort role, a drop **between** items is possible too;
- the model gives only visual hints through `supportsDropping()`;
- the receiver of the controller signal decides the final operation.

In Dolphin, `itemDropEvent` leads to `DolphinView::slotItemDropEvent()` and on to KIO. The framework
itself copies or moves no files.

### 6.8 Inline rename

```text
DolphinView::renameSelectedItems()
  → KItemListView::editRole(index, "text")
  → visible KStandardItemListWidget::setEditedRole("text")
  → KItemListRoleEditor (KTextEdit)
  → roleEditingFinished(index, "text", new text)
  → DolphinView::slotRoleEditingFinished()
  → KIO::moveAs(...)
```

The editor is coupled to the visible widget. The view prevents or ends editing appropriately on model
or event changes. For files, `KFileItemListWidget::selectionLength()` makes sure that typically the
name without the extension is selected at the start.

---

## 7. Pipeline for icons, roles and previews

### 7.1 Creation and inputs

`KFileItemListView::onModelChanged()` creates exactly one `KFileItemModelRolesUpdater` per model
(`kfileitemlistview.cpp:234–249`). The view gives it:

- requested roles;
- visible index range;
- maximum number of visible items;
- icon size and device pixel ratio;
- previews on/off and the enlarge option;
- enabled thumbnail plugins;
- hover item and sequence index.

Scrolling is debounced by 50 ms, zoom/icon size changes by 300 ms (`kfileitemlistview.cpp:26–58`).
During view transactions or these delays the updater is paused.

### 7.2 State machine

The updater has five states:

```text
Idle
Paused
ResolvingSortRole
ResolvingAllRoles
PreviewJobRunning
```

The pause remembers whether previews, icon size or roles changed and catches up on resuming.
Self-triggered `model->setData()` changes are protected against an endless loop by
`m_applyingChangesToModel`.

### 7.3 Prioritization

`startUpdating()` works in this order:

1. determine final icons of the visible items synchronously for up to 200 ms;
2. with an expensive sort role, complete its values for all items first;
3. build interesting indexes with `indexesToResolve()`;
4. start a preview job, or otherwise resolve roles item by item through 0 ms timers;
5. handle items changed in between again.

The priorities of `indexesToResolve()`:

1. visible files, then visible folders;
2. up to five pages after the viewport;
3. up to five pages before it, in reverse order;
4. the last page;
5. the first page;
6. further items up to the limit of 500.

So scrolling, Home and End feel fast without thumbnailing every huge folder completely.

### 7.4 Preview job

With previews on, one `KIO::PreviewJob` bundles all pending interesting items:

```text
KIO::PreviewJob::generated(item, image)
  → transformPreviewImage()
  → rolesData(item, index)
  → model->setData(index, { iconPixmap, supportsSequencing, ... })
  → KFileItemModel::itemsChanged(ranges, roles)
  → KItemListView::slotItemsChanged()
  → the visible widget invalidates its pixmap/text cache
  → the next paint() shows the preview
```

KIO's cache works with 128×128 or 256×256 pixels. The updater scales, frames and considers the DPR and
"enlarge small previews".

### 7.5 Hover sequences

On hover the item widget regularly increments a sequence index, which travels through
`KFileItemListView::setHoverSequenceState()` to the updater. Because of the preview job API, every
next image needs its own job. The updater buffers five images ahead and keeps sequence data for at
most the 20 most recently used items.

The normal preview stays in `iconPixmap`; index 0 of the hover sequence is not stored twice.

### 7.6 Folder count and size

Depending on `ContentDisplaySettings::directorySizeMode()` there are two ways:

- content count or slow items: `KIO::listDir`, not recursive, directly per item;
- recursive size computation: `KDirectoryContentsCounter` with a shared worker in its own `QThread`,
  FIFO and priority queues and `KDirWatch` invalidation.

Visible folders get high priority. Remote folders get an "unknown" value for sizes instead of a
local recursion.

### 7.7 Baloo and overlay plugins

With `HAVE_BALOO`, `KBalooRolesProvider` adds metadata roles and a `Baloo::FileMonitor` updates
changed files. These values matter especially for additional columns and sorting by them.

The updater also loads all plugins from `kf6/overlayicon`. Their overlay names are merged with
`KFileItem::overlays()` and passed to the presentation as `iconOverlays`. This is the existing generic
plugin entry into item decoration.

VCS overlays take a different path: `VersionControlObserver` sets the role `version`, and only
`DolphinFileItemListWidget` translates it into a concrete overlay.

---

## 8. Important end-to-end flows

### 8.1 Showing a new folder

```text
DolphinView::setUrl/loadDirectory
  → KFileItemModel::loadDirectory
  → KDirLister (KIO, asynchronous)
  → pending ItemData
  → sort/filter/insert
  → itemsInserted
      ├─ KItemListView adapts layout and visible widgets
      ├─ SelectionManager shifts current item/selection
      ├─ RolesUpdater starts resolving
      └─ DolphinView updates the item count
  → directoryLoadingCompleted
  → DolphinView restores selection, scroll position and expansion
```

### 8.2 A model item changes

```text
KDirLister::refreshItems or roles updater
  → KFileItemModel::setData / slotRefreshItems
  → determine changed roles
  → itemsChanged
  → if relevant for sorting: resort timer
      → itemsMoved
  → the view updates visible widget data and size hints
  → the widget invalidates only the affected caches
```

The role set in `itemsChanged` is therefore decisive for performance: "everything changed" forces
needless cache and size work.

### 8.3 Reporting the selection to the application

```text
controller input
  → KItemListSelectionManager
  → selectionChanged(KItemSet current, previous)
      ├─ KItemListView marks visible widgets
      └─ DolphinView::slotSelectionChanged
           → 0 ms when empty↔non-empty; otherwise debounced by 300 ms
           → build a KFileItemList
           → DolphinView::selectionChanged
           → status bar, actions, main window, Information panel
```

### 8.4 Switching the view mode

```text
DolphinView / ViewModeController
  → DolphinItemListView::setItemLayout
  → beginTransaction
  → adapt highlight/expansion/group collapse/scroll direction
  → reconfigure visible widgets
  → apply zoom/grid sizes
  → endTransaction
  → one relayout
  → update visible range and preview size with a delay
```

Neither model nor controller are replaced. Selection, loaded items and roles are kept.

---

## 9. Accessibility

The graphics view structure is not automatically a usable file list hierarchy for Qt's
accessibility. `kitemviews/accessibility/` therefore has its own bridge:

```text
KItemListContainerAccessible       QWidget world ↔ own accessibility world
  └─ KItemListViewAccessible       table, selection, actions, view state
      └─ KItemListDelegateAccessible per model index
```

`KItemListViewAccessible` implements, among others, `QAccessibleSelectionInterface`,
`QAccessibleTableInterface` and `QAccessibleActionInterface`. Single files/folders are represented by
`KItemListDelegateAccessible`, not by the currently recycled visible `KItemListWidget`s, so model
items that are not visible stay addressable for assistive technology.

The list of accessible delegates must react to inserts, removes and moves just like selection and
view. `KItemListContainer::focusInEvent()` moves the accessible focus to the view with a delay and
triggers a complete announcement.

Consequence for changes: new interaction states, item texts or layout semantics are not only painting
work. Accessible name/description, roles, selection and table coordinates have to be considered and
checked with a screen reader.

---

## 10. File map

### 10.1 Public framework files

| File | Purpose |
|---|---|
| `kitemviews/kitemmodelbase.*` | abstract model contract |
| `kitemviews/kfileitemmodel.*` | KIO-based file model |
| `kitemviews/kfileitemmodelrolesupdater.*` | expensive/asynchronous roles and previews |
| `kitemviews/kitemlistview.*` | view core, virtualization, layout coordination |
| `kitemviews/kitemlistwidget.*` | base of a visible item |
| `kitemviews/kstandarditemlistview.*` | three standard layouts |
| `kitemviews/kstandarditemlistwidget.*` | standard painting and text/icon caches |
| `kitemviews/kfileitemlistview.*` | file view and roles updater integration |
| `kitemviews/kfileitemlistwidget.*` | file formatting and hover previews |
| `kitemviews/kitemlistcontroller.*` | all input handling |
| `kitemviews/kitemlistselectionmanager.*` | current/selection/anchor |
| `kitemviews/kitemlistcontainer.*` | QWidget/graphics/scroll bar bridge |
| `kitemviews/kitemlistheader.*` | public header facade |
| `kitemviews/kitemlistgroupheader.*` | base for group headers |
| `kitemviews/kstandarditemlistgroupheader.*` | standard group header |
| `kitemviews/kitemset.*`, `kitemrange.h` | compressed index sets/ranges |
| `kitemviews/kitemliststyleoption.*` | shared presentation parameters |
| `kitemviews/kfileitemlisttostring.*` | textual presentation of KFileItem lists |

### 10.2 Internals under `private/`

| File | Purpose |
|---|---|
| `kitemlistviewlayouter.*` | index/geometry/viewport/group layout |
| `kitemlistsizehintresolver.*` | managing size hints across model changes |
| `kitemlistviewanimation.*` | widget animations |
| `kitemlistsmoothscroller.*` | animated offset changes |
| `kitemlistheaderwidget.*` | visible/interactive Details header |
| `kitemlistrubberband.*` | selection rectangle state |
| `kitemlistselectiontoggle.*` | hover check mark |
| `kitemlistkeyboardsearchmanager.*` | type-ahead buffer and search logic |
| `kitemlistroleeditor.*` | inline text editor |
| `kfileitemmodelfilter.*` | name/glob/regex/MIME filter |
| `kfileitemmodelsortalgorithm.h` | parallel merge sort |
| `kdirectorycontentscounter*` | asynchronous recursive folder counting |
| `kbaloorolesprovider.*` | Baloo metadata → model roles |
| `kfileitemclipboard.*` | fast cache of cut URLs |
| `kpixmapmodifier.*` | preview scaling and frames |
| `kitemviewsutils.*` | small view helpers, among them the device pixel ratio |

### 10.3 Dolphin-specific files outside `kitemviews/`

| File | Relation to `kitemviews` |
|---|---|
| `views/dolphinview.*` | creates the MVC, connects application signals, navigation and file operations |
| `views/dolphinitemlistview.*` | zoom and view properties |
| `views/dolphinfileitemlistwidget.*` | VCS overlay from `version` |
| `views/viewmodecontroller.*` | actions for Icons/Compact/Details |
| `views/viewproperties.*` | persistent layout, role, sort and preview settings |
| `views/versioncontrol/versioncontrolobserver.*` | fills `version` |
| `views/draganddrophelper.*` | KIO drop helpers |

---

## 11. Where which change belongs

### 11.1 A new additional file role/column

For a synchronously cheap role:

1. add the role and its description to `KFileItemModel::RoleType`/`rolesInfoMap()`;
2. add the mapping in `typeForRole()` and `roleForType()`;
3. set the value in `retrieveData()` only when the role is requested;
4. add text formatting in `KFileItemListWidgetInformant::roleText()`;
5. check sort and grouping behavior in `sortRoleCompare()`/`groups()`;
6. add model tests for role changes, sorting and filter state.

For an expensive role the computation belongs in the roles updater or a clearly separated provider
instead. In particular, never put synchronous file I/O into `data()` or `paint()`.

### 11.2 A new item decoration

Possible ways:

- standardized generic icon overlay: `KOverlayIconPlugin` → `iconOverlays`;
- fork-internal state: a new role, translated into an overlay or a color in
  `DolphinFileItemListWidget::refreshCache()`;
- completely new painting: preferably in a widget subclass and cache-aware;
- state that affects geometry: additionally adapt informant, size hints and
  `itemSizeHintUpdateRequired()`.

A decoration must not assume that every item permanently has a widget.

### 11.3 A new interaction

- mouse/keyboard/touch semantics: `KItemListController`;
- a new hit zone: abstract rectangles in `KItemListWidget` and the concrete computation in the
  standard/file widget;
- an application action in response: a controller signal and its connection in `DolphinView`;
- the actual file operation: `DolphinView`/KIO, not in the controller;
- a global action/shortcut: `DolphinViewActionHandler` or `DolphinMainWindow`, not in `kitemviews`.

### 11.4 A new view mode

This is a big change, because several `switch (ItemLayout)` places assume exactly three modes.
Affected at least:

- `KStandardItemListView::ItemLayout` and the default style;
- `KStandardItemListWidget::Layout`, geometry, painting and size hints;
- layouter/scroll orientation;
- `DolphinItemListView::updateGridSize()` and zoom tables;
- view properties, settings, actions and persistence;
- DnD pixmaps, header, grouping, expansion and accessibility;
- tests for navigation and hit zones.

A new widget alone is not enough. (The fork added other views as plugins beside the file list
instead, see "View plugins" in `design-notes.md`.)

### 11.5 An alternative data source

Another model can derive from `KItemModelBase`, but it must provide more than `count()` and `data()`
correctly:

- precise change ranges;
- stable roles and keyboard search;
- MIME data/DnD information if wanted;
- sorting/grouping or a deliberately empty sort role;
- optional tree API;
- URL/directory information if the existing controller DnD path is used.

`KFileItemListView` concretely requires `KFileItemModel`; for really different data,
`KStandardItemListView` plus own subclasses is the better entry point.

---

## 12. Pitfalls and invariants

1. **An index is not an identity.** After move/insert/remove never keep an old index for the same
   file. For asynchronous work store the `KFileItem`/URL and determine the index again later through
   the model.
2. **Only visible items have widgets.** Logic that concerns all items belongs in the model or a
   provider, not in a loop over `visibleItemListWidgets()`.
3. **Recycling does not clear every subclass state automatically.** With new widget fields, check all
   data change paths and cache invalidation.
4. **`data()` must stay cheap.** MIME detection, stat, folder recursion or thumbnailing in the
   synchronous getter would block scrolling and loading.
5. **Role changes can resort.** The roles updater must not create uncontrolled feedback during its own
   updates; `setModelData()` protects exactly this path.
6. **Sort role and visible roles differ.** An invisible sort or group role must still be requested and
   resolved.
7. **Tree and filter are coupled.** A matching child needs visible ancestors; collapsing must also
   dispose of filtered descendants.
8. **Model signals are contracts.** Wrong ranges damage selection, visible widgets, size hints and
   accessibility at the same time.
9. **Graphics and QWidget coordinates differ.** DnD, context menu and hit tests map several times
   between global, viewport, scene, view and item.
10. **Bundle layout changes.** Several `set…()` calls without a transaction can repeatedly trigger
    layout, preview updates and animation.
11. **Think of remote URLs.** `KFileItem` and KIO are URL based; local `QFileInfo` is only allowed in
    explicitly guarded places.
12. **Accessibility is its own parallel view.** A visually correct feature can still be wrong or
    invisible for screen readers.
13. **Do not pull Dolphin-specific code down needlessly.** VCS or fork logic belongs in `views/` as
    long as the general framework does not need it.
14. **Keep the private API upstream-friendly.** Big renames or internal reorganizations make KDE merges
    needlessly hard; extensions should stay small and at natural hooks.

---

## 13. Tests and a sensible test strategy

### 13.1 Direct unit tests

| Test | Focus |
|---|---|
| `kfileitemmodeltest` | listing, roles, insert/remove, sorting, filter, groups, expansion, MIME data |
| `kfileitemmodelbenchmark` | inserting/removing many items, performance |
| `kitemlistselectionmanagertest` | current, anchor, selection and index tracking |
| `kitemlistcontrollertest` | mouse/keyboard, selection, DnD hover, group collapse, inline rename |
| `kitemlistcontrollerexpandtest` | folder expansion |
| `kitemlistcontainertest` | scroll bar/scroller synchronization |
| `kitemlistsmoothscrollertest` | scroll targets and completion signals |
| `kitemlistkeyboardsearchmanagertest` | type-ahead and backward search |
| `kitemlistroleeditortest` | focus behavior of the inline editor |
| `dolphinitemlistviewtest` | the concrete file view, geometry and presentation |

### 13.2 Focused runs

The development build lives outside the repository. Typical calls:

```bash
cmake --build ../build-whale --target dolphin
ctest --test-dir ../build-whale -R 'kfileitemmodeltest|kitemlist.*test' --output-on-failure
../build-whale/bin/kfileitemmodeltest testExpandItems
../build-whale/bin/kitemlistcontrollertest testMouseClickActivation
```

### 13.3 What should also be checked by hand

- a very big local folder and a slow/remote KIO URL;
- Icons, Compact and Details at several zoom levels;
- sorting by an expensive role and quick re-sorting;
- grouped, collapsing/expanding groups;
- expanding the Details tree while filtering, refreshing and collapsing;
- previews on/off, zooming during running preview jobs, hover sequences;
- Ctrl/Shift/rubber band, single/double click and selection mode;
- dragging onto folders, between items and auto activation;
- inline rename with focus changes;
- HiDPI, RTL, dark color scheme;
- keyboard only and screen reader.

---

## 14. Recommended reading order

### First pass: the main flow

1. `views/dolphinview.cpp:89–242` – construction and signal wiring
2. `kitemviews/kitemmodelbase.h` – model contract
3. `kitemviews/kfileitemmodel.h` – public API and internal data structure
4. `kitemviews/kitemlistcontroller.h` – inputs and outgoing signals
5. `kitemviews/kitemlistview.h` – view responsibility and creator
6. `kitemviews/kitemlistcontainer.cpp:26–112, 244–410` – graphics/scroll bridge

### Second pass: rendering and performance

7. `kitemviews/kstandarditemlistview.cpp`
8. `kitemviews/kitemlistview.cpp:1864–2399` – model connection, layout, recycling, groups
9. `kitemviews/kstandarditemlistwidget.h/.cpp` – geometry, caches, painting
10. `kitemviews/private/kitemlistviewlayouter.h/.cpp`
11. `kitemviews/kfileitemlistview.cpp` – roles updater integration
12. `kitemviews/kfileitemmodelrolesupdater.h/.cpp` – asynchronous data pipeline

### Third pass: hard special cases

13. `kitemviews/kfileitemmodel.cpp:874–1205` – expansion plus filter
14. `kitemviews/kfileitemmodel.cpp:1329–2102` – sorting, insert/remove
15. `kitemviews/kitemlistcontroller.cpp:238–524, 604–1010, 1649–1950` – interaction
16. `kitemviews/kitemlistselectionmanager.cpp`
17. `kitemviews/accessibility/`
18. the corresponding tests, especially `kfileitemmodeltest.cpp` and `kitemlistcontrollertest.cpp`

---

## 15. Short mental models

These five sentences help to remember:

- **The model is a sorted, flat list of file data, not a Qt tree.**
- **The view knows all geometries but creates widgets only for what is visible.**
- **The controller interprets input but does not own the selection itself.**
- **The roles updater makes expensive data available step by step, close to the viewport.**
- **`DolphinView` translates between this internal index/role system and the rest of the application,
  which is based on URLs and `KFileItem`s.**
