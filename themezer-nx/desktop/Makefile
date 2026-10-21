# The app built for a computer with SDL2, to look at the UI without a Switch: make -C desktop, then
# ./desktop/themezer-nx-desktop from themezer-nx/ (where romfs: is). Needs SDL2, SDL2_image, SDL2_ttf and curl.

SOURCES := $(wildcard ../source/*.cpp ../source/*/*.cpp) switch.cpp
C_SOURCES := $(wildcard ../source/libs/*.c)
OBJECTS := $(patsubst ../source/%,build/%,$(SOURCES:.cpp=.o)) $(patsubst ../source/%,build/%,$(C_SOURCES:.c=.o))
OBJECTS := $(subst build/switch.o,build/desktop/switch.o,$(OBJECTS:switch.o=build/desktop/switch.o))

SDL_CFLAGS := -I/opt/homebrew/include -I/opt/homebrew/include/SDL2
SDL_LIBS := -L/opt/homebrew/lib -lSDL2 -lSDL2_image -lSDL2_ttf
FLAGS := -g -O1 -MMD -MP -Iinclude -I../source $(SDL_CFLAGS) -DAPP_VERSION=\"4.0.0-desktop\" -DTZ_DESKTOP -include hooks.h

themezer-nx-desktop: $(OBJECTS)
	$(CXX) -o $@ $^ $(SDL_LIBS) -lcurl -lz

build/%.o: ../source/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) -std=c++23 $(FLAGS) -c $< -o $@

build/%.o: ../source/%.c
	@mkdir -p $(dir $@)
	$(CC) $(FLAGS) -c $< -o $@

build/desktop/switch.o: switch.cpp
	@mkdir -p $(dir $@)
	$(CXX) -std=c++23 $(FLAGS) -c $< -o $@

-include $(OBJECTS:.o=.d)

clean:
	rm -rf build themezer-nx-desktop

# The reader of .nxtheme archives, on archives the test makes, and the ThumbHash encoder.
test: build/tests/archive_test build/tests/thumbhash_test
	./build/tests/archive_test
	./build/tests/thumbhash_test

build/tests/archive_test: tests/archive_test.cpp ../source/core/archive.cpp
	@mkdir -p $(dir $@)
	$(CXX) -std=c++23 -g -O1 -I../source $^ -o $@ -lz

build/tests/thumbhash_test: tests/thumbhash_test.cpp ../source/libs/thumbhash.c
	@mkdir -p $(dir $@)
	$(CC) -std=c11 -g -O1 -c ../source/libs/thumbhash.c -o build/tests/thumbhash.o
	$(CXX) -std=c++23 -g -O1 -I../source tests/thumbhash_test.cpp build/tests/thumbhash.o -o $@
