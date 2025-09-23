
.PHONY: mac linux windows clean all

mac:
	@echo "Building for macOS..."
	$(MAKE) -f Makefile.mac

windows:
	@echo "Building for Windows..."
	$(MAKE) -f Makefile.windows

linux:
	@echo "Building for Linux..."
	$(MAKE) -f Makefile.linux

clean:
	@echo "Cleaning build artifacts..."
	$(MAKE) -f Makefile.clean all

all: mac linux windows
	@echo "Built all platforms (where toolchains available)."

default: mac


