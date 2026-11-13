add_library(vyzpublish STATIC
  src/verify.cpp
  src/ghcr.cpp
  src/objectstore.cpp
  src/local.cpp
  src/store_factory.cpp
  src/composite.cpp
)

target_include_directories(vyzpublish PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(vyzpublish PUBLIC okfw vyzdomain)
