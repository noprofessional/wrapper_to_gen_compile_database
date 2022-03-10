all: wrapper

wrapper: main.cpp
	g++ -o wrapper main.cpp -I nlohmann_json_v3.10.5/

clean:
	rm -f wrapper

install: wrapper
	mv wrapper ~/.local/bin
