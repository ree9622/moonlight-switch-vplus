cmake_minimum_required(VERSION 3.16)
project(FoundationProtocolTests LANGUAGES CXX)

enable_testing()
find_package(EXPAT REQUIRED)
find_path(JANSSON_INCLUDE_DIR NAMES jansson.h REQUIRED)
find_library(JANSSON_LIBRARY NAMES jansson REQUIRED)

add_executable(foundation_protocol_test
    foundation_protocol_test.cpp
    ../app/src/libgamestream/foundation_protocol.cpp)
target_compile_features(foundation_protocol_test PRIVATE cxx_std_17)
target_include_directories(foundation_protocol_test PRIVATE
    ../app/src/libgamestream "${JANSSON_INCLUDE_DIR}")
target_link_libraries(foundation_protocol_test PRIVATE EXPAT::EXPAT "${JANSSON_LIBRARY}")
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(foundation_protocol_test PRIVATE -Wall -Wextra -Wpedantic)
endif()
add_test(NAME foundation_protocol COMMAND foundation_protocol_test)
