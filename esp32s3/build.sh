#!/bin/bash
# ESP-IDF build environment setup for MSYS2
export IDF_PATH=G:/esp-idf
export ESP_IDF_VERSION=5.4.2
export IDF_PYTHON_ENV_PATH="C:/Users/Administrator/.espressif/python_env/idf5.4_py3.14_env"
export IDF_TOOLS_PATH="C:/Users/Administrator/.espressif"

# Proxy settings for component downloads
export HTTPS_PROXY="http://127.0.0.1:10806"
export HTTP_PROXY="http://127.0.0.1:10806"

export ESPPORT="${ESPPORT:-}"

# Randomly select a splash image from assets/
ASSETS_DIR="$(dirname "$0")/assets"
MAIN_DIR="$(dirname "$0")/main"
SPLASH_H="$MAIN_DIR/splash.h"
if [ -d "$ASSETS_DIR" ]; then
    PNG=$(find "$ASSETS_DIR" -name "*.png" -maxdepth 1 | shuf -n 1)
    if [ -n "$PNG" ]; then
        echo "Splash image: $(basename "$PNG")"
        python "$(dirname "$0")/tools/png_to_rgb565.py" "$PNG" "$MAIN_DIR"
        # Rename output to splash.h (script outputs {input_name}.h)
        GEN_H="$MAIN_DIR/$(basename "${PNG%.png}").h"
        if [ -f "$GEN_H" ] && [ "$GEN_H" != "$SPLASH_H" ]; then
            mv "$GEN_H" "$SPLASH_H"
        fi
    elif [ ! -f "$SPLASH_H" ]; then
        echo "ERROR: No PNGs in assets/ and no splash.h — run png_to_rgb565.py manually"
        exit 1
    fi
fi

# Build
"$IDF_PYTHON_ENV_PATH/Scripts/python.exe" "$IDF_PATH/tools/idf.py" "$@"
