cmake_minimum_required(VERSION 3.18)
project(instance_segmentation_inference LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(OpenCV REQUIRED COMPONENTS core imgproc imgcodecs)

add_executable(inference inference.cc)

target_include_directories(inference PRIVATE ${OpenCV_INCLUDE_DIRS})
target_link_libraries(inference PRIVATE
    qbruntime
    sp500
    opencv_core
    opencv_imgproc
    opencv_imgcodecs
)
