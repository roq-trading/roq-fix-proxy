set(TARGET_NAME ${PROJECT_NAME}-auth)

set(SOURCES session.cpp)

add_library(${TARGET_NAME} OBJECT ${SOURCES})

add_dependencies(${TARGET_NAME} ${PROJECT_NAME}-flags-autogen-headers)

target_compile_definitions(
  ${TARGET_NAME} PRIVATE ROQ_PACKAGE_NAME="${TARGET_NAME}" ROQ_HOST="${ROQ_HOST}" ROQ_BUILD_VERSION="${GIT_REPO_VERSION}"
                         ROQ_GIT_DESCRIBE_HASH="${GIT_DESCRIBE_HASH}" ROQ_BUILD_NUMBER="${ROQ_BUILD_NUMBER}" ROQ_BUILD_TYPE="${ROQ_BUILD_TYPE}")

target_link_libraries(${TARGET_NAME} PRIVATE roq-api::roq-api fmt::fmt)
