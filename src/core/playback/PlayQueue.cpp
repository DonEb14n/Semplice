#include "core/playback/PlayQueue.h"

#include <QJsonArray>

#include <algorithm>

namespace semplice {

namespace {
constexpr int kHistoryLimit = 500;
}

// ------------------------------------------------------------------------------------------ building

bool PlayQueue::start(const PlayRequest& request, bool shuffle, RepeatMode repeat)
{
    clear();
    m_shuffle = shuffle;
    m_repeat = repeat;
    m_afterEnd = request.afterEnd;
    int startGroup = -1;
    for (int i = 0; i < request.groups.size(); ++i) {
        if (request.groups.at(i).tracks.isEmpty())
            continue;
        if (i <= request.startGroup)
            startGroup = static_cast<int>(m_groups.size());
        GroupState gs;
        gs.group = request.groups.at(i);
        gs.uid = m_nextUid++;
        gs.played.fill(false, gs.group.tracks.size());
        naturalOrder(gs);
        m_groups.append(std::move(gs));
    }
    if (m_groups.isEmpty())
        return false;
    // the requested group, or the first non-empty one if it was empty
    if (request.startGroup >= 0 && request.startGroup < request.groups.size() && !request.groups.at(request.startGroup).tracks.isEmpty())
        m_group = startGroup;
    else
        m_group = std::max(0, startGroup);
    const int n = static_cast<int>(m_groups.at(m_group).group.tracks.size());
    const int first = (request.startIndex >= 0 && request.startIndex < n) ? request.startIndex : -1;
    resetGroup(m_group, first);
    return true;
}

void PlayQueue::clear()
{
    m_groups.clear();
    m_history.clear();
    m_group = -1;
    m_ghost = false;
}

void PlayQueue::naturalOrder(GroupState& gs)
{
    gs.order.clear();
    for (int i = 0; i < gs.group.tracks.size(); ++i)
        gs.order.append(i);
}

void PlayQueue::buildBag(GroupState& gs, int firstIndex)
{
    QList<int> rest;
    for (int i = 0; i < gs.group.tracks.size(); ++i)
        if (i != firstIndex && !gs.played.value(i))
            rest.append(i);
    std::shuffle(rest.begin(), rest.end(), m_rng);
    gs.order.clear();
    if (firstIndex >= 0)
        gs.order.append(firstIndex);
    gs.order.append(rest);
    gs.pos = firstIndex >= 0 ? 0 : -1;
}

void PlayQueue::resetGroup(int g, int firstIndex)
{
    GroupState& gs = m_groups[g];
    const int n = static_cast<int>(gs.group.tracks.size());
    gs.played.fill(false, n);
    if (m_shuffle && gs.group.origin.kind != OriginKind::Recommendations) {  // recommendations stay in the order they were picked
        if (firstIndex < 0) {
            firstIndex = static_cast<int>(m_rng() % static_cast<quint64>(n));
            // do not start the next pass with the track that just ended it
            const int last = (m_group == g && gs.pos >= 0 && gs.pos < gs.order.size()) ? gs.order.at(gs.pos) : -1;
            if (n > 1 && firstIndex == last)
                firstIndex = (firstIndex + 1) % n;
        }
        buildBag(gs, firstIndex);
    } else {
        naturalOrder(gs);
        if (firstIndex < 0)
            firstIndex = 0;
        gs.pos = firstIndex;
    }
    gs.played[firstIndex] = true;
    m_ghost = false;
}

// ------------------------------------------------------------------------------------------ queries

int PlayQueue::currentIndexOf(const GroupState& gs) const
{
    return (gs.pos >= 0 && gs.pos < gs.order.size()) ? gs.order.at(gs.pos) : -1;
}

std::optional<QueuePosition> PlayQueue::current() const
{
    if (m_group < 0 || m_ghost)
        return std::nullopt;
    const int idx = currentIndexOf(m_groups.at(m_group));
    if (idx < 0)
        return std::nullopt;
    return QueuePosition{m_group, idx};
}

std::optional<QueueTrack> PlayQueue::currentTrack() const
{
    const auto c = current();
    if (!c)
        return std::nullopt;
    return m_groups.at(c->group).group.tracks.at(c->index);
}

std::optional<QueueTrack> PlayQueue::trackAt(int g, int pos) const
{
    if (g < 0 || g >= m_groups.size())
        return std::nullopt;
    const GroupState& gs = m_groups.at(g);
    if (pos < 0 || pos >= gs.order.size())
        return std::nullopt;
    return gs.group.tracks.at(gs.order.at(pos));
}

// ------------------------------------------------------------------------------------------ movement

void PlayQueue::pushHistory()
{
    if (m_ghost || m_group < 0)
        return;
    const GroupState& gs = m_groups.at(m_group);
    const int idx = currentIndexOf(gs);
    if (idx < 0)
        return;
    m_history.append({gs.uid, idx});
    if (m_history.size() > kHistoryLimit)
        m_history.removeFirst();
}

PlayQueue::Step PlayQueue::advance(bool natural)
{
    if (m_group < 0)
        return Step::End;
    GroupState& gs = m_groups[m_group];
    if (!m_ghost && natural && m_repeat == RepeatMode::One && currentIndexOf(gs) >= 0)
        return Step::Moved;  // the same track again

    const bool recommendations = gs.group.origin.kind == OriginKind::Recommendations;
    if (gs.pos + 1 < gs.order.size()) {
        pushHistory();
        ++gs.pos;
        gs.played[gs.order.at(gs.pos)] = true;
        m_ghost = false;
        return Step::Moved;
    }
    if (m_repeat == RepeatMode::All && !recommendations) {
        pushHistory();
        resetGroup(m_group, -1);
        return Step::Moved;
    }
    if (m_group + 1 < m_groups.size()) {
        pushHistory();
        ++m_group;
        resetGroup(m_group, -1);
        return Step::Moved;
    }
    return m_afterEnd == AfterEnd::Recommend ? Step::NeedRecommendations : Step::End;
}

bool PlayQueue::canPrevious() const
{
    if (m_group < 0)
        return false;
    const GroupState& gs = m_groups.at(m_group);
    return !m_history.isEmpty() || gs.pos > 0 || m_group > 0 || (m_ghost && gs.pos >= 0);
}

bool PlayQueue::goPrevious()
{
    if (m_group < 0)
        return false;
    while (!m_history.isEmpty()) {
        const HistoryEntry h = m_history.takeLast();
        int gi = -1;
        for (int i = 0; i < m_groups.size(); ++i)
            if (m_groups.at(i).uid == h.uid)
                gi = i;
        if (gi < 0 || h.index >= m_groups.at(gi).group.tracks.size())
            continue;
        GroupState& gs = m_groups[gi];
        int p = static_cast<int>(gs.order.indexOf(h.index));
        if (p < 0) {  // the shuffle bag no longer holds it: put it right here
            p = std::clamp(gs.pos, 0, static_cast<int>(gs.order.size()));
            gs.order.insert(p, h.index);
        }
        m_group = gi;
        gs.pos = p;
        m_ghost = false;
        return true;
    }
    GroupState& gs = m_groups[m_group];
    if (m_ghost) {
        m_ghost = false;
        if (gs.pos >= 0)
            return true;  // rests on the predecessor already
    } else if (gs.pos > 0) {
        --gs.pos;
        return true;
    }
    if (m_group > 0) {
        --m_group;
        GroupState& prev = m_groups[m_group];
        prev.pos = static_cast<int>(prev.order.size()) - 1;
        m_ghost = false;
        return true;
    }
    return false;
}

bool PlayQueue::jumpTo(int group, int index)
{
    if (group < 0 || group >= m_groups.size() || index < 0 || index >= m_groups.at(group).group.tracks.size())
        return false;
    pushHistory();
    if (group != m_group) {
        m_group = group;
        if (m_shuffle) {
            resetGroup(group, index);
            return true;
        }
    }
    GroupState& gs = m_groups[m_group];
    int p = static_cast<int>(gs.order.indexOf(index));
    if (p < 0) {
        p = std::clamp(gs.pos + 1, 0, static_cast<int>(gs.order.size()));
        gs.order.insert(p, index);
    }
    gs.pos = p;
    gs.played[index] = true;
    m_ghost = false;
    return true;
}

std::optional<QueueTrack> PlayQueue::peekNext() const
{
    if (m_group < 0 || m_ghost || m_repeat == RepeatMode::One)
        return std::nullopt;
    const GroupState& gs = m_groups.at(m_group);
    if (gs.pos + 1 < gs.order.size())
        return trackAt(m_group, gs.pos + 1);
    const bool recommendations = gs.group.origin.kind == OriginKind::Recommendations;
    if (m_repeat == RepeatMode::All && !recommendations)
        return m_shuffle ? std::nullopt : trackAt(m_group, 0);
    if (m_group + 1 < m_groups.size() && !m_shuffle)
        return m_groups.at(m_group + 1).group.tracks.first();
    return std::nullopt;
}

// ------------------------------------------------------------------------------------------ views

QList<QueueTrack> PlayQueue::upNext(int count) const
{
    QList<QueueTrack> out;
    if (m_group < 0)
        return out;
    const GroupState& gs = m_groups.at(m_group);
    for (int p = gs.pos + 1; p < gs.order.size() && out.size() < count; ++p)
        out.append(gs.group.tracks.at(gs.order.at(p)));
    for (int g = m_group + 1; g < m_groups.size() && out.size() < count; ++g)
        for (const QueueTrack& t : m_groups.at(g).group.tracks)
            if (out.size() < count)
                out.append(t);
    return out;
}

QList<QueueTrack> PlayQueue::playedBefore(int count) const
{
    QList<QueueTrack> out;
    for (int i = static_cast<int>(m_history.size()) - 1; i >= 0 && out.size() < count; --i) {
        for (const GroupState& gs : m_groups)
            if (gs.uid == m_history.at(i).uid && m_history.at(i).index < gs.group.tracks.size())
                out.append(gs.group.tracks.at(m_history.at(i).index));
    }
    return out;
}

QList<QueueTrack> PlayQueue::currentGroupInOrder() const
{
    QList<QueueTrack> out;
    if (m_group < 0)
        return out;
    const GroupState& gs = m_groups.at(m_group);
    for (int idx : gs.order)
        out.append(gs.group.tracks.at(idx));
    return out;
}

// ------------------------------------------------------------------------------------------ modes + editing

void PlayQueue::setShuffle(bool on)
{
    if (on == m_shuffle)
        return;
    m_shuffle = on;
    if (m_group < 0)
        return;
    GroupState& gs = m_groups[m_group];
    const int cur = m_ghost ? -1 : currentIndexOf(gs);
    if (gs.group.origin.kind == OriginKind::Recommendations)
        return;  // recommendations keep the order they were picked in
    if (on) {
        if (cur >= 0)
            gs.played[cur] = true;
        buildBag(gs, cur);
        if (m_ghost)
            gs.pos = -1;
    } else {
        naturalOrder(gs);
        gs.pos = cur >= 0 ? cur : std::clamp(gs.pos, -1, static_cast<int>(gs.order.size()) - 1);
    }
}

void PlayQueue::appendGroup(const QueueGroup& group)
{
    if (group.tracks.isEmpty())
        return;
    GroupState gs;
    gs.group = group;
    gs.uid = m_nextUid++;
    gs.played.fill(false, group.tracks.size());
    naturalOrder(gs);
    m_groups.append(std::move(gs));
    if (m_group < 0)
        m_group = 0;
}

void PlayQueue::appendToGroup(int group, const QList<QueueTrack>& tracks)
{
    if (group < 0 || group >= m_groups.size())
        return;
    GroupState& gs = m_groups[group];
    for (const QueueTrack& t : tracks) {
        const int idx = static_cast<int>(gs.group.tracks.size());
        gs.group.tracks.append(t);
        gs.played.append(false);
        if (m_shuffle && group == m_group && gs.group.origin.kind != OriginKind::Recommendations && gs.pos + 1 < gs.order.size()) {
            const int span = static_cast<int>(gs.order.size()) - gs.pos;
            gs.order.insert(gs.pos + 1 + static_cast<int>(m_rng() % static_cast<quint64>(span)), idx);
        } else {
            gs.order.append(idx);
        }
    }
}

PlayQueue::RemoveResult PlayQueue::removeTracks(const QSet<TrackId>& ids)
{
    RemoveResult result;
    if (ids.isEmpty() || m_group < 0)
        return result;
    QList<int> erased;
    for (int gi = 0; gi < m_groups.size(); ++gi) {
        GroupState& gs = m_groups[gi];
        const int n = static_cast<int>(gs.group.tracks.size());
        QList<int> map(n, -1);
        QList<QueueTrack> kept;
        for (int i = 0; i < n; ++i) {
            if (ids.contains(gs.group.tracks.at(i).track))
                ++result.removed;
            else {
                map[i] = static_cast<int>(kept.size());
                kept.append(gs.group.tracks.at(i));
            }
        }
        if (kept.size() == n)
            continue;
        const bool isCurrentGroup = gi == m_group;
        const bool currentGone = isCurrentGroup && !m_ghost && gs.pos >= 0 && gs.pos < gs.order.size() && map.at(gs.order.at(gs.pos)) < 0;
        QList<int> newOrder;
        QList<bool> newPlayed(kept.size(), false);
        int survivorsUpToPos = 0;
        for (int k = 0; k < gs.order.size(); ++k) {
            const int mapped = map.at(gs.order.at(k));
            if (mapped >= 0) {
                newOrder.append(mapped);
                if (k <= gs.pos)
                    ++survivorsUpToPos;
            }
        }
        for (int i = 0; i < n; ++i)
            if (map.at(i) >= 0)
                newPlayed[map.at(i)] = gs.played.value(i);
        gs.group.tracks = kept;
        gs.order = newOrder;
        gs.played = newPlayed;
        gs.pos = survivorsUpToPos - 1;
        if (currentGone) {
            m_ghost = true;
            result.currentRemoved = true;
        }
        QList<HistoryEntry> history;
        for (const HistoryEntry& h : std::as_const(m_history)) {
            if (h.uid != gs.uid) {
                history.append(h);
                continue;
            }
            if (h.index < n && map.at(h.index) >= 0)
                history.append({h.uid, map.at(h.index)});
        }
        m_history = history;
        if (kept.isEmpty())
            erased.append(gi);
    }
    for (int k = static_cast<int>(erased.size()) - 1; k >= 0; --k) {
        const int gi = erased.at(k);
        m_groups.removeAt(gi);
        if (gi < m_group) {
            --m_group;
        } else if (gi == m_group) {
            result.currentRemoved = true;
            if (gi > 0) {
                m_group = gi - 1;
                m_groups[m_group].pos = static_cast<int>(m_groups.at(m_group).order.size()) - 1;
                m_ghost = true;
            } else if (!m_groups.isEmpty()) {
                m_group = 0;
                m_groups[0].pos = -1;
                m_ghost = true;
            } else {
                m_group = -1;
                m_ghost = false;
            }
        }
    }
    if (m_groups.isEmpty()) {
        m_group = -1;
        m_ghost = false;
        m_history.clear();
    }
    return result;
}

// ------------------------------------------------------------------------------------------ persistence

QJsonObject PlayQueue::toJson() const
{
    QJsonArray groups;
    for (const GroupState& gs : m_groups) {
        QJsonArray tracks, order, played;
        for (const QueueTrack& t : gs.group.tracks)
            tracks.append(QJsonArray{static_cast<double>(t.track.value), static_cast<double>(t.sourceId), static_cast<int>(t.sourceKind)});
        for (int o : gs.order)
            order.append(o);
        for (bool b : gs.played)
            played.append(b);
        groups.append(QJsonObject{{QStringLiteral("uid"), gs.uid},
                                  {QStringLiteral("kind"), static_cast<int>(gs.group.origin.kind)},
                                  {QStringLiteral("originId"), static_cast<double>(gs.group.origin.id)},
                                  {QStringLiteral("title"), gs.group.origin.title},
                                  {QStringLiteral("key"), gs.group.origin.key},
                                  {QStringLiteral("tracks"), tracks},
                                  {QStringLiteral("order"), order},
                                  {QStringLiteral("played"), played},
                                  {QStringLiteral("pos"), gs.pos}});
    }
    QJsonArray history;
    for (const HistoryEntry& h : m_history)
        history.append(QJsonArray{h.uid, h.index});
    return QJsonObject{{QStringLiteral("groups"), groups},
                       {QStringLiteral("history"), history},
                       {QStringLiteral("group"), m_group},
                       {QStringLiteral("ghost"), m_ghost},
                       {QStringLiteral("shuffle"), m_shuffle},
                       {QStringLiteral("repeat"), static_cast<int>(m_repeat)},
                       {QStringLiteral("afterEnd"), static_cast<int>(m_afterEnd)},
                       {QStringLiteral("nextUid"), m_nextUid}};
}

std::optional<PlayQueue> PlayQueue::fromJson(const QJsonObject& o, quint64 seed)
{
    PlayQueue q(seed);
    const QJsonArray groups = o.value(QStringLiteral("groups")).toArray();
    for (const QJsonValue& gv : groups) {
        const QJsonObject go = gv.toObject();
        GroupState gs;
        gs.uid = go.value(QStringLiteral("uid")).toInt();
        const int kind = go.value(QStringLiteral("kind")).toInt(-1);
        if (kind < 0 || kind > static_cast<int>(OriginKind::Recommendations))
            return std::nullopt;
        gs.group.origin = {static_cast<OriginKind>(kind), static_cast<qint64>(go.value(QStringLiteral("originId")).toDouble()),
                           go.value(QStringLiteral("title")).toString(), go.value(QStringLiteral("key")).toString()};
        for (const QJsonValue& tv : go.value(QStringLiteral("tracks")).toArray()) {
            const QJsonArray t = tv.toArray();
            if (t.size() != 3)
                return std::nullopt;
            gs.group.tracks.append({TrackId(static_cast<qint64>(t.at(0).toDouble())), static_cast<qint64>(t.at(1).toDouble()),
                                    t.at(2).toInt() == 1 ? ContainerKind::Playlist : ContainerKind::Album});
        }
        const int n = static_cast<int>(gs.group.tracks.size());
        if (n == 0)
            return std::nullopt;
        QSet<int> seen;
        for (const QJsonValue& ov : go.value(QStringLiteral("order")).toArray()) {
            const int idx = ov.toInt(-1);
            if (idx < 0 || idx >= n || seen.contains(idx))
                return std::nullopt;
            seen.insert(idx);
            gs.order.append(idx);
        }
        for (const QJsonValue& pv : go.value(QStringLiteral("played")).toArray())
            gs.played.append(pv.toBool());
        if (gs.played.size() != n)
            gs.played.fill(false, n);
        gs.pos = go.value(QStringLiteral("pos")).toInt(-1);
        if (gs.pos < -1 || gs.pos >= gs.order.size())
            return std::nullopt;
        q.m_groups.append(std::move(gs));
    }
    if (q.m_groups.isEmpty())
        return std::nullopt;
    q.m_group = o.value(QStringLiteral("group")).toInt(-1);
    if (q.m_group < 0 || q.m_group >= q.m_groups.size())
        return std::nullopt;
    q.m_ghost = o.value(QStringLiteral("ghost")).toBool();
    q.m_shuffle = o.value(QStringLiteral("shuffle")).toBool();
    const int repeat = o.value(QStringLiteral("repeat")).toInt();
    q.m_repeat = repeat == 1 ? RepeatMode::All : repeat == 2 ? RepeatMode::One : RepeatMode::Off;
    q.m_afterEnd = o.value(QStringLiteral("afterEnd")).toInt() == 1 ? AfterEnd::Recommend : AfterEnd::Stop;
    q.m_nextUid = std::max(o.value(QStringLiteral("nextUid")).toInt(1), 1);
    for (const QJsonValue& hv : o.value(QStringLiteral("history")).toArray()) {
        const QJsonArray h = hv.toArray();
        if (h.size() == 2)
            q.m_history.append({h.at(0).toInt(), h.at(1).toInt()});
    }
    return q;
}

}  // namespace semplice

namespace semplice {

QList<int> PlayQueue::currentGroupOrder() const
{
    return m_group < 0 ? QList<int>{} : m_groups.at(m_group).order;
}

}  // namespace semplice
