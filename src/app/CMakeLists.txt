qt_add_executable(Semplice WIN32
    main.cpp
    AppBootstrap.h
    AppBootstrap.cpp
)

# Windows manifest (long paths, per-monitor DPI); CMake merges it with Qt's own via mt.exe
if(WIN32)
    target_sources(Semplice PRIVATE app.manifest)
    # version resource + icon (Explorer, installer, task manager)
    set(SEMPLICE_ICON_PATH "${CMAKE_SOURCE_DIR}/resources/AppIcon.ico")
    configure_file(Semplice.rc.in "${CMAKE_CURRENT_BINARY_DIR}/Semplice.rc" @ONLY)
    target_sources(Semplice PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/Semplice.rc")
    # the icon is read when the resource is compiled: a new icon file must rebuild it
    set_source_files_properties("${CMAKE_CURRENT_BINARY_DIR}/Semplice.rc" PROPERTIES OBJECT_DEPENDS "${SEMPLICE_ICON_PATH}")
endif()

# Fonts and icons (not QML files): available as :/fonts/... and :/icons/...
file(GLOB SEMPLICE_FONT_FILES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/resources/fonts/*.ttf")
file(GLOB SEMPLICE_ICON_FILES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/resources/icons/*.svg")
file(GLOB SEMPLICE_APP_ICON_FILES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/resources/icons/app/*.png")
file(GLOB SEMPLICE_LICENSE_FILES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/resources/license/*.txt")
qt_add_resources(Semplice "assets"
    PREFIX "/"
    BASE "${CMAKE_SOURCE_DIR}/resources"
    FILES ${SEMPLICE_FONT_FILES} ${SEMPLICE_ICON_FILES} ${SEMPLICE_APP_ICON_FILES} ${SEMPLICE_LICENSE_FILES}
)

# Translations: compiled to .qm and embedded as :/i18n/app_<lang>.qm
qt_add_translations(Semplice
    TS_FILES
        ${CMAKE_SOURCE_DIR}/translations/app_en.ts
        ${CMAKE_SOURCE_DIR}/translations/app_ru.ts
        ${CMAKE_SOURCE_DIR}/translations/app_zh_CN.ts
        ${CMAKE_SOURCE_DIR}/translations/app_ja.ts
    RESOURCE_PREFIX "/i18n"
)

target_compile_definitions(Semplice PRIVATE SEMPLICE_VERSION="${PROJECT_VERSION}")
target_link_libraries(Semplice PRIVATE
    semplice_ui semplice_platform semplice_core
    semplice_qml semplice_qmlplugin
    Qt6::Quick Qt6::QuickControls2
)
semplice_set_warnings(Semplice)

install(TARGETS Semplice RUNTIME DESTINATION .)

