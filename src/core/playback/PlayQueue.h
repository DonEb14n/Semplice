#pragma once

#include "core/playback/PlaybackTypes.h"

#include <QJsonObject>
#include <QSet>

#include <optional>
#include <random>

namespace semplice {

// The queue logic of SPEC 7.2 without any audio, timers or database: ordered groups, a pointer, the
// shuffle bag, repeat modes and the real played history. Deterministic given its seed.
class PlayQueue {
public:
    explicit PlayQueue(quint64 seed = 1) : m_rng(seed) {}

    enum class Step { Moved, NeedRecommendations, End };

    // Replaces everything. Empty groups are dropped. Returns false if nothing playable was given.
    bool start(const PlayRequest& request, bool shuffle, RepeatMode repeat);
    void clear();

    bool isEmpty() const { return m_groups.isEmpty(); }
    std::optional<QueuePosition> current() const;  // nullopt while empty or after the current track was removed
    std::optional<QueueTrack> currentTrack() const;
    const QueueGroup& group(int index) const { return m_groups.at(index).group; }
    int groupCount() const { return static_cast<int>(m_groups.size()); }
    int currentGroupIndex() const { return m_group; }
    const QueueOrigin& currentOrigin() const { return m_groups.at(m_group).group.origin; }
    AfterEnd afterEnd() const { return m_afterEnd; }
    bool shuffle() const { return m_shuffle; }
    RepeatMode repeat() const { return m_repeat; }

    // ---- movement
    // natural = the track ended by itself (repeat One applies); false = the user pressed Next.
    Step advance(bool natural);
    // Previous: history first, then the previous track / group in sequence. False = nothing before.
    bool goPrevious();
    bool canPrevious() const;
    // Click in the queue panel: the track at (group, index) becomes current.
    bool jumpTo(int group, int index);
    // The track that would follow by itself (for preloading); nullopt for repeat One or at a hard end.
    std::optional<QueueTrack> peekNext() const;

    // ---- views for the queue panels
    QList<QueueTrack> upNext(int count) const;     // following tracks, then following groups
    QList<QueueTrack> playedBefore(int count) const;  // most recent first, from the real history
    QList<QueueTrack> currentGroupInOrder() const; // the whole current group in play order (hotbar popup)
    QList<int> currentGroupOrder() const;          // the same as indexes into the group (for playAt)

    // ---- modes
    void setShuffle(bool on);
    void setRepeat(RepeatMode mode) { m_repeat = mode; }

    // ---- editing from outside
    void appendGroup(const QueueGroup& group);
    void appendToGroup(int group, const QList<QueueTrack>& tracks);
    struct RemoveResult {
        int removed = 0;
        bool currentRemoved = false;
    };
    // Deleted or missing tracks. If the current one is removed the pointer rests just before where it
    // was (current() is empty) and the next advance() lands on the following track.
    RemoveResult removeTracks(const QSet<TrackId>& ids);

    // ---- persistence
    QJsonObject toJson() const;
    static std::optional<PlayQueue> fromJson(const QJsonObject& object, quint64 seed = 1);

private:
    struct GroupState {
        QueueGroup group;
        int uid = 0;
        QList<int> order;      // indexes into group.tracks in play order
        QList<bool> played;    // visited in this pass
        int pos = -1;          // pointer into order
    };
    struct HistoryEntry {
        int uid = 0;
        int index = 0;
    };

    void resetGroup(int g, int firstIndex);  // new pass: natural order or a fresh bag; pointer on the first entry
    void buildBag(GroupState& gs, int firstIndex);
    void naturalOrder(GroupState& gs);
    void pushHistory();
    int currentIndexOf(const GroupState& gs) const;
    std::optional<QueueTrack> trackAt(int g, int pos) const;

    QList<GroupState> m_groups;
    QList<HistoryEntry> m_history;
    int m_group = -1;
    bool m_ghost = false;  // the current track was removed; pointer sits just before its successor
    bool m_shuffle = false;
    RepeatMode m_repeat = RepeatMode::Off;
    AfterEnd m_afterEnd = AfterEnd::Stop;
    int m_nextUid = 1;
    std::mt19937_64 m_rng;
};

}  // namespace semplice
