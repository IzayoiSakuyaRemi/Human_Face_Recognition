#!/bin/bash
# Rebuild script for Human Face Recognition project
# Fixes the ESP_HOSTED slave target issue by directly patching sdkconfig.h after cmake

set -e

# ESP-IDF environment
export IDF_PATH="D:/Espressif/espidf/.espressif/v5.5.4/esp-idf"
export IDF_TOOLS_PATH="D:/Espressif"

BUILD_DIR="D:/works/esp-who-master/examples/human_face_recognition/build"
SRC_DIR="D:/works/esp-who-master/examples/human_face_recognition"
TOOLCHAIN="D:/Espressif/espidf/.espressif/v5.5.4/esp-idf/tools/cmake/toolchain-esp32p4.cmake"
CMAKE="C:/Espressif/tools/cmake/3.30.2/bin/cmake.exe"
NINJA="D:/Espressif/tools/ninja/1.12.1/ninja.exe"

# Step 1: Fix the source sdkconfig (add C5 slave target)
SDKCONFIG="$SRC_DIR/sdkconfig"
# Ensure SLAVE_IDF_TARGET_ESP32C5 is set
if ! grep -q "CONFIG_SLAVE_IDF_TARGET_ESP32C5=y" "$SDKCONFIG" 2>/dev/null; then
    # Add slave target config
    cat >> "$SDKCONFIG" << 'EOF'
CONFIG_SLAVE_IDF_TARGET_ESP32C5=y
CONFIG_SLAVE_IDF_TARGET_ARCH_RISCV=y
EOF
fi
# Fix the invalid slave target
sed -i 's/CONFIG_ESP_HOSTED_IDF_SLAVE_TARGET="invalid"/CONFIG_ESP_HOSTED_IDF_SLAVE_TARGET="esp32c5"/' "$SDKCONFIG"

# Step 2: Run cmake
"$CMAKE" --regenerate-during-build \
    -S"$SRC_DIR" \
    -B"$BUILD_DIR" \
    -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN" \
    -DIDF_TARGET=esp32p4 \
    -DSDKCONFIG="$SDKCONFIG"

# Step 3: Fix generated sdkconfig.h (cmake Kconfig may still produce "invalid")
SDKCONFIG_H="$BUILD_DIR/config/sdkconfig.h"
sed -i 's/#define CONFIG_ESP_HOSTED_IDF_SLAVE_TARGET "invalid"/#define CONFIG_SLAVE_IDF_TARGET_ESP32C5 1\n#define CONFIG_ESP_HOSTED_IDF_SLAVE_TARGET "esp32c5"/' "$SDKCONFIG_H"
if ! grep -q "CONFIG_SLAVE_IDF_TARGET_ESP32C5" "$SDKCONFIG_H"; then
    # Insert before the ESP_HOSTED line if not present
    sed -i 's/#define CONFIG_ESP_HOSTED_IDF_SLAVE_TARGET/#define CONFIG_SLAVE_IDF_TARGET_ESP32C5 1\n#define CONFIG_ESP_HOSTED_IDF_SLAVE_TARGET/' "$SDKCONFIG_H"
fi
# Also ensure ESP_HOSTED_SPI_CLK_FREQ is set
if ! grep -q "CONFIG_ESP_HOSTED_SPI_CLK_FREQ" "$SDKCONFIG_H"; then
    echo '#define CONFIG_ESP_HOSTED_SPI_CLK_FREQ 40' >> "$SDKCONFIG_H"
    echo '#define CONFIG_ESP_HOSTED_SPI_FREQ_ESP32XX 40' >> "$SDKCONFIG_H"
fi

# Step 4: Build
"$NINJA" -C "$BUILD_DIR" -j4

echo "Build complete!"
