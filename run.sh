#!/bin/bash
set -e
cd "$(dirname "$0")"
cmake --build build
./build/minicraft
