CMAKE ?= cmake
CTEST ?= ctest
JOBS ?=

DEBUG_BUILD_DIR ?= .build/debug
RELEASE_BUILD_DIR ?= .build/release
CONFIG_HOME := $(if $(XDG_CONFIG_HOME),$(XDG_CONFIG_HOME),$(HOME)/.config)
PLUGIN_DIR ?= $(CONFIG_HOME)/hypr/plugins
PLUGIN_PATH := $(PLUGIN_DIR)/hyprlane.so
RUNTIME_DIR := $(if $(XDG_RUNTIME_DIR),$(XDG_RUNTIME_DIR),/run/user/$(shell id -u))
HYPRLAND_INSTANCE_SIGNATURE ?= $(shell ls -1t "$(RUNTIME_DIR)/hypr" 2>/dev/null | head -n 1)
export HYPRLAND_INSTANCE_SIGNATURE

.PHONY: all debug release clean install dev test check reload

define configure_build
	@if [ -f "$(1)/CMakeCache.txt" ] && ! grep -Fqx "CMAKE_HOME_DIRECTORY:INTERNAL=$(CURDIR)" "$(1)/CMakeCache.txt"; then \
		$(CMAKE) -E remove_directory "$(1)"; \
	fi
	$(CMAKE) -S . -B "$(1)" -DCMAKE_BUILD_TYPE=$(2) -DBUILD_TESTING=$(3) -DHYPRLANE_WARNINGS_AS_ERRORS=$(4) -DCMAKE_PREFIX_PATH="$(PREFIX)"
endef

all: release

debug:
	$(call configure_build,$(DEBUG_BUILD_DIR),Debug,ON,OFF)
	$(CMAKE) --build "$(DEBUG_BUILD_DIR)" --parallel $(JOBS)
	ln -sfn "$(DEBUG_BUILD_DIR)/compile_commands.json" compile_commands.json
	ln -sfn "$(DEBUG_BUILD_DIR)/hyprlane.so" hyprlane.so

release:
	$(call configure_build,$(RELEASE_BUILD_DIR),Release,OFF,OFF)
	$(CMAKE) --build "$(RELEASE_BUILD_DIR)" --parallel $(JOBS)
	ln -sfn "$(RELEASE_BUILD_DIR)/compile_commands.json" compile_commands.json
	ln -sfn "$(RELEASE_BUILD_DIR)/hyprlane.so" hyprlane.so

test:
	$(call configure_build,$(DEBUG_BUILD_DIR),Debug,ON,ON)
	$(CMAKE) --build "$(DEBUG_BUILD_DIR)" --parallel $(JOBS)
	$(CTEST) --test-dir "$(DEBUG_BUILD_DIR)" --output-on-failure

check: test release

clean:
	$(CMAKE) -E remove_directory .build
	rm -f hyprlane.so compile_commands.json

install: release
	install -d "$(PLUGIN_DIR)"
	install -m 0755 "$(RELEASE_BUILD_DIR)/hyprlane.so" "$(PLUGIN_PATH)"

dev: debug

reload:
	@test -n "$(HYPRLAND_INSTANCE_SIGNATURE)" || { echo "No running Hyprland instance found" >&2; exit 1; }
	@echo "Using Hyprland instance: $(HYPRLAND_INSTANCE_SIGNATURE)"
	@echo "Switching to master layout..."
	hyprctl eval 'hl.config({ general = { layout = "master" } })' || { echo "Failed to switch to master layout" >&2; exit 1; }; \
	layout="$$(hyprctl getoption general:layout)" || { echo "Failed to verify master layout" >&2; exit 1; }; \
	echo "$$layout" | grep -Eq '^[[:space:]]*str:[[:space:]]*master([[:space:]]|$$)' || { echo "Master layout verification failed: $$layout" >&2; exit 1; }
	@echo "Unloading hyprlane..."
	-hyprctl plugin unload "$(PLUGIN_PATH)"
	@echo "Building and installing..."
	$(MAKE) install
	@echo "Loading new hyprlane..."
	hyprctl plugin load "$(PLUGIN_PATH)"
	@echo "Switching back to scroller layout..."
	hyprctl eval 'hl.config({ general = { layout = "scroller" } })' || { echo "Failed to switch to scroller layout" >&2; exit 1; }; \
	layout="$$(hyprctl getoption general:layout)" || { echo "Failed to verify scroller layout" >&2; exit 1; }; \
	echo "$$layout" | grep -Eq '^[[:space:]]*str:[[:space:]]*scroller([[:space:]]|$$)' || { echo "Scroller layout verification failed: $$layout" >&2; exit 1; }
	@echo "Done!"
