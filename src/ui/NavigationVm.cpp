#include "ui/NavigationVm.h"

namespace semplice {

void NavigationVm::open(int kind, qint64 id, const QString& text)
{
    m_direction = 1;
    if (m_history.open({static_cast<PageKind>(kind), id, text}))
        emit pageChanged();
}

void NavigationVm::back()
{
    m_direction = -1;
    if (m_history.back())
        emit pageChanged();
}

void NavigationVm::forward()
{
    m_direction = 1;
    if (m_history.forward())
        emit pageChanged();
}

void NavigationVm::forget(PageKind kind, qint64 id)
{
    const bool wasOpen = m_history.current().kind == kind && m_history.current().id == id;
    m_history.forget(kind, id);
    if (wasOpen)
        emit pageChanged();  // only when the open page was the one that went away
}

}  // namespace semplice
