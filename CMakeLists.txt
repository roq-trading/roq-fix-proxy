cmake_minimum_required(VERSION 4.0)

# cmake

set(CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake ${CMAKE_MODULE_PATH})

# config

include(RoqConfig)

# version (using git tag)

include(GetGitRepoVersion)

message("Using GIT_REPO_VERSION=${GIT_REPO_VERSION}")

# project

project(roq-fix-proxy VERSION ${GIT_REPO_VERSION})

# language

enable_language(CXX)

# filesystem

include(GNUInstallDirs)

# dependencies

find_package(absl REQUIRED)
find_package(fmt REQUIRED)
find_package(magic_enum REQUIRED)
find_package(nameof REQUIRED)
find_package(roq-api REQUIRED)
find_package(roq-client REQUIRED)
find_package(roq-codec REQUIRED)
find_package(roq-fix REQUIRED)
find_package(roq-flags REQUIRED)
find_package(roq-io REQUIRED)
find_package(roq-logging REQUIRED)
find_package(roq-utils REQUIRED)
find_package(roq-web REQUIRED)
find_package(tomlplusplus REQUIRED)
find_package(unordered_dense REQUIRED)

if(CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME)
  include(CTest)
endif()

if(BUILD_TESTING)
  find_package(Catch2 REQUIRED)
endif()

option(BUILD_BENCHMARK "Enable benchmark" ON)

if(BUILD_BENCHMARK)
  find_package(benchmark REQUIRED)
endif()

# autogen

find_program(ROQ_AUTOGEN roq-autogen REQUIRED)

set(TEMPLATE_DIR ${CMAKE_SOURCE_DIR}/scripts/templates)

# clang-format

find_program(CLANG_FORMAT clang-format REQUIRED)

# include

include_directories(
  ${CMAKE_SOURCE_DIR}/src
  # >>> autogen >>>
  ${CMAKE_BINARY_DIR}/src)

# sub-projects

add_subdirectory(${CMAKE_SOURCE_DIR}/src/roq/fix_proxy)

if(BUILD_TESTING)
  add_subdirectory(${CMAKE_SOURCE_DIR}/test)
endif()

if(BUILD_BENCHMARK)
  add_subdirectory(${CMAKE_SOURCE_DIR}/benchmark)
endif()

# install

install(DIRECTORY ${CMAKE_SOURCE_DIR}/share/ DESTINATION ${CMAKE_INSTALL_DATADIR}/${PROJECT_NAME})

install(FILES ${CMAKE_SOURCE_DIR}/CHANGELOG.md DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/${PROJECT_NAME})

# docs

option(BUILD_DOCS "Enable documentation" OFF)

if(BUILD_DOCS)
  install(DIRECTORY ${CMAKE_SOURCE_DIR}/share/ DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/${PROJECT_NAME})
endif()
