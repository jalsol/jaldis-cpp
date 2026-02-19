cmake_minimum_required(VERSION 3.25)

project(jaldis LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(FetchContent)
FetchContent_Declare(
  Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG v3.5.2
)
FetchContent_MakeAvailable(Catch2)


add_executable(jaldis
  src/main.cpp
  src/server.cpp
  src/storage.cpp
  src/resp/parser.cpp
  src/resp/handler.cpp
)

add_executable(resp_tests
  src/resp/parser_tests.cpp
  src/resp/handler_tests.cpp
  src/resp/serializer_tests.cpp
  src/resp/parser.cpp
  src/resp/handler.cpp
)

add_executable(storage_tests
  src/storage_tests.cpp
  src/storage.cpp
)

add_executable(command_tests
  src/command_handler_tests.cpp
  src/storage.cpp
)

target_link_libraries(resp_tests PRIVATE Catch2::Catch2WithMain)
target_link_libraries(storage_tests PRIVATE Catch2::Catch2WithMain)
target_link_libraries(command_tests PRIVATE Catch2::Catch2WithMain)
