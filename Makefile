# Build and verify the tree.
#
#   make          configure and build (Release)
#   make test     build and run the unit suite
#   make lint     clang-tidy and cppcheck
#   make check    everything: configure, build, test, lint (scripts/check.sh)
#   make clean
#
# scripts/check.sh is the single source of truth for "is the tree clean"; these
# targets are thin wrappers over it and cmake.

BUILD_DIR ?= build
CMAKE_FLAGS ?= -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

CPPCHECK_INCLUDES = -I okfw/include -I domain/include \
  -I modules/plan/include -I modules/adapter/include -I modules/containerbuild/include \
  -I modules/package/include -I modules/publish/include -I modules/schema/include

SOURCES = $(shell find okfw domain modules cmd -name '*.cpp')

.PHONY: all configure test lint check clean

all: configure
	cmake --build $(BUILD_DIR) --parallel

configure:
	cmake -S . -B $(BUILD_DIR) $(CMAKE_FLAGS)

test: all
	ctest --test-dir $(BUILD_DIR) --output-on-failure

lint: configure
	clang-tidy -p $(BUILD_DIR) --quiet $(SOURCES)
	cppcheck $(CPPCHECK_INCLUDES) okfw domain modules cmd

check:
	./scripts/check.sh

clean:
	rm -rf $(BUILD_DIR)
