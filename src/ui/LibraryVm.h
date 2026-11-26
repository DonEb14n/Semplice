#pragma once

#include "core/browse/UiState.h"
#include "ui/LibraryListModel.h"

#include <QObject>

namespace semplice {

// The Library panel: tabs, view modes, sort and filter, and the three lists (UI doc p.5-17).
class LibraryVm : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool ready READ ready NOTIFY readyChanged FINAL)
    Q_PROPERTY(int tab READ tab WRITE setTab NOTIFY tabChanged FINAL)           // 0 playlists, 1 artists, 2 albums
    Q_PROPERTY(int viewMode READ viewMode WRITE setViewMode NOTIFY viewChanged FINAL)  // 0 list, 1 large (2 per row), 2 extra large (3)
    Q_PROPERTY(int sortKey READ sortKey NOTIFY sortChanged FINAL)               // 0 alphabet, 1 recent, 2 year
    Q_PROPERTY(bool sortAscending READ sortAscending NOTIFY sortChanged FINAL)
    Q_PROPERTY(bool sortDown READ sortDown NOTIFY sortChanged FINAL)           // the "down" arrow is the active one
    Q_PROPERTY(int typeMask READ typeMask NOTIFY sortChanged FINAL)             // visible album types, bit per type
    Q_PROPERTY(bool hiddenOnly READ hiddenOnly NOTIFY sortChanged FINAL)        // Artists tab: the artists hidden by hand instead of the others
    Q_PROPERTY(QObject* model READ model NOTIFY tabChanged FINAL)               // the active tab's list
    Q_PROPERTY(int count READ count NOTIFY countChanged FINAL)
    Q_PROPERTY(bool loading READ loading NOTIFY countChanged FINAL)
public:
    enum Tab { Playlists = 0, Artists = 1, Albums = 2 };
    Q_ENUM(Tab)
    enum Mode { List = 0, Large = 1, ExtraLarge = 2 };
    Q_ENUM(Mode)
    static constexpr int kAllTypes = 0x3F;

    LibraryVm(UiState& state, QObject* parent = nullptr);

    bool ready() const { return m_ready; }
    int tab() const { return static_cast<int>(m_tab); }
    void setTab(int tab);
    int viewMode() const { return static_cast<int>(m_state.viewMode(m_tab)); }
    void setViewMode(int mode);
    int sortKey() const { return static_cast<int>(m_sort[index()].key); }
    bool sortAscending() const { return m_sort[index()].direction == SortDirection::Ascending; }
    // Down arrow: A to Z for the alphabet, newest first for recency and year (UI doc p.6, p.10).
    bool sortDown() const { return m_sort[index()].key == SortKey::Alphabet ? sortAscending() : !sortAscending(); }
    int typeMask() const { return m_typeMask; }
    bool hiddenOnly() const { return m_hiddenOnly; }
    QObject* model() { return &m_models[index()]; }
    int count() const { return m_models[index()].count(); }
    bool loading() const { return m_loading[index()]; }

    // Sort menu (UI doc p.6, p.10): choosing the key already in use flips the arrow.
    Q_INVOKABLE void chooseSort(int key);
    Q_INVOKABLE void setSortDown(bool down);
    Q_INVOKABLE void toggleType(int albumType);
    Q_INVOKABLE void toggleHiddenOnly();
    Q_INVOKABLE bool keyAllowed(int key) const;

    // From the engine thread.
    void setReady(bool ready);
    void setRows(int tab, QList<ListRow> rows);
    void invalidate();  // the library changed: reload what is visible, the rest when it becomes visible

signals:
    void readyChanged();
    void tabChanged();
    void viewChanged();
    void sortChanged();
    void countChanged();
    // The panel asks the controller to fetch a list from the engine thread.
    void loadRequested(int tab, int sortKey, int direction, int typeMask);

private:
    int index() const { return static_cast<int>(m_tab); }
    void load(int tab);

    UiState& m_state;
    LibraryTab m_tab = LibraryTab::Artists;
    bool m_ready = false;
    LibrarySort m_sort[3];
    int m_typeMask = kAllTypes;
    bool m_hiddenOnly = false;
    LibraryListModel m_models[3];
    bool m_dirty[3] = {true, true, true};
    bool m_loading[3] = {false, false, false};
};

}  // namespace semplice
