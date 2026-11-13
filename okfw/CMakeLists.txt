add_library(okfw STATIC
  src/log.cpp
  src/subprocess.cpp
  src/sha256.cpp
  src/json.cpp
  src/toml.cpp
  src/ids.cpp
  src/interrupt.cpp
)

target_include_directories(okfw PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
