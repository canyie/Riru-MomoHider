//
// Created by canyie on 2021/1/1.
//

#include <malloc.h>
#include <string.h>
#include <sched.h>
#include <errno.h>
#include <pthread.h>
#include <cstdlib>
#include "log.h"
#include "external/xhook/xhook.h"
#include "external/riru/riru.h"
#include "external/magisk/magiskhide.h"

constexpr const char* kMagicHandleAppZygote = "/data/misc/isolatedmagiskhider/app_zygote_magic";

extern "C" {
int riru_api_version = 0;
RiruApiV9* riru_api_v9;
}

bool magic_handle_app_zygote_ = false;
int app_id_ = -1;
bool child_zygote_ = false;

pid_t (*orig_fork)() = nullptr;

bool IsApp() {
    return app_id_ >= 10000 && app_id_ <= 19999;
}

bool IsIsolated() {
    return app_id_ >= 90000;
}

bool IsAppZygote() {
    return child_zygote_ && (IsApp() || IsIsolated());
}

void StartHide() {
    LOGI("Created isolated process or app zygote %d, starting magisk hide...", getpid());
    if (unshare(CLONE_NEWNS) == -1) {
        LOGE("Failed to create new namespace for current process: %s (%d)", strerror(errno), errno);
        return;
    }
    hide_unmount();
    LOGI("Unmounted magisk file system.");
}

pid_t MagicHandleAppZygote() {
    LOGI("Magic handling app zygote");
    // App zygote, fork new process and exit current process to make getppid() returns init
    // This makes some detection not working
    pid_t pid = orig_fork();
    if (pid > 0) {
        // parent
        exit(0);
    } else if (pid == 0) {
        pid = getpid();
    } else { // pid < 0
        LOGE("Failed to fork new process for app zygote");
    }
    return pid;
}

pid_t ForkReplace() {
    bool isolated = IsIsolated();
    bool app_zygote = IsAppZygote();
    int read_fd = -1, write_fd = -1;

    if (app_zygote && magic_handle_app_zygote_) {
        int pipe_fd[2];
        if (pipe(pipe_fd) == -1) {
            LOGE("Failed to create pipe for new app zygote: %s", strerror(errno));
        } else {
            read_fd = pipe_fd[0];
            write_fd = pipe_fd[1];
        }
    }

    pid_t pid = orig_fork();

    if (pid < 0) {
        // fork() failed, clean up
        if (read_fd != -1)
            close(read_fd);
        if (write_fd != -1)
            close(write_fd);
    } else if (pid == 0) {
        // child process

        if (isolated || app_zygote)
            StartHide();

        if (read_fd != -1 && write_fd != -1) {
            close(read_fd);
            pid_t new_pid = MagicHandleAppZygote();
            LOGI("Child zygote forked substitute %d", new_pid);
            char buf[16] = {0};
            snprintf(buf, 15, "%d", new_pid);
            write(write_fd, buf, 15);
            close(write_fd);
        }

        // Clean up
        void* origin = reinterpret_cast<void*>(orig_fork);
        bool success = xhook_register(".*\\libandroid_runtime.so$", "fork", origin, nullptr) == 0
                && xhook_refresh(0) == 0;
        if (success)
            xhook_clear();
        else
            LOGE("Failed to clean up hooks");
    } else {
        // parent process
        if (read_fd != -1 && write_fd != -1) {
            close(write_fd);
            char buf[16] = {0};
            read(read_fd, buf, 15);
            close(read_fd);
            pid = atoi(buf);
            LOGI("Zygote received new substitute pid %d", pid);
        }
    }
    return pid;
}

EXPORT int shouldSkipUid(int uid) { return false; }

// Before Riru v22
EXPORT void nativeForkAndSpecializePre(JNIEnv* env, jclass, jint* uid_ptr, jint* gid_ptr,
                                       jintArray*, jint*, jobjectArray*, jint*, jstring*,
                                       jstring*, jintArray*, jintArray*,
                                       jboolean* is_child_zygote, jstring*, jstring*, jboolean*,
                                       jobjectArray*) {
    int uid = *uid_ptr;
    app_id_ = uid % 100000;
    child_zygote_ = *is_child_zygote;
}

EXPORT int nativeForkAndSpecializePost(JNIEnv*, jclass, jint result) {
    app_id_ = -1;
    child_zygote_ = false;
    return 0;
}

EXPORT void onModuleLoaded() {
    LOGI("Registering fork monitor");
    xhook_enable_debug(1);
    xhook_enable_sigsegv_protection(0);
    void* replace = reinterpret_cast<void*>(ForkReplace);
    void** backup = reinterpret_cast<void**>(&orig_fork);
    bool success = xhook_register(".*\\libandroid_runtime.so$", "fork", replace, backup) == 0
            && xhook_refresh(0) == 0;
    if (success)
        xhook_clear();
    else
        LOGE("Failed to hook fork");

    magic_handle_app_zygote_ = access(kMagicHandleAppZygote, F_OK) == 0;

}

// After Riru v22
static void forkAndSpecializePre(
        JNIEnv* env, jclass, jint* _uid, jint* gid, jintArray* gids, jint* runtimeFlags,
        jobjectArray* rlimits, jint* mountExternal, jstring* seInfo, jstring* niceName,
        jintArray* fdsToClose, jintArray* fdsToIgnore, jboolean* is_child_zygote,
        jstring* instructionSet, jstring* appDataDir, jboolean* isTopApp,
        jobjectArray* pkgDataInfoList,
        jobjectArray* whitelistedDataInfoList, jboolean* bindMountAppDataDirs,
        jboolean* bindMountAppStorageDirs) {
    int uid = *_uid;
    app_id_ = uid % 100000;
    child_zygote_ = *is_child_zygote;
}

static void forkAndSpecializePost(JNIEnv*, jclass, jint res) {
    app_id_ = -1;
    child_zygote_ = false;
}

/*
 * Init will be called three times.
 *
 * The first time:
 *   Returns the highest version number supported by both Riru and the module.
 *
 *   arg: (int *) Riru's API version
 *   returns: (int *) the highest possible API version
 *
 * The second time:
 *   Returns the RiruModuleX struct created by the module.
 *   (X is the return of the first call)
 *
 *   arg: (RiruApiVX *) RiruApi strcut, this pointer can be saved for further use
 *   returns: (RiruModuleX *) RiruModule strcut
 *
 * The second time:
 *   Let the module to cleanup (such as RiruModuleX struct created before).
 *
 *   arg: null
 *   returns: (ignored)
 *
 */
EXPORT void* init(void* arg) {
    static int step = 0;
    step++;

    static void* _module;

    switch (step) {
        case 1: {
            int core_max_api_version = *static_cast<int*>(arg);
            riru_api_version =
                    core_max_api_version <= RIRU_NEW_MODULE_API_VERSION ? core_max_api_version
                                                                        : RIRU_NEW_MODULE_API_VERSION;
            return &riru_api_version;
        }
        case 2: {
            switch (riru_api_version) {
                // RiruApiV10 and RiruModuleInfoV10 are equal to V9
                case 10:
                case 9: {
                    riru_api_v9 = (RiruApiV9*) arg;

                    auto module = (RiruModuleInfoV9*) malloc(sizeof(RiruModuleInfoV9));
                    memset(module, 0, sizeof(RiruModuleInfoV9));
                    _module = module;

                    module->supportHide = true;

                    module->version = RIRU_MODULE_VERSION_CODE;
                    module->versionName = RIRU_MODULE_VERSION_NAME;
                    module->shouldSkipUid = shouldSkipUid;
                    module->onModuleLoaded = onModuleLoaded;
                    module->forkAndSpecializePre = forkAndSpecializePre;
                    module->forkAndSpecializePost = forkAndSpecializePost;
                    return module;
                }
                default: {
                    return nullptr;
                }
            }
        }
        case 3: {
            free(_module);
            return nullptr;
        }
        default:
            return nullptr;
    }
}
