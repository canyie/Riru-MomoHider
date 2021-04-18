//
// Created by canyie on 2021/1/1.
// This file is sourced from https://github.com/topjohnwu/Magisk/
//

#include <cerrno>
#include <string>
#include <vector>
#include <mntent.h>
#include <sys/mount.h>
#include <unistd.h>
#include "magiskhide.h"
#include "../../log.h"

using namespace std;

#define INTLROOT    ".magisk"
#define BLOCKDIR    INTLROOT "/block"

// MomoHider changed: fn have no return type
void parse_mnt(const char *file, const function<void(mntent*)> &fn) {
    auto fp = std::unique_ptr<FILE, decltype(&fclose)>(setmntent(file, "re"), endmntent);
    if (fp) {
        mntent mentry{};
        char buf[4096];
        while (getmntent_r(fp.get(), &mentry, buf, sizeof(buf))) {
            fn(&mentry);
        }
    }
}

static void lazy_unmount(const char* mountpoint) {
    if (strcmp(mountpoint, "/system/framework/XposedBridge.jar") == 0) {
        LOGE("Skip unmount XposedBridge.jar because the rovo89's Xposed framework can't handle this case.");
        return;
    }
    if (umount2(mountpoint, MNT_DETACH) != -1) {
        //LOGD("hide_policy: Unmounted (%s)\n", mountpoint);
    } else {
        LOGE("hide_policy: can't unmount %s: %s", mountpoint, strerror(errno));
    }
}

// MomoHider changed: don't pass pid, the target process is always myself.
// MomoHider changed: don't print log in app process
void hide_unmount(const char* magisk_tmp) {
    // MomoHider changed: don't change namespace, the target process is always myself.
//    if (switch_mnt_ns())
//        return;

    vector<string> targets;

    // Unmount dummy skeletons and /sbin links
    targets.push_back(magisk_tmp);

#define TMPFS_MNT(dir) (mentry->mnt_type == "tmpfs"sv && \
strncmp(mentry->mnt_dir, "/" #dir, sizeof("/" #dir) - 1) == 0)

    parse_mnt("/proc/self/mounts", [&](mntent *mentry) {
        if (TMPFS_MNT(system) || TMPFS_MNT(vendor) || TMPFS_MNT(product) || TMPFS_MNT(system_ext))
            targets.emplace_back(mentry->mnt_dir);
    });

#undef TMPFS_MNT

    reverse(targets.begin(), targets.end());
    for (auto &s : targets)
        lazy_unmount(s.data());
    targets.clear();

    // Unmount all Magisk created mounts
    parse_mnt("/proc/self/mounts", [&](mntent *mentry) {
        if (strstr(mentry->mnt_fsname, BLOCKDIR)) {
            targets.emplace_back(mentry->mnt_dir);
        }
    });

    reverse(targets.begin(), targets.end());
    for (auto &s : targets)
        lazy_unmount(s.data());
}

