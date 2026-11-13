add_library(vyzschema STATIC
  src/recipe.cpp
  src/validate.cpp
)

target_include_directories(vyzschema PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(vyzschema PUBLIC vyzdomain okfw)
