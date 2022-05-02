all: wrapper

wrapper: main.cpp
	g++ -g -o wrapper main.cpp -Inlohmann_json_v3.10.5/

clean:
	rm -f wrapper

install: wrapper
	mv wrapper ~/.local/bin
