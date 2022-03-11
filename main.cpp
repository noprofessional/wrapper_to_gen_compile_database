#include <assert.h>
#include <libgen.h>
#include <limits.h>
#include <pwd.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include <fstream>
#include <iostream>
#include <json.hpp>
#include <sstream>

#include "pstream.h"

using namespace std;
using nlohmann::json;

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

    char* replace = getenv("REPLACE_COMMAND");
    bool noNeedReplace = (!replace || strcmp(replace, "1") != 0);
    cout << "noNeedReplace:" << noNeedReplace << endl;

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
        // get current json
        json root;
        try
        {
            reader >> root;
        }
        catch (const std::exception& ex)
        {
            cerr << "not valid file(" << ex.what() << "). will be overwrite." << endl;
            reader.close();
        }

        if (!root.is_null() && !root.is_array())
        {
            cerr << "root not empty and not array." << endl;
            cout.clear();
            return 1;
        }

        bool found = false;
        for (auto& jsonEle : root)
        {
            if (!jsonEle.contains("file") || !jsonEle["file"].is_string())
            {
                cerr << "has invalid ele:" << jsonEle << endl;
                cout.clear();
                return 1;
            }

            if (jsonEle["file"].get<string>() == file)
            {
                found = false;
                if (noNeedReplace)
                {
                    jsonEle["directory"] = cwd;
                    jsonEle["command"] = jsoncommand;
                }
                else
                {
                    cout << "file:" << file << "already has commands." << endl;
                    cout.clear();
                    return 0;
                }
            }
        }

        if (!found)
        {
            json new_command_json;
            new_command_json["file"] = file;
            new_command_json["directory"] = cwd;
            new_command_json["command"] = jsoncommand;
            root.push_back(new_command_json);
        }

        ofstream writer(path);
        assert(writer);
        writer << root;
        writer.flush();
        writer.close();
    }
    cout.clear();

    if (res != 0)
        return 1;
    else
        return 0;
}
