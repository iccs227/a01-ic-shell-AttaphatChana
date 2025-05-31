CXX = g++
CXXFLAGS = -std=c++20 -Wall -Wextra 
TARGET = icsh
SOURCE = icsh.cpp

all: $(TARGET)

$(TARGET): $(SOURCE)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(SOURCE)

clean:
	rm -f $(TARGET)

.PHONY: all clean