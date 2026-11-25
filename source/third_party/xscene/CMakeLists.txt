cmake_minimum_required(VERSION 3.25)

project(xscene LANGUAGES CXX)

# xscene: reads .scene.json files ("format": "mgs-scene") into plain data.
# Standard library only: the game connects its own json library through
# xscene::json_value, and turns the transforms into matrices itself.
ADD_LIBRARY(xscene STATIC
            ${CMAKE_CURRENT_LIST_DIR}/include/xscene.h
            ${CMAKE_CURRENT_LIST_DIR}/src/xscene.cpp)
TARGET_INCLUDE_DIRECTORIES(xscene PUBLIC ${CMAKE_CURRENT_LIST_DIR}/include)
TARGET_COMPILE_FEATURES(xscene PUBLIC cxx_std_20)

SET_TARGET_PROPERTIES(xscene PROPERTIES FOLDER "third-party")
