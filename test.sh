#!/bin/bash
set -e
make clean
make

cp wrapper g++
OLD_PATH=$PATH
export PATH=.:$PATH

rm -f example.cpp
echo "#include <cstdio>" >> example.cpp
echo "int main(int argc, char**argv)">> example.cpp
echo "{">> example.cpp
echo "printf(\"hello world.\");">> example.cpp
echo "return 0;">> example.cpp
echo "}">> example.cpp
touch compile_commands.json
g++ -o example example.cpp

export PATH=$OLD_PATH
rm -f g++
rm -f example.cpp
rm -f example
