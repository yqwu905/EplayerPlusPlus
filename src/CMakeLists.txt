set(SOURCES
    main.cpp
    app/MainWindow.cpp
    app/MainWindow.h
    models/FolderModel.cpp
    models/FolderModel.h
    models/CompareSession.cpp
    models/CompareSession.h
    models/ImageListModel.cpp
    models/ImageListModel.h
    widgets/FolderPanel.cpp
    widgets/FolderPanel.h
    widgets/BrowsePanel.cpp
    widgets/BrowsePanel.h
    widgets/FlowLayout.cpp
    widgets/FlowLayout.h
    widgets/ComparePanel.cpp
    widgets/ComparePanel.h
    widgets/ImageContextMenu.cpp
    widgets/ImageContextMenu.h
    widgets/ThumbnailWidget.cpp
    widgets/ThumbnailWidget.h
    widgets/ArrowOverlay.cpp
    widgets/ArrowOverlay.h
    widgets/ZoomableImageWidget.cpp
    widgets/ZoomableImageWidget.h
    utils/FileUtils.cpp
    utils/FileUtils.h
    utils/ImageUtils.cpp
    utils/ImageUtils.h
    services/SettingsManager.cpp
    services/SettingsManager.h
    services/ImageMarkManager.cpp
    services/ImageMarkManager.h
    services/CategoryExporter.cpp
    services/CategoryExporter.h
    services/ImageLoader.cpp
    services/ImageLoader.h
    services/ImageComparer.cpp
    services/ImageComparer.h
    styles/FluentStyle.cpp
    styles/FluentStyle.h
)

# ---- Library target for unit testing (excludes main.cpp) ----
set(LIB_SOURCES
    app/MainWindow.cpp
    app/MainWindow.h
    models/FolderModel.cpp
    models/FolderModel.h
    models/CompareSession.cpp
    models/CompareSession.h
    models/ImageListModel.cpp
    models/ImageListModel.h
    widgets/FolderPanel.cpp
    widgets/FolderPanel.h
    widgets/BrowsePanel.cpp
    widgets/BrowsePanel.h
    widgets/FlowLayout.cpp
    widgets/FlowLayout.h
    widgets/ComparePanel.cpp
    widgets/ComparePanel.h
    widgets/ImageContextMenu.cpp
    widgets/ImageContextMenu.h
    widgets/ThumbnailWidget.cpp
    widgets/ThumbnailWidget.h
    widgets/ArrowOverlay.cpp
    widgets/ArrowOverlay.h
    widgets/ZoomableImageWidget.cpp
    widgets/ZoomableImageWidget.h
    utils/FileUtils.cpp
    utils/FileUtils.h
    utils/ImageUtils.cpp
    utils/ImageUtils.h
    services/SettingsManager.cpp
    services/SettingsManager.h
    services/ImageMarkManager.cpp
    services/ImageMarkManager.h
    services/CategoryExporter.cpp
    services/CategoryExporter.h
    services/ImageLoader.cpp
    services/ImageLoader.h
    services/ImageComparer.cpp
    services/ImageComparer.h
    styles/FluentStyle.cpp
    styles/FluentStyle.h
)

add_library(ImageCompareLib STATIC ${LIB_SOURCES})

target_include_directories(ImageCompareLib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(ImageCompareLib PUBLIC
    Qt6::Widgets
    Qt6::Gui
    Qt6::Concurrent
)

# ---- Executable target ----
qt_add_resources(APP_RESOURCES ${CMAKE_SOURCE_DIR}/resources/resources.qrc)
add_executable(ImageCompare ${SOURCES} ${APP_RESOURCES})

target_include_directories(ImageCompare PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(ImageCompare PRIVATE
    ImageCompareLib
)

# Use Windows subsystem (no console window) on Windows
set_target_properties(ImageCompare PROPERTIES
    WIN32_EXECUTABLE $<BOOL:$<PLATFORM_ID:Windows>>
)

# ---- Windows version-info + icon resource ----
# Without this, ImageCompare.exe has no CompanyName/ProductName/FileVersion,
# Explorer's "Properties → Details" tab is empty, the UAC prompt shows
# "Unknown publisher", and SmartScreen's heuristics flag the binary harder.
# Embedding VS_VERSION_INFO + an ICON resource is independent of code signing
# but is a prerequisite for not looking suspicious.
if(WIN32)
    set(APP_NAME        "ImageCompare")
    set(APP_COMPANY     "ImageCompare")
    set(APP_DESCRIPTION "Cross-platform image comparison tool")
    set(APP_COPYRIGHT   "Copyright (C) 2026 wuyuanqing")
    set(APP_VER_MAJOR   ${PROJECT_VERSION_MAJOR})
    set(APP_VER_MINOR   ${PROJECT_VERSION_MINOR})
    set(APP_VER_PATCH   ${PROJECT_VERSION_PATCH})
    set(APP_VER_FULL    "${PROJECT_VERSION}")
    # rc.exe is happy with forward slashes; CMake variables are already in that
    # form on every host, so no native-path conversion is needed.
    set(APP_ICON_PATH   "${CMAKE_SOURCE_DIR}/resources/icons/app.ico")

    configure_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/AppInfo.rc.in
        ${CMAKE_CURRENT_BINARY_DIR}/AppInfo.rc
        @ONLY
    )
    target_sources(ImageCompare PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/AppInfo.rc)
endif()
