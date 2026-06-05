BUILD_DIR ?= build
CMAKE ?= cmake
CTEST ?= ctest
CONFIG ?= Debug

.PHONY: all configure build test run clean distclean rebuild

all: build

configure:
	$(CMAKE) -S . -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=$(CONFIG)

build: configure
	$(CMAKE) --build $(BUILD_DIR) --config $(CONFIG)

test: build
	$(CTEST) --test-dir $(BUILD_DIR) --output-on-failure

run: build
	$(BUILD_DIR)/i486_tests

clean:
	$(CMAKE) --build $(BUILD_DIR) --target clean --config $(CONFIG) || true

distclean:
	rm -rf $(BUILD_DIR)

rebuild: distclean all
