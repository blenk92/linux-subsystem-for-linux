#include <stdexcept>

#include "common.h"

fs::path nsMntDir(MNTDIR);
fs::path linksDir(LINKSDIR);
fs::path executorPath(EXECUTORPATH);
fs::path config(CONFIGPATH);

void dropToCapabilities(const std::vector<cap_value_t>& cap_list) {
    cap_t newCaps = cap_init();
    if (cap_set_flag(newCaps, CAP_EFFECTIVE, cap_list.size(), cap_list.data(), CAP_SET) == -1) {
        throw std::runtime_error("Couldn't set effective capabilites");
    }
    if (cap_set_flag(newCaps, CAP_PERMITTED, cap_list.size(), cap_list.data(), CAP_SET) == -1) {
        throw std::runtime_error("Couldn't set permitted capabilites");
    }
    if (cap_set_proc(newCaps) == -1) {
        throw std::runtime_error("Couldn't set drop capabilites to CAP_SYS_CHROOT");
    }
}

