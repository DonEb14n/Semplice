# M9 - Recommender (SPEC 7.6, UI doc "Recommendation system")

Goal: after an album / artist / carousel queue ends (and repeat-all is off), music keeps coming: track by track, close to what was just heard, without boring repeats. M5 already has the hook (`IRecommendationSource`, the "Recommendations" queue group, a look-ahead of 3 tracks for "Up next", the player header). M9 supplies the real source.

## Pieces (all in `core/recommend/`, no Qt GUI)
| Class | Job |
|---|---|
| `RecommenderConfig` | every weight and limit in one struct with the SPEC defaults (tags +0.50, same artist +0.30, related genre +0.20, year +0.10, opposite genre -0.30, recently played -0.40 over 7 days; top 100; at most 2 per album in 10 positions). |
| `TrackFeatures` / `TagRelations` | what the scorer sees of a track: id, home album, artist ids, tag indexes, year, last played. Tags are small integers; the relation table says which are related / opposite. |
| `SeedProfile` | what the finished source "is": tag frequencies, artists, albums, average year. Built from the seed tracks. |
| `Scorer` | PURE: `score(track, seed, now, config) -> ScoreParts` (every component kept, for the debug view and the tests). |
| `Selector` | takes the pool, a seed, the exclude list and an injected random generator; scores, keeps the top 100, picks weighted by score under the variety rules, remembers what it picked. Relaxation order when fewer than 2 candidates remain: (1) allow seed albums, (2) allow already-recommended tracks (the "used" set restarts), else return nothing. Variety rules are relaxed (window rule first, then "same album twice in a row") only when no candidate satisfies them. |
| `FeatureLoader` | bulk SQL (4 queries) -> the pool; works on any read connection. |
| `LibraryRecommendationSource` | the `IRecommendationSource` the player uses. Builds the pool on a worker thread when a library opens (and again after a library change); `pick()` waits for it only if it is not ready yet. Owns a `Selector` and a seedable generator. |

## Data flow
`PlaybackService::startRecommendations / ensureLookahead` -> `pick(request)` -> selector (pool in memory, no DB on the GUI thread) -> track ids -> catalog resolves (drops unplayable) -> queue group "Recommendations". Played recommended tracks update `play_count` / `last_played_at` through the existing store (SPEC 7.5) and never touch Recently Played (already so).

## Decisions
* A track's tags = its own tags + the tags of the albums that contain it (playlist-only tracks: own tags only). Artist tags are not mixed in (they describe the artist, not the song).
* Variety uses the track's FIRST album; playlist-only tracks have none and are never limited by the album rules.
* Year score = 1 - distance / 20 years (floor 0), times 0.10; unknown year scores 0.
* Selection weight = score shifted so the worst of the top 100 has a small positive weight (a negative score still has a chance, a small one).
* Randomness: `std::mt19937_64` behind a tiny `IRandom`; tests inject a fixed sequence.

## Debug view
`semplice_recotool <library folder> --album <id> [--artist <id>] [--top 20] [--seed N]` prints the top candidates with each score component, so weights can be tuned (not part of the app UI).

## Tests
Scorer: each component alone, related vs opposite genre, recency decay, unknown year. Selector: exclude list honoured, never the same album twice in a row, at most 2 per album in any window of 10 (when the pool allows), no repeats until the pool is exhausted, relaxation order (seed albums, then used tracks), nothing when fewer than 2 playable tracks, determinism with a fixed seed, look-ahead sizes. Loader: against a real library (tags from albums + tracks, credits, year). Integration: `PlaybackService` + real source on a small library keeps playing past the end of an album.
