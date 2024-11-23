CXX = g++
CXXFLAGS = -std=c++17 -Wall -g
AR = ar
ARFLAGS = rcs

LIB_NAME = libMemoryManager.a
OBJECTS = MemoryManager.o

all: $(LIB_NAME)

$(LIB_NAME): $(OBJECTS)
	$(AR) $(ARFLAGS) $@ $^

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f *.o $(LIB_NAME)

.PHONY: all clean