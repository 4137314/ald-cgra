# Include after the component's flags and BUILD are defined. The caller sets
# BUILD_CONFIG_EXTRA for generated paths/optional dependencies. Compiler and
# flags are prerequisites, not an instruction to run a manual clean.
define BUILD_CONFIG_TEXT
$(CC)
$(AR)
PROFILE=$(PROFILE)
CPPFLAGS=$(CPPFLAGS)
CFLAGS=$(CFLAGS)
LDFLAGS=$(LDFLAGS)
LDLIBS=$(LDLIBS)
ARFLAGS=$(ARFLAGS)
$(BUILD_CONFIG_EXTRA)
endef
BUILD_CONFIG = $(BUILD)/.build-config.json
.PHONY: force-build-config
force-build-config:

$(BUILD_CONFIG): force-build-config ../scripts/build-config.py | $(BUILD)
	$(file >$@.input,$(BUILD_CONFIG_TEXT))
	@python3 ../scripts/build-config.py $@.input $@
