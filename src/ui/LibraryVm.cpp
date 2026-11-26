#include "ui/LibraryVm.h"

namespace semplice {

LibraryVm::LibraryVm(UiState& state, QObject* parent) : QObject(parent), m_state(state)
{
    m_tab = state.tab();
    for (LibrarySort& s : m_sort)
        s = {SortKey::Recent, SortDirection::Descending};  // default: newest first, never persisted
    for (LibraryListModel& m : m_models)
        connect(&m, &LibraryListModel::countChanged, this, &LibraryVm::countChanged);
}

void LibraryVm::setReady(bool ready)
{
    if (m_ready == ready)
        return;
    m_ready = ready;
    for (bool& d : m_dirty)
        d = true;
    if (ready)
        load(index());
    else
        for (LibraryListModel& m : m_models)
            m.setRows({});
    emit readyChanged();
}

void LibraryVm::setTab(int tab)
{
    if (tab < 0 || tab > 2 || static_cast<LibraryTab>(tab) == m_tab)
        return;
    m_tab = static_cast<LibraryTab>(tab);
    m_state.setTab(m_tab);
    emit tabChanged();
    emit viewChanged();
    emit sortChanged();
    emit countChanged();
    if (m_ready && m_dirty[index()])
        load(index());
}

void LibraryVm::setViewMode(int mode)
{
    if (mode < 0 || mode > 2 || mode == viewMode())
        return;
    m_state.setViewMode(m_tab, static_cast<ViewMode>(mode));
    emit viewChanged();
}

bool LibraryVm::keyAllowed(int key) const
{
    return LibraryQueries::isAllowed(m_tab, static_cast<SortKey>(key));
}

void LibraryVm::chooseSort(int key)
{
    if (!keyAllowed(key))
        return;
    LibrarySort& s = m_sort[index()];
    if (static_cast<int>(s.key) == key)
        return;  // the direction has its own arrows
    s.key = static_cast<SortKey>(key);
    // a new key starts with the "down" arrow: A to Z, newest first, newest year first
    s.direction = s.key == SortKey::Alphabet ? SortDirection::Ascending : SortDirection::Descending;
    emit sortChanged();
    if (m_ready)
        load(index());
}

void LibraryVm::setSortDown(bool down)
{
    LibrarySort& s = m_sort[index()];
    const bool ascending = s.key == SortKey::Alphabet ? down : !down;
    const SortDirection wanted = ascending ? SortDirection::Ascending : SortDirection::Descending;
    if (s.direction == wanted)
        return;
    s.direction = wanted;
    emit sortChanged();
    if (m_ready)
        load(index());
}

void LibraryVm::toggleType(int albumType)
{
    if (albumType < 0 || albumType > static_cast<int>(AlbumType::Other))
        return;
    m_typeMask ^= (1 << albumType);
    if (m_typeMask == 0)
        m_typeMask = kAllTypes;  // hiding everything makes no sense: show all again
    emit sortChanged();
    if (m_ready)
        load(static_cast<int>(LibraryTab::Albums));
}

void LibraryVm::toggleHiddenOnly()
{
    m_hiddenOnly = !m_hiddenOnly;
    emit sortChanged();
    if (m_ready)
        load(static_cast<int>(LibraryTab::Artists));
}

void LibraryVm::load(int tab)
{
    m_dirty[tab] = false;
    m_loading[tab] = true;
    const LibrarySort& s = m_sort[tab];
    emit loadRequested(tab, static_cast<int>(s.key), s.direction == SortDirection::Ascending ? 0 : 1, tab == static_cast<int>(LibraryTab::Artists) ? (m_hiddenOnly ? 1 : 0) : (m_typeMask == kAllTypes ? 0 : m_typeMask));
}

void LibraryVm::setRows(int tab, QList<ListRow> rows)
{
    if (tab < 0 || tab > 2)
        return;
    m_loading[tab] = false;
    m_models[tab].setRows(std::move(rows));
}

void LibraryVm::invalidate()
{
    for (bool& d : m_dirty)
        d = true;
    if (m_ready)
        load(index());
}

}  // namespace semplice
