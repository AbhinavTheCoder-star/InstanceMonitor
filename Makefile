CC ?= cc
CPPFLAGS += -D_GNU_SOURCE -Iinclude $(shell pkg-config --cflags ncursesw)
CFLAGS ?= -O2 -g
CFLAGS += -std=c17 -Wall -Wextra -Wpedantic -Wconversion -Wshadow
LDLIBS += $(shell pkg-config --libs ncursesw)
SOURCES = main.c src/cli.c src/proc.c src/metrics.c src/output.c src/ui.c src/sysmon.c
OBJECTS = $(SOURCES:%.c=build/%.o)

.PHONY: all clean check sanitize
all: instmon
instmon: $(OBJECTS)
	$(CC) $(LDFLAGS) -o $@ $(OBJECTS) $(LDLIBS)
build/%.o: %.c include/instmon.h include/sysmon.h
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c $< -o $@
build/test_core: tests/test_core.c src/proc.c src/metrics.c include/instmon.h
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) tests/test_core.c src/proc.c src/metrics.c -o $@
build/sysmon_probe: tests/sysmon_probe.c src/sysmon.c src/proc.c include/sysmon.h include/instmon.h
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) tests/sysmon_probe.c src/sysmon.c src/proc.c -o $@
check: instmon build/test_core build/sysmon_probe
	./build/test_core
	python3 tests/integration.py
	python3 tests/test_install.py
	python3 tests/test_sysmon.py
sanitize:
	$(MAKE) clean
	$(MAKE) CFLAGS='-O1 -g -std=c17 -Wall -Wextra -Wpedantic -fsanitize=address,undefined -fno-omit-frame-pointer' LDFLAGS='-fsanitize=address,undefined' check
clean:
	rm -rf build instmon
-include $(OBJECTS:.o=.d)
