CXX ?= g++
CXXFLAGS ?= -std=c++14 -Wall -Wextra -O2 -I include

SRCS = src/alu.cpp \
       src/control_unit.cpp \
       src/cpu_pipeline.cpp \
       src/assembler.cpp \
       src/main.cpp

BIN = simplerisc_sim.exe

all: $(BIN)

$(BIN): $(SRCS)
	$(CXX) $(CXXFLAGS) $(SRCS) -o $(BIN)

clean:
	del /Q $(BIN) 2>NUL || rm -f $(BIN)

run: $(BIN)
	./$(BIN)

.PHONY: all clean run
