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

# the web player's optimisation (project setting build.opt; tools/build_site.sh passes it)
set(LOOP_OPT "-O3" CACHE STRING "optimisation for the web player")

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
  # Two players from one engine: `player` on one core (any browser), and
  # `player_mt` on many (a page the browser isolates, so its cores can share
  # memory: web/isolate.js). The page picks. Both use the browser's vector
  # maths (SIMD).
  # script errors use C++ exceptions (caught inside the VM)
  target_compile_options(loop3d PUBLIC --use-port=emdawnwebgpu -fexceptions -msimd128)
  add_library(loop3d_mt STATIC ${ENGINE_SOURCES})
  target_include_directories(loop3d_mt PUBLIC ${CMAKE_SOURCE_DIR})
  target_compile_options(loop3d_mt PRIVATE -Wall -Wextra -Werror)
  target_compile_options(loop3d_mt PUBLIC --use-port=emdawnwebgpu -fexceptions -msimd128 -pthread)
  set(PLAYER_LINK
    --use-port=emdawnwebgpu
    -fexceptions
    -sALLOW_MEMORY_GROWTH=1
    -sINITIAL_MEMORY=64MB
    -sSTACK_SIZE=2MB
    "-sEXPORTED_FUNCTIONS=_main,_malloc,_free,_loop_run_script,_loop_key,_loop_move,_loop_look,_loop_set_mode,_loop_set_quality,_loop_emit,_loop_stats,_loop_meter,_loop_capture,_loop_audio,_loop_choose,_loop_set_autoplay,_loop_perf_begin,_loop_perf_end,_loop_perf_part,_loop_perf_go,_loop_perf_graph,_loop_set_option,_loop_set_look,_loop_set_world,_loop_scene_json,_loop_log,_loop_set_setting,_loop_get_setting,_loop_settings_json,_loop_set_project_settings,_loop_edit_camera,_loop_pick,_loop_view,_loop_thing,_loop_thing_set,_loop_music_set,_loop_music_step,_loop_clips,_loop_clip_set,_loop_clip_moment,_loop_clip_curve"
    "-sEXPORTED_RUNTIME_METHODS=UTF8ToString,stringToUTF8,lengthBytesUTF8,HEAPU8,HEAPF32,FS"
    -sFORCE_FILESYSTEM=1
    ${LOOP_OPT})
  add_executable(player web/player.cpp)
  target_link_libraries(player PRIVATE loop3d)
  target_compile_options(player PRIVATE -Wall -Wextra -Werror)
  target_link_options(player PRIVATE ${PLAYER_LINK} -sENVIRONMENT=web)
  add_executable(player_mt web/player.cpp)
  target_link_libraries(player_mt PRIVATE loop3d_mt)
  target_compile_options(player_mt PRIVATE -Wall -Wextra -Werror)
  # the cores' workers start with the page (one fewer than the cores, at most
  # 7: engine/core/jobs.cpp asks for no more; and one more for the background
  # cook), each with room for a deep job
  target_link_options(player_mt PRIVATE ${PLAYER_LINK} -pthread -sENVIRONMENT=web,worker -sMAXIMUM_MEMORY=1GB
    "-sPTHREAD_POOL_SIZE=Math.max(1,Math.min(navigator.hardwareConcurrency||2,8)-1)+1" -sDEFAULT_PTHREAD_STACK_SIZE=1MB -Wno-pthreads-mem-growth)
endif()
