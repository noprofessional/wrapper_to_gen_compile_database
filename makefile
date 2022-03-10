all: wrapper

wrapper: main.cpp
	g++ -o wrapper main.cpp

clean:
	rm -f wrapper

install: wrapper
	mv wrapper ~/.local/bin
