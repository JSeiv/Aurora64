CC ?= cc
STRICT_CFLAGS := -std=c11 -Wall -Wextra -Werror -pedantic
override CFLAGS := $(filter-out $(STRICT_CFLAGS),$(CFLAGS)) $(STRICT_CFLAGS)
CPPFLAGS ?=
LDFLAGS ?=
SANITIZE_FLAGS := -fsanitize=address,undefined -fno-omit-frame-pointer

BUILD_DIR := build
TEST_BINARY ?= $(BUILD_DIR)/tests
SANITIZE_BUILD_DIR := $(BUILD_DIR)/sanitize
SANITIZE_TEST_BINARY := $(SANITIZE_BUILD_DIR)/tests
SOURCES := test_main.c support/test_alloc.c

.PHONY: test test-sanitize clean force-test-build

test: $(TEST_BINARY)
	./$(TEST_BINARY)

$(TEST_BINARY): force-test-build $(SOURCES) support/test_alloc.h ../src/libs/acutest/acutest.h
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) -I../src/libs/acutest -I. $(SOURCES) $(LDFLAGS) -o $@

test-sanitize:
	rm -rf $(SANITIZE_BUILD_DIR)
	$(MAKE) test TEST_BINARY="$(SANITIZE_TEST_BINARY)" CFLAGS="$(CFLAGS) $(SANITIZE_FLAGS)" LDFLAGS="$(LDFLAGS) $(SANITIZE_FLAGS)"

force-test-build:

clean:
	rm -rf $(BUILD_DIR)
