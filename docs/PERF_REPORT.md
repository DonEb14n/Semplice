# Performance report (M10)

Measured on the development PC (Windows 11), **Release**, MinGW 13.1 + Qt 6.11.2, generated libraries from `semplice_gen` (database only: no real audio files, no cover pictures). "Target" = 50,000 tracks / 5,000 albums / 2,000 artists / 500 playlists; "limit" = 200,000 tracks / 20,000 albums / 8,000 artists. Timings come from the log lines `perf: ...` the app writes at start-up (`%LOCALAPPDATA%\Semplice\logs\semplice.log`), three runs each, warm disk cache.

| Metric (SPEC 9) | Budget target / limit | Measured at target (50k) | Measured at limit (200k) | Verdict |
|---|---|---|---|---|
| Cold start to visible window | < 1.5 s / < 3 s | first frame 0.41 s | first frame 0.41 s | met |
| Library open | - | 0.41 - 0.49 s | 0.66 - 1.17 s | - |
| Home usable | < 2.5 s / < 5 s | 0.46 - 0.54 s | 0.81 - 1.31 s | met |
| Search latency p95 | < 100 ms / < 250 ms | 2.7 ms | 19 ms (20.6 ms for 1-2 letter queries) | met (M1 numbers, `semplice_bench`) |
| Scrolling | 60 fps / >= 30 fps | - | average 6.1 ms per frame (about 160 fps) on 20k albums in the library list (M7, `--bench-scroll`) | met on this PC; **the 60 fps on a mid-range integrated-GPU laptop is a manual check** |
| Import throughput (no copy) | >= 20 tracks/s | 600 tiny files imported (copy included) in 3.2 s = about 190 tracks/s | - | met (tiny files: tag read is trivial; real files with big covers will be slower, to re-check with the owner's music) |
| Memory, idle | < 250 MB / < 500 MB | **private 207 MB, working set 314 MB** | private 298 MB, working set 389 MB | private bytes meet the budget; the working set at target is 64 MB over because it also counts the shared pages of Qt, FFmpeg and the GPU driver. Needs a decision, see below |
| Database size | ~60 MB / < 300 MB | 26 MB | 103 MB | met |
| Thumbnail cache | ~200 MB / cap 1 GB, LRU | not measured with real covers; the cap and least-recently-used clean-up now exist (`ThumbnailCache`, run at every library open, tested) | - | met by design; the files are PNG (about 2-3x JPEG), see decision 120 |
| Recommender pick (extra) | GUI budget ~5 ms | - | one pick of 3 tracks 9 ms (pool 200k, sampled to 20k) | slightly over on 200k, only at the end of a queue |

## What is not measured by a machine
* 60 fps scrolling on a laptop with an integrated GPU and a real cover collection.
* Import speed with real audio files (4-10 MB each, big embedded covers) and from a slow disk.
* Thumbnail disk usage with a real collection (expected ~40-130 KB per album as PNG).
* DPI 125 / 150 / 200 % and CJK were checked by screenshots (`QT_SCALE_FACTOR`), not on a real high-DPI screen.

## Memory: what to decide
The working set at 50k tracks is 314 MB against a 250 MB budget; private memory is 207 MB. Most of the gap is shared code (Qt, FFmpeg, QML engine) and is the same for any Qt Quick program. If you want the working-set number under 250 MB the cheapest levers are: smaller in-memory cover cache (now 64 MB), loading the search index lazily, and not creating the Queue / Search popups until first used. I did not change them without your decision.

## Re-running
```
build\windows-mingw-release\semplice_gen.exe --preset 50k --library D:\tmp_50k
(set the library path in %APPDATA%\Semplice\settings.ini, or open the folder in the app)
build\windows-mingw-release\Semplice.exe --grab out.png --grab-delay 6000      (then read the "perf:" lines in the log)
build\windows-mingw-release\semplice_bench.exe --preset 200k                    (search, index, database)
build\windows-mingw-release\Semplice.exe --bench-scroll                         (scrolling)
build\windows-mingw-release\semplice_recotool.exe <library> --album 5           (recommender pick time)
```
