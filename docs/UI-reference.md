# Semplice UI reference (extracted from UI.pdf)
> Machine-extracted from the owner's original document (`docs/source/UI.pdf`). Text is verbatim, including the owner's typos ("Atrists", "Alphabeth", "RECOMENTATION") - do NOT copy those typos into the UI; see SPEC.md section 13.> Each page has a screenshot under `docs/design/pages/`. The screenshots are layout *intent* from a first-time designer, not pixel specs (see SPEC.md section 8.2).

---
## Page 1

![page 1](design/pages/p-01.jpg)

```text
ARCHITECTURE AND DESIGN REFERENCE

Semplice

The Player is a completely offline desktop music player for Windows with its own managed library.
Instead of pointing the application at an existing messy folder, the user chooses an empty directory (the
library root). The application creates and maintains the folder tree inside it, copies imported music into
it, and indexes everything in a SQLite database. The library is organized around artists and their
discography (albums, singles, EPs) plus free-form playlists that do not force the user to add an artist to
the library.

 Item                         Value

 Working title                Semplice

 Application type             Completely offline music player with its own managed library

 Platform                     Windows 10 (1809 or later) and Windows 11, x64

 Languages (UI)               English (source and priority), Russian, Chinese (Simplified), Japanese

 Audio formats                MP3, FLAC, M4A/AAC, OGG (Vorbis), WAV, WMA

 Image formats                JPEG, PNG (cover art, artist images)

 Implementation               C++17/20, Qt 6 with QML (Qt Quick), CMake, SQLite (via Qt SQL), TagLib

 Design                       Figma (UI mock-ups; not part of the build)

 Distribution                 Free, open source (Github repository + standalone installer), voluntary donations (Patreon
                              etc.)



Source tree
 MusicPlayer/

   CMakeLists.txt

   cmake/                    # helper modules (TagLib, deploy, version.h)

   src/

        app/                 # main.cpp, application bootstrap, DI wiring

        core/                # static library, NO QML dependency

          domain/            # Artist, Album, Track, Playlist, Tag (plain structs/enums)

          db/                # Database, Migrations, repositories, schema/*.sql

          services/          # Library, Import, Playback, Category, Search, Settings

          storage/           # FileStorageService, PathSanitizer, OperationJournal

          tags/              # TagService (TagLib), GenreMap, EncodingFixer

          audio/             # IAudioBackend, QtAudioBackend (BassBackend later)

          util/              # Logger, Result<T,Error>, Job, Normalizer

        ui/                  # C++ side of the UI: models, viewmodels, image provider

        qml/

          Main.qml

          theme/             # Theme.qml (singleton), Fonts, Icons

          components/        # Carousel, TrackRow, CoverImage, Dialog, Toast, ...

          views/             # Home, Artist, Album, Playlist, Editor, Settings, ...

          panels/            # Library, Player, SidePanel, Hotbar
```

---
## Page 2

![page 2](design/pages/p-02.jpg)

```text
resources/                     # icons (SVG), genre seed, fonts (if any)

   translations/                  # app_en.ts, app_ru.ts, app_zh_CN.ts, app_ja.ts

   tests/

     unit/    integration/   qml/    tools/ (synthetic library generator)

   docs/

Library root layout
 <LibraryRoot>/

   .library/                             # hidden, owned by the Player

     library.db                          # SQLite database (+ -wal, -shm while running)

     library.db.bak1 .. bak3             # rotating backups

     library.json                        # marker: app id, library uid, schema version

     covers/                             # thumbnail + extracted-cover cache (can be rebuilt)

     journal/                            # pending file operations (crash recovery)

   Artists/

     <Artist dir_name>/

       artist_cover.jpg|png              # optional

       <Album dir_name>/

            album_cover.jpg|png          # optional

            01 - Track title.mp3

            02 - Other title.flac

   Playlists/

     <Playlist dir_name>/

       playlist_cover.jpg|png            # optional, user-chosen

       Track title - Artist.mp3          # only tracks that have no album

INTERFACE AND FUNCTIONS

GENERAL VIEW
```

---
## Page 3

![page 3](design/pages/p-03.jpg)

```text
Left panel – Library, Middle panel – Main window, Right panel – Player on top and Support window
under it. At the bottom, across the full width of the window – Hotbar.




Empty state




First launch

LIBRARY
```

---
## Page 4

![page 4](design/pages/p-04.jpg)

```text
The default view when the library is empty.
```

---
## Page 5

![page 5](design/pages/p-05.jpg)

```text
If user select Album’s category and exit app, library should remember last state and recover it in next
launch. The Artists category displays all the artists in the user's collection (excluding hidden ones, which
do not appear here). Clicking an artist's name opens their profile card in the center pane. Clicking their
avatar starts playback of their entire discography. If shuffle mode is enabled in the player, the
discography plays in random order (creating a queue of all the artist's tracks); if shuffle mode is disabled,
tracks play in order of release date, from newest to oldest.
```

---
## Page 6

![page 6](design/pages/p-06.jpg)

```text
The library window also features sorting and display view settings. A button next to the category name
opens this submenu, with display options at the top and sorting options below. There are three display
modes: list view, large icons (2 per row), and extra-large icons (3 per row). Sorting is based on two
criteria: alphabetical order and recency. An upward arrow combined with the alphabetical option sorts
from the end of the alphabet to the beginning, while a downward arrow sorts from the beginning to the
end. View settings are configured individually for each tab; they are saved upon exiting the application
and restored when it is launched again. Sorting settings are also specific to each tab but are not saved.
The default setting for recency is "newest to oldest" (recently added/updated).
```

---
## Page 7

![page 7](design/pages/p-07.jpg)

```text
Large icons (2 per row).
```

---
## Page 8

![page 8](design/pages/p-08.jpg)

```text
Large icons (3 per row). This view does not display the artist's name beneath the avatar, as the text does
not fit within the frame. Consequently, opening the artist card is not possible in this view (only
discography playback is available).
```

---
## Page 9

![page 9](design/pages/p-09.jpg)

```text
The Albums tab. Here, you can see how the library tab switching works visually: the title of the active
tab remains centered, effectively replacing the previous one. You switch between tabs by clicking the
title of an adjacent tab. Visually, this functions like the carousel in the center pane, but applied to tab
titles. The Albums tab displays the album artwork, the album title, and the artist's name. Clicking the
album artwork starts playback; clicking the album title opens it in the center pane; and clicking the
artist's name opens their profile card.
```

---
## Page 10

![page 10](design/pages/p-10.jpg)

```text
The album display view has been modified in a way similar to the artist view, though the sorting differs.
Options for "Year" and "Artist" have been added. The "Year" option is straightforward: a downward
arrow sorts by release date from newest to oldest, while an upward arrow sorts from oldest to newest.
By default, items are sorted by "recently added/updated," from newest to oldest. Filtering options allow
you to choose which album types appear in the list—such as whether or not to display singles. By
default, all album categories are shown.
```

---
## Page 11

![page 11](design/pages/p-11.jpg)

```text
Large icons (2 per row).
```

---
## Page 12

![page 12](design/pages/p-12.jpg)

```text
Large icons (3 per row). The issue is similar. This view does not display the album title or artist name, as
the text does not fit within the frame. Consequently, it is not possible to open the artist card or the
album itself in the center pane while using this view (playback only).
```

---
## Page 13

![page 13](design/pages/p-13.jpg)

```text
The playlists tab is the simplest of the three. It displays the playlist cover and title. Clicking the avatar
starts playback, while clicking the title opens the playlist in the middle pane.
```

---
## Page 14

![page 14](design/pages/p-14.jpg)

```text
The sorting and display parameter logic is similar to that of the other tabs, but with fewer options: only
recently added/updated playlists and alphabetical order.
```

---
## Page 15

![page 15](design/pages/p-15.jpg)

```text
Large icons (2 per row).
```

---
## Page 16

![page 16](design/pages/p-16.jpg)

```text
Large icons (3 per row). The issue is similar. This view does not display the playlist name, as the text
does not fit in the window. Consequently, opening the playlist in the middle window is not possible in
this view (only playback is available).
```

---
## Page 17

![page 17](design/pages/p-17.jpg)

```text
At the very top of this window is an "Add" button. This button opens a submenu containing several
options: "Add Artist" opens the artist addition window in the center pane, "Add Album" opens the
album addition window in the center pane, and "Add Playlist" opens the playlist addition window in the
center pane.

HOTBAR



This is the default view. A search bar is located on the left. On the right, there is a "Home" button that
returns the central window to the default view. To the right of this button are the history navigation
arrows for the central window: the left arrow returns the window to its previous state, while the right
arrow moves it to the next state (if it is currently in a previous state). The settings button opens the
settings menu within the central window. The volume slider is set to 50% by default; the player must
remember the user-selected level and restore it upon the next launch. Clicking the volume icon instantly
```

---
## Page 18

![page 18](design/pages/p-18.jpg)

```text
mutes the sound, and clicking it again restores the volume to the previously set level. Next is the queue
button, which is inactive by default if the queue is empty and nothing is currently playing.




This section displays the submenus that can be triggered by this window. On the left is the search results
window, which appears when the user enters text into the search bar. The categories are listed in the
following order: artists, albums, playlists, and tracks. Certain categories may be omitted if there are no
matching results for them. Only five results are displayed per category, but a "Show More" button at the
end of each category allows the user to view all search results for that category; this button does not
appear if there are five or fewer results. The rules for opening and playback are the same as those for
the library window. If the user starts a single track from the "Tracks" category, the playback queue
consists solely of that track. For playlist/album – only that playlist/album. For artist – only that
discography. Search results doesn’t support “endless stream”.

The queue window performs the same functions as the standard queue view but allows the user to see
the entire queue, rather than just the previous, current, and next tracks. The queue is displayed from
top to bottom.

The queue window and the search window cannot be open simultaneously. Any interaction outside
these windows closes them. If the search bar is closed in this manner (without the user clearing the
text), the text is retained until the application is exited, and the corresponding search results appear
when the search bar is clicked.

The queue displays only the current discography, album, or playlist. On the last track in the queue, you
can press the "next track" button, and the infinite stream will start the next album (replacing the current
queue with that album). If the track currently playing or the next one in line is deleted, it is skipped and
removed from the queue. Restore on launch should come back paused, and skip tracks that no longer
exist.

SUPPORT PANEL
```

---
## Page 19

![page 19](design/pages/p-19.jpg)

```text
Default view when the queue is empty.




The currently playing track is displayed in the center. The previous track appears above it, and the next
one below. You can switch tracks either by clicking the desired track's cover art or by holding down the
left mouse button in the current track area and dragging up or down—similar to the carousel in the
middle pane.

PLAYER PANEL
```

---
## Page 20

![page 20](design/pages/p-20.jpg)

```text
The default view when the queue is empty and nothing is playing.
```

---
## Page 21

![page 21](design/pages/p-21.jpg)

```text
Above the player's cover art, the current playback source is displayed: if a playlist is playing, the playlist
name appears; if an album is playing, the album name appears; if an artist's discography is playing, the
artist's name is shown along with an indication that the discography is playing. Clicking any of these
opens the corresponding view in the central panel.

The cover art is displayed below. It is sourced from the track's metadata, which takes priority. If the
metadata lacks cover art, the display depends on the playback context. If the track is from a playlist and
does not appear in any album within the discography, a placeholder image with a musical note icon is
used. If the track belongs to an album that has cover art, that album art is used. If the track is part of an
album but neither the album nor the track's metadata contains cover art, the placeholder image with
the musical note icon is used.

The track title and artist(s) are displayed below the cover art. Clicking the title or artist name opens the
corresponding view in the central panel.

The track progress bar: the current playback time is on the left, followed by the progress bar itself, and
the total track duration is on the right.
```

---
## Page 22

![page 22](design/pages/p-22.jpg)

```text
Music control buttons: Shuffle (on/off), Previous Track, Play/Pause, Next Track, and Repeat (off, repeat
album/playlist, repeat current track). If the track has played for 5 seconds or less, the "Previous" button
switches to the previous track; otherwise, it rewinds the current track to the beginning. The "Previous"
button may be inactive at the very start of playback. If shuffle mode is enabled while a queue is playing,
tracks that have already played as part of the album or playlist are not repeated.




Another view of the player window. This shows interaction with the track progress bar (with the cursor
positioned at the 2-minute mark). It also displays the appearance of the repeat button when "repeat
single track" is enabled, as well as the play/pause button while a track is playing.

The player must remember the current track, its progress, and the queue so that, upon relaunching, it
can restore this state and resume playback from the exact second where it left off, maintaining the
same queue.

Player header states. This displays only the name of the current album or playlist from which the track is
playing. It serves as a shortcut to the album or playlist view.

MAIN WINDOW
```

---
## Page 23

![page 23](design/pages/p-23.jpg)

```text
The default view when the library is empty. "Listener" is displayed instead of the username by default.
The user can change how they are addressed in the settings.
```

---
## Page 24

![page 24](design/pages/p-24.jpg)

```text
The standard view presents the user with a "Recently Played" list and categories automatically
generated by the player. At the top, there is a greeting message that varies based on the context—such
as New Year's, the first day of a season (summer, autumn, winter, spring), and so on.

Below that lies the "Recently Played" category. Technically, this and other similar sections function as
carousels—playlists of albums or playlists generated by the player itself. Specifically, this category
contains albums or playlists the user has listened to recently; playback is triggered simply by starting an
album or playlist. The category holds up to 50 items and retains its state, ensuring it remains populated
upon subsequent launches. It uses a carousel display style: the active album or playlist appears centered
and, in the foreground, while the others recede into the background. Navigation is achieved by clicking
and holding the left mouse button over the cover art and dragging the mouse sideways. When playing
back the discography, each album is registered separately.

The carousel supports continuous, infinite scrolling. Technically, the carousel consists of a maximum of
50 elements. Its infinity is a visual effect. By default, the first item in the carousel is the active one; the
next item sits to its right, while the last item sits to its left, creating the infinite loop effect. For albums,
the display shows both the title and the artist(s); for playlists, only the title is shown. The "Recently
Played" category is always present and appears first.

The remaining categories fall into several types:

Artist-based category. This carousel is titled "[Artist Name]: Discography" and contains the artist's
complete discography. Within this carousel, the artist's name is not displayed beneath the selected
album—only the album title appears. The albums are arranged in random order.
```

---
## Page 25

![page 25](design/pages/p-25.jpg)

```text
Year-based category. This carousel is named after the year upon which it is based. It contains albums
from the collection released in that specific year (as indicated in the carousel's title). Maximum of 50
items. A minimum of 5 items is required to generate and display this category.

Decade-based category. Similar to the year-based category, but based on a decade. It contains all
albums in the collection released during the 10-year period specified in the carousel's title. Maximum of
50 items. A minimum of 5 items is required to generate and display this category.

Genre-based category. This carousel is named after the genre tag upon which it is based. It contains all
albums tagged with that genre. Maximum of 50 items. A minimum of 5 items is required to generate
and display this category.

Playlist carousel. Titled "Your Playlists." It contains all playlists created by the user.

Album’s carousel. Titled “Your Albums”. It contains all albums added by the user.

5 carousels are generated upon each launch (excluding the "Recently Played" carousel). If the user
scrolls down, another categories should be generated on the fly as long as there is sufficient content.




The user can click on the category name to open a more familiar view that immediately displays all items
in that category.
```

---
## Page 26

![page 26](design/pages/p-26.jpg)

```text
Your playlists carousel
```

---
## Page 27

![page 27](design/pages/p-27.jpg)

```text
An information banner may appear at the bottom of the middle window—displaying general
information, error details, and so on. It does not disappear on its own, and the newest banner appears
below the previous ones. A maximum of three banners are displayed simultaneously. The oldest
banners disappear automatically.
```

---
## Page 28

![page 28](design/pages/p-28.jpg)

```text
More error banners. Error -1 is default error banner if something went wrong.
```

---
## Page 29

![page 29](design/pages/p-29.jpg)

```text
The central window serves as a drop zone for music. When you drag and drop tracks (whether a single
file, multiple files, or an entire folder), the header text changes to "Import," the rest of the window
becomes blurred, and an import icon appears. The specific window that opens next depends on the
number and type of tracks: 1–3 tracks trigger the "Single" view, 4–7 trigger "EP," and 8+ trigger "Album."
This logic applies if the tracks share the same album metadata or if more than half feature the same
artist; otherwise, the playlist creation window opens. Since this system may occasionally misidentify the
content, the album/playlist and artist addition window can also be accessed manually via the "Add"
button in the library view.
```

---
## Page 30

![page 30](design/pages/p-30.jpg)

```text
Import progress bar.
```

---
## Page 31

![page 31](design/pages/p-31.jpg)

```text
Artist addition window. It is appropriately named. Here, you can enter the artist's name and select an
avatar. At the bottom are buttons to save or cancel the current operation. An artist added via this menu
appears in the library (i.e., is not hidden).
```

---
## Page 32

![page 32](design/pages/p-32.jpg)

```text
The artist editing window. It is appropriately named. Here, you can change the artist's name and avatar,
add or remove tags, and modify or delete albums from their discography. At the very bottom, there are
buttons to save changes, cancel the operation, and delete the artist. If the artist's name is blank, the
changes cannot be saved.
```

---
## Page 33

![page 33](design/pages/p-33.jpg)

```text
Deleting an artist. In this same window, a warning appears if the user chooses to delete an artist. Once
the user confirms the deletion, there are two possible outcomes. If the artist has only solo albums
(meaning they are the sole author of those albums) and their name does not appear on any tracks
within playlists or in other artists' discographies, they are deleted entirely. If their profile is still required
(due to dependencies such as collaborative albums—even if they are the primary/lead artist—or their
name appearing on specific playlist tracks, etc.), then albums without dependencies are deleted, and the
artist's profile is moved to the "hidden artist" category (since it remains in use). The warning text shown
in the screenshot is for demonstration purposes; it should, however, at least roughly reflect the actual
operation that will take place.
```

---
## Page 34

![page 34](design/pages/p-34.jpg)

```text
This is the artist card—specifically for an artist who is not hidden. The window title displays the artist's
name. The view includes the artist's avatar, an indicator that this is an artist card, the artist's name, and
statistics such as the number of albums, singles, etc. Statistics are omitted for categories that do not
exist for the artist (e.g., if the artist has no live albums, that statistic is not shown). Tags specific to the
artist appear below. Further down, there are buttons to play the entire discography in order of release
date, from newest to oldest. Shuffle launch whole discography on random. And an edit button that
opens the artist editing window.

The first carousel displays the discography; it always appears at the top. These carousels function
similarly to the standard ones in the central window. Clicking a carousel title opens that category,
displaying the albums in a tile layout. All albums are listed in chronological order by release date, from
```

---
## Page 35

![page 35](design/pages/p-35.jpg)

```text
newest to oldest, with the album type displayed next to the release year. Opening this carousel displays
it under the title "Discography: [Artist Name]." Below this are carousels for specific album types; for
instance, the singles section is titled "Singles: [Artist Name]." The bottom two carousels are particularly
noteworthy: "Collaborations" shows albums where the artist is listed as a contributor but not the
primary artist (i.e., not the main album author). The "Feats" carousel covers albums where the artist is
not listed as the primary author but appears as a secondary or additional artist on one or more tracks.

If an artist's profile is open and the user drops tracks or a music folder into that window, the "Add
Album" window will open with that artist already listed as the author.




Hidden artist card. The panel displays the artist's name. It features a standard dark-gray avatar with the
first letter or digit of the name centered. It indicates that the entry is an artist and shows their name;
statistics and tags are not displayed. There are buttons to play all of the artist's tracks and to add them
to the discography. The hidden artist card contains only two carousels. The "Discography" carousel holds
a single album titled "Unsorted tracks" (automatically created). This album contains all tracks belonging
to the artist (i.e., tracks found in playlists, excluding those in the "Feat’s" carousel). The "Feat’s" carousel
functions just like that of a standard artist. The system-generated "Unsorted tracks" album has the
following parameters: type—"other"; title—"Unsorted tracks"; cover art—a placeholder similar to the
player's default (dark gray with a musical note icon); no tags; and a release date matching the album's
creation date. For a hidden artist, the "Unsorted Tracks" album serves as a logical container for tracks.
For an artist in the discography, however, this becomes a real album where the tracks are physically
```

---
## Page 36

![page 36](design/pages/p-36.jpg)

```text
stored. Its title should be translated at display time and not stored as text, because the language can
change at runtime.

It should be excluded from the year, decade and genre carousels. And this album appears only on the
artist's page.




Album addition window. The window bears the appropriate title. Here, you can select the album name
and its cover art. If a file named `cover.jpg` or `cover.png` exists in the folder, it is used as the default
cover by priority. If no such file is present, the cover is derived from the first track's metadata (and saved
in the directory as a separate file). If the user has manually specified a cover, that image takes
precedence over the automatic selection. The same logic applies to the album name: the system first
attempts to read it from the metadata; if none is found, the field remains blank by default. If the user
has already entered an album name, the automatic form-filling function is disabled.

Below this, the user must specify the artists. At least one artist is required; otherwise, the album cannot
be saved. Next are the tags; these can be automatically populated from track metadata, or the user can
add or remove them manually. These tags are subsequently associated with the artist. Adding tags is
optional.

Next is the album release date. The year is mandatory, while the day and month are optional; a "-"
option is available when selecting the day or month.
```

---
## Page 37

![page 37](design/pages/p-37.jpg)

```text
Below that is the album type selection. All album types are listed here. The logic for automatically
selecting this category was described earlier. The visual layout mirrors the categories found in the library
window. The order is: Single, Album, EP, Live, Compilation, Other.

Finally, there is the tracklist section. It displays the track number, track cover art, artist name, track title,
and duration. An album cannot be saved without tracks.

All artist names and album titles must be unique. Playlists too.

If a user attempts to add an album, artist, or playlist with a name that already exists, an informational
banner appears at the bottom of the window stating that such an album, artist, or playlist already exists.




This demonstrates the appearance and functionality of the submenus. The "Add Artist" button opens a
submenu displaying a search bar, a button to add a new artist, and a list of all existing artists (including
hidden ones). The search bar and results occupy five slots in total, but the submenu can be scrolled.
```

---
## Page 38

![page 38](design/pages/p-38.jpg)

```text
Clicking the "Add Artist" button opens the "Add New Artist" window in the center pane; if text was
entered into the search bar, it is automatically populated in the new artist window.

If a hidden artist was added by the album's author, that artist is automatically added to the discography
and ceases to be hidden. A track-level setting keeps it hidden.

Deleting an artist who is primary on a collaborative album. The artist becomes hidden but still owns a
visible album. Need to re-home the primary credit to a surviving co-artist, which renames the folder.

Hovering over a track in the tracklist reveals an options icon containing several parameters: "Add to
Playlist" opens a submenu similar to the artist-adding one but for playlists. Selecting "Add as New
Playlist" opens a playlist creation window with the name taken from the search bar and the track
included in the list. Selecting an existing playlist simply adds the track to that playlist without opening
further windows. The "Add to Album" button functions similarly. The "Edit" button allows you to modify
the specific track—changing its title and adding or removing artists. The "Remove" button removes the
track from the album; this button is active only if the track exists elsewhere and this is not its only copy
(e.g., it appears in a playlist or another album). Otherwise, the button is inactive. The "Delete" button
removes the track entirely, including its entries in playlists and other albums.

A single track can belong to multiple albums simultaneously. Physically, the file resides in the location
where it was first added. If it is deleted from that original location, the file is moved to the second
location where it exists.




Warning when deleting a track.
```

---
## Page 39

![page 39](design/pages/p-39.jpg)

```text
This is how single-track editing should be implemented. The mechanism for adding artists is similar to
that used for albums. However, if a user adds a new artist via the submenu, the artist-addition window
opens, but the artist is created as a hidden entry.

This same window also displays a submenu for adding tags; it is similar to the artist submenu but does
not allow for the addition of custom tags. These same tags are automatically added to the artist when
the first album is added. Once tags have been assigned to the artist, they are never automatically
assigned to that artist again.
```

---
## Page 40

![page 40](design/pages/p-40.jpg)

```text
This demonstrates the selection process for the album's release month and year. Clicking on a value
opens a scrolling wheel, which can be spun to the desired setting using the mouse wheel or by clicking
and dragging with the left mouse button. Clicking outside the scrolling wheel applies the selected value.
The day and month parameters include a "-" option. If the date is invalid, highlight it in red and prevent
the album from being saved. The same applies if only the day and year are specified. Specifying only the
month and year is acceptable; for sorting purposes, a missing day is treated as the 1st of the month, and
a missing month as January. The date format is the same for all application languages. A year ranging
from 1744 to the current year plus one, since a user might add classical music released 100 or more
years ago.
```

---
## Page 41

![page 41](design/pages/p-41.jpg)

```text
In this same window, you can add music to the current album. Tracks are added to the end of the list
and do not trigger automatic field filling if the fields are already populated.
```

---
## Page 42

![page 42](design/pages/p-42.jpg)

```text
Album editing window. It is named accordingly. Its functionality is similar to that of the album addition
window, with the addition of an album deletion button.
```

---
## Page 43

![page 43](design/pages/p-43.jpg)

```text
Album deletion screen. Deletes the album and all tracks within it. If these tracks do not exist elsewhere,
they are permanently deleted; if they do exist elsewhere, they remain in the collection. If this is the
artist's only album and the user deletes it—but some of its tracks are included in a playlist—the artist
card becomes hidden, and those tracks are moved to the "Unsorted Tracks" album.
```

---
## Page 44

![page 44](design/pages/p-44.jpg)

```text
Album card. The window title displays the album name. It contains the album cover, album type, and
album title. Below this, the number of tracks, total duration, and release year are shown. The artists
featured on the album are listed below; clicking on them opens the artist card. Album playback buttons
appear below, with an edit button to the right that opens the album editing window. The tracklist is
displayed further down. At the very bottom, the album tags and full release date (if available) are
shown, along with the list of artists. You can open the track options submenu (via the three-dot icon)
without navigating to the album editing window. If an album is started by pressing the play button and
"repeat all" is enabled, only that album repeats. If "repeat all" is disabled, then “endless stream”
```

---
## Page 45

![page 45](design/pages/p-45.jpg)

```text
Playlist addition window. The window bears the appropriate title. You can specify the playlist name and
cover art (the mechanism for adding cover art is the same as for albums). Tracks can also be added
within the window. An album or playlist cannot exist without tracks; if no tracks remain in a playlist or
album following a cascading deletion, the album or playlist itself is also deleted.
```

---
## Page 46

![page 46](design/pages/p-46.jpg)

```text
Playlist editing window. Similar to the "Add" window. Tracks have an option to be added to a
discography, along with a selection for the artist to whom the track will belong. This button creates an
"Unsorted tracks" album for the selected artist containing that track.
```

---
## Page 47

![page 47](design/pages/p-47.jpg)

```text
Deleting a playlist. This deletes the playlist itself as well as any tracks within it that do not exist
elsewhere (i.e., are not found in the discography or other playlists).
```

---
## Page 48

![page 48](design/pages/p-48.jpg)

```text
Playlist card. It displays the playlist title, an indicator showing it is a playlist, the cover art, and the
playlist name. It also shows the number of songs and their total duration. There is a play button and a
button to open the playlist editing window. Playlists doesn’t support “endless stream”.
```

---
## Page 49

![page 49](design/pages/p-49.jpg)

```text
Settings screen. It has a corresponding window title. It opens via the settings button on the hotbar. It
contains all the player's settings, a button to open the license agreement, and a button that opens the
application's official website in a browser.
```

---
## Page 50

![page 50](design/pages/p-50.jpg)

```text
License and user agreement.
```

---
## Page 51

![page 51](design/pages/p-51.jpg)

```text
How parameters can be changed.

COLOR PALETTE




BUTTON STATES
```

---
## Page 52

![page 52](design/pages/p-52.jpg)

```text
FONTS

Inter font for English and Russian translations and swap to Segoe UI for Japanese and Chinese.

LOGO




ACTUAL INFORMATION


PLAYBACK RULES

The play button in the central window creates a playback queue.

Play button behavior:

In the playlist window: creates a queue consisting of that single playlist.

In the album window: creates a queue consisting of that single album.

In the artist window: creates a queue consisting of the artist’s entire discography, ordered from newest
to oldest by release date.

In the carousel window: creates a queue consisting of all items in the carousel.



When a queue created by the play button in the central window reaches its end:

In the playlist window: playback stops; recommendations do not begin.

In the album window: recommendations begin.

In the artist window: recommendations begin.

In the carousel window: recommendations begin.
```

---
## Page 53

![page 53](design/pages/p-53.jpg)

```text
If an album or playlist is launched from within a carousel (rather than by using the "play all" button for
the entire carousel):

Multiple independent queues are created, one for each album included in that carousel. When one
queue ends, the next one begins. The player tracks all these queues, allowing the user to return to a
previous one. Once all these queues have finished, a queue of recommendations begins.

The "repeat all" function repeats the current queue, and the "shuffle" function shuffles the current
queue—in every case.

RECOMENTATION SYSTEM

To ensure variety at the individual track level and maintain a distinct playback queue, the algorithm
should be structured as follows:

Seed Data. Albums and artists from the source that has just finished playing.

Candidates. All tracks available for playback that are not part of the "seed" albums (including tracks
found only in playlists).

Score. A weighted sum of the following metrics:

shared tags with the seed data (highest weight);

tracks featuring the same artists (preserving the inclusion of collaborations at the track level);

proximity in terms of release year or decade (low weight);

a penalty for tracks that have been listened to recently.

Selection. Random selection—weighted by score—from the top 100 (approximately) candidates, rather
than simply picking the top-ranked item; this ensures variety.

Constraints for Variety. No more than 2 tracks from the same album per 10 positions; the same album
cannot be selected twice in a row; no repeats until the pool of candidates is exhausted.

Look-ahead. Pre-selecting the next 2–3 tracks ensures that the information displayed in the "Up Next"
section of the queue panel is always accurate.

Relaxing Rules. If the pool of candidates runs out, the algorithm may draw from the "seed" albums, and
subsequently, from tracks that have already been played. Playback stops only if fewer than 2 playable
tracks remain in the library.

Recently Played History. Recommended tracks are not added to the "Recently Played" history, as a
single track should not cause its parent album to appear in the list of recently played items. Playing an
album or playlist from the carousel creates a playback queue consisting solely of that item. Once the
queue finishes—provided that "repeat all" is not enabled—the system advances to the queue for the
next album or playlist in the carousel, while remembering the previous queue so that you can return to
it. Once all queues have been completed, the system displays recommendations similar to those found
in the carousel view.

DISPLAY IN THE PLAYER WINDOW ABOVE THE COVER ART

The title above the cover art indicates the current queue.

If recommendations are playing – Recommendations

A queue consisting of a single album – The title of that album
```

---
## Page 54

![page 54](design/pages/p-54.jpg)

```text
A queue consisting of a single playlist – The title of that playlist

A queue consisting of an artist’s discography – The artist’s name

A queue consisting of an entire carousel – The name of the carousel
```
