# The app built for a computer with SDL2, to look at the UI without a Switch: make -C desktop, then
# ./desktop/themezer-nx-desktop from themezer-nx/ (where romfs: is). Needs SDL2, SDL2_image, SDL2_ttf and curl.

# make -C desktop asan: the same with AddressSanitizer and UndefinedBehaviorSanitizer, as themezer-nx-desktop-asan.
BUILD ?= build
TARGET ?= themezer-nx-desktop
SANITIZE ?=

SOURCES := $(wildcard ../source/*.cpp ../source/*/*.cpp)
C_SOURCES := $(wildcard ../source/libs/*.c)
OBJECTS := $(patsubst ../source/%,$(BUILD)/%,$(SOURCES:.cpp=.o)) $(patsubst ../source/%,$(BUILD)/%,$(C_SOURCES:.c=.o)) \
           $(BUILD)/desktop/switch.o

SDL_CFLAGS := -I/opt/homebrew/include -I/opt/homebrew/include/SDL2
SDL_LIBS := -L/opt/homebrew/lib -lSDL2 -lSDL2_image -lSDL2_ttf
APP_VERSION ?= 4.0.0-desktop
FLAGS := -g -O1 $(SANITIZE) -MMD -MP -Iinclude -I../source $(SDL_CFLAGS) -DAPP_VERSION=\"$(APP_VERSION)\" -DTZ_DESKTOP -include hooks.h

$(TARGET): $(OBJECTS)
	$(CXX) $(SANITIZE) -o $@ $^ $(SDL_LIBS) -lcurl -lz

$(BUILD)/%.o: ../source/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) -std=c++23 $(FLAGS) -c $< -o $@

$(BUILD)/%.o: ../source/%.c
	@mkdir -p $(dir $@)
	$(CC) $(FLAGS) -c $< -o $@

$(BUILD)/desktop/switch.o: switch.cpp
	@mkdir -p $(dir $@)
	$(CXX) -std=c++23 $(FLAGS) -c $< -o $@

-include $(OBJECTS:.o=.d)

asan:
	$(MAKE) BUILD=build-asan TARGET=themezer-nx-desktop-asan \
	    SANITIZE="-fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=undefined"

clean:
	rm -rf build build-asan themezer-nx-desktop themezer-nx-desktop-asan

# The reader of .nxtheme archives, on archives the test makes, the ThumbHash encoder, versions, file names, and what
# tells an outdated item and swaps it for the new one.
test: build/tests/archive_test build/tests/thumbhash_test build/tests/version_test build/tests/names_test \
      build/tests/outdated_test build/tests/report_test build/tests/certificates_test build/tests/session_test
	./build/tests/archive_test
	./build/tests/thumbhash_test
	./build/tests/version_test
	./build/tests/names_test
	./build/tests/outdated_test
	./build/tests/report_test
	./build/tests/certificates_test
	./build/tests/session_test

build/tests/archive_test: tests/archive_test.cpp ../source/core/archive.cpp
	@mkdir -p $(dir $@)
	$(CXX) -std=c++23 -g -O1 -I../source $^ -o $@ -lz

build/tests/thumbhash_test: tests/thumbhash_test.cpp ../source/libs/thumbhash.c
	@mkdir -p $(dir $@)
	$(CC) -std=gnu11 -g -O1 -c ../source/libs/thumbhash.c -o build/tests/thumbhash.o
	$(CXX) -std=c++23 -g -O1 -I../source tests/thumbhash_test.cpp build/tests/thumbhash.o -o $@

build/tests/version_test: tests/version_test.cpp ../source/core/version.cpp
	@mkdir -p $(dir $@)
	$(CXX) -std=c++23 -g -O1 -I../source $^ -o $@

build/tests/names_test: tests/names_test.cpp ../source/domain/names.cpp
	@mkdir -p $(dir $@)
	$(CXX) -std=c++23 -g -O1 -I../source $^ -o $@

build/tests/outdated_test: tests/outdated_test.cpp ../source/domain/outdated.cpp ../source/domain/names.cpp
	@mkdir -p $(dir $@)
	$(CXX) -std=c++23 -g -O1 -I../source $^ -o $@

build/tests/report_test: tests/report_test.cpp ../source/domain/report.cpp
	@mkdir -p $(dir $@)
	$(CXX) -std=c++23 -g -O1 -I../source $^ -o $@

build/tests/certificates_test: tests/certificates_test.cpp ../source/core/certificates.cpp
	@mkdir -p $(dir $@)
	$(CXX) -std=c++23 -g -O1 -I../source $^ -o $@

build/tests/session_test: tests/session_test.cpp ../source/domain/session.cpp
	@mkdir -p $(dir $@)
	$(CXX) -std=c++23 -g -O1 -I../source $^ -o $@
