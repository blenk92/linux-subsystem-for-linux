#include <stdexcept>

#include "common.h"

fs::path nsMntDir(MNTDIR);
fs::path linksDir(LINKSDIR);
fs::path executorPath(EXECUTORPATH);
fs::path config(CONFIGPATH);

class CapWrapper {
  public:
    ~CapWrapper() {
        if (caps) {
            cap_free(caps);
        }
    }
    void init() {
        caps = cap_init();
        if (!caps) {
            throw std::runtime_error(
                "Couldn't initialize capabilities context");
        }
    }

    void allowCaps(const std::vector<cap_value_t> &capList, cap_flag_t set) {
        if (cap_set_flag(caps, set, capList.size(), capList.data(), CAP_SET) ==
            -1) {
            throw std::runtime_error("Couldn't set capabiliets on context");
        }
    }

    void enableCaps() {
        if (cap_set_proc(caps) == -1) {
            throw std::runtime_error("Couldn't drop capabilities");
        }
    }

  private:
    cap_t caps;
};

void dropToCapabilities(const std::vector<cap_value_t> &capList) {
    CapWrapper cw;
    cw.init();

    cw.allowCaps(capList, CAP_PERMITTED);
    cw.allowCaps(capList, CAP_EFFECTIVE);

    cw.enableCaps();
}
