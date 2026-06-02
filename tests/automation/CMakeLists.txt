get_filename_component(_TEST_NAME ${CMAKE_CURRENT_LIST_DIR} NAME)

add_executable(${_TEST_NAME}_tests
    automation_tests.cpp
    test_serializer.cpp
    test_locator.cpp
    test_dispatcher.cpp
    MockUiBackend.cpp
    ${CMAKE_SOURCE_DIR}/src/slic3r/GUI/Automation/WidgetSerializer.cpp
    ${CMAKE_SOURCE_DIR}/src/slic3r/GUI/Automation/Locator.cpp
    ${CMAKE_SOURCE_DIR}/src/slic3r/GUI/Automation/JsonRpcDispatcher.cpp
)
target_link_libraries(${_TEST_NAME}_tests test_common Catch2::Catch2WithMain nlohmann_json)
# The nlohmann_json INTERFACE target only exposes deps_src/nlohmann on the include
# path (so <json.hpp> resolves). The rest of the codebase reaches <nlohmann/json.hpp>
# via the deps_src root that admesh re-exports transitively; this pure unit-test
# target does not link admesh, so add the deps_src root explicitly to match.
target_include_directories(${_TEST_NAME}_tests PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/deps_src)
set_property(TARGET ${_TEST_NAME}_tests PROPERTY FOLDER "tests")
orcaslicer_copy_test_dlls()
catch_discover_tests(${_TEST_NAME}_tests)
