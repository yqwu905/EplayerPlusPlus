# ---- Helper function to add a unit test ----
function(add_unit_test TEST_NAME TEST_SOURCE)
    add_executable(${TEST_NAME} ${TEST_SOURCE})
    target_link_libraries(${TEST_NAME} PRIVATE
        ImageCompareLib
        Qt6::Test
    )
    add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
    set_tests_properties(${TEST_NAME} PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    )
endfunction()

# ---- Unit tests ----
add_unit_test(tst_FileUtils tst_FileUtils.cpp)
add_unit_test(tst_ImageUtils tst_ImageUtils.cpp)
add_unit_test(tst_SettingsManager tst_SettingsManager.cpp)
add_unit_test(tst_ImageComparer tst_ImageComparer.cpp)
add_unit_test(tst_FolderModel tst_FolderModel.cpp)
add_unit_test(tst_CompareSession tst_CompareSession.cpp)
add_unit_test(tst_ImageListModel tst_ImageListModel.cpp)
add_unit_test(tst_ZoomableImageWidget tst_ZoomableImageWidget.cpp)
add_unit_test(tst_BrowsePanel tst_BrowsePanel.cpp)
add_unit_test(tst_ImageLoader tst_ImageLoader.cpp)
add_unit_test(tst_FolderPanel tst_FolderPanel.cpp)

# ---- Benchmark (not registered with CTest, run manually) ----
add_executable(bench_ThumbnailPerf bench_ThumbnailPerf.cpp)
target_link_libraries(bench_ThumbnailPerf PRIVATE
    ImageCompareLib
    Qt6::Test
)
