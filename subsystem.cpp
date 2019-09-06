#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/ini_parser.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/format.hpp>

#include <vector>
#include <iostream>
#include <optional>

#define _GNU_SOURCE 1
#include <cerrno>
#include <sys/stat.h>
#include <fcntl.h>
#include <sched.h>
#include <threads.h>
#include <sys/mount.h>
#include <wait.h>
#include <unistd.h>
#include <sys/syscall.h>

#include "common.h"

namespace bpt = boost::property_tree;
namespace ba = boost::algorithm;

/**
 * Data class that contains infos from the configuration file
 */
class SubsystemConfig {
public:
    SubsystemConfig(const std::string& name, const fs::path& path, const std::vector<std::pair<fs::path, fs::path>>& mntPoints,
                    const std::vector<fs::path>& bins, const std::optional<fs::path>& interpreter)
        : name(name), path(path), mntPoints(mntPoints), bins(bins), interpreter(interpreter) {}

    std::string name;
    fs::path path;
    std::vector<std::pair<fs::path, fs::path>> mntPoints;
    std::vector<fs::path> bins;
    std::optional<fs::path> interpreter;
};

/**
 * Wrapper around C mutexes
 */
class MutexWrapper {
    mtx_t mtx;
public:
    MutexWrapper() {
        mtx_init(&mtx, mtx_plain);
    }

    ~MutexWrapper() {
        mtx_destroy(&mtx);
    }

    void lock() {
        mtx_lock(&mtx);
    }

    void unlock() {
        mtx_unlock(&mtx);
    }
};

/**
 * Mutex to synchronize creationg and mounting of the mount namespace
 */
MutexWrapper mtx;

/**
 * Create a file at the given location.
 * (std::filesystem does not provide a way to do this)
 * @param path Path to the file to be created
 * @return true if file has been created, false otherwise
 */
inline bool createFile(const fs::path& path) {
    int fd = open(path.c_str(), O_CREAT|O_NOCTTY|O_NONBLOCK, 0600);
    if (fd == -1) {
        return false;
    }
    close(fd);
    return true;
}

bool DEBUG;
template <typename M>
void debug(const M& m) {
    if (DEBUG)
        std::cout << "[DEBUG] " << m << std::endl;
}

/**
 * C++ wrapper for mount system call.
 * @return true if mount was successfull, false otherwise.
 */
inline bool mountWrapper(const fs::path& source, const fs::path& target, const char* filesystemtype, unsigned long mountflags, const void* data) {
    auto ret = mount(source.c_str(), target.c_str(), filesystemtype, mountflags, data);
    debug(boost::format("mount(%1%, %2%, ...) = %3%") % source % target % ret);
    return ret == 0;
}

/**
 * Binds the mount namespace of the parent process to nsMntDir/<container-name>
 * (default /tmp/subsys/<container-name>)
 * @param data (std::string*) name of the container
 * @return 0 if the mount namespace has successfully been mounted, 1 otherwise
 */
int childBindMountNamespace(void* data) {
    std::string* container = reinterpret_cast<std::string*>(data);
    fs::path nsMntPath = nsMntDir / *container;

    // Create file to mount ot if not already existing
    if (!fs::exists(nsMntPath)) {
        if (!createFile(nsMntPath)) {
            std::cout << "Couldn't create mount file " << nsMntPath <<" for namespace of " << *container << std::endl;
            return 1;
        }
    }

    fs::path mntNs = fs::path("/proc/") / std::to_string(getppid()) / "ns/mnt";

    // Wait until mount namespace is entered by parent
    mtx.lock();
    if (!mountWrapper(mntNs, nsMntPath, 0, MS_BIND, 0)) {
        std::cerr << "Couldn't create bind mount for mount namespace of " << *container << ": " << strerror(errno) << std::endl;
        return 1;
    }
    mtx.unlock();
    return 0;
}

enum Request : uint_fast8_t {
    START,
    RELINK,
    STOP,
};

inline void usage(char* progName){
    std::cout << "Usage: " << progName << " [start | stop | relink] <--debug>\n";
    std::cout << "\tstart: Start containers (setup namespaces and create symlinks)\n";
    std::cout << "\tstop: Stop containers (reomve links and namespaces)\n";
    std::cout << "\trelink: Recreate Symlinks (use only when already started)\n";
    std::cout << "\n";
    std::cout << "Options:\n";
    std::cout << "\t--debug: Enable some debugging output\n";
}

int main(int argc, char** argv) {
    if (argc != 2 && argc != 3) {
        usage(argv[0]);
        return 0;
    }

    Request request;
    if (strcmp(argv[1], "start") == 0) {
        request = Request::START;
    }
    else if (strcmp(argv[1], "relink") == 0) {
        request = Request::RELINK;
    }
    else if (strcmp(argv[1], "stop") == 0) {
        request = Request::STOP;
    }
    else {
        usage(argv[0]);
        return 1;
    }

    if (argc == 3 && strcmp(argv[2], "--debug") == 0) {
        DEBUG = true;
    }

    // Handle start or relink request
    if (request == Request::START || request == Request::RELINK) {

        // Check that subsystem is not already enabled
        if (fs::exists(nsMntDir) && strcmp(argv[1], "start") == 0) {
            std::cout << "Subsystem seems to be already running. Please call \"" << argv[0] << " stop\" to stop." << std::endl;
            return 1;
        }

        if (!fs::exists(config) || !fs::is_regular_file(config)) {
            std::cerr << "Couldn't find config file at " << config << ". Exiting..." << std::endl;
            return 1;
        }

        // Parse config file
        bpt::ptree pt;
        bpt::ini_parser::read_ini(config, pt);
        std::vector<SubsystemConfig> subsystems;
        for (auto &section : pt) {

            const std::string& name = section.first;
            fs::path path;
            std::vector<std::pair<fs::path, fs::path>> mntPoints;
            std::vector<fs::path> bins;
            std::optional<fs::path> interpreter;

            // Mount /dev and /run by default
            mntPoints.emplace_back("/dev", "/dev");
            mntPoints.emplace_back("/run", "/run");

            for (auto &option : section.second) {
                if (option.first == "path") {
                    path = option.second.get_value<fs::path>();
                    if (!fs::exists(path) || !fs::is_directory(path)) {
                        std::cerr << path << " is not a path to a directory. Ignoring " << name << std::endl;
                        continue;
                    }
                } else if (option.first == "mnt") {
                    std::string v = option.second.get_value<std::string>();
                    std::vector<std::string> mntPointsTemp;
                    ba::split(mntPointsTemp, v, boost::is_any_of(";"));
                    for (auto &mntPoint : mntPointsTemp) {
                        std::vector<fs::path> mntPaths;
                        ba::split(mntPaths, mntPoint, boost::is_any_of(":"));
                        std::pair<fs::path, fs::path> mntInfo;
                        if (mntPaths.size() > 1) {
                            mntInfo = {mntPaths[0], mntPaths[1]};
                        } else {
                            mntInfo = {mntPaths[0], mntPaths[0]};
                        }
                        if (!fs::exists(mntPaths[0])) {
                            std::cerr << "File " << mntPaths[0] << "couldn't be found. Ignoring mount..." << std::endl;
                            continue;
                        }
                        mntPoints.push_back(mntInfo);
                    }
                } else if (option.first == "bins") {
                    std::string v = option.second.get_value<std::string>();
                    ba::split(bins, v, boost::is_any_of(";"));
                } else if (option.first == "interpreter") {
                    interpreter = option.second.get_value<std::string>();
                }
            }
            subsystems.emplace_back(name, path, mntPoints, bins, interpreter);
        }

        // If start request --> mount namespace and appropriate mounts need to be performed.
        if (request == Request::START) {

            // Create mount directory if not existing
            if (!fs::exists(nsMntDir)) {
                if (!fs::create_directory(nsMntDir) || !mountWrapper(nsMntDir, nsMntDir, 0, MS_BIND|MS_REC, 0)) {
                    std::cerr << "Couldn't create tmp directory at " << nsMntDir << std::endl;
                    return 1;
                }
            }
            if (!mountWrapper("", nsMntDir, 0, MS_PRIVATE, 0)) {
                std::cerr << "Couldn't make tmp directory at " << nsMntDir << " private." << std::endl;
            }

            // Create mount namespace and perform mounts for each configured container
            for (auto &subsystem : subsystems) {

                debug("");
                debug(subsystem.name);

                // Copy interpreter to new rootfs
                if (subsystem.interpreter.value_or("") != "") {
                    fs::path interpreter = subsystem.interpreter.value();
                    fs::path target = subsystem.path.string() + (subsystem.bins.front() / interpreter.filename()).string();
                    if (fs::exists(target)) {
                        fs::remove(target);
                    }

                    fs::copy_file(interpreter, target);
                }

                // Perform the creation of the mount namespace and mounts in a child process to keep the
                // main process within the current mount namespace
                pid_t child = fork();
                if (child == 0) {
                    // Create mount namespace and bind mount it to keep it alive after the child exits.
                    char *stk = new char[4096];
                    mtx.lock();
                    // Use clone directly to create a child process with different pid but same virtual memory
                    clone(childBindMountNamespace, stk + 4096, CLONE_VM | SIGCHLD, &(subsystem.name));
                    if (unshare(CLONE_NEWNS) != 0) {
                        std::cerr << "Couldn't create mount namespace: " << strerror(errno) << ". Exiting..." << std::endl;
                        return 1;
                    }
                    mtx.unlock();
                    wait(NULL);
                    delete[] stk;

                    // Configure all mount points of the new mount namespace as slaves to not propagate the following mounts
                    if (!mountWrapper("", "/", 0, MS_SLAVE | MS_REC, 0)) {
                        std::cerr << "Couldn't set root mount as slave" << std::endl;
                        return 1;
                    }

                    // Bind mount the directory containing the new root filesystem to itself to enable usage of pivot_root
                    if (!mountWrapper(subsystem.path, subsystem.path, 0, MS_BIND, 0)) {
                        std::cerr << "Couldn't bind mount new root" << std::endl;
                        return 1;
                    }

                    // Perform mounts specified in the config file
                    for (auto &mnt : subsystem.mntPoints) {
                        std::string tmpPath = mnt.second;
                        if (ba::starts_with(mnt.second.string(), "/")) {
                            tmpPath.erase(0, 1);
                        }

                        fs::path mntPoint = subsystem.path / tmpPath;
                        fs::remove_all(mntPoint);
                        if (!fs::exists(mntPoint)) {
                            if (fs::is_directory(mnt.first)) {
                                fs::create_directories(mntPoint);
                            } else {
                                createFile(mntPoint);
                            }
                        }
                        if (!mountWrapper(mnt.first, mntPoint, 0, MS_BIND, 0)) {
                            std::cerr << "Failed to bind mount " << mnt.first << " into " << subsystem.name
                                      << std::endl;
                        }
                    }

                    // Fix permissions of the /run mount
                    for (auto & p : fs::directory_iterator("/run/user")) {
                        fs::path userPath = subsystem.path / "run/user" / p.path().filename();
                        if(!mountWrapper(p.path(), userPath, 0, MS_BIND, 0)) {
                            std::cout << "Could not bind mount " << p << " onto " << userPath << std::endl;
                            return 1;
                        }
                    }

                    // Mount procfs
                    if (!mountWrapper("", (subsystem.path / "proc"), "proc", 0, 0)){
                        std::cout << "Couldn't mount procfs" << std::endl;
                        return 1;
                    }

                    // Mount sysfs
                    if (!mountWrapper("", (subsystem.path / "sys"), "sysfs", 0, 0)){
                        std::cout << "Couldn't mount sysfs" << std::endl;
                        return 1;
                    }

                    // Mount additional virtual filesystem within the /dev directory
                    if (!mountWrapper("", (subsystem.path / "dev/pts"), "devpts", 0, 0)) {
                        std::cout << "Couldn't mount pts" << std::endl;
                        return 1;
                    }

                    if (!mountWrapper("", (subsystem.path / "dev/shm"), "tmpfs", 0, 0)) {
                        std::cout << "Couldn't mount shm" << std::endl;
                        return 1;
                    }

                    if (!mountWrapper("", (subsystem.path / "dev/mqueue"), "mqueue", 0, 0)) {
                        std::cout << "Couldn't mount mqueue" << std::endl;
                        return 1;
                    }

                    if (!mountWrapper("", (subsystem.path / "dev/hugepages"), "hugetlbfs", 0, 0)) {
                        std::cout << "Couldn't mount hugepages" << std::endl;
                        return 1;
                    }

                    // pivot_root into the new root filesystem and put old root into /oldRoot
                    fs::path putOldRoot = subsystem.path / "oldRoot";
                    if (!fs::exists(putOldRoot)) {
                        fs::create_directory(putOldRoot);
                    }
                    syscall(SYS_pivot_root, subsystem.path.c_str(), putOldRoot.c_str());
                    return 0;
                }
                wait(NULL);
            }
        }

        if (fs::is_directory(linksDir))
            fs::remove_all(linksDir);
        fs::create_directory(linksDir);

        // Create links to executables of the containers
        for (auto &subsystem : subsystems) {
            for (auto & binPath : subsystem.bins) {
                // Construct path within the container
                fs::path absBinPath;
                {
                    std::string tmp = binPath.string();
                    tmp.erase(0, 1);
                    absBinPath = subsystem.path / tmp;
                }

                // If the path points to a directory create links for all contained files
                if (fs::is_directory(absBinPath)) {
                    for (auto & path : fs::directory_iterator(absBinPath)) {
                        fs::path linkName = linksDir / (subsystem.name + ":" + path.path().filename().string());
                        if (!fs::is_symlink(linkName)) {
                            fs::create_symlink(executorPath, linkName);
                        }
                    }
                }
                else {
                    fs::path linkName = linksDir / (subsystem.name + ":" + binPath.filename().string());
                    if (!fs::is_symlink(linkName)) {
                        fs::create_symlink(executorPath, linkName);
                    }
                }
            }
        }
    }
    // Handle stop request --> remove bind mounts of mount namespaces and remove all links
    else if (request == Request::STOP)
    {
        if (fs::exists(nsMntDir)) {
            for (auto &p: fs::directory_iterator(nsMntDir)) {
                if (umount2(p.path().c_str(), 0) != 0) {
                    std::cerr << "Couldn't unmount " << p << ". Manual unmount required?" << std::endl;
                }
            }
            if (umount2(nsMntDir.c_str(), 0) != 0) {
                std::cerr << "Couldn't unmount " << nsMntDir << std::endl;
            }
            fs::remove_all(nsMntDir);
        }
        fs::remove_all(linksDir);
    }

    return 0;
}
