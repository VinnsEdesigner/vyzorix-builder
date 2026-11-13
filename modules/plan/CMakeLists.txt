add_library(vyzplan STATIC
  src/catalog.cpp
)

target_include_directories(vyzplan PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(vyzplan PUBLIC vyzdomain okfw)
