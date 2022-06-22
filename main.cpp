#include <assert.h>
#include <fcntl.h>
#include <libgen.h>
#include <limits.h>
#include <pwd.h>
#include <semaphore.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <fstream>
#include <iostream>
#include <json.hpp>
#include <sstream>

#include "pstream.h"

using namespace std;
using nlohmann::json;

struct RAIIForSemaphoreCreate
{
    RAIIForSemaphoreCreate(const char* name) : m_name(name)
    {
        m_ori = sem_open(m_name.c_str(), O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP, 1);
    }

    ~RAIIForSemaphoreCreate()
    {
        if (m_ori)
            sem_unlink(m_name.c_str());
    }

    RAIIForSemaphoreCreate(RAIIForSemaphoreCreate&) = delete;
    RAIIForSemaphoreCreate& operator=(const RAIIForSemaphoreCreate&) = delete;

    string m_name;
    sem_t* m_ori;
};

struct RAIIForSemaphoreWait
{
    RAIIForSemaphoreWait(RAIIForSemaphoreCreate& semCreated) : m_semCreated(semCreated)
    {
        assert(m_semCreated.m_ori);
        sem_wait(m_semCreated.m_ori);
    }

    ~RAIIForSemaphoreWait()
    {
        assert(m_semCreated.m_ori);
        sem_post(m_semCreated.m_ori);
    }

    RAIIForSemaphoreWait(RAIIForSemaphoreWait&) = delete;
    RAIIForSemaphoreWait& operator=(const RAIIForSemaphoreWait&) = delete;

    RAIIForSemaphoreCreate& m_semCreated;
};

// TODO close on exec stream
class LogStream : public std::ostringstream
{
    static std::string m_path;
    static int m_fd;
    static RAIIForSemaphoreCreate* m_sem;

public:
    LogStream() {}

    ~LogStream()
    {
        // write to file
        assert(m_fd);
        const string& log = str();
        if (log.back() != '\n')
            (*this) << '\n';

        RAIIForSemaphoreWait scope_lock(*m_sem);
        ::write(m_fd, str().c_str(), str().size());
    }

    static bool init(const std::string& path, RAIIForSemaphoreCreate* logsem)
    {
        m_path = path;
        int res = open(m_path.c_str(), O_CLOEXEC | O_CREAT | O_APPEND | O_WRONLY, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);
        if (res < 0)
        {
            cerr << " open with err:" << strerror(errno) << endl;
            return false;
        }

        m_sem = logsem;
        m_fd = res;
        return true;
    }

    static void syncLog()
    {
        ::fsync(m_fd);
        ::close(m_fd);
        m_fd = 0;
    }
};

int LogStream::m_fd = 0;
string LogStream::m_path;
RAIIForSemaphoreCreate* LogStream::m_sem = nullptr;

struct RAIIForceSync
{
    RAIIForceSync() {}
    ~RAIIForceSync()
    {
        LogStream::syncLog();
    }
};

#define INIT_LOG(x) assert(LogStream::init(x))
#define LOG() LogStream()
#define ENSURE_SYNC_LOG() RAIIForceSync temp;

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

enum ErrCode
{
    E_OK = 0,
    E_NOC1PLUS = 1,
    E_EMPTYCOMMANS = 2,
    E_ENOSOURCEFILE = 3,
    E_JSON_NOT_VALID = 4,
    E_NO_JSON_FILE = 5,
    E_SEM_OPEN_FAILED = 6,
};

vector<string> suffixes{
    ".cpp",
    ".cc",
    ".c",
};

bool isSourceFile(char* arg)
{
    string temp(arg);

    // find source file
    for (int i = 0; i < suffixes.size(); ++i)
    {
        const string& suffix = suffixes[i];
        auto suffixpos = temp.find(suffix);
        if (suffixpos == string::npos)
            continue;
        else
            return true;
    }
    return false;
}

int runParent(int argc, char** argv)
{
    // try open semaphore first
    RAIIForSemaphoreCreate semCreated("/liuzheng_wrapper_only");
    if (!semCreated.m_ori)
    {
        LOG() << " create sem failed with err:" << strerror(errno);
        return E_SEM_OPEN_FAILED;
    }

    // get cwd
    char cwd[PATH_MAX];
    if (getcwd(cwd, sizeof(cwd)) == NULL)
        return 1;
    LOG() << "cwd:" << cwd << endl;

    // get $HOME
    struct passwd* pw = getpwuid(getuid());
    const char* homedir = pw->pw_dir;
    LOG() << "home:" << homedir << endl;

    // recursive find compile_commands.json until $HOME or root
    char dirpath[PATH_MAX];
    strcpy(dirpath, cwd);
    string path;
    ifstream reader;
    do
    {
        path = dirpath;
        path += "/compile_commands.json";
        LOG() << "try path:" << path << endl;
        ifstream fs(path);
        if (fs)
        {
            swap(reader, fs);
            break;
        }
    } while (strcmp(dirname(dirpath), homedir) != 0 && strcmp(dirpath, "/") != 0);

    if (!reader.is_open())
    {
        LOG() << "missing compile_commands.json" << endl;
        return E_NO_JSON_FILE;
    }

    LOG() << "json path:" << path << endl;

    // I don't think ifstream constructor actually read the file
    // so the lock operation can be delay to right before operator>>
    RAIIForSemaphoreWait scope_lock(semCreated);

    // get current json
    json root;
    try
    {
        reader >> root;
    }
    catch (const std::exception& ex)
    {
        LOG() << "not valid file(" << ex.what() << "). will be overwrite.";
        reader.close();
    }

    if (!root.is_null() && !root.is_array())
    {
        LOG() << "root not empty and not array." << endl;
        return E_JSON_NOT_VALID;
    }

    // multiple source use same command
    string command;
    vector<pair<size_t, string>> sourceFiles;
    for (int i = 0; i < argc; ++i)
    {
        if (isSourceFile(argv[i]))
        {
            sourceFiles.push_back(make_pair(command.size(), argv[i]));
            command += string(strlen(argv[i]), ' ');
        }
        else
        {
            command += argv[i];
        }
        command += " ";
    }

    size_t lastPos = 0;
    const string* lastFile = nullptr;
    for (int i = 0; i < sourceFiles.size(); ++i)
    {
        size_t pos = sourceFiles[i].first;
        const string& sourceFile = sourceFiles[i].second;
        command.replace(pos, sourceFile.size(), sourceFile);
        if (lastFile)
            command.replace(lastPos, lastFile->size(), string(lastFile->size(), ' '));
        lastPos = pos;
        lastFile = &sourceFile;

        bool found = false;
        for (auto& jsonEle : root)
        {
            if (!jsonEle.contains("file") || !jsonEle["file"].is_string())
            {
                LOG() << "has invalid ele:" << jsonEle << endl;
                return 1;
            }

            // always replace
            if (jsonEle["file"].get<string>() == sourceFile)
            {
                found = true;
                jsonEle["directory"] = cwd;
                jsonEle["command"] = command;
                break;
            }
        }

        if (!found)
        {
            json new_command_json;
            new_command_json["file"] = sourceFile;
            new_command_json["directory"] = cwd;
            new_command_json["command"] = command;
            root.push_back(new_command_json);
        }
    }

    ofstream writer(path);
    assert(writer);
    writer << setw(4) << root;
    writer.flush();
    writer.close();

    return 0;
}

int main(int argc, char** argv)
{
    RAIIForSemaphoreCreate logSem("/liuzheng_log_only");
    if(!logSem.m_ori)
    {
        cerr << "log semaphore create failed:" << strerror(errno) << endl;
        return 1;
    }

    LogStream::init("test.log", &logSem);

    char* oriarg0 = argv[0];
    char exepath[PATH_MAX];
    const char* prefix = "/usr/bin/";
    strcpy(exepath, prefix);
    strcpy(exepath + strlen(prefix), argv[0]);
    argv[0] = exepath;
    LOG() << "arg0:" << argv[0];

    // fork and run original command
    // give stdout and stderr to child process
    int pid = fork();
    if (pid < 0)
    {
        LOG() << "fork failed. with err:" << strerror(errno) << endl;
        return 1;
    }
    else if (pid == 0)
    {
        // run original command with stdout and stderr owned
        int res = execvp(argv[0], &argv[0]);
        // only return if error happened
        // directly exit DO NOT RUN CODE AFTER THIS
        exit(res);
    }

    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);

    int res = runParent(argc, argv);
    if (res != 0)
    {
        LOG() << "record command failed with res:" << res;
    }

    ENSURE_SYNC_LOG();
    // wait for child end
    // child error return negative error
    int status;
    if (waitpid(pid, &status, 0) == -1)
    {
        // handle error
        LOG() << "wait pid err:" << errno;
        return -errno;
    }
    else
    {
        if (WIFEXITED(status))
        {
            LOG() << "child exit with code:" << WEXITSTATUS(status);
            return 0;
        }
        else
        {
            LOG() << "child other status change:" << status;
            return -1;
        }
    }
}
