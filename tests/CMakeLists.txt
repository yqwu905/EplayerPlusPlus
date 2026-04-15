# ---- Helper function to add a unit test ----
function(add_unit_test TEST_NAME TEST_SOURCE)
    add_executable(${TEST_NAME} ${TEST_SOURCE})
    target_link_libraries(${TEST_NAME} PRIVATE
        ImageCompareLib
        Qt6::Test
    )
    add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
endfunction()

# ---- Unit tests ----
add_unit_test(tst_FileUtils tst_FileUtils.cpp)
add_unit_test(tst_ImageUtils tst_ImageUtils.cpp)
add_unit_test(tst_SettingsManager tst_SettingsManager.cpp)
add_unit_test(tst_ImageComparer tst_ImageComparer.cpp)
add_unit_test(tst_FolderModel tst_FolderModel.cpp)
add_unit_test(tst_CompareSession tst_CompareSession.cpp)
add_unit_test(tst_ImageListModel tst_ImageListModel.cpp)
