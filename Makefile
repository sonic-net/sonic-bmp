# Makefile

# Define the default target
.DEFAULT_GOAL := all

# Specify the CMake build directory
BUILD_DIR := build

# Build targets
.PHONY: all clean

all: $(BUILD_DIR)
	@$(MAKE) -C $(BUILD_DIR)

$(BUILD_DIR):
	@mkdir -p $(BUILD_DIR)
	@cd $(BUILD_DIR) && cmake -DCMAKE_INSTALL_PREFIX:PATH=/usr -DENABLE_REDIS=ON ../ && make && cp ../Server/openbmpd.conf Server/openbmpd.conf

clean:
	@rm -rf $(BUILD_DIR)
