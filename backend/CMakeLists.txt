find_package(Threads REQUIRED)

# vendor 单头库统一 include 路径（httplib / nlohmann-json / doctest）
add_library(thirdparty INTERFACE)
target_include_directories(thirdparty INTERFACE ${CMAKE_SOURCE_DIR}/third_party)

# merging_core：领域模型与匹配引擎（P1 起逐步填充）
file(GLOB_RECURSE CORE_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)
list(FILTER CORE_SOURCES EXCLUDE REGEX "main\\.cpp$")
add_library(merging_core STATIC ${CORE_SOURCES})
target_link_libraries(merging_core PUBLIC thirdparty Threads::Threads)

# merging_server：HTTP 服务入口
add_executable(merging_server src/main.cpp)
target_link_libraries(merging_server PRIVATE merging_core)

enable_testing()
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    add_subdirectory(tests)
endif()
