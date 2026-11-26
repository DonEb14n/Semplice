#include "ui/SearchVm.h"

namespace semplice {

SearchVm::SearchVm(QObject* parent) : QObject(parent)
{
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(120);
    connect(&m_debounce, &QTimer::timeout, this, &SearchVm::request);
}

void SearchVm::setText(const QString& text)
{
    if (text == m_text)
        return;
    m_text = text;
    emit textChanged();
    if (m_text.trimmed().isEmpty()) {
        ++m_token;  // late answers for the old text are dropped
        m_debounce.stop();
        clearResults();
        return;
    }
    m_debounce.start();
}

void SearchVm::request()
{
    emit queryRequested(m_text, ++m_token);
}

void SearchVm::clearResults()
{
    for (int c = 0; c < 4; ++c) {
        m_all[c].clear();
        m_totals[c] = 0;
        m_expanded[c] = false;
        m_models[c].setRows({});
    }
    emit resultsChanged();
}

void SearchVm::fill(int category)
{
    const QList<ListRow>& all = m_all[category];
    m_models[category].setRows(m_expanded[category] ? all : all.mid(0, kPerCategory));
}

void SearchVm::setResults(const SearchResults& results, int token)
{
    if (token != m_token)
        return;
    const SearchCategory* cats[4] = {&results.artists, &results.albums, &results.playlists, &results.tracks};
    for (int c = 0; c < 4; ++c) {
        m_all[c].clear();
        for (const SearchHit& hit : cats[c]->top)
            {
                ListRow row{hit.id, hit.display, {}, {}, 0, 0, 0};
                row.dimmed = hit.userHidden;
                m_all[c].append(row);
            }
        m_totals[c] = cats[c]->total;
        m_expanded[c] = false;
        fill(c);
    }
    emit resultsChanged();
}

void SearchVm::showMore(int category)
{
    if (category < 0 || category > 3)
        return;
    m_expanded[category] = true;
    fill(category);
    emit resultsChanged();
}

}  // namespace semplice
