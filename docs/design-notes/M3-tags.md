# M3 - Tags: design note

Goal: read and write file metadata correctly for MP3, FLAC, M4A, OGG Vorbis, WAV and WMA, repair mojibake, and own the closed genre vocabulary. `core` only; the Windows pieces sit behind interfaces. No UI.

## 1. New dependency (allowed by SPEC 2, announced here)
**TagLib 2.3.2** (LGPL-2.1/MPL-1.1), pinned in CMake by URL and SHA-256 (`cmake/TagLib.cmake`, `FetchContent`), built **statically**, without zlib, examples, bindings or tests. The release tarball (not a git archive) contains its bundled `utfcpp`. The first configure needs internet access; after that it is cached in the build folder. Listed in `THIRD_PARTY_NOTICES.md`.

## 2. Pieces (`core/tags/`)
| Class | Job |
|---|---|
| `TrackTags`, `TagWrite`, `CoverImage` | plain data: what a file says / what we want it to say |
| `ITagService`, `TagService` | TagLib read/write. Reads open the file **read-only** (originals are never opened for writing). Writes go to a temporary copy and replace the original atomically |
| `IAtomicReplace`, `PortableAtomicReplace` (+ `WindowsAtomicReplace` in `platform/`, `ReplaceFileW`) | the "replace" step of SPEC 6.2 |
| `IFileReleaser` | handshake with the audio backend (M5 implements it through `IAudioBackend::release`): the tag writer asks to release a file the player holds open and tells it when the file is ready again |
| `ICodepageConverter`, `EncodingFixer` (+ `WindowsCodepageConverter` in `platform/`, a table-driven fake for tests) | SPEC 6.3 |
| `GenreVocabulary`, `GenreMap` | `resources/tags/genres.json` -> keys, localised names, relations; raw genre text -> keys |
| `CoverDiscovery` | the cover-file rules of SPEC 4.1 and the stored file names |

## 3. Reading
* MP3 is read frame by frame so we know which text frames are *declared ISO-8859-1* (and which fields only exist in ID3v1): those are flagged `legacy8bit` in `TrackTags` and are the only ones the `EncodingFixer` may touch. Everything else (UTF-8/UTF-16 frames, Vorbis comments, MP4 atoms, ASF) is trusted as-is.
* All other formats use TagLib's `PropertyMap` (TITLE, ARTIST, ALBUM, ALBUMARTIST, TRACKNUMBER, DATE, GENRE), multi-value fields stay lists.
* Date strings become `ReleaseDate` (year, optional month and day); track numbers accept `3` and `3/12`.
* Embedded cover: the front cover if present, else the first picture; only JPEG/PNG (checked by magic bytes) are accepted.
* Properties: duration, codec name, bitrate.

## 4. Writing (SPEC 6.2)
1. Release the file via `IFileReleaser` (if one is installed); on every path out, tell it the file is ready again.
2. Copy the original to `<name>.semplice-tagtmp-<id>` in the same folder, write the tags to the **copy**, re-open the copy and verify it (title/artist read back), then replace the original atomically (`ReplaceFileW` on Windows). A crash can therefore never leave a half-written audio file; stale temp files are removed by `TagService::removeStaleTempFiles(root)` at start-up.
3. Fields: TITLE, ARTIST (multi-value), ALBUM, ALBUMARTIST, TRACKNUMBER, DATE, GENRE (English names from the vocabulary), cover. Album-less (playlist) tracks pass `album = nullopt` and keep their album fields untouched. All other existing properties (comments, lyrics ...) are preserved: we read the full property map, change our keys, write it back.
4. MP3: **ID3v2.4, UTF-8**, and ID3v1 is stripped. WAV: ID3v2 chunk, best effort. FLAC/OGG pictures as PICTURE blocks, M4A `covr`, WMA `WM/Picture`.
5. Failure (read-only file, locked, disk full) -> `E_TAG_WRITE`/`E_FILE_LOCKED`, the original is untouched, the temp file is removed; the caller records "tags out of sync".

## 5. Mojibake (SPEC 6.3)
Only for fields flagged `legacy8bit` whose characters are all < U+0100 and not all ASCII. Candidate decodings of the Latin-1 bytes: **UTF-8** (double-encoded), then CP1251, CP936 (GBK), CP932 (Shift-JIS), CP949 (EUC-KR), CP950 (Big5) through `ICodepageConverter` (order configurable, default = this list). A candidate is accepted only if decoding succeeds without errors and the result is plausible: at least two characters of the target script, no replacement/private-use characters, the target script outweighs ASCII letters. Kana in a Shift-JIS result is a strong signal and is preferred. Correct Latin text ("Café", "Björk") fails these tests and is never changed. **Known limit:** Han-only text can be valid in several Chinese/Japanese/Korean code pages (and Hangul bytes are valid GBK). The fixer picks the first plausible one in the configured order and reports the alternatives; the Add Album form (M8) shows the result so the user can correct it.

## 6. Genre vocabulary (`resources/tags/genres.json`, reviewed by you)
~72 genres. Per genre: `key` (kebab-case English slug, never changes), `names` en/ru/zh_CN/ja (**ru/zh/ja are machine drafts**), `aliases` (raw spellings), `related`, `opposite`. Plus the ID3v1 table 0-191 (Winamp extensions included) mapped to keys; entries without a sensible genre ("Other", "Pranks", "Podcast"...) map to nothing. Validation (test): unique keys, all four names present, aliases unique after normalisation, every referenced key exists, `related` and `opposite` symmetric and disjoint, no self references, all 192 numbers present. `docs/GENRES.md` is generated from the JSON for your review.
`GenreMap::resolve(raw)`: normalise (case, diacritics, `&`->and, punctuation -> space), alias lookup; understands `17`, `(17)`, `(17)Rock`, and `A; B`, `A/B`, `A, B` lists; unknown strings are ignored, never create tags.

## 7. Covers (SPEC 4.1)
`CoverDiscovery::findInFolder(dir)`: `cover.*` first, then `album_cover.*` (jpg/jpeg/png, any case). `storedName(kind, ext)`: always `album_cover.<ext>` / `artist_cover.<ext>` / `playlist_cover.<ext>`. `resolve(...)` applies the priority user-chosen > folder file > embedded cover of the first track; the extracted bytes are written by M4's import through the journaled storage.

## 8. Tests (QtTest)
Real files generated in code (`tests/tools/TestAudioFiles`): MP3 (silent frames, ID3v1/v2.3/v2.4 variants), FLAC (STREAMINFO), OGG Vorbis (hand-built pages), M4A (atoms), WAV (PCM), WMA (ASF header). `tst_tag_service`: for every format a write -> read round trip (all fields, multi-value artists, Cyrillic/CJK/emoji text, partial dates, cover set/replace/remove), other properties preserved, album fields untouched when not given, ID3v1 stripped and v2.4/UTF-8 verified at byte level, original untouched when the write fails, locked-file handshake (a file held open is released, written, and re-announced), temp file never left behind, stale-temp sweep. `tst_encoding_fixer`: fixtures built from known-good strings re-encoded to the wrong code page prove both directions (broken -> fixed, correct text untouched, ambiguous/short text untouched). `tst_genre_*`, `tst_cover_discovery`. Windows-only: real `MultiByteToWideChar` fixtures and `ReplaceFileW`.

## 9. Decisions to record
TagLib version and static linking; reading is strictly read-only; temp-copy naming `.semplice-tagtmp-*`; ID3v2.4/UTF-8 + ID3v1 removal; encoding order and its known limit; `legacy8bit` flag instead of re-decoding everything; genre names for ru/zh/ja are drafts; ID3v1 entries without a sensible genre map to nothing.
