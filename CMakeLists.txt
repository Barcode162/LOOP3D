cmake_minimum_required(VERSION 3.20)
project(Loop3D LANGUAGES CXX)

# Loop3D: the engine (engine/), its language (Looped Script, engine/script),
# the `loop` command (tools/) and the tests (tests/).
#   Native:  cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j
#   Web:     emcmake cmake -B build-web && cmake --build build-web -j
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

file(GLOB_RECURSE ENGINE_SOURCES CONFIGURE_DEPENDS engine/*.cpp)
# The GPU renderer and the web runtime need Emscripten's WebGPU.
list(FILTER ENGINE_SOURCES EXCLUDE REGEX "/engine/(gpu|web)/")

if(EMSCRIPTEN)
  file(GLOB_RECURSE GPU_SOURCES CONFIGURE_DEPENDS engine/gpu/*.cpp engine/web/*.cpp)
  list(APPEND ENGINE_SOURCES ${GPU_SOURCES})
endif()

add_library(loop3d STATIC ${ENGINE_SOURCES})
target_include_directories(loop3d PUBLIC ${CMAKE_SOURCE_DIR})
target_compile_options(loop3d PRIVATE -Wall -Wextra -Werror)
if(NOT EMSCRIPTEN)
  find_package(Threads REQUIRED)
  target_link_libraries(loop3d PUBLIC Threads::Threads)
  if(CMAKE_BUILD_TYPE STREQUAL "Release")
    target_compile_options(loop3d PRIVATE -O3 -march=native)
  endif()

  add_executable(loop tools/loop.cpp)
  target_link_libraries(loop PRIVATE loop3d)
  target_compile_options(loop PRIVATE -Wall -Wextra -Werror)

  file(GLOB TEST_SOURCES CONFIGURE_DEPENDS tests/*.cpp)
  add_executable(loop_tests ${TEST_SOURCES})
  target_link_libraries(loop_tests PRIVATE loop3d)
  target_compile_options(loop_tests PRIVATE -Wall -Wextra -Werror)
  enable_testing()
  add_test(NAME loop_tests COMMAND loop_tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
else()
  # script errors use C++ exceptions (caught inside the VM)
  target_compile_options(loop3d PUBLIC --use-port=emdawnwebgpu -fexceptions)
  add_executable(player web/player.cpp)
  target_link_libraries(player PRIVATE loop3d)
  target_compile_options(player PRIVATE -Wall -Wextra -Werror)
  target_link_options(player PRIVATE
    --use-port=emdawnwebgpu
    -fexceptions
    -sALLOW_MEMORY_GROWTH=1
    -sINITIAL_MEMORY=64MB
    -sSTACK_SIZE=2MB
    -sENVIRONMENT=web
    "-sEXPORTED_FUNCTIONS=_main,_malloc,_free,_loop_run_script,_loop_key,_loop_move,_loop_look,_loop_set_mode,_loop_set_quality,_loop_emit,_loop_stats,_loop_capture,_loop_audio,_loop_choose,_loop_set_autoplay,_loop_perf_begin,_loop_perf_end,_loop_perf_graph,_loop_set_option,_loop_set_look,_loop_set_world,_loop_scene_json,_loop_log"
    "-sEXPORTED_RUNTIME_METHODS=UTF8ToString,stringToUTF8,lengthBytesUTF8,HEAPU8,HEAPF32"
    -O3)
endif()
