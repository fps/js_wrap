cmake_minimum_required (VERSION 2.6)
set(CMAKE_BUILD_TYPE Release)
project (js_wrap)

set(PROJECT_VERSION "0.1")
set(ARCHIVE_NAME ${CMAKE_PROJECT_NAME}-${PROJECT_VERSION})
add_custom_target(dist
    COMMAND git archive --prefix=${ARCHIVE_NAME}/ HEAD
        | bzip2 > ${CMAKE_BINARY_DIR}/${ARCHIVE_NAME}.tar.bz2
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

find_package(PkgConfig)

find_package(Boost COMPONENTS program_options)

add_executable(js_wrap js_wrap.cc)

pkg_check_modules(JS_WRAP jack)
target_link_libraries(js_wrap jack ${Boost_PROGRAM_OPTIONS_LIBRARY})
include_directories(${JASS_INCLUDE_DIRS})

install(TARGETS js_wrap RUNTIME DESTINATION bin)

