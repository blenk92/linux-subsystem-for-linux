#include <iostream>
#include <boost/algorithm/string.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/ini_parser.hpp>
#define _GNU_SOURCE 1
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <cerrno>

#include "common.h"

namespace bpt = boost::property_tree;
namespace ba = boost::algorithm;

extern char** env;

int main (int argc, char** argv) {
    if (!ba::contains(argv[0], ":") && argc < 3) {
        std::cout << "Usage: " << argv[0] << " container path-to-bin <args...>" << std::endl;
        std::cout << "Alternative: rename or link to ./container:bin <args...>" << std::endl;
        return 0;
    }

    // Determine container and binary
    std::string container;
    std::string binary;
    char ** args = nullptr;
    if (ba::contains(argv[0], ":")) {
        std::vector<std::string> vec;
        ba::split(vec, argv[0], boost::is_any_of(":"));
        container = vec[0];
        binary = vec[1];
        args = argv;
        args[0] = strdup(binary.c_str());
    }
    else {
        container = argv[1];
        binary = argv[2];
        args = argv + 2;
    }
    fs::path containerPath = nsMntDir / container;
    if (!fs::exists(containerPath)) {
        std::cerr << "Container " << container << " seems not to be enabled." << std::endl;
        return 1;
    }

    // Get current credentials to be able to drop back after entering the mount namespace
    uid_t ruid, euid, suid;
    if (getresuid(&ruid, &euid, &suid) != 0) {
        std::cerr << "Couldn't get uids" << std::endl;
        return 1;
    }
    gid_t rgid, egid, sgid;
    if (getresgid(&rgid, &egid, &sgid) != 0) {
        std::cerr << "Couldn't get gids" << std::endl;
        return 1;
    }

    // Enter mount namepsace of the container
    int fd = open(containerPath.c_str(), O_RDONLY);
    if (fd == -1) {
        std::cerr << "Couldn't open namespace file" << std::endl;
        return 1;
    }
    if (setns(fd, CLONE_NEWNS) != 0) {
        std::cerr << "Couldn't enter namespace: " << strerror(errno) << std::endl;
        return 1;
    }
    close(fd);

    // Drop credentials to real gid and uid (because of setuid)
    if (setresgid(rgid, rgid, rgid) != 0) {
        std::cerr << "Couldn't set gids" << std::endl;
        return 1;
    }
    if (setresuid(ruid, ruid, ruid) != 0) {
        std::cerr << "Couldn't set uids" << std::endl;
        return 1;
    }

    bpt::ptree pt;
    bpt::ini_parser::read_ini(config, pt);

    // Parse config file to get paths to search the binary in if the path is not an absolute path
    if (!ba::starts_with(binary, "/")) {
        std::string bins = pt.get<std::string>(container + ".bins");
        std::vector<fs::path> binPaths;
        ba::split(binPaths, bins, boost::is_any_of(";"));
        bool found = false;
        for (auto & path : binPaths) {
            if (fs::is_directory(path)) {
                for (auto & file : fs::directory_iterator(path)) {
                    if (file.path().filename().string() == binary) {
                        binary = file.path();
                        found = true;
                        break;
                    }
                }
                if (found) {
                    break;
                }
            }
            else {
                if (path.filename().string() == binary) {
                    binary = path;
                    found = true;
                    break;
                }
            }
        }
        if (!found) {
            std::cerr << "Couldn't find " << binary << " in " << container << std::endl;
            return 1;
        }
    }
    std::cout << "Executing " << binary << " in " << container << "\n" << std::endl;

    // Adjust PATH environment variable if specified in the config file
    boost::optional<std::string> envPath = pt.get_optional<std::string>(container + ".envPath");
    if (envPath != boost::none) {
        setenv("PATH",(*envPath).c_str(), 1);
    }

    execv(binary.c_str(), args);
}