cmake_minimum_required(VERSION 3.22)
project(rendering_to_perception_demos LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(DEMO_ENABLE_ML "Build AutoForge centroid and YOLO adapters" OFF)
option(DEMO_BUILD_TESTS "Build stream behavior tests" ON)

find_package(OpenCV 4.10 REQUIRED COMPONENTS core imgproc imgcodecs videoio)
find_package(pyramidal_klt CONFIG REQUIRED)
find_package(spectra_rt CONFIG REQUIRED)
find_package(glfw3 3.3 REQUIRED)
find_package(OpenGL REQUIRED)

add_library(demo_core STATIC src/demo_core.cpp)
target_include_directories(demo_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(demo_core PUBLIC pyramidal_klt::pyramidal_klt_pipeline
                                       ${OpenCV_LIBS} glfw OpenGL::GL)
target_compile_options(demo_core PRIVATE -Wall -Wextra -Wpedantic)

add_executable(render_stream_demo src/render_stream_demo.cpp)
target_link_libraries(render_stream_demo PRIVATE demo_core spectra_rt::spectra_rt)
# Match Spectra-RT's native-tuned Eigen allocation ABI without -march=native here.
target_compile_definitions(render_stream_demo PRIVATE EIGEN_MALLOC_ALREADY_ALIGNED=0)
target_compile_options(render_stream_demo PRIVATE -Wall -Wextra -Wpedantic)

add_executable(camera_stream_demo src/camera_stream_demo.cpp)
target_link_libraries(camera_stream_demo PRIVATE demo_core)
target_compile_options(camera_stream_demo PRIVATE -Wall -Wextra -Wpedantic)

if(DEMO_ENABLE_ML)
    find_package(autoforge_deploy CONFIG REQUIRED)
    target_sources(demo_core PRIVATE src/model_adapter.cpp)
    target_link_libraries(demo_core PUBLIC autoforge_deploy::autoforge_deploy)
    target_compile_definitions(demo_core PUBLIC DEMO_ENABLE_ML=1)
endif()

if(DEMO_BUILD_TESTS)
    enable_testing()
    find_package(Python3 3.12 REQUIRED COMPONENTS Interpreter)
    add_test(NAME camera_stream_contract
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/stream_contract.py
                     $<TARGET_FILE:camera_stream_demo>)
endif()
