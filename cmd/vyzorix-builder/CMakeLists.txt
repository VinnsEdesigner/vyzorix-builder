# The composition root: builds the binary and links every layer by name. This is
# the only target that names concrete implementations.
add_executable(vyzorix-builder
  main.cpp
  cli.cpp
)

target_link_libraries(vyzorix-builder PRIVATE
  vyzschema
  vyzadapter
  vyzcontainerbuild
  vyzpackage
  vyzpublish
  vyzplan
  vyzdomain
  okfw
)

install(TARGETS vyzorix-builder RUNTIME DESTINATION bin)
