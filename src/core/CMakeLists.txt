add_library(semplice_core STATIC
    util/Clock.h
    util/Job.h
    util/Error.h
    util/Error.cpp
    util/Result.h
    util/Logger.h
    util/Logger.cpp
    util/Normalizer.h
    util/Normalizer.cpp
    util/WindowGeometry.h
    util/WindowGeometryPolicy.h
    util/WindowGeometryPolicy.cpp
    domain/Ids.h
    domain/ReleaseDate.h
    domain/ReleaseDate.cpp
    domain/Types.h
    domain/Types.cpp
    db/Database.h
    db/Database.cpp
    db/Migrator.h
    db/Migrator.cpp
    db/ArtistRepo.h
    db/ArtistRepo.cpp
    db/AlbumRepo.h
    db/AlbumRepo.cpp
    db/PlaylistRepo.h
    db/PlaylistRepo.cpp
    db/TrackRepo.h
    db/TrackRepo.cpp
    db/TagRepo.h
    db/TagRepo.cpp
    services/ISettings.h
    services/SettingsService.h
    services/SettingsService.cpp
    services/IPathNamer.h
    services/IPathNamer.cpp
    services/TagVocabulary.h
    services/LibraryTypes.h
    services/LibraryService.h
    services/LibraryService.cpp
    services/LibraryServiceEdits.cpp
    services/LibraryServiceInternal.h
    services/SearchIndex.h
    services/SearchIndex.cpp
    browse/LibraryQueries.h
    browse/LibraryQueries.cpp
    browse/PlayRequestBuilder.h
    cards/CardQueries.h
    cards/CardQueries.cpp
    home/Greeting.h
    home/Greeting.cpp
    home/CategoryTypes.h
    home/CategoryTypes.cpp
    home/CategoryGenerator.h
    home/CategoryGenerator.cpp
    home/HomeQueries.h
    home/HomeQueries.cpp
    browse/PlayRequestBuilder.cpp
    browse/NavigationHistory.h
    browse/BannerQueue.h
    browse/UiState.h
    browse/UiState.cpp
    playback/PlaybackTypes.h
    playback/PlayQueue.h
    playback/PlayQueue.cpp
    playback/PlaybackInterfaces.h
    playback/PlaybackService.h
    playback/PlaybackService.cpp
    playback/LibraryPlayback.h
    playback/LibraryPlayback.cpp
    services/ImportService.h
    services/ImportService.cpp
    services/Maintenance.h
    services/Maintenance.cpp
    services/RescanService.h
    services/RescanService.cpp
    services/LibrarySession.h
    services/LibrarySession.cpp
    storage/IFileStorage.h
    storage/FaultInjector.h
    storage/ITrash.h
    storage/QtTrash.cpp
    storage/IFileAttributes.h
    storage/OwnFiles.h
    storage/OwnFiles.cpp
    storage/PathSanitizer.h
    storage/PathSanitizer.cpp
    storage/OperationJournal.h
    storage/OperationJournal.cpp
    storage/DbCommitLog.h
    storage/DbCommitLog.cpp
    storage/FileStorageService.h
    storage/FileStorageService.cpp
    storage/LibraryFolder.h
    storage/LibraryFolder.cpp
    tags/TrackTags.h
    tags/TrackTags.cpp
    tags/IFileReleaser.h
    tags/IAtomicReplace.h
    tags/IAtomicReplace.cpp
    tags/TagService.h
    tags/TagService.cpp
    tags/GenreVocabulary.h
    tags/GenreVocabulary.cpp
    tags/GenreMap.h
    tags/GenreMap.cpp
    tags/EncodingFixer.h
    tags/EncodingFixer.cpp
    tags/CoverDiscovery.h
    tags/CoverDiscovery.cpp
    tags/ArtistStringSplitter.h
    tags/ArtistStringSplitter.cpp
    storage/ThumbnailCache.h
    storage/ThumbnailCache.cpp
    recommend/Recommender.h
    recommend/Recommender.cpp
    recommend/LibraryRecommendationSource.h
    recommend/LibraryRecommendationSource.cpp
    share/SmplFile.h
    share/SmplFile.cpp
    share/ShareService.h
    share/ShareService.cpp
)

# Database migrations are embedded: ":/semplice/db/0001_init.sql"
qt_add_resources(semplice_core "migrations"
    PREFIX "/semplice/db"
    BASE "${CMAKE_CURRENT_SOURCE_DIR}/db/schema"
    FILES db/schema/0001_init.sql db/schema/0002_op_log.sql db/schema/0003_tags_dirty.sql db/schema/0004_artist_user_hidden.sql
)

# Include as "core/util/Result.h" (root = src/)
target_include_directories(semplice_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(semplice_core PUBLIC Qt6::Core Qt6::Sql semplice_taglib)
semplice_set_warnings(semplice_core WERROR)

# The closed tag vocabulary: ":/semplice/tags/genres.json"
qt_add_resources(semplice_core "genres"
    PREFIX "/semplice/tags"
    BASE "${CMAKE_SOURCE_DIR}/resources/tags"
    FILES "${CMAKE_SOURCE_DIR}/resources/tags/genres.json"
                "${CMAKE_SOURCE_DIR}/resources/tags/languages.json"
                "${CMAKE_SOURCE_DIR}/resources/tags/moods.json"
)
