add_library(vyzpackage STATIC
  src/package.cpp
)

target_include_directories(vyzpackage PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(vyzpackage PUBLIC okfw)
