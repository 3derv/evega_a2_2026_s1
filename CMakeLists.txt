cmake_minimum_required(VERSION 3.10)
project(RayTracer)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(include)

add_executable(raytracer src/main.cpp)

# Opcional: flags de optimización
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O2")