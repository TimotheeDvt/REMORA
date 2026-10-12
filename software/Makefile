.PHONY: all configure build clean preset-stats c b ccb

all: configure build

# Generates the Visual Studio / MSVC build files
configure:
	cmake -B build -DCMAKE_BUILD_TYPE=Release -G Ninja

# Compiles the project using the generated build files
build: configure
	cmake --build build --config Release -j4

# Builds every preset graph, prints its node composition, and regenerates the NIME paper's preset table
preset-stats: configure
	cmake --build build --config Release --target REMORAPresetStats -j4
	./build/REMORAPresetStats_artefacts/Release/REMORAPresetStats ../papers/nime/preset-table.tex

# Cleans the build directory
clean:
	rm -rf build

# Shorthand aliases
c: configure
b: build
ccb: clean configure build