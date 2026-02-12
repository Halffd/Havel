.PHONY: debug release test clean all mode help

SCRIPT_NAME := ./build.sh  # Assuming your script is named 'build.sh' and is executable
DEFAULT_MODE := 2

# --- Standard Targets ---

# Default target: Full Debug Build (Mode 0)
debug:
	@echo "--- Starting full DEBUG build (Mode ${DEFAULT_MODE}) ---"
	@$(SCRIPT_NAME) ${DEFAULT_MODE} build

# Full Release Build (Mode 5)
release:
	@echo "--- Starting full RELEASE build (Mode 5) ---"
	@$(SCRIPT_NAME) 5 build

# Run all tests (requires building with tests enabled, e.g., Modes 0, 5, 6, 9)
test:
	@echo "--- Running tests (using current configured mode: $$(./$(SCRIPT_NAME) 2>/dev/null | awk '/Mode:/ {print $$2; exit}')) ---"
	@$(SCRIPT_NAME) test
	@echo "--- Running Havel test suite ---"
	@if [ -f "test_suite.hv" ]; then \
		echo "🧪 Running Jest-like test suite..."; \
		./hav test_suite.hv; \
	else \
		echo "⚠️  test_suite.hv not found"; \
	fi
	@echo "--- Running performance benchmarks ---"
	@if [ -d "benchmarks" ]; then \
		echo "📊 Running benchmarks..."; \
		for file in benchmarks/*.havel; do \
			if [ -f "$$file" ]; then \
				echo "🏃 Running $$file..."; \
				./hav "$$file"; \
			fi; \
		done; \
	else \
		echo "⚠️  benchmarks directory not found"; \
	fi

# Clean the build directory
clean:
	@echo "--- Cleaning build directories and logs ---"
	@$(SCRIPT_NAME) clean

# Clean, Build, and Run (Default Mode 0)
all:
	@echo "--- Clean, Build, and Run in Default Debug Mode (Mode ${DEFAULT_MODE}) ---"
	@$(SCRIPT_NAME) ${DEFAULT_MODE} all

# --- Dynamic Targets ---

# Generic target to run a command with a specific mode
# Usage: make mode MODE=1 CMD=build
mode:
ifndef MODE
	@echo "Error: MODE must be set. Usage: make mode MODE=X CMD=command"
	@exit 1
endif
ifndef CMD
	@echo "Error: CMD must be set. Usage: make mode MODE=X CMD=command"
	@exit 1
endif
	@echo "--- Executing command '$(CMD)' in Build Mode $(MODE) ---"
	@$(SCRIPT_NAME) $(MODE) $(CMD)

# --- Help/Usage ---

help:
	@echo ""
	@echo "Makefile Targets for the Havel Build Script"
	@echo "------------------------------------------"
	@echo "debug      : Clean + Build in Mode 0 (Default: Debug, Tests, Lang, LLVM)."
	@echo "release    : Build in Mode 5 (Full Release, Tests, Lang, LLVM)."
	@echo "test       : Run all tests (unit tests + Havel test suite + benchmarks)."
	@echo "clean      : Clean build directory and logs."
	@echo "all        : Clean, Build, and Run in Mode 0."
	@echo "mode       : Dynamic target. Usage: make mode MODE=<X> CMD=<command>"
	@echo "             e.g., make mode MODE=6 CMD=build"
	@echo ""
	@echo "Test Suite Features:"
	@echo "  🧪 Jest-like test runner with describe(), test(), expect()"
	@echo "  📊 Performance benchmarks for competitive analysis"
	@echo "  🔍 Debugger protocol with breakpoints and stack inspection"
	@echo "  📈 Coverage reporting and test statistics"
	@echo ""
	@$(SCRIPT_NAME) usage
