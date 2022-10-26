cmake_minimum_required(VERSION 3.0)
set (TARGET_NAME client)
project (${TARGET_NAME})
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Werror=return-type -Wall")
file(GLOB
  SOURCES
  ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/*.h)

add_executable(${TARGET_NAME} ${SOURCES})

target_link_libraries(${TARGET_NAME} ncurses pthread)
