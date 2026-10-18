CC = aarch64-linux-gnu-gcc
CFLAGS = -static -O3 -s -Wall -Wextra
SRC = src/qg_daemon.c
BUILD_DIR = build
TARGET_BIN = $(BUILD_DIR)/qg-daemon
ZIP_NAME = $(BUILD_DIR)/gauguin-battery-fix-v4.0.zip

.PHONY: all clean module

all: $(TARGET_BIN) module

$(TARGET_BIN): $(SRC)
	@mkdir -p $(BUILD_DIR)
	@echo "[CC] Compiling native ARM64 binary..."
	$(CC) $(CFLAGS) $< -o $@
	@echo "[OK] Binary created: $@"

module: $(TARGET_BIN)
	@echo "[ZIP] Packaging Magisk Module..."
	@mkdir -p $(BUILD_DIR)/package
	@cp magisk/module.prop $(BUILD_DIR)/package/
	@cp magisk/service.sh $(BUILD_DIR)/package/
	@cp magisk/customize.sh $(BUILD_DIR)/package/
	@touch $(BUILD_DIR)/package/auto_mount
	@cp $(TARGET_BIN) $(BUILD_DIR)/package/
	@cd $(BUILD_DIR)/package && zip -r -9 ../../$(ZIP_NAME) * >/dev/null
	@rm -rf $(BUILD_DIR)/package
	@echo "[OK] Flashable module created: $(ZIP_NAME)"

clean:
	rm -rf $(BUILD_DIR)
