/*
 * oneir: выбор DRM-устройства для Display HDI.
 */
#include "oneir_drm_select.h"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <mutex>
#include <string>
#include <unistd.h>
#include <xf86drm.h>
#include <xf86drmMode.h>


#include "display_log.h"

namespace {
constexpr int MAX_CARDS = 16;
constexpr const char *DEFAULT_PATH = "/dev/dri/card0";

std::once_flag g_selectOnce;
std::string g_path;

bool HasConnectedConnector(int fd)
{
    drmModeRes *res = drmModeGetResources(fd);
    if (res == nullptr) {
        return false; // не KMS-устройство или нет прав
    }
    bool found = false;
    for (int i = 0; i < res->count_connectors && !found; ++i) {
        drmModeConnector *conn = drmModeGetConnector(fd, res->connectors[i]);
        if (conn != nullptr) {
            found = (conn->connection == DRM_MODE_CONNECTED);
            drmModeFreeConnector(conn);
        }
    }
    drmModeFreeResources(res);
    return found;
}

void SelectDevice()
{
    const char *env = getenv("ONEIR_DRM_DEVICE");
    if (env != nullptr && env[0] != '\0') {
        g_path = env;
        DISPLAY_LOGI("drm device from ONEIR_DRM_DEVICE: %{public}s", g_path.c_str());
        return;
    }

    std::string firstOpenable;
    for (int i = 0; i < MAX_CARDS; ++i) {
        std::string path = "/dev/dri/card" + std::to_string(i);
        int fd = open(path.c_str(), O_RDWR | O_CLOEXEC);
        if (fd < 0) {
            continue;
        }
        if (firstOpenable.empty()) {
            firstOpenable = path;
        }
        bool connected = HasConnectedConnector(fd);
        close(fd);
        if (connected) {
            g_path = path;
            DISPLAY_LOGI("drm device selected: %{public}s (has connected connector)", g_path.c_str());
            return;
        }
    }

    g_path = firstOpenable.empty() ? DEFAULT_PATH : firstOpenable;
    DISPLAY_LOGW("no drm device with connected connector, fallback: %{public}s", g_path.c_str());
}
} // namespace

extern "C" const char *OneirDrmDevicePath(void)
{
    std::call_once(g_selectOnce, SelectDevice);
    return g_path.c_str();
}

extern "C" int OneirOpenDrmDevice(void)
{
    const char *path = OneirDrmDevicePath();
    int fd = open(path, O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        DISPLAY_LOGE("open %{public}s failed: %{public}s", path, strerror(errno));
    }
    return fd;
}
