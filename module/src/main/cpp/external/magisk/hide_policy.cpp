//
// Created by canyie on 2021/1/1.
// This file is sourced from https://github.com/topjohnwu/Magisk/
//

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

void parse_mnt(const char *file, const function<bool(mntent*)> &fn) {
    auto fp = std::unique_ptr<FILE, decltype(&fclose)>(setmntent(file, "re"), endmntent);
    if (fp) {
        mntent mentry{};
        char buf[4096];
        while (getmntent_r(fp.get(), &mentry, buf, sizeof(buf))) {
            if (!fn(&mentry))
                break;
        }
    }
}

static void lazy_unmount(const char* mountpoint) {
    LOGD("unmounting %s", mountpoint);
    if (umount2(mountpoint, MNT_DETACH) != -1)
        LOGD("hide_policy: Unmounted (%s)\n", mountpoint);
}

// IsolatedMagiskHider changed: don't pass pid, the target process is always myself.
void hide_unmount() {
    // IsolatedMagiskHider changed: don't change namespace, the target process is always myself.
//    if (switch_mnt_ns())
//        return;

    vector<string> targets;

    // Unmount dummy skeletons and /sbin links
    // FIXME The MAGISKTMP is randomly generated on Android 11, it should be applied via $(magisk --path)
    //targets.push_back(MAGISKTMP);
    if (access("/sbin", R_OK) == 0) {
        targets.push_back("/sbin");
    }

#define TMPFS_MNT(dir) (mentry->mnt_type == "tmpfs"sv && \
strncmp(mentry->mnt_dir, "/" #dir, sizeof("/" #dir) - 1) == 0)

    parse_mnt("/proc/self/mounts", [&](mntent *mentry) {
        if (TMPFS_MNT(system) || TMPFS_MNT(vendor) || TMPFS_MNT(product) || TMPFS_MNT(system_ext))
            targets.emplace_back(mentry->mnt_dir);
        return true;
    });

#undef TMPFS_MNT

    reverse(targets.begin(), targets.end());

    for (auto &s : targets)
        lazy_unmount(s.data());
    targets.clear();

    // Unmount all Magisk created mounts
    parse_mnt("/proc/self/mounts", [&](mntent *mentry) {
        if (strstr(mentry->mnt_fsname, BLOCKDIR))
            targets.emplace_back(mentry->mnt_dir);
        return true;
    });

    reverse(targets.begin(), targets.end());

    for (auto &s : targets)
        lazy_unmount(s.data());
}