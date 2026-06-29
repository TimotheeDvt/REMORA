.PHONY: all configure build clean

all: configure build

# Generates the Visual Studio / MSVC build files
configure:
	cmake -B build -DCMAKE_BUILD_TYPE=Release -G Ninja

# Compiles the project using the generated build files
build:
	cmake --build build --config Release

# Cleans the build directory
clean:
	rm -rf build
