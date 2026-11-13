cmake_minimum_required(VERSION 3.16)
project(vyzorix-builder CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

add_compile_options(-Wall -Wextra -Wpedantic)

# The tree is layered: okfw <- domain <- modules/* <- cmd. Each subdirectory
# contributes a static library, and a module links only the modules below it, so the
# dependency direction is enforced by the link graph and not just by convention.
add_subdirectory(okfw)
add_subdirectory(domain)
add_subdirectory(modules)
add_subdirectory(cmd/vyzorix-builder)

enable_testing()
add_subdirectory(tests)
