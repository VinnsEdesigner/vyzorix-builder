add_library(vyzcontainerbuild STATIC
  src/container_builder.cpp
  src/image_pins.cpp
)

target_include_directories(vyzcontainerbuild PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(vyzcontainerbuild PUBLIC vyzplan vyzpackage okfw)
