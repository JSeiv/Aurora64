CC ?= cc
STRICT_CFLAGS := -std=c11 -Wall -Wextra -Werror -pedantic
override CFLAGS := $(filter-out $(STRICT_CFLAGS),$(CFLAGS)) $(STRICT_CFLAGS)
CPPFLAGS ?=
override CPPFLAGS += -DLIBRARY_FS_HOST_TEST -DPATH_HOST_TEST -DLIBRARY_VIEW_HOST_TEST
LDFLAGS ?=
SANITIZE_FLAGS := -fsanitize=address,undefined -fno-omit-frame-pointer

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

.PHONY: test test-sanitize test-compat-flags clean force-test-build

define assert-library-config
	@actual="$$($(MAKE) -s --no-print-directory -f ../Makefile SKIP_N64_MK=1 $(1) print-library-config)"; \
	expected='$(2)'; \
	if test "$$actual" != "$$expected"; then \
		printf 'compatibility case %s mismatch\nexpected: %s\nactual:   %s\n' \
			'$(3)' "$$expected" "$$actual" >&2; \
		exit 1; \
	fi
endef

test: test-compat-flags $(TEST_BINARY)
	./$(TEST_BINARY)

test-compat-flags:
	$(call assert-library-config,,home=0 library=0 launch_proof=0 library_origin=undefined,default)
	$(call assert-library-config,AURORA64_LAUNCH_PROOF=1,home=0 library=1 launch_proof=1 library_origin=undefined,compat)
	$(call assert-library-config,AURORA64_LIBRARY=0 AURORA64_LAUNCH_PROOF=1,home=0 library=0 launch_proof=1 library_origin=command line,explicit0)
	$(call assert-library-config,AURORA64_LIBRARY=1 AURORA64_LAUNCH_PROOF=0,home=0 library=1 launch_proof=0 library_origin=command line,explicit1)

$(TEST_BINARY): force-test-build $(SOURCES) support/test_alloc.h ../src/libs/acutest/acutest.h
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) -I../src -I../src/libs -I../src/libs/acutest -I../src/libs/miniz -I../libdragon/include -I. $(SOURCES) $(LDFLAGS) -o $@

test-sanitize:
	rm -rf $(SANITIZE_BUILD_DIR)
	$(MAKE) test TEST_BINARY="$(SANITIZE_TEST_BINARY)" CFLAGS="$(CFLAGS) $(SANITIZE_FLAGS)" LDFLAGS="$(LDFLAGS) $(SANITIZE_FLAGS)"

force-test-build:

clean:
	rm -rf $(BUILD_DIR)
