# The unit suite links every layer, so a test can reach any of them without the
# modules having to know about tests.
add_executable(vyzorix-builder-tests
  unit/test_main.cpp
  unit/sha256_test.cpp
  unit/naming_test.cpp
  unit/paths_test.cpp
  unit/target_test.cpp
  unit/recipe_test.cpp
  unit/adapter_test.cpp
  unit/package_test.cpp
  unit/store_test.cpp
  unit/toml_test.cpp
  unit/json_test.cpp
  unit/result_test.cpp
  unit/log_test.cpp
  unit/subprocess_test.cpp
  unit/context_scratch_test.cpp
  unit/image_pins_test.cpp
  unit/retry_test.cpp
  unit/ids_test.cpp
  unit/validate_test.cpp
)

target_include_directories(vyzorix-builder-tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(vyzorix-builder-tests PRIVATE
  vyzschema
  vyzadapter
  vyzcontainerbuild
  vyzpackage
  vyzpublish
  vyzplan
  vyzdomain
  okfw
)

add_test(NAME unit COMMAND vyzorix-builder-tests)

# The integration suite drives real Docker: one test per adapter's output
# contract, plus the engine's staging/pack/glibc behaviour. It is a separate
# executable and a separate ctest entry, so `ctest -R unit` stays Docker-free.
add_executable(vyzorix-builder-integration
  integration/test_main.cpp
  integration/engine_test.cpp
  integration/publish_test.cpp
)

target_include_directories(vyzorix-builder-integration PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/integration
)

target_link_libraries(vyzorix-builder-integration PRIVATE
  vyzschema
  vyzadapter
  vyzcontainerbuild
  vyzpackage
  vyzpublish
  vyzplan
  vyzdomain
  okfw
)

add_test(NAME integration COMMAND vyzorix-builder-integration)
