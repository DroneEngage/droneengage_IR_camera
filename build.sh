#!/bin/bash

# Default build type is DEBUG
BUILD_TYPE="DEBUG"
ADDITIONAL_DEFINES=""

# Check for RELEASE parameter
if [ "$1" = "RELEASE" ]; then
    BUILD_TYPE="RELEASE"
    shift  # Remove RELEASE from arguments
fi

# Add any additional defines from remaining parameters
for arg in "$@"; do
    ADDITIONAL_DEFINES="$ADDITIONAL_DEFINES -D$arg"
done

rm -rf ./build
mkdir build
cd build
cmake -D CMAKE_BUILD_TYPE=$BUILD_TYPE -D CMAKE_VERBOSE_MAKEFILE:BOOL=ON $ADDITIONAL_DEFINES ../
make

