# M7 - Home view: design note

Goal: the middle window's default page (UI doc p.23-26, SPEC 8.6): greeting, "Recently Played", generated category carousels with lazy generation, the category tile view, the empty state and the drop-zone overlay. The import window behind a drop is M8; in M7 a drop only switches the page to the "Import" placeholder.

## 1. Pure, tested core (`src/core/home`)
| Piece | Rule |
|---|---|
| `Greeting` | `pick(now, hasContent)`: empty library -> "Welcome, {name}!" (UI p.23); otherwise an occasion wins: New Year (Dec 31 - Jan 2), first day of a meteorological season (Mar 1 spring, Jun 1 summer, Sep 1 autumn, Dec 1 winter), else time of day: morning 05-11, afternoon 12-17, evening 18-04. The kind is returned as an enum; the view-model owns the (translated) texts. DEFAULT reading of SPEC 8.6: "Welcome back" is not used because the occasion/time-of-day texts replace it. |
| `ICategorySource` | what the generator needs to know about the library: artists with >= 3 albums, years / decades / genres with >= 5 albums, the number of playlists (>= 2) and albums (>= 5), and the items of one category. The Unsorted album is excluded everywhere. `LibraryCategorySource` implements it with SQL on the engine thread. |
| `CategoryGenerator` | seeded RNG; `next(n)` returns up to n specs. Types: artist discography, year, decade, genre, "Your Playlists", "Your Albums". **No type repeats until every eligible type has been used once, then the cycle restarts**; a type picks an *unused instance* (another artist, another year, ...); a type without an unused instance is not eligible; fewer specs than asked (or none) when the content is exhausted. Max 50 items per carousel. |
| `CarouselItem` | `{kind album/playlist, id, title, subtitle (artists, albums only), cover, owner, year, type}`. |
| `PlayRequestBuilder` additions | `carouselFrom(items, index)`: one independent group per carousel item (album or playlist, so each registers in Recently Played), started at the clicked one, recommendations after the last (UI p.53); `carouselAll(items, title, key)`: ONE `Carousel` group with every track tagged by its source container (the category page's play button). |
| `QueueOrigin.key` | the category key travels with a `Carousel` origin so the player header can open the category page. |

Category keys (also the page text of navigation): `recent`, `artist:<id>`, `year:<y>`, `decade:<d>` (1990 = the 1990s), `genre:<tag>`, `playlists`, `albums`.

## 2. Engine side
`LibraryEngine`: `loadHome()` (Recently Played from `RecentRepo`, newest first, <= 50, joined with album/playlist info; plus the first 5 categories - once per library session, later calls return the same ones), `loadMoreCategories(n)`, `loadCategory(key)` (ALL items of that category, with their count and total duration), `buildCarouselRequest(items, index, all, title, key)`. The generator lives in the engine and is reset when the library closes or changes (the categories are regenerated lazily).

## 3. View-models and QML
* `CarouselModel` = `LibraryListModel` with `kind` and `year` roles (playlists have no subtitle).
* `HomeVm`: greeting text, `recent` model, `categories` list model (`title`, `key`, `items` model), `loadMore()` when the page nears its end, `refresh()` after the library changed or something was played. `CategoryVm`: title, "Carousel" label, "15 albums, 6 h 20 min", tile model, `playAll()`, `play(index)`.
* `Carousel.qml`: circular `PathView` (7 visible items, centre enlarged and in front, side items smaller and dimmer), <= 50 items, drag to rotate, click the centre item to play it (carousel queue), click a side item to bring it to the centre, title (and artist for albums) under the centre item, wheel rotates. Delegates exist only for the visible ones.
* `HomePage.qml`: vertical list of "title + carousel" sections (Recently Played first, always); the category title opens the category page; reaching the end asks for more categories while content lasts. `CategoryPage.qml`: label "Carousel", big title, statistics, play button, 3-column tile grid (cover plays that item, title opens it).
* `DropOverlay.qml`: over the middle window while files are dragged in: page title becomes "Import", the page is blurred, an import icon in the centre; a drop opens the Import page (placeholder until M8).
* Empty library: the page is only the greeting (UI p.23).

## 4. Tests
`tst_greeting` (every boundary), `tst_category_generator` (cycle rule with fake sources: no repeats until all types used, instances unused, minimums, exhaustion, determinism with a seed, 50-item cap), `tst_home_queries` (real in-memory library: eligibility counts, items, recent list order and cap, category stats, hidden/Unsorted exclusion), `tst_play_request_builder` additions for carousel queues, UI model tests for `HomeVm` (greeting refresh, lazy loading), a screenshot check of populated / empty / drop states, and a scroll benchmark of the Home page.
