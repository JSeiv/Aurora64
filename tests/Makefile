CC ?= cc
STRICT_CFLAGS := -std=c11 -Wall -Wextra -Werror -pedantic
override CFLAGS := $(filter-out $(STRICT_CFLAGS),$(CFLAGS)) $(STRICT_CFLAGS)
CPPFLAGS ?=
override CPPFLAGS += -DLIBRARY_FS_HOST_TEST -DPATH_HOST_TEST -DLIBRARY_VIEW_HOST_TEST -DERROR_CONTEXT_HOST_TEST
LDFLAGS ?=
SANITIZE_FLAGS := -fsanitize=address,undefined -fno-omit-frame-pointer

ifeq ($(shell uname -s),Darwin)
HOST_SECTION_GC := -Wl,-dead_strip
else
HOST_SECTION_GC := -Wl,--gc-sections
endif

BUILD_DIR := build
TEST_BINARY ?= $(BUILD_DIR)/tests
SANITIZE_BUILD_DIR := $(BUILD_DIR)/sanitize
SANITIZE_TEST_BINARY := $(SANITIZE_BUILD_DIR)/tests
SOURCES := test_main.c test_rom_header.c test_sha256.c test_rom_identity.c test_library_view_model.c \
	test_library_roots.c test_library_fs_libdragon.c test_library_scanner.c test_library_snapshot.c test_library_service.c support/fake_library_fs.c \
	support/test_alloc.c support/rom_fixture_builder.c \
	../src/menu/library/rom_header.c ../src/menu/library/sha256.c \
	../src/menu/library/rom_identity.c ../src/menu/library/library_roots.c \
	../src/menu/library/library_scanner.c ../src/menu/library/library_snapshot.c \
	../src/menu/library/library_service.c ../src/menu/path.c ../src/menu/views/all_games.c \
	../src/menu/library/library_fs_libdragon.c
LOAD_ROM_OBJECT = $(dir $(TASK10_TEST_BINARY))load_rom_host.o
TASK10_TEST_BINARY ?= $(BUILD_DIR)/task10_tests
SANITIZE_TASK10_TEST_BINARY := $(SANITIZE_BUILD_DIR)/task10_tests
TASK10_SOURCES := test_error_context.c ../src/menu/path.c ../src/menu/views/error.c \
	../src/menu/library/rom_header.c
TASK11_TEST_BINARY ?= $(BUILD_DIR)/task11_tests
SANITIZE_TASK11_TEST_BINARY := $(SANITIZE_BUILD_DIR)/task11_tests
TASK11_MENU_OBJECT = $(dir $(TASK11_TEST_BINARY))menu_transition_host.o
TASK11_DISK_OBJECT = $(dir $(TASK11_TEST_BINARY))load_disk_host.o
TASK11_EMULATOR_OBJECT = $(dir $(TASK11_TEST_BINARY))load_emulator_host.o
TASK11_USB_OBJECT = $(dir $(TASK11_TEST_BINARY))usb_comm_host.o
TASK10_STUB_OBJECT = $(dir $(TASK10_TEST_BINARY))task11_task10_stubs.o

.PHONY: test test-sanitize test-task10 test-task11 test-compat-flags clean force-test-build

define assert-library-config
	@actual="$$($(MAKE) -s --no-print-directory -f ../Makefile SKIP_N64_MK=1 $(1) print-library-config)"; \
	expected='$(2)'; \
	if test "$$actual" != "$$expected"; then \
		printf 'compatibility case %s mismatch\nexpected: %s\nactual:   %s\n' \
			'$(3)' "$$expected" "$$actual" >&2; \
		exit 1; \
	fi
endef

test: test-compat-flags $(TEST_BINARY) $(TASK10_TEST_BINARY) $(TASK11_TEST_BINARY)
	$(TEST_BINARY)
	$(TASK10_TEST_BINARY)
	$(TASK11_TEST_BINARY)

test-task10: $(TASK10_TEST_BINARY)
	$(TASK10_TEST_BINARY)

test-task11: $(TASK11_TEST_BINARY)
	$(TASK11_TEST_BINARY)

test-compat-flags:
	$(call assert-library-config,,home=0 library=0 launch_proof=0 library_origin=undefined,default)
	$(call assert-library-config,AURORA64_LAUNCH_PROOF=1,home=0 library=1 launch_proof=1 library_origin=undefined,compat)
	$(call assert-library-config,AURORA64_LIBRARY=0 AURORA64_LAUNCH_PROOF=1,home=0 library=0 launch_proof=1 library_origin=command line,explicit0)
	$(call assert-library-config,AURORA64_LIBRARY=1 AURORA64_LAUNCH_PROOF=0,home=0 library=1 launch_proof=0 library_origin=command line,explicit1)

$(TEST_BINARY): force-test-build $(SOURCES) support/test_alloc.h ../src/libs/acutest/acutest.h
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) -ffunction-sections -I../src -I../src/libs -I../src/libs/acutest -I../src/libs/miniz -I../libdragon/include -I. $(SOURCES) $(LDFLAGS) $(HOST_SECTION_GC) -o $@

$(LOAD_ROM_OBJECT): ../src/menu/views/load_rom.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) -ffunction-sections -DLOAD_ROM_HOST_TEST \
		-DFEATURE_AURORA_HOME_ENABLED=1 -DFEATURE_AURORA_LIBRARY_ENABLED=1 \
		-I../src -I../src/libs -I../src/libs/acutest -isystem ../src/libs/miniz -isystem ../libdragon/include -I. -c $< -o $@

$(TASK10_STUB_OBJECT): test_library_transition_guard.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) -ffunction-sections -DTASK11_STUBS_ONLY -DTASK10_LINK \
		-I../src -I../src/libs -isystem ../src/libs/miniz -isystem ../libdragon/include -I. -c $< -o $@

$(TASK10_TEST_BINARY): force-test-build $(TASK10_SOURCES) $(LOAD_ROM_OBJECT) $(TASK11_MENU_OBJECT) $(TASK10_STUB_OBJECT) ../src/libs/acutest/acutest.h
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) -ffunction-sections -I../src -I../src/libs \
		-I../src/libs/acutest -isystem ../src/libs/miniz -isystem ../libdragon/include -I. \
		$(TASK10_SOURCES) $(LOAD_ROM_OBJECT) $(TASK11_MENU_OBJECT) $(TASK10_STUB_OBJECT) $(LDFLAGS) $(HOST_SECTION_GC) -o $@

$(TASK11_MENU_OBJECT): ../src/menu/menu.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) -ffunction-sections -DMENU_TRANSITION_HOST_TEST \
		-I../src -I../src/libs -isystem ../src/libs/miniz -isystem ../libdragon/include -I. -c $< -o $@

$(TASK11_DISK_OBJECT): ../src/menu/views/load_disk.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) -ffunction-sections -DLOAD_DISK_HOST_TEST \
		-I../src -I../src/libs -isystem ../src/libs/miniz -isystem ../libdragon/include -I. -c $< -o $@

$(TASK11_EMULATOR_OBJECT): ../src/menu/views/load_emulator.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) -ffunction-sections -DLOAD_EMULATOR_HOST_TEST \
		-I../src -I../src/libs -isystem ../src/libs/miniz -isystem ../libdragon/include -I. -c $< -o $@

$(TASK11_USB_OBJECT): ../src/menu/usb_comm.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) -ffunction-sections -DUSB_COMM_HOST_TEST \
		-I../src -I../src/libs -isystem ../src/libs/miniz -isystem ../libdragon/include -I. -c $< -o $@

$(TASK11_TEST_BINARY): force-test-build test_library_transition_guard.c $(TASK11_MENU_OBJECT) \
		$(LOAD_ROM_OBJECT) $(TASK11_DISK_OBJECT) $(TASK11_EMULATOR_OBJECT) \
		$(TASK11_USB_OBJECT) ../src/menu/path.c ../src/menu/library/rom_header.c \
		../src/libs/acutest/acutest.h
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) -ffunction-sections -I../src -I../src/libs \
		-I../src/libs/acutest -isystem ../src/libs/miniz -isystem ../libdragon/include -I. \
		test_library_transition_guard.c $(TASK11_MENU_OBJECT) $(LOAD_ROM_OBJECT) \
		$(TASK11_DISK_OBJECT) $(TASK11_EMULATOR_OBJECT) $(TASK11_USB_OBJECT) \
		../src/menu/path.c ../src/menu/library/rom_header.c \
		$(LDFLAGS) $(HOST_SECTION_GC) -o $@

test-sanitize:
	@mkdir -p $(SANITIZE_BUILD_DIR)
	$(MAKE) test TEST_BINARY="$(SANITIZE_TEST_BINARY)" \
		TASK10_TEST_BINARY="$(SANITIZE_TASK10_TEST_BINARY)" \
		TASK11_TEST_BINARY="$(SANITIZE_TASK11_TEST_BINARY)" \
		CFLAGS="$(CFLAGS) $(SANITIZE_FLAGS)" LDFLAGS="$(LDFLAGS) $(SANITIZE_FLAGS)"

force-test-build:

clean:
	rm -rf $(BUILD_DIR)

# Phase 4 mixed-v2 source-closure binaries. Legacy targets above remain exact.
.PHONY: test-layer1-foundation test-layer1-foundation-sanitize \
	test-layer1-foundation-off

FOUNDATION_INCLUDE_FLAGS := -I../src -I../src/libs -I../src/libs/acutest \
	-isystem ../src/libs/miniz -isystem ../libdragon/include -I.
FOUNDATION_CPPFLAGS = $(filter-out -DLIBRARY_FS_HOST_TEST% -DPATH_HOST_TEST% \
	-DLIBRARY_VIEW_HOST_TEST% -DERROR_CONTEXT_HOST_TEST%,$(CPPFLAGS))
FOUNDATION_COMMON_CFLAGS = $(CFLAGS) -ffunction-sections $(FOUNDATION_EXTRA_CFLAGS)
FOUNDATION_BASE_FLAGS := -DFEATURE_AURORA_HOME_ENABLED=1 \
	-DFEATURE_AURORA_LIBRARY_ENABLED=1 \
	-DFEATURE_AURORA_LIBRARY_TIMING_ENABLED=1 \
	-DLIBRARY_METRICS_HOST_TEST=1
FOUNDATION_TEST_FLAGS := $(FOUNDATION_BASE_FLAGS) -DLAYER1_FOUNDATION_FOCUSED_TESTS=1
FOUNDATION_CLOSURE_TEST_FLAGS := $(FOUNDATION_TEST_FLAGS) -DPHASE4_CLOSURE_TESTS=1
FOUNDATION_RENDER_PREINCLUDE_FLAGS := -DLAYER1_VIEW_HOST_PREINCLUDE=1 \
	-include support/layer1_view_host_shims.h
FOUNDATION_ALL_GAMES_PREINCLUDE_FLAGS := $(FOUNDATION_RENDER_PREINCLUDE_FLAGS) \
	-DLAYER1_VIEW_HOST_REPLACE_PATH_TRY_INIT=1

FOUNDATION_OBJECT_DIR ?= $(BUILD_DIR)/layer1-foundation
FOUNDATION_BINARY ?= $(BUILD_DIR)/layer1-foundation-tests
FOUNDATION_EXTRA_CFLAGS ?=
FOUNDATION_EXTRA_LDFLAGS ?=

FOUNDATION_OBJECTS := \
	$(FOUNDATION_OBJECT_DIR)/test_library_fs_libdragon.o \
	$(FOUNDATION_OBJECT_DIR)/test_library_scanner.o \
	$(FOUNDATION_OBJECT_DIR)/test_library_snapshot.o \
	$(FOUNDATION_OBJECT_DIR)/test_library_service.o \
	$(FOUNDATION_OBJECT_DIR)/test_library_transition_guard.o \
	$(FOUNDATION_OBJECT_DIR)/test_library_ui_render.o \
	$(FOUNDATION_OBJECT_DIR)/fake_library_fs.o \
	$(FOUNDATION_OBJECT_DIR)/rom_fixture_builder.o \
	$(FOUNDATION_OBJECT_DIR)/layer1_view_host_shims.o \
	$(FOUNDATION_OBJECT_DIR)/library_fs_libdragon.o \
	$(FOUNDATION_OBJECT_DIR)/library_roots.o \
	$(FOUNDATION_OBJECT_DIR)/rom_header.o \
	$(FOUNDATION_OBJECT_DIR)/sha256.o \
	$(FOUNDATION_OBJECT_DIR)/rom_identity.o \
	$(FOUNDATION_OBJECT_DIR)/library_scanner.o \
	$(FOUNDATION_OBJECT_DIR)/library_snapshot.o \
	$(FOUNDATION_OBJECT_DIR)/library_service.o \
	$(FOUNDATION_OBJECT_DIR)/library_metrics.o \
	$(FOUNDATION_OBJECT_DIR)/path.o \
	$(FOUNDATION_OBJECT_DIR)/menu_transition.o \
	$(FOUNDATION_OBJECT_DIR)/view_home.o \
	$(FOUNDATION_OBJECT_DIR)/view_all_games.o

$(FOUNDATION_OBJECT_DIR)/test_library_fs_libdragon.o: test_library_fs_libdragon.c
	@mkdir -p $(@D)
	$(CC) $(FOUNDATION_CPPFLAGS) $(FOUNDATION_INCLUDE_FLAGS) $(FOUNDATION_COMMON_CFLAGS) $(FOUNDATION_TEST_FLAGS) -DLIBRARY_FS_HOST_TEST=1 -c $< -o $@

$(FOUNDATION_OBJECT_DIR)/test_library_scanner.o: test_library_scanner.c
	@mkdir -p $(@D)
	$(CC) $(FOUNDATION_CPPFLAGS) $(FOUNDATION_INCLUDE_FLAGS) $(FOUNDATION_COMMON_CFLAGS) $(FOUNDATION_TEST_FLAGS) -c $< -o $@

$(FOUNDATION_OBJECT_DIR)/test_library_snapshot.o: test_library_snapshot.c
	@mkdir -p $(@D)
	$(CC) $(FOUNDATION_CPPFLAGS) $(FOUNDATION_INCLUDE_FLAGS) $(FOUNDATION_COMMON_CFLAGS) $(FOUNDATION_TEST_FLAGS) -c $< -o $@

$(FOUNDATION_OBJECT_DIR)/test_library_service.o: test_library_service.c
	@mkdir -p $(@D)
	$(CC) $(FOUNDATION_CPPFLAGS) $(FOUNDATION_INCLUDE_FLAGS) $(FOUNDATION_COMMON_CFLAGS) $(FOUNDATION_TEST_FLAGS) -c $< -o $@

$(FOUNDATION_OBJECT_DIR)/test_library_transition_guard.o: test_library_transition_guard.c
	@mkdir -p $(@D)
	$(CC) $(FOUNDATION_CPPFLAGS) $(FOUNDATION_INCLUDE_FLAGS) $(FOUNDATION_COMMON_CFLAGS) $(FOUNDATION_CLOSURE_TEST_FLAGS) $(FOUNDATION_RENDER_PREINCLUDE_FLAGS) -c $< -o $@

$(FOUNDATION_OBJECT_DIR)/test_library_ui_render.o: test_library_ui_render.c
	@mkdir -p $(@D)
	$(CC) $(FOUNDATION_CPPFLAGS) $(FOUNDATION_INCLUDE_FLAGS) $(FOUNDATION_COMMON_CFLAGS) $(FOUNDATION_CLOSURE_TEST_FLAGS) $(FOUNDATION_RENDER_PREINCLUDE_FLAGS) -c $< -o $@

$(FOUNDATION_OBJECT_DIR)/fake_library_fs.o: support/fake_library_fs.c
	@mkdir -p $(@D)
	$(CC) $(FOUNDATION_CPPFLAGS) $(FOUNDATION_INCLUDE_FLAGS) $(FOUNDATION_COMMON_CFLAGS) $(FOUNDATION_BASE_FLAGS) -c $< -o $@

$(FOUNDATION_OBJECT_DIR)/rom_fixture_builder.o: support/rom_fixture_builder.c
	@mkdir -p $(@D)
	$(CC) $(FOUNDATION_CPPFLAGS) $(FOUNDATION_INCLUDE_FLAGS) $(FOUNDATION_COMMON_CFLAGS) $(FOUNDATION_BASE_FLAGS) -c $< -o $@

$(FOUNDATION_OBJECT_DIR)/layer1_view_host_shims.o: support/layer1_view_host_shims.c
	@mkdir -p $(@D)
	$(CC) $(FOUNDATION_CPPFLAGS) $(FOUNDATION_INCLUDE_FLAGS) $(FOUNDATION_COMMON_CFLAGS) $(FOUNDATION_BASE_FLAGS) $(FOUNDATION_RENDER_PREINCLUDE_FLAGS) -c $< -o $@

$(FOUNDATION_OBJECT_DIR)/library_fs_libdragon.o: ../src/menu/library/library_fs_libdragon.c
	@mkdir -p $(@D)
	$(CC) $(FOUNDATION_CPPFLAGS) $(FOUNDATION_INCLUDE_FLAGS) $(FOUNDATION_COMMON_CFLAGS) $(FOUNDATION_BASE_FLAGS) -DLIBRARY_FS_HOST_TEST=1 -c $< -o $@

$(FOUNDATION_OBJECT_DIR)/library_roots.o: ../src/menu/library/library_roots.c
	@mkdir -p $(@D)
	$(CC) $(FOUNDATION_CPPFLAGS) $(FOUNDATION_INCLUDE_FLAGS) $(FOUNDATION_COMMON_CFLAGS) $(FOUNDATION_BASE_FLAGS) -c $< -o $@

$(FOUNDATION_OBJECT_DIR)/rom_header.o: ../src/menu/library/rom_header.c
	@mkdir -p $(@D)
	$(CC) $(FOUNDATION_CPPFLAGS) $(FOUNDATION_INCLUDE_FLAGS) $(FOUNDATION_COMMON_CFLAGS) $(FOUNDATION_BASE_FLAGS) -c $< -o $@

$(FOUNDATION_OBJECT_DIR)/sha256.o: ../src/menu/library/sha256.c
	@mkdir -p $(@D)
	$(CC) $(FOUNDATION_CPPFLAGS) $(FOUNDATION_INCLUDE_FLAGS) $(FOUNDATION_COMMON_CFLAGS) $(FOUNDATION_BASE_FLAGS) -c $< -o $@

$(FOUNDATION_OBJECT_DIR)/rom_identity.o: ../src/menu/library/rom_identity.c
	@mkdir -p $(@D)
	$(CC) $(FOUNDATION_CPPFLAGS) $(FOUNDATION_INCLUDE_FLAGS) $(FOUNDATION_COMMON_CFLAGS) $(FOUNDATION_BASE_FLAGS) -c $< -o $@

$(FOUNDATION_OBJECT_DIR)/library_scanner.o: ../src/menu/library/library_scanner.c
	@mkdir -p $(@D)
	$(CC) $(FOUNDATION_CPPFLAGS) $(FOUNDATION_INCLUDE_FLAGS) $(FOUNDATION_COMMON_CFLAGS) $(FOUNDATION_BASE_FLAGS) -c $< -o $@

$(FOUNDATION_OBJECT_DIR)/library_snapshot.o: ../src/menu/library/library_snapshot.c
	@mkdir -p $(@D)
	$(CC) $(FOUNDATION_CPPFLAGS) $(FOUNDATION_INCLUDE_FLAGS) $(FOUNDATION_COMMON_CFLAGS) $(FOUNDATION_BASE_FLAGS) -c $< -o $@

$(FOUNDATION_OBJECT_DIR)/library_service.o: ../src/menu/library/library_service.c
	@mkdir -p $(@D)
	$(CC) $(FOUNDATION_CPPFLAGS) $(FOUNDATION_INCLUDE_FLAGS) $(FOUNDATION_COMMON_CFLAGS) $(FOUNDATION_BASE_FLAGS) -c $< -o $@

$(FOUNDATION_OBJECT_DIR)/library_metrics.o: ../src/menu/library/library_metrics.c
	@mkdir -p $(@D)
	$(CC) $(FOUNDATION_CPPFLAGS) $(FOUNDATION_INCLUDE_FLAGS) $(FOUNDATION_COMMON_CFLAGS) $(FOUNDATION_BASE_FLAGS) -c $< -o $@

$(FOUNDATION_OBJECT_DIR)/path.o: ../src/menu/path.c
	@mkdir -p $(@D)
	$(CC) $(FOUNDATION_CPPFLAGS) $(FOUNDATION_INCLUDE_FLAGS) $(FOUNDATION_COMMON_CFLAGS) $(FOUNDATION_BASE_FLAGS) -DPATH_HOST_TEST=1 -c $< -o $@

$(FOUNDATION_OBJECT_DIR)/menu_transition.o: ../src/menu/menu.c
	@mkdir -p $(@D)
	$(CC) $(FOUNDATION_CPPFLAGS) $(FOUNDATION_INCLUDE_FLAGS) $(FOUNDATION_COMMON_CFLAGS) $(FOUNDATION_BASE_FLAGS) -DMENU_TRANSITION_HOST_TEST=1 $(FOUNDATION_RENDER_PREINCLUDE_FLAGS) -c $< -o $@

$(FOUNDATION_OBJECT_DIR)/view_home.o: ../src/menu/views/home.c
	@mkdir -p $(@D)
	$(CC) $(FOUNDATION_CPPFLAGS) $(FOUNDATION_INCLUDE_FLAGS) $(FOUNDATION_COMMON_CFLAGS) $(FOUNDATION_BASE_FLAGS) $(FOUNDATION_RENDER_PREINCLUDE_FLAGS) -c $< -o $@

$(FOUNDATION_OBJECT_DIR)/view_all_games.o: ../src/menu/views/all_games.c
	@mkdir -p $(@D)
	$(CC) $(FOUNDATION_CPPFLAGS) $(FOUNDATION_INCLUDE_FLAGS) $(FOUNDATION_COMMON_CFLAGS) $(FOUNDATION_BASE_FLAGS) $(FOUNDATION_ALL_GAMES_PREINCLUDE_FLAGS) -c $< -o $@

$(FOUNDATION_BINARY): $(FOUNDATION_OBJECTS)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(FOUNDATION_EXTRA_LDFLAGS) $(HOST_SECTION_GC) $(LDFLAGS) -o $@ $(FOUNDATION_OBJECTS) $(LDLIBS)

test-layer1-foundation: $(FOUNDATION_BINARY)
	./$(FOUNDATION_BINARY)

FOUNDATION_SANITIZE_OBJECT_DIR := $(SANITIZE_BUILD_DIR)/layer1-foundation
FOUNDATION_SANITIZE_BINARY := $(SANITIZE_BUILD_DIR)/layer1-foundation-tests

test-layer1-foundation-sanitize:
	$(MAKE) FOUNDATION_OBJECT_DIR="$(FOUNDATION_SANITIZE_OBJECT_DIR)" FOUNDATION_BINARY="$(FOUNDATION_SANITIZE_BINARY)" FOUNDATION_EXTRA_CFLAGS="$(SANITIZE_FLAGS)" FOUNDATION_EXTRA_LDFLAGS="$(SANITIZE_FLAGS)" "$(FOUNDATION_SANITIZE_BINARY)"
	ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=print_stacktrace=1 ./$(FOUNDATION_SANITIZE_BINARY)

FOUNDATION_DISABLED_OBJECT_DIR := $(BUILD_DIR)/layer1-foundation-off
FOUNDATION_DISABLED_BINARY := $(BUILD_DIR)/layer1-foundation-off-tests
FOUNDATION_DISABLED_FLAGS := -DFEATURE_AURORA_HOME_ENABLED=1 \
	-DFEATURE_AURORA_LIBRARY_ENABLED=1 \
	-DFEATURE_AURORA_LIBRARY_TIMING_ENABLED=0
FOUNDATION_DISABLED_OBJECTS := \
	$(FOUNDATION_DISABLED_OBJECT_DIR)/test_layer1_disabled.o \
	$(FOUNDATION_DISABLED_OBJECT_DIR)/layer1_view_host_shims.o \
	$(FOUNDATION_DISABLED_OBJECT_DIR)/library_fs_libdragon.o \
	$(FOUNDATION_DISABLED_OBJECT_DIR)/library_roots.o \
	$(FOUNDATION_DISABLED_OBJECT_DIR)/rom_header.o \
	$(FOUNDATION_DISABLED_OBJECT_DIR)/sha256.o \
	$(FOUNDATION_DISABLED_OBJECT_DIR)/rom_identity.o \
	$(FOUNDATION_DISABLED_OBJECT_DIR)/library_scanner.o \
	$(FOUNDATION_DISABLED_OBJECT_DIR)/library_snapshot.o \
	$(FOUNDATION_DISABLED_OBJECT_DIR)/library_service.o \
	$(FOUNDATION_DISABLED_OBJECT_DIR)/path.o \
	$(FOUNDATION_DISABLED_OBJECT_DIR)/menu_transition.o \
	$(FOUNDATION_DISABLED_OBJECT_DIR)/view_home.o \
	$(FOUNDATION_DISABLED_OBJECT_DIR)/view_all_games.o

$(FOUNDATION_DISABLED_OBJECT_DIR)/test_layer1_disabled.o: test_layer1_disabled.c
	@mkdir -p $(@D)
	$(CC) $(FOUNDATION_CPPFLAGS) $(FOUNDATION_INCLUDE_FLAGS) $(CFLAGS) -ffunction-sections $(FOUNDATION_DISABLED_FLAGS) -DPHASE4_CLOSURE_DISABLED_TESTS=1 $(FOUNDATION_RENDER_PREINCLUDE_FLAGS) -c $< -o $@

$(FOUNDATION_DISABLED_OBJECT_DIR)/layer1_view_host_shims.o: support/layer1_view_host_shims.c
	@mkdir -p $(@D)
	$(CC) $(FOUNDATION_CPPFLAGS) $(FOUNDATION_INCLUDE_FLAGS) $(CFLAGS) -ffunction-sections $(FOUNDATION_DISABLED_FLAGS) -DLAYER1_VIEW_HOST_DISABLED_FS_SHIMS=1 $(FOUNDATION_RENDER_PREINCLUDE_FLAGS) -c $< -o $@

$(FOUNDATION_DISABLED_OBJECT_DIR)/library_fs_libdragon.o: ../src/menu/library/library_fs_libdragon.c
	@mkdir -p $(@D)
	$(CC) $(FOUNDATION_CPPFLAGS) $(FOUNDATION_INCLUDE_FLAGS) $(CFLAGS) -ffunction-sections $(FOUNDATION_DISABLED_FLAGS) -DLIBRARY_FS_HOST_TEST=1 -c $< -o $@

$(FOUNDATION_DISABLED_OBJECT_DIR)/library_roots.o: ../src/menu/library/library_roots.c
	@mkdir -p $(@D)
	$(CC) $(FOUNDATION_CPPFLAGS) $(FOUNDATION_INCLUDE_FLAGS) $(CFLAGS) -ffunction-sections $(FOUNDATION_DISABLED_FLAGS)  -c $< -o $@

$(FOUNDATION_DISABLED_OBJECT_DIR)/rom_header.o: ../src/menu/library/rom_header.c
	@mkdir -p $(@D)
	$(CC) $(FOUNDATION_CPPFLAGS) $(FOUNDATION_INCLUDE_FLAGS) $(CFLAGS) -ffunction-sections $(FOUNDATION_DISABLED_FLAGS)  -c $< -o $@

$(FOUNDATION_DISABLED_OBJECT_DIR)/sha256.o: ../src/menu/library/sha256.c
	@mkdir -p $(@D)
	$(CC) $(FOUNDATION_CPPFLAGS) $(FOUNDATION_INCLUDE_FLAGS) $(CFLAGS) -ffunction-sections $(FOUNDATION_DISABLED_FLAGS)  -c $< -o $@

$(FOUNDATION_DISABLED_OBJECT_DIR)/rom_identity.o: ../src/menu/library/rom_identity.c
	@mkdir -p $(@D)
	$(CC) $(FOUNDATION_CPPFLAGS) $(FOUNDATION_INCLUDE_FLAGS) $(CFLAGS) -ffunction-sections $(FOUNDATION_DISABLED_FLAGS)  -c $< -o $@

$(FOUNDATION_DISABLED_OBJECT_DIR)/library_scanner.o: ../src/menu/library/library_scanner.c
	@mkdir -p $(@D)
	$(CC) $(FOUNDATION_CPPFLAGS) $(FOUNDATION_INCLUDE_FLAGS) $(CFLAGS) -ffunction-sections $(FOUNDATION_DISABLED_FLAGS)  -c $< -o $@

$(FOUNDATION_DISABLED_OBJECT_DIR)/library_snapshot.o: ../src/menu/library/library_snapshot.c
	@mkdir -p $(@D)
	$(CC) $(FOUNDATION_CPPFLAGS) $(FOUNDATION_INCLUDE_FLAGS) $(CFLAGS) -ffunction-sections $(FOUNDATION_DISABLED_FLAGS)  -c $< -o $@

$(FOUNDATION_DISABLED_OBJECT_DIR)/library_service.o: ../src/menu/library/library_service.c
	@mkdir -p $(@D)
	$(CC) $(FOUNDATION_CPPFLAGS) $(FOUNDATION_INCLUDE_FLAGS) $(CFLAGS) -ffunction-sections $(FOUNDATION_DISABLED_FLAGS)  -c $< -o $@

$(FOUNDATION_DISABLED_OBJECT_DIR)/path.o: ../src/menu/path.c
	@mkdir -p $(@D)
	$(CC) $(FOUNDATION_CPPFLAGS) $(FOUNDATION_INCLUDE_FLAGS) $(CFLAGS) -ffunction-sections $(FOUNDATION_DISABLED_FLAGS) -DPATH_HOST_TEST=1 -c $< -o $@

$(FOUNDATION_DISABLED_OBJECT_DIR)/menu_transition.o: ../src/menu/menu.c
	@mkdir -p $(@D)
	$(CC) $(FOUNDATION_CPPFLAGS) $(FOUNDATION_INCLUDE_FLAGS) $(CFLAGS) -ffunction-sections $(FOUNDATION_DISABLED_FLAGS) -DMENU_TRANSITION_HOST_TEST=1 $(FOUNDATION_RENDER_PREINCLUDE_FLAGS) -c $< -o $@

$(FOUNDATION_DISABLED_OBJECT_DIR)/view_home.o: ../src/menu/views/home.c
	@mkdir -p $(@D)
	$(CC) $(FOUNDATION_CPPFLAGS) $(FOUNDATION_INCLUDE_FLAGS) $(CFLAGS) -ffunction-sections $(FOUNDATION_DISABLED_FLAGS) $(FOUNDATION_RENDER_PREINCLUDE_FLAGS) -c $< -o $@

$(FOUNDATION_DISABLED_OBJECT_DIR)/view_all_games.o: ../src/menu/views/all_games.c
	@mkdir -p $(@D)
	$(CC) $(FOUNDATION_CPPFLAGS) $(FOUNDATION_INCLUDE_FLAGS) $(CFLAGS) -ffunction-sections $(FOUNDATION_DISABLED_FLAGS) $(FOUNDATION_ALL_GAMES_PREINCLUDE_FLAGS) -c $< -o $@

$(FOUNDATION_DISABLED_BINARY): $(FOUNDATION_DISABLED_OBJECTS)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(HOST_SECTION_GC) $(LDFLAGS) -o $@ $(FOUNDATION_DISABLED_OBJECTS) $(LDLIBS)

test-layer1-foundation-off: $(FOUNDATION_DISABLED_BINARY)
	./$(FOUNDATION_DISABLED_BINARY)
