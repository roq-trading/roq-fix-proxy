set(TARGET_NAME ${PROJECT_NAME}-client)

set(SOURCES listener.cpp manager.cpp session.cpp)

add_library(${TARGET_NAME} OBJECT ${SOURCES})

add_dependencies(${TARGET_NAME} ${PROJECT_NAME}-flags-autogen-headers)

target_link_libraries(${TARGET_NAME} PRIVATE roq-api::roq-api unordered_dense::unordered_dense fmt::fmt)
