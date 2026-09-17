# ESP-IDF project definition for the BLE <-> ATmega16M1 tuning gateway.
# Build with: idf.py set-target esp32 && idf.py build flash monitor

cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(tune_gateway)
