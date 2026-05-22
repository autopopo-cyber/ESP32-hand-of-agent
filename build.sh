#!/bin/bash
# ESP-IDF build environment setup for MSYS2
export IDF_PATH=G:/esp-idf
export ESP_IDF_VERSION=5.4.2
export IDF_PYTHON_ENV_PATH="C:/Users/Administrator/.espressif/python_env/idf5.4_py3.12_env"
export IDF_TOOLS_PATH="C:/Users/Administrator/.espressif"

# Proxy settings for component downloads
export HTTPS_PROXY="http://127.0.0.1:10806"
export HTTP_PROXY="http://127.0.0.1:10806"

PROJECT_DIR="$(dirname "$0")"
ASSETS_DIR="$PROJECT_DIR/assets"
MAIN_DIR="$PROJECT_DIR/main"
SPLASH_H="$MAIN_DIR/splash.h"
STORAGE_BIN="$PROJECT_DIR/storage.bin"
PYTHON="$IDF_PYTHON_ENV_PATH/Scripts/python.exe"

# Step 1: Generate built-in splash.h (random pick from assets, for fallback)
if [ -d "$ASSETS_DIR" ]; then
    PNG=$(find "$ASSETS_DIR" -name "*.png" -maxdepth 1 | shuf -n 1)
    if [ -n "$PNG" ]; then
        echo "Splash image: $(basename "$PNG")"
        python "$PROJECT_DIR/tools/png_to_rgb565.py" "$PNG" "$MAIN_DIR"
        GEN_H="$MAIN_DIR/$(basename "${PNG%.png}").h"
        if [ -f "$GEN_H" ] && [ "$GEN_H" != "$SPLASH_H" ]; then
            mv "$GEN_H" "$SPLASH_H"
        fi
    elif [ ! -f "$SPLASH_H" ]; then
        echo "ERROR: No PNGs in assets/ and no splash.h — run png_to_rgb565.py manually"
        exit 1
    fi
fi

# Step 2: Generate storage.bin (all images for on-device random display)
if [ -d "$ASSETS_DIR" ]; then
    echo "Generating storage.bin from assets/..."
    python "$PROJECT_DIR/tools/prep_images.py" "$ASSETS_DIR" "$STORAGE_BIN"
fi

# Step 3: Build and/or flash
if echo "$*" | grep -q "flash"; then
    PORT="${ESPPORT:-COM9}"
    MONITOR=0
    for arg in "$@"; do
        case "$arg" in
            -p) PORT="" ;;
            monitor) MONITOR=1 ;;
            flash) ;;
            *)
                if [ -z "$PORT" ]; then
                    PORT="$arg"
                fi
                ;;
        esac
    done

    # Build first
    "$PYTHON" "$IDF_PATH/tools/idf.py" build || exit $?

    # Flash everything in one esptool call
    echo "Flashing to $PORT..."
    FLASH_FILES="0x0 build/bootloader/bootloader.bin \
        0x8000 build/partition_table/partition-table.bin \
        0x10000 build/oda_hid.bin"

    if [ -f "$STORAGE_BIN" ]; then
        STORAGE_SIZE=$(wc -c < "$STORAGE_BIN" | tr -d ' ')
        echo "Storage image: $STORAGE_BIN ($STORAGE_SIZE bytes)"
        FLASH_FILES="$FLASH_FILES 0x310000 $STORAGE_BIN"
    fi

    "$PYTHON" -m esptool \
        --chip esp32s3 \
        --port "$PORT" \
        --baud 460800 \
        --before default_reset \
        --after hard_reset \
        write_flash --flash_mode dio --flash_freq 80m --flash_size 16MB \
        $FLASH_FILES

    if [ "$MONITOR" = "1" ]; then
        sleep 1
        "$PYTHON" "$IDF_PATH/tools/idf.py" -p "$PORT" monitor
    fi
else
    "$PYTHON" "$IDF_PATH/tools/idf.py" "$@"
fi
