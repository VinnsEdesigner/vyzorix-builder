add_library(vyzadapter STATIC
  src/builtin.cpp
  src/golang.cpp
  src/rust.cpp
  src/cpp.cpp
  src/node_webroot.cpp
  src/node_server.cpp
  src/deno.cpp
  src/bun.cpp
  src/zig.cpp
  src/dotnet.cpp
  src/ruby.cpp
  src/php.cpp
  src/node_server.cpp
  src/deno.cpp
  src/bun.cpp
  src/zig.cpp
  src/dotnet.cpp
  src/ruby.cpp
  src/php.cpp
  src/deno.cpp
  src/bun.cpp
  src/zig.cpp
  src/dotnet.cpp
  src/ruby.cpp
  src/php.cpp
  src/bun.cpp
  src/zig.cpp
  src/dotnet.cpp
  src/ruby.cpp
  src/php.cpp
  src/python.cpp
  src/jvm.cpp
  src/containerfile.cpp
)

target_include_directories(vyzadapter PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(vyzadapter PUBLIC vyzplan)
