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

# Build
"$IDF_PYTHON_ENV_PATH/Scripts/python.exe" "$IDF_PATH/tools/idf.py" "$@"
