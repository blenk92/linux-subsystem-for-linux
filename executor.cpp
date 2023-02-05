#include <boost/algorithm/string.hpp>
#include <boost/property_tree/ini_parser.hpp>
#include <boost/property_tree/ptree.hpp>
#include <iostream>
#define _GNU_SOURCE 1
#include <cerrno>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "common.h"

namespace bpt = boost::property_tree;
namespace ba = boost::algorithm;

extern char **env;

int main(int argc, char **argv) {
    // CAP_SYS_CHROOT and CAP_SYS_ADMIN are needed to enter mount namespace
    dropToCapabilities({CAP_SYS_CHROOT, CAP_SYS_ADMIN});

    if (!ba::contains(argv[0], ":") && argc < 3) {
        std::cout << "Usage: " << argv[0] << " container path-to-bin <args...>"
                  << std::endl;
        std::cout << "Alternative: rename or link to ./container:bin <args...>"
                  << std::endl;
        return 0;
    }

    // Determine container and binary
    std::string container;
    std::string binary;
    std::vector<char *> args;
    if (ba::contains(argv[0], ":")) {
        std::vector<std::string> vec;
        ba::split(vec, argv[0], boost::is_any_of(":"));
        container = vec[0];
        binary = vec[1];
        args = std::vector<char *>(argv, argv + argc);
        args[0] = strdup(binary.c_str());
    } else {
        container = argv[1];
        binary = argv[2];
        args = std::vector<char *>(argv + 2, argv + (argc - 2));
    }
    args.push_back(NULL);

    fs::path containerPath = nsMntDir / container;
    if (!fs::exists(containerPath)) {
        std::cerr << "Container " << container << " seems not to be enabled."
                  << std::endl;
        return 1;
    }

    // Parse config file before entering mount namespace
    bpt::ptree pt;
    bpt::ini_parser::read_ini(config, pt);

    char *cwd_cstr = get_current_dir_name();
    fs::path cwd(cwd_cstr + 1);
    free(cwd_cstr);

    // Enter mount namespace of the container
    int fd = open(containerPath.c_str(), O_RDONLY);
    if (fd == -1) {
        std::cerr << "Couldn't open namespace file" << std::endl;
        return 1;
    }
    if (setns(fd, CLONE_NEWNS) != 0) {
        std::cerr << "Couldn't enter namespace: " << strerror(errno)
                  << std::endl;
        return 1;
    }
    close(fd);

    // Get current credentials to be able to drop back after entering the mount
    // namespace
    uid_t ruid, euid, suid;
    if (getresuid(&ruid, &euid, &suid) != 0) {
        throw std::runtime_error("Couldn't get uids");
    }
    gid_t rgid, egid, sgid;
    if (getresgid(&rgid, &egid, &sgid) != 0) {
        throw std::runtime_error("Couldn't get gids");
    }

    // Drop credentials to real gid and uid (because of setuid)
    // This also drops the capabilities if real uid is not root
    if (setregid(rgid, rgid) != 0) {
        std::cerr << "Couldn't set gids" << std::endl;
        return 1;
    }
    if (setreuid(ruid, ruid) != 0) {
        std::cerr << "Couldn't set uids" << std::endl;
        return 1;
    }

    // Parse config file to get paths to search the binary in if the path is not
    // an absolute path
    if (!ba::starts_with(binary, "/")) {
        std::string bins = pt.get<std::string>(container + ".bins");
        std::vector<fs::path> binPaths;
        ba::split(binPaths, bins, boost::is_any_of(";"));
        bool found = false;
        for (auto &path : binPaths) {
            if (fs::is_directory(path)) {
                for (auto &file : fs::directory_iterator(path)) {
                    if (file.path().filename().string() == binary) {
                        binary = file.path();
                        found = true;
                        break;
                    }
                }
                if (found) {
                    break;
                }
            } else {
                if (path.filename().string() == binary) {
                    binary = path;
                    found = true;
                    break;
                }
            }
        }
        if (!found) {
            std::cerr << "Couldn't find " << binary << " in " << container
                      << std::endl;
            return 1;
        }
    }
    std::cout << "Executing " << binary << " in " << container << "\n"
              << std::endl;

    // Adjust PATH environment variable if specified in the config file
    boost::optional<std::string> envPath =
        pt.get_optional<std::string>(container + ".envPath");
    if (envPath != boost::none) {
        setenv("PATH", (*envPath).c_str(), 1);
    }

    // set cwd to current directory
    if (chdir(("/oldRoot" / cwd).c_str()) != 0) {
        std::cout << "Warning: Could not change working directory" << std::endl;
    }
    boost::optional<std::string> interpreter =
        pt.get_optional<std::string>(container + ".interpreter");
    if (interpreter != boost::none) {
        args.front() = strdup(binary.c_str());
        args.insert(args.begin(), strdup(binary.c_str()));
        binary = "/oldRoot";
        binary += *interpreter;
    }
    execv(binary.c_str(), args.data());
}
