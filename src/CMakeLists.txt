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
    widgets/ComparePanel.cpp
    widgets/ComparePanel.h
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
    widgets/ComparePanel.cpp
    widgets/ComparePanel.h
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
