//
// Created by canyie on 2021/1/1.
//

#include <malloc.h>
#include <string.h>
#include <sched.h>
#include <errno.h>
#include <pthread.h>
#include <cstdlib>
#include <sys/stat.h>
#include "log.h"
#include "external/xhook/xhook.h"
#include "external/riru/riru.h"
#include "external/magisk/magiskhide.h"

constexpr const char* kMagicHandleAppZygote = "/data/misc/isolatedmagiskhider/app_zygote_magic";

extern "C" {
int riru_api_version = 0;
RiruApiV9* riru_api_v9;
}

const char* magisk_tmp_ = nullptr;
struct stat zygote_stat_;
bool magic_handle_app_zygote_ = false;
bool in_child_ = false;
bool isolated_ = false;
bool app_zygote_ = false;

pid_t (*orig_fork)() = nullptr;
int (*orig_unshare)(int) = nullptr;

const char* ReadMagiskTmp() {
    constexpr const char* path = "/data/misc/isolatedmagiskhider/magisk_tmp";
    const char* magisk_tmp = "/sbin";
    FILE* fp = fopen(path, "re");
    if (fp) {
        char tmp[PATH_MAX];
        fseek(fp, 0, SEEK_END);
        long size = ftell(fp);
        rewind(fp);

        if (size == fread(tmp, 1, static_cast<size_t>(size), fp)) {
            magisk_tmp = strdup(tmp);
        } else {
            LOGE("read magisk tmp failed: %s", strerror(errno));
        }
        fclose(fp);
    } else {
        LOGE("open magisk tmp failed: %s", strerror(errno));
    }
    return magisk_tmp;
}

void EnsureSeparatedNamespace(jint* mountMode) {
    if (*mountMode == 0) {
        LOGI("Changed mount mode from MOUNT_EXTERNAL_NONE to MOUNT_EXTERNAL_DEFAULT");
        *mountMode = 1;
    }
}

bool IsApp(int app_id) {
    return app_id >= 10000 && app_id <= 19999;
}

void InitProcessState(int uid, bool is_child_zygote) {
    int app_id = uid % 100000;
    isolated_ = app_id >= 90000;
    app_zygote_ = is_child_zygote && (IsApp(app_id) || isolated_);
}

void ClearProcessState() {
    isolated_ = false;
    app_zygote_ = false;
}

bool RegisterHook(const char* name, void* replace, void** backup) {
    int ret = xhook_register(".*\\libandroid_runtime.so$", name, replace, backup);
    if (ret != 0) {
        LOGE("Failed to hook %s", name);
        return true;
    }
    return false;
}

void ClearHooks() {
    xhook_enable_debug(1);
    xhook_enable_sigsegv_protection(0);
    bool failed = false;
#define UNHOOK(NAME) \
failed = failed || RegisterHook(#NAME, reinterpret_cast<void*>(orig_##NAME), nullptr)

    UNHOOK(fork);
    UNHOOK(unshare);
#undef UNHOOK

    if (failed || xhook_refresh(0)) {
        LOGE("Failed to clear hooks!");
        return;
    }
    xhook_clear();
}

void ReadSelfNs(struct stat* st) {
    stat("/proc/self/ns/mnt", st);
}

void StartHide() {
    LOGI("Created isolated process or app zygote %d, starting magisk hide...", getpid());
    struct stat self_stat;
    ReadSelfNs(&self_stat);
    if (self_stat.st_ino == zygote_stat_.st_ino && self_stat.st_dev == zygote_stat_.st_dev) {
        // This should not happen, we changed mount mode to ensure ns is separated
        LOGE("Skip hide this process because ns is not separated from zygote");
        return;
    }
    hide_unmount(magisk_tmp_);
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
    int read_fd = -1, write_fd = -1;

    if (app_zygote_ && magic_handle_app_zygote_) {
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
        // Do not hide here because the namespace not separated
        in_child_ = true;
        if (read_fd != -1 && write_fd != -1) {
            close(read_fd);
            pid_t new_pid = MagicHandleAppZygote();
            LOGI("Child zygote forked substitute %d", new_pid);
            char buf[16] = {0};
            snprintf(buf, 15, "%d", new_pid);
            write(write_fd, buf, 15);
            close(write_fd);
        }
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

int UnshareReplace(int flags) {
    int res = orig_unshare(flags);
    if (res == -1) return res;
    if (!in_child_) return res;
    if (flags & CLONE_NEWNS) {
        // Start hiding before dropping any privileges
        if (isolated_ || app_zygote_)
            StartHide();
        ClearHooks();
    }
    return res;
}

void RegisterHooks() {
    xhook_enable_debug(1);
    xhook_enable_sigsegv_protection(0);
    bool failed = false;
#define HOOK(NAME, REPLACE) \
failed = failed || RegisterHook(#NAME, reinterpret_cast<void*>(REPLACE), reinterpret_cast<void**>(&orig_##NAME))

    HOOK(fork, ForkReplace);
    HOOK(unshare, UnshareReplace);
#undef HOOK

    if (failed || xhook_refresh(0)) {
        LOGE("Failed to register hooks!");
        return;
    }
    xhook_clear();
}

EXPORT int shouldSkipUid(int uid) { return false; }

// Before Riru v22
EXPORT void nativeForkAndSpecializePre(JNIEnv* env, jclass, jint* uid_ptr, jint* gid_ptr,
                                       jintArray*, jint*, jobjectArray*, jint* mount_external,
                                       jstring*, jstring*, jintArray*, jintArray*,
                                       jboolean* is_child_zygote, jstring*, jstring*, jboolean*,
                                       jobjectArray*) {
    InitProcessState(*uid_ptr, *is_child_zygote);
    EnsureSeparatedNamespace(mount_external);
}

EXPORT int nativeForkAndSpecializePost(JNIEnv*, jclass, jint result) {
    ClearProcessState();
    return 0;
}

EXPORT void onModuleLoaded() {
    magisk_tmp_ = ReadMagiskTmp();
    LOGI("Magisk temp path is %s", magisk_tmp_);
    ReadSelfNs(&zygote_stat_);
    LOGI("Registering fork monitor");
    RegisterHooks();
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
    InitProcessState(*_uid, *is_child_zygote);
    EnsureSeparatedNamespace(mountExternal);
}

static void forkAndSpecializePost(JNIEnv*, jclass, jint res) {
    ClearProcessState();
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
