cmake_minimum_required(VERSION 3.20)
project(CommentManagerPlugin VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# Qt6 パッケージの検索
find_package(Qt6 COMPONENTS Core Widgets Sql Charts Network REQUIRED)

# ホストアプリのsharedヘッダーをインクルードパスに含める
set(HOST_SHARED_DIR "${CMAKE_CURRENT_SOURCE_DIR}/tests/TwitchChannnelManagementTool/src")

# ソース定義
set(PLUGIN_SOURCES
    src/database_manager.cpp
    src/config_manager.cpp
    src/comment_manager_widget.cpp
    src/comment_manager_plugin.cpp
)

set(PLUGIN_HEADERS
    src/database_manager.h
    src/config_manager.h
    src/comment_manager_widget.h
    src/comment_manager_plugin.h
)

# プラグイン共有ライブラリ(DLL)の生成
add_library(CommentManagerPlugin SHARED
    ${PLUGIN_SOURCES}
    ${PLUGIN_HEADERS}
    resources.qrc
)

set_target_properties(CommentManagerPlugin PROPERTIES
    PREFIX "lib"
    OUTPUT_NAME "CommentManagerPlugin"
)

target_compile_definitions(CommentManagerPlugin PRIVATE QT_DEPRECATED_WARNINGS)

target_include_directories(CommentManagerPlugin PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${HOST_SHARED_DIR}"
)

target_link_libraries(CommentManagerPlugin PRIVATE
    Qt6::Core
    Qt6::Widgets
    Qt6::Sql
    Qt6::Charts
    Qt6::Network
)
if (WIN32)
    target_link_libraries(CommentManagerPlugin PRIVATE Crypt32)
endif()

# 単体テスト用オプション
option(BUILD_TESTING "Build the test suite" OFF)
if (BUILD_TESTING)
    enable_testing()
    add_subdirectory(tests)
endif()
