# Shared helpers for tests and tools: integrity checker, synthetic library generator.
add_subdirectory(tools)

# Each unit test is its own executable, registered with CTest (visible in Qt Creator's test pane).
function(semplice_add_unit_test name)
    qt_add_executable(${name} unit/${name}.cpp)
    target_link_libraries(${name} PRIVATE semplice_core semplice_testtools Qt6::Test)
    target_include_directories(${name} PRIVATE unit)
    semplice_set_warnings(${name})
    add_test(NAME ${name} COMMAND ${name})
    # Let ctest find the Qt DLLs and the SQL driver plugin when run outside Qt Creator.
    set_tests_properties(${name} PROPERTIES
        ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
endfunction()

semplice_add_unit_test(tst_result)
semplice_add_unit_test(tst_settings)
semplice_add_unit_test(tst_logger)
semplice_add_unit_test(tst_windowgeometry)
semplice_add_unit_test(tst_normalizer)
semplice_add_unit_test(tst_releasedate)
semplice_add_unit_test(tst_migrator)
semplice_add_unit_test(tst_repos)
semplice_add_unit_test(tst_library_rules)
semplice_add_unit_test(tst_search_index)
semplice_add_unit_test(tst_synthetic_library)
semplice_add_unit_test(tst_path_sanitizer)
semplice_add_unit_test(tst_file_storage)
semplice_add_unit_test(tst_journal_recovery)
semplice_add_unit_test(tst_library_folder)
semplice_add_unit_test(tst_storage_integration)
semplice_add_unit_test(tst_tag_service)
semplice_add_unit_test(tst_genres)
semplice_add_unit_test(tst_encoding_fixer)
semplice_add_unit_test(tst_cover_discovery)
semplice_add_unit_test(tst_import_service)
semplice_add_unit_test(tst_share)
semplice_add_unit_test(tst_library_edits)
semplice_add_unit_test(tst_rescan)
semplice_add_unit_test(tst_library_session)
semplice_add_unit_test(tst_artist_splitter)
semplice_add_unit_test(tst_play_queue)
semplice_add_unit_test(tst_playback_service)
semplice_add_unit_test(tst_library_playback)
semplice_add_unit_test(tst_browse)
semplice_add_unit_test(tst_home)
semplice_add_unit_test(tst_cards)
semplice_add_unit_test(tst_recommender)
semplice_add_unit_test(tst_translations)
semplice_add_unit_test(tst_thumbnail_cache)
target_compile_definitions(tst_translations PRIVATE SEMPLICE_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

# Windows-only: the real platform implementations.
if(TARGET semplice_platform)
    semplice_add_unit_test(tst_cover_provider)
    target_link_libraries(tst_cover_provider PRIVATE semplice_ui)
    semplice_add_unit_test(tst_ui_models)
    target_link_libraries(tst_ui_models PRIVATE semplice_ui)
    semplice_add_unit_test(tst_engine_editors)
    target_link_libraries(tst_engine_editors PRIVATE semplice_ui semplice_testtools)
    semplice_add_unit_test(tst_editor_vm)
    target_link_libraries(tst_editor_vm PRIVATE semplice_ui)
    semplice_add_unit_test(tst_home_vm)
    target_link_libraries(tst_home_vm PRIVATE semplice_ui)
    semplice_add_unit_test(tst_windows_attributes)
    target_link_libraries(tst_windows_attributes PRIVATE semplice_platform)
    semplice_add_unit_test(tst_windows_platform)
    target_link_libraries(tst_windows_platform PRIVATE semplice_platform)
endif()

# "No code path deletes permanently": only core/storage/OwnFiles.cpp may call a delete API.
add_test(NAME no_direct_deletes
    COMMAND ${CMAKE_COMMAND} -DSRC_DIR=${CMAKE_SOURCE_DIR}/src -P ${CMAKE_CURRENT_SOURCE_DIR}/check_no_direct_deletes.cmake)
# ...and the guard itself must notice a violation.
add_test(NAME no_direct_deletes_detects_violations
    COMMAND ${CMAKE_COMMAND} -DSRC_DIR=${CMAKE_CURRENT_SOURCE_DIR}/data/delete_violation -P ${CMAKE_CURRENT_SOURCE_DIR}/check_no_direct_deletes.cmake)
set_tests_properties(no_direct_deletes_detects_violations PROPERTIES WILL_FAIL TRUE)

# "No hard-coded user text": QML must go through qsTr() (M10).
add_test(NAME no_untranslated_strings
    COMMAND ${CMAKE_COMMAND} -DSRC_DIR=${CMAKE_SOURCE_DIR}/src/qml -P ${CMAKE_CURRENT_SOURCE_DIR}/check_strings.cmake)
add_test(NAME no_untranslated_strings_detects_violations
    COMMAND ${CMAKE_COMMAND} -DSRC_DIR=${CMAKE_CURRENT_SOURCE_DIR}/data/string_violation -P ${CMAKE_CURRENT_SOURCE_DIR}/check_strings.cmake)
set_tests_properties(no_untranslated_strings_detects_violations PROPERTIES WILL_FAIL TRUE)
