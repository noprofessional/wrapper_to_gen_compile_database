#include <assert.h>
#include <libgen.h>
#include <limits.h>
#include <pwd.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include <fstream>
#include <iostream>
#include <sstream>

#include "pstream.h"

using namespace std;

int exec(const char* cmd)
{
    redi::ipstream proc(cmd, redi::pstreams::pstdout | redi::pstreams::pstderr);
    std::string line;
    // read child's stdout
    while (std::getline(proc.out(), line))
        std::cout << line << '\n';
    // if reading stdout stopped at EOF then reset the state:
    if (proc.eof() && proc.fail())
        proc.clear();
    // read child's stderr
    while (std::getline(proc.err(), line))
        std::cerr << line << '\n';
    return proc.close();
}

int main(int argc, char** argv)
{
    char* debug = getenv("WRAPPER_DEBUG");
    bool needDebug = (!debug || strcmp(debug, "1") != 0);
    if (needDebug)
        cout.setstate(std::ios::failbit);

    // get cwd
    char cwd[PATH_MAX];
    if (getcwd(cwd, sizeof(cwd)) == NULL)
        return 1;
    cout << "pwd:" << cwd << endl;

    // get $HOME
    struct passwd* pw = getpwuid(getuid());
    const char* homedir = pw->pw_dir;
    cout << "home:" << homedir << endl;

    // recursive find compile_commands.json
    char dirpath[PATH_MAX];
    strcpy(dirpath, cwd);
    string path;
    ifstream reader;
    do
    {
        path = dirpath;
        path += "/compile_commands.json";
        cout << "try path:" << path << endl;
        ifstream fs(path);
        if (fs)
        {
            swap(reader, fs);
            break;
        }
    } while (strcmp(dirname(dirpath), homedir) != 0);

    if (!reader.is_open())
    {
        cerr << "missing compile_commands.json" << endl;
    }
    cout << "json path:" << path << endl;

    // get filename
    // get command
    string file;
    string allcommand = "/usr/bin/";
    for (int i = 0; i < argc; ++i)
    {
        char* suffix_c = strstr(argv[i], ".c");
        char* suffix_cc = strstr(argv[i], ".cc");
        char* suffix_cpp = strstr(argv[i], ".cpp");
        if ((suffix_c && suffix_c[2] == '\0') ||
            (suffix_cc && suffix_cc[3] == '\0') ||
            (suffix_cpp && suffix_cpp[4] == '\0'))
        {
            file = argv[i];
        }

        allcommand += argv[i];
        allcommand += " ";
    }

    cout << "c/c++ source file:" << file << endl;

    string jsoncommand = allcommand;
    cout << "json command:" << jsoncommand << endl;

    // check redirects
    char outname[256];
    ssize_t rval;
    rval = readlink("/proc/self/fd/1", outname, sizeof(outname));
    outname[rval] = '\0';
    cout << "stdout:" << outname << endl;
    if (strcmp(outname, "/dev/null") == 0)
    {
        allcommand += " 1>/dev/null";
    }

    char errname[256];
    rval = readlink("/proc/self/fd/2", errname, sizeof(errname));
    errname[rval] = '\0';
    cout << "stderr:" << errname << endl;
    if (strcmp(errname, "/dev/null") == 0)
    {
        allcommand += " 2>/dev/null";
    }
    cout << "all command:" << allcommand << endl;

    // run ori command
    cout.clear();
    int res = exec(allcommand.c_str());
    if (needDebug)
        cout.setstate(std::ios::failbit);
    cout << "status:" << res << endl;

    if (res == 0 && file.size() && reader.is_open())
    {
        // record json
        std::stringstream buffer;
        buffer << reader.rdbuf();
        string content = buffer.str();
        reader.close();

        ofstream writer(path);
        assert(writer);

        if (content.empty())
        {
            content += "[";
        }
        else
        {
            assert(content[content.length() - 1] == ']');
            content.pop_back();
            assert(content[content.length() - 1] == '}');
            content += ",";
        }

        content += "{\"directory\":\"";
        content += cwd;
        content += "\",\"command\":\"";
        content += jsoncommand;
        content += "\",\"file\":\"";
        content += file;
        content += "\"}]";
        writer << content;
        writer.flush();
        writer.close();
    }
    cout.clear();

    if (res != 0)
        return 1;
    else
        return 0;
}
