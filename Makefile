SRC = $(wildcard src/*.cpp)

build:
	g++ -Wall $(SRC) -o build/gpu-db

run:
	./build/gpu-db

.PHONY: build run