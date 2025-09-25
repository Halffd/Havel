CXX = g++
CXXFLAGS = -std=c++20 -Wall -Wextra -O2
INCLUDES = -Iinclude -Isrc
LDFLAGS = -lm -lpthread -llua5.4 -lX11 -lXtst -lxkbcommon -lXrandr

# Project structure
SRCDIR = src
BUILDDIR = build
BINDIR = bin
CONFIG_DIR = config
LOG_DIR = log

# Automatically find all .cpp files in the src directory
SOURCES = $(shell find $(SRCDIR) -name "*.cpp")
OBJECTS = $(patsubst $(SRCDIR)/%.cpp,$(BUILDDIR)/%.o,$(SOURCES))
DEPS = $(OBJECTS:.o=.d)

# Add C source files
C_SOURCES = $(shell find $(SRCDIR) -name "*.c")
C_OBJECTS = $(patsubst $(SRCDIR)/%.c,$(BUILDDIR)/%.o,$(C_SOURCES))
DEPS += $(C_OBJECTS:.o=.d)

# Define the target executable
TARGET = $(BINDIR)/HvC

# Define phony targets (targets that don't produce a file with the target's name)
.PHONY: all clean setup directories run test

# Default target
all: setup $(TARGET)

# Create the necessary directories
directories:
	@echo "Creating directory structure..."
	@mkdir -p $(BUILDDIR)
	@mkdir -p $(BINDIR)
	@mkdir -p $(CONFIG_DIR)/hotkeys
	@mkdir -p $(LOG_DIR)

# Run setup script to ensure configuration and log directories exist
setup: directories
	@echo "Setting up project environment..."
	@bash setup.sh

# Build the target executable
$(TARGET): $(OBJECTS) $(C_OBJECTS)
	@echo "Linking $@..."
	@mkdir -p $(BINDIR)
	@$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)
	@echo "Build successful!"

# Compile C++ source files into object files
$(BUILDDIR)/%.o: $(SRCDIR)/%.cpp
	@echo "Compiling $<..."
	@mkdir -p $(dir $@)
	@$(CXX) $(CXXFLAGS) $(INCLUDES) -MMD -MP -c $< -o $@

# Compile C source files into object files
$(BUILDDIR)/%.o: $(SRCDIR)/%.c
	@echo "Compiling $< (C)..."
	@mkdir -p $(dir $@)
	@gcc -Wall -Wextra -O2 $(INCLUDES) -MMD -MP -c $< -o $@

# Clean build artifacts
clean:
	@echo "Cleaning build directory..."
	@rm -rf $(BUILDDIR)/*
	@rm -f $(TARGET)
	@echo "Clean complete!"

# Fix build issues target
fix-build:
	chmod +x fix_build.sh
	./fix_build.sh

# Clean and rebuild
rebuild: fix-build
	mkdir -p build && cd build && cmake .. && cmake --build .

# Run the application
run: all
	@echo "Running $(TARGET)..."
	@cd $(dir $(TARGET)) && ./$(notdir $(TARGET))

# Run tests if available
test: all
	@echo "Running tests..."
	@if [ -f "$(BINDIR)/test_runner" ]; then \
		$(BINDIR)/test_runner; \
	else \
		echo "No test runner found."; \
	fi

# Include dependency files
-include $(DEPS) 