cmake_minimum_required(VERSION 3.10)
project(RayTracer)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(include)

add_executable(raytracer 
    src/main.cpp 
    src/Exporter.cpp
    src/SequentialRenderer.cpp
    src/FinegrainedRenderer.cpp
    src/CoarseRenderer.cpp
    src/SMTRenderer.cpp
    src/CMPRenderer.cpp
)

# Flags de compilación
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O2 -pthread")

# Link pthread para soporte de threads
find_package(Threads REQUIRED)
target_link_libraries(raytracer Threads::Threads)
