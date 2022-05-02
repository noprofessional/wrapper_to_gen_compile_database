#!/bin/bash
set -e
make clean
make

cp wrapper g++
OLD_PATH=$PATH
export PATH=.:$PATH

rm -f example.cpp
rm -f example.cpp
rm -f func1.h
rm -f func1.cpp
rm -f example

# example.cpp
echo "#include <cstdio>" >> example.cpp
echo "#include \"func1.h\"" >> example.cpp
echo "int main(int argc, char**argv)">> example.cpp
echo "{">> example.cpp
echo "printf(\"hello world.\");">> example.cpp
echo "return 0;">> example.cpp
echo "}">> example.cpp

# func1.h
echo "void fun1();" >> func1.h

# func1.cpp
echo "#include <cstdio>" >> func1.cpp
echo "void fun1(){" >> func1.cpp
echo "printf(\"hello again.\");" >> func1.cpp
echo "}" >> func1.cpp

touch compile_commands.json
g++ -o example example.cpp func1.cpp
rm -f wrapper
make

export PATH=$OLD_PATH
rm -f g++
rm -f example.cpp
rm -f example.cpp
rm -f func1.h
rm -f func1.cpp
rm -f example
