# Each layer is its own library. plan depends on domain+okfw; adapter depends on
# plan; build depends on plan+okfw; package depends on okfw; publish depends on
# okfw; schema depends on domain+okfw. Nothing depends on a layer above it.
add_subdirectory(plan)
add_subdirectory(adapter)
add_subdirectory(containerbuild)
add_subdirectory(package)
add_subdirectory(publish)
add_subdirectory(schema)
