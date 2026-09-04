cmake_minimum_required(VERSION 3.14)
project(light CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

option(LIGHT_ENABLE_ASAN "Build with AddressSanitizer" OFF)
option(LIGHT_ENABLE_UBSAN "Build with UndefinedBehaviorSanitizer" OFF)
option(LIGHT_BUILD_TESTS "Build tests" ON)

# do not add_subdirectory(third_party/containers): their CMakeLists fetches googletest from the network
add_library(ct INTERFACE)
target_include_directories(ct INTERFACE
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party/containers/include)

# consumers (cli, engine) use only: link Light + include <light/*.h>
add_library(Light STATIC
  libLight/src/light.cpp
  libLight/src/vm.cpp)
target_include_directories(Light
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/libLight/include
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/libLight/src)
target_link_libraries(Light PUBLIC ct)

if(MSVC)
  target_compile_options(Light PRIVATE /W4 /WX /GR-)
else()
  target_compile_options(Light PRIVATE
    -Wall -Wextra -Werror -pedantic
    -fno-exceptions -fno-rtti)
endif()

if(LIGHT_ENABLE_ASAN)
  add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address)
endif()
if(LIGHT_ENABLE_UBSAN)
  add_compile_options(-fsanitize=undefined)
  add_link_options(-fsanitize=undefined)
endif()

if(LIGHT_BUILD_TESTS)
  enable_testing()

  add_executable(test_foundation tests/foundation/test_foundation.cpp)
  target_link_libraries(test_foundation PRIVATE Light)
  add_test(NAME foundation COMMAND test_foundation)

  add_executable(test_bytecode tests/bytecode/test_encoding.cpp)
  target_link_libraries(test_bytecode PRIVATE Light)
  target_include_directories(test_bytecode PRIVATE libLight/src)
  add_test(NAME bytecode COMMAND test_bytecode)

  add_executable(test_error tests/error/test_error.cpp)
  target_link_libraries(test_error PRIVATE Light)
  add_test(NAME error COMMAND test_error)

  add_executable(test_value tests/value/test_value.cpp)
  target_link_libraries(test_value PRIVATE Light)
  target_include_directories(test_value PRIVATE libLight/src)
  add_test(NAME value COMMAND test_value)

  add_executable(test_lbc tests/lbc/test_lbc.cpp)
  target_link_libraries(test_lbc PRIVATE Light)
  target_include_directories(test_lbc PRIVATE libLight/src)
  add_test(NAME lbc COMMAND test_lbc)

  add_executable(test_limits tests/limits/test_limits.cpp)
  target_link_libraries(test_limits PRIVATE Light)
  add_test(NAME limits COMMAND test_limits)

  add_executable(test_chunk tests/chunk/test_chunk.cpp)
  target_link_libraries(test_chunk PRIVATE Light)
  target_include_directories(test_chunk PRIVATE libLight/src)
  add_test(NAME chunk COMMAND test_chunk)

  add_executable(test_vm tests/vm/test_vm.cpp)
  target_link_libraries(test_vm PRIVATE Light)
  target_include_directories(test_vm PRIVATE libLight/src)
  add_test(NAME vm COMMAND test_vm)

  add_executable(test_calls tests/vm/test_calls.cpp)
  target_link_libraries(test_calls PRIVATE Light)
  target_include_directories(test_calls PRIVATE libLight/src)
  add_test(NAME calls COMMAND test_calls)

  add_executable(test_natives tests/vm/test_natives.cpp)
  target_link_libraries(test_natives PRIVATE Light)
  target_include_directories(test_natives PRIVATE libLight/src)
  add_test(NAME natives COMMAND test_natives)

  add_executable(test_torture tests/vm/test_torture.cpp)
  target_link_libraries(test_torture PRIVATE Light)
  target_include_directories(test_torture PRIVATE libLight/src)
  add_test(NAME torture COMMAND test_torture)

  if(NOT WIN32)
    add_test(NAME check_includes
      COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/tools/check_includes.sh)
  endif()
endif()
