pragma Singleton
import QtQuick

// Numbers of the C++ enums that QML needs (AppController::operate, NavigationVm pages, PlayKind).
QtObject {
    // OperationKind
    readonly property int addTrackToAlbum: 0
    readonly property int addTrackToPlaylist: 1
    readonly property int removeTrackFromAlbum: 2
    readonly property int removeTrackFromPlaylist: 3
    readonly property int deleteTrack: 4
    readonly property int deleteAlbum: 5
    readonly property int deleteArtist: 6
    readonly property int deletePlaylist: 7
    readonly property int addToDiscography: 8
    readonly property int newPlaylistFromTrack: 9
    readonly property int materialiseHiddenArtist: 10
    // PlayKind
    readonly property int playArtist: 0
    readonly property int playHiddenArtist: 1
    readonly property int playAlbum: 2
    readonly property int playPlaylist: 3
    readonly property int playTrack: 4
    // NavigationVm.Kind
    readonly property int pageHome: 0
    readonly property int pageSettings: 1
    readonly property int pageArtist: 2
    readonly property int pageAlbum: 3
    readonly property int pagePlaylist: 4
    readonly property int pageCategory: 5
    readonly property int pageAddArtist: 7
    readonly property int pageAddAlbum: 8
    readonly property int pageAddPlaylist: 9
    readonly property int pageImport: 10
    readonly property int pageEditArtist: 11
    readonly property int pageEditAlbum: 12
    readonly property int pageEditPlaylist: 13
    readonly property int pageEditTrack: 14
    readonly property int pageShare: 15           // id = the artist / album / playlist, text = "artist" | "album" | "playlist"
}
