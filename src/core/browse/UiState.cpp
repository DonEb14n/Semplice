#include "core/browse/UiState.h"

namespace semplice {

namespace {

QString tabName(LibraryTab tab)
{
    switch (tab) {
    case LibraryTab::Playlists: return QStringLiteral("playlists");
    case LibraryTab::Artists: return QStringLiteral("artists");
    case LibraryTab::Albums: return QStringLiteral("albums");
    }
    return QStringLiteral("artists");
}

}  // namespace

LibraryTab UiState::tab() const
{
    const QString value = m_settings.uiValue(QStringLiteral("library_tab"), QStringLiteral("artists"));
    for (LibraryTab t : {LibraryTab::Playlists, LibraryTab::Artists, LibraryTab::Albums})
        if (tabName(t) == value)
            return t;
    return LibraryTab::Artists;
}

void UiState::setTab(LibraryTab tab)
{
    m_settings.setUiValue(QStringLiteral("library_tab"), tabName(tab));
}

ViewMode UiState::viewMode(LibraryTab tab) const
{
    const QString value = m_settings.uiValue(QStringLiteral("view_") + tabName(tab), QStringLiteral("list"));
    if (value == QLatin1String("large"))
        return ViewMode::Large;
    if (value == QLatin1String("xlarge"))
        return ViewMode::ExtraLarge;
    return ViewMode::List;
}

void UiState::setViewMode(LibraryTab tab, ViewMode mode)
{
    const QString text = mode == ViewMode::Large ? QStringLiteral("large") : mode == ViewMode::ExtraLarge ? QStringLiteral("xlarge") : QStringLiteral("list");
    m_settings.setUiValue(QStringLiteral("view_") + tabName(tab), text);
}

}  // namespace semplice
