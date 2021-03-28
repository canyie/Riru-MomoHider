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

const char* magisk_tmp_ = nullptr;
struct stat zygote_stat_;
bool magic_handle_app_zygote_ = false;
bool in_child_ = false;
bool isolated_ = false;
bool app_zygote_ = false;

pid_t (*orig_fork)() = nullptr;
int (*orig_unshare)(int) = nullptr;

int* riru_allow_unload = nullptr;

void AllowUnload() {
    if (riru_allow_unload) *riru_allow_unload = 1;
}

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
            tmp[size] = '\0';
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
    ClearHooks();
    AllowUnload();
}

static void specializeAppProcessPre(
        JNIEnv *env, jclass clazz, jint *uid, jint *gid, jintArray *gids, jint *runtimeFlags,
        jobjectArray *rlimits, jint *mountExternal, jstring *seInfo, jstring *niceName,
        jboolean *startChildZygote, jstring *instructionSet, jstring *appDataDir,
        jboolean *isTopApp, jobjectArray *pkgDataInfoList, jobjectArray *whitelistedDataInfoList,
        jboolean *bindMountAppDataDirs, jboolean *bindMountAppStorageDirs) {
    InitProcessState(*uid, *startChildZygote);
    EnsureSeparatedNamespace(mountExternal);
}

static void specializeAppProcessPost(JNIEnv *env, jclass clazz) {
    ClearProcessState();
    ClearHooks();
    AllowUnload();
}

extern "C" {
int riru_api_version = 0;
RiruApiV9* riru_api_v9;
static auto module = RiruVersionedModuleInfo {
        .moduleApiVersion = RIRU_NEW_MODULE_API_VERSION,
        .moduleInfo = RiruModuleInfo {
                .supportHide = true,
                .version = RIRU_MODULE_VERSION_CODE,
                .versionName = RIRU_MODULE_VERSION_NAME,
                .onModuleLoaded = onModuleLoaded,
                .shouldSkipUid = shouldSkipUid,
                .forkAndSpecializePre = forkAndSpecializePre,
                .forkAndSpecializePost = forkAndSpecializePost,
                .forkSystemServerPre = nullptr,
                .forkSystemServerPost = nullptr,
                .specializeAppProcessPre = specializeAppProcessPre,
                .specializeAppProcessPost = specializeAppProcessPost
        }
};
}

static int step = 0;

EXPORT void* init(Riru* arg) {
    step++;

    switch (step) {
        case 1: {
            int core_max_api_version = arg->riruApiVersion;
            riru_api_version = core_max_api_version <= RIRU_NEW_MODULE_API_VERSION
                    ? core_max_api_version : RIRU_NEW_MODULE_API_VERSION;
            if (riru_api_version > 10 && riru_api_version < 25) {
                // V24 is pre-release version, not supported
                riru_api_version = 10;
            }
            if (riru_api_version >= 25) {
                module.moduleApiVersion = riru_api_version;
                riru_allow_unload = arg->allowUnload;
                return &module;
            } else {
                return &riru_api_version;
            }
        }
        case 2: {
            return &module.moduleInfo;
        }
        case 3:
        default:
            return nullptr;
    }
}
