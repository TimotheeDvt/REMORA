.PHONY: all configure build clean preset-stats analyzer-check c b ccb

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

# Runs the simulation checks (calibration, orientation filter, thrust, facing); pass REPLAY=log.csv to replay a packet log instead
analyzer-check: configure
	cmake --build build --config Release --target REMORAAnalyzerCheck -j4
	./build/REMORAAnalyzerCheck_artefacts/Release/REMORAAnalyzerCheck $(if $(REPLAY),--replay $(REPLAY))

# Cleans the build directory
clean:
	rm -rf build

# Shorthand aliases
c: configure
b: build
ccb: clean configure build
