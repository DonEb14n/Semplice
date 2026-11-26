#pragma once

#include <QList>
#include <QString>

#include <optional>

namespace semplice {

enum class PageKind { Home, Settings, Artist, Album, Playlist, Category, SearchResults, AddArtist, AddAlbum, AddPlaylist, Import, EditArtist, EditAlbum, EditPlaylist, EditTrack, Share };

// A state of the middle window (what back / forward return to).
struct PageRef {
    PageKind kind = PageKind::Home;
    qint64 id = 0;
    QString text;  // category key or the search text, depending on the kind
    bool operator==(const PageRef&) const = default;
};

// Back / forward arrows of the hotbar (UI doc p.17). Opening a page while standing in the past drops the
// forward part; opening the page that is already shown changes nothing.
class NavigationHistory {
public:
    NavigationHistory() { m_pages.append(PageRef{}); }  // starts on Home

    const PageRef& current() const { return m_pages.at(m_index); }
    bool canBack() const { return m_index > 0; }
    bool canForward() const { return m_index + 1 < m_pages.size(); }

    // Returns false when `page` is the current one.
    bool open(const PageRef& page)
    {
        if (page == current())
            return false;
        m_pages.resize(m_index + 1);
        m_pages.append(page);
        if (m_pages.size() > kLimit)
            m_pages.removeFirst();
        m_index = static_cast<int>(m_pages.size()) - 1;
        return true;
    }
    std::optional<PageRef> back() { return canBack() ? std::optional<PageRef>(m_pages.at(--m_index)) : std::nullopt; }
    std::optional<PageRef> forward() { return canForward() ? std::optional<PageRef>(m_pages.at(++m_index)) : std::nullopt; }

    // A page that was deleted: remove it everywhere and fall back to a neighbour.
    void forget(PageKind kind, qint64 id)
    {
        for (int i = static_cast<int>(m_pages.size()) - 1; i >= 1; --i) {
            if (m_pages.at(i).kind == kind && m_pages.at(i).id == id) {
                m_pages.removeAt(i);
                if (m_index >= i)
                    --m_index;
            }
        }
        // neighbouring duplicates created by the removal
        for (int i = static_cast<int>(m_pages.size()) - 1; i >= 1; --i) {
            if (m_pages.at(i) == m_pages.at(i - 1)) {
                m_pages.removeAt(i);
                if (m_index >= i)
                    --m_index;
            }
        }
        if (m_index < 0)
            m_index = 0;
    }

    static constexpr int kLimit = 100;

private:
    QList<PageRef> m_pages;
    int m_index = 0;
};

}  // namespace semplice
