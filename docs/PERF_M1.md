# M1 performance numbers

Measured with `semplice_bench` (Release build, MSVC 2022, Qt 6.11.2) on the development PC, DB-only synthetic libraries (SPEC 9 sizes). Re-run any time:

```
build\windows-msvc-release\semplice_bench.exe --preset 50k
build\windows-msvc-release\semplice_bench.exe --preset 200k
```
(Qt's `bin` folder must be on PATH, as for any Qt program run outside Qt Creator.)

| Metric | Target size (50k tracks, 5k albums, 2k artists, 500 playlists) | Hard limit (200k tracks, 20k albums, 8k artists) | Budget (SPEC 9) |
|---|---|---|---|
| Generate library (DB only) | 1.4 s | 6.9 s | - |
| Database size | 25.9 MB | 102 MB | ~60 MB target / < 300 MB limit |
| Open database + schema check | 1 ms | 2 ms | - |
| Build in-memory search index | 50 ms | 205 ms | part of cold start < 1.5 s |
| Index memory (working set growth) | 13 MB | 47 MB | idle < 250 MB / < 500 MB |
| Search latency p50 / p95 / max (400 queries of 1-6 chars) | 1.2 / 2.7 / 3.4 ms | 13 / 19 / 25 ms | p95 < 100 ms / < 250 ms |
| Same, only 1-2 character queries (worst case) | p95 2.7 ms | p95 20.6 ms | |

All budgets met with a wide margin. What is *not* measured yet (later milestones): cold start of the real app, UI scrolling, import throughput, thumbnail cache.

The generator output passes the integrity checker at both sizes (`semplice_gen --preset 200k --out x.db --check`).
