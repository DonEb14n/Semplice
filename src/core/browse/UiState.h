#pragma once

#include "core/browse/LibraryQueries.h"
#include "core/services/ISettings.h"

namespace semplice {

enum class ViewMode { List, Large, ExtraLarge };  // list, 2 per row, 3 per row

// What the library panel remembers between launches (UI doc p.5-6): the active tab and the view mode of
// every tab. Sort and filter are deliberately NOT stored.
class UiState {
public:
    explicit UiState(ISettings& settings) : m_settings(settings) {}

    LibraryTab tab() const;
    void setTab(LibraryTab tab);
    ViewMode viewMode(LibraryTab tab) const;
    void setViewMode(LibraryTab tab, ViewMode mode);

private:
    ISettings& m_settings;
};

}  // namespace semplice
