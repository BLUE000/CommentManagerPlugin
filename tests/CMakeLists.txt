include(FetchContent)
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

find_package(Qt6 COMPONENTS Core Widgets Sql Charts Test REQUIRED)

# テスト実行バイナリの定義
add_executable(CommentManagerPluginTest
    test_comment_manager.cpp
    ../src/database_manager.cpp
    ../src/config_manager.cpp
    ../src/comment_manager_widget.cpp
    ../src/comment_manager_plugin.cpp
)

target_include_directories(CommentManagerPluginTest PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../src"
    "${CMAKE_CURRENT_SOURCE_DIR}/../tests/TwitchChannnelManagementTool/src"
)

target_link_libraries(CommentManagerPluginTest PRIVATE
    Qt6::Core
    Qt6::Widgets
    Qt6::Sql
    Qt6::Charts
    Qt6::Test
    gtest
)
if (WIN32)
    target_link_libraries(CommentManagerPluginTest PRIVATE Crypt32)
endif()

add_test(NAME CommentManagerPluginTest COMMAND CommentManagerPluginTest)
