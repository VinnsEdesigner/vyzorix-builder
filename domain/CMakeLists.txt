add_library(vyzdomain STATIC
  src/target.cpp
  src/naming.cpp
  src/paths.cpp
)

target_include_directories(vyzdomain PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(vyzdomain PUBLIC okfw)
