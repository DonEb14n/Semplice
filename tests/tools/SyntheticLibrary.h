#pragma once

#include "core/db/Database.h"

namespace semplice::testing {

// Size and shape of a generated library. Everything is derived from `seed`, so a run is repeatable.
struct SyntheticSpec {
    int artists = 20;
    int albums = 40;
    int tracks = 400;
    int playlists = 10;
    quint32 seed = 1;
    double hiddenArtistFraction = 0.05;       // of all artists
    double playlistOnlyTrackFraction = 0.03;  // of all tracks: live in a playlist, not in an album
    int playlistExtraTracks = 40;             // tracks borrowed from albums per playlist

    // SPEC 9: target and hard-limit sizes.
    static SyntheticSpec target50k() { return {2000, 5000, 50000, 500, 1, 0.05, 0.03, 40}; }
    static SyntheticSpec hardLimit200k() { return {8000, 20000, 200000, 500, 1, 0.05, 0.03, 40}; }
    static SyntheticSpec small() { return {}; }
};

struct SyntheticStats {
    int artists = 0;
    int hiddenArtists = 0;
    int albums = 0;
    int tracks = 0;
    int playlists = 0;
};

// DB-only mode: writes rows directly (no files). `db` must be migrated and empty.
// The result satisfies every invariant checked by checkLibraryIntegrity().
Result<SyntheticStats> generateSyntheticLibrary(Database& db, const SyntheticSpec& spec);

}  // namespace semplice::testing
