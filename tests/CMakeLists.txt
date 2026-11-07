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

add_executable(input_session_test input_session_test.cpp)
target_compile_features(input_session_test PRIVATE cxx_std_17)
target_include_directories(input_session_test PRIVATE ../app/src/streaming)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(input_session_test PRIVATE -Wall -Wextra -Wpedantic)
endif()
add_test(NAME input_session COMMAND input_session_test)

add_executable(motion_report_test motion_report_test.cpp)
target_compile_features(motion_report_test PRIVATE cxx_std_17)
target_include_directories(motion_report_test PRIVATE ../app/src/streaming)
add_test(NAME motion_report COMMAND motion_report_test)

add_executable(video_stats_window_test video_stats_window_test.cpp)
target_compile_features(video_stats_window_test PRIVATE cxx_std_17)
target_include_directories(video_stats_window_test PRIVATE ../app/src/streaming/ffmpeg)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(video_stats_window_test PRIVATE -Wall -Wextra -Wpedantic)
endif()
add_test(NAME video_stats_window COMMAND video_stats_window_test)

add_executable(host_cadence_test host_cadence_test.cpp)
target_compile_features(host_cadence_test PRIVATE cxx_std_17)
target_include_directories(host_cadence_test PRIVATE ../app/src/streaming)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(host_cadence_test PRIVATE -Wall -Wextra -Wpedantic)
endif()
add_test(NAME host_cadence COMMAND host_cadence_test)

find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG_QUEUE REQUIRED IMPORTED_TARGET libavcodec libavutil)
find_package(Threads REQUIRED)
add_executable(precise_queue_test
    precise_queue_test.cpp
    ../app/src/streaming/AVFrameHolder.cpp
    ../app/src/streaming/DisplayVsyncClock.cpp)
target_compile_features(precise_queue_test PRIVATE cxx_std_17)
target_include_directories(precise_queue_test PRIVATE ../app/src/streaming)
target_link_libraries(precise_queue_test PRIVATE PkgConfig::FFMPEG_QUEUE Threads::Threads)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(precise_queue_test PRIVATE -Wall -Wextra -Wpedantic)
endif()
add_test(NAME precise_queue COMMAND precise_queue_test)
