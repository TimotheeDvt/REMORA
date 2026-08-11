.PHONY: all configure build clean c b

all: configure build

# Generates the Visual Studio / MSVC build files
configure:
	cmake -B build -DCMAKE_BUILD_TYPE=Release -G Ninja

# Compiles the project using the generated build files
build: configure
	cmake --build build --config Release -j4

# Cleans the build directory
clean:
	rm -rf build

# Shorthand aliases
c: configure
b: build