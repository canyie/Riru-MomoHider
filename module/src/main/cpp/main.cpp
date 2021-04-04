//
// Created by canyie on 2021/1/1.
//

#include <cstdlib>
#include <malloc.h>
#include <string.h>
#include <fcntl.h>
#include <sched.h>
#include <errno.h>
#include <pthread.h>
#include <dirent.h>
#include <string>
#include "log.h"
#include "external/xhook/xhook.h"
#include "external/riru/riru.h"
#include "external/magisk/magiskhide.h"

#define BASE_DIR "/data/adb/momohider"
constexpr const char* kSetNs = BASE_DIR "/setns";
constexpr const char* kMagicHandleAppZygote = BASE_DIR "/app_zygote_magic";
constexpr const char* kMagiskTmp = BASE_DIR "/magisk_tmp";
constexpr const char* kIsolated = BASE_DIR "/isolated";

const char* magisk_tmp_ = nullptr;
bool magic_handle_app_zygote_ = false;
bool hide_isolated_ = false;
bool in_child_ = false;
bool isolated_ = false;
bool app_zygote_ = false;

bool use_nsholder_ = false;
char* nsholder_mnt_ns_ = nullptr;
pid_t nsholder_pid_ = -1;

pid_t (*orig_fork)() = nullptr;
int (*orig_unshare)(int) = nullptr;

int* riru_allow_unload = nullptr;

bool Exists(const char* path) {
    if (access(path, F_OK) == 0) return true;
    LOGD("access %s failed: %s", path, strerror(errno));
    return false;
}

void AllowUnload() {
    if (riru_allow_unload) *riru_allow_unload = 1;
}

int ReadIntAndClose(int fd) {
    char buf[16] = {0};
    read(fd, buf, 15);
    close(fd);
    return atoi(buf);
}

void WriteIntAndClose(int fd, int value) {
    char buf[16] = {0};
    snprintf(buf, 15, "%d", value);
    write(fd, buf, 15);
    close(fd);
}

const char* ReadMagiskTmp() {
    const char* magisk_tmp = "/sbin";
    FILE* fp = fopen(kMagiskTmp, "re");
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
        LOGI("Changed mount mode from NONE to DEFAULT");
        *mountMode = 1;
    }
}

void HideMagisk() {
    LOGI("Hiding magisk for process %d...", getpid());
    hide_unmount(magisk_tmp_);
    LOGI("Unmounted magisk file system.");
}

void MaybeInitNsHolder(JNIEnv* env) {
    if (!use_nsholder_) return;

    if (nsholder_mnt_ns_) {
        if (access(nsholder_mnt_ns_, F_OK) != 0) {
            // Maybe the nsholder died
            LOGW("access %s failed with error %s", nsholder_mnt_ns_, strerror(errno));
            if (nsholder_pid_ > 0){
                kill(nsholder_pid_, SIGKILL);
            }
            free(nsholder_mnt_ns_);
        } else { // Still alive
            return;
        }
    }

    LOGI("Initializing nsholder");
    int read_fd, write_fd;
    {
        int pipe_fd[2];
        if (pipe(pipe_fd) == -1) {
            LOGE("Failed to create pipe for nsholder: %s", strerror(errno));
            return;
        } else {
            read_fd = pipe_fd[0];
            write_fd = pipe_fd[1];
        }
    }

    nsholder_pid_ = orig_fork();
    if (nsholder_pid_ < 0) { // failed, cleanup
        LOGE("fork nsholder failed: %s", strerror(errno));
        close(read_fd);
        close(write_fd);
        nsholder_mnt_ns_ = nullptr;
    } else if (nsholder_pid_ == 0) { // child
        close(read_fd);
        if (orig_unshare(CLONE_NEWNS) == -1) {
            LOGE("nsholder: failed to clone new ns: %s", strerror(errno));
            WriteIntAndClose(write_fd, 1);
            exit(1);
        }
        HideMagisk();

        // Change process name
        {
            jstring name = env->NewStringUTF(sizeof(void*) == 8 ? "nsholder64" : "nsholder32");
            jclass Process = env->FindClass("android/os/Process");
            jmethodID setArgV0 = env->GetStaticMethodID(Process, "setArgV0", "(Ljava/lang/String;)V");
            if (env->ExceptionCheck()) {
                env->ExceptionClear();
                LOGW("Process.setArgV0(String) not found");
            } else {
                env->CallStaticVoidMethod(Process, setArgV0, name);
                if (env->ExceptionCheck()) {
                    env->ExceptionClear();
                    LOGW("Process.setArgV0(String) threw exception");
                }
            }
            env->DeleteLocalRef(name);
            env->DeleteLocalRef(Process);
        }

        // We're in the "cleaned" ns, notify the zygote we're ready and stop us
        WriteIntAndClose(write_fd, 0);

        // All done, but we should keep alive, because we need to keep the namespace
        // If a fd references the namespace, the ns won't be destroyed
        // but we need to open a fd in zygote, and Google don't want we opened new fd across fork,
        // zygote will abort with error like "Not whitelisted (41): mnt:[4026533391]"
        // We can manually call the Zygote.nativeAllowAcrossFork(), but this can be detected by app;
        // or, we can use the "fdsToIgnore" argument, but for usap, forkApp() haven't the argument.
        // To keep it simple, just let fd not opened in zygote
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmissing-noreturn"
        for (;;) {
            pause();
            LOGW("nsholder wakes up unexpectedly, sleep again");
        }
#pragma clang diagnostic pop

    } else { // parent, wait the nsholder enter a "clean" ns
        close(write_fd);
        int status = ReadIntAndClose(read_fd);
        if (status == 0) {
            kill(nsholder_pid_, SIGSTOP); // make nsholder is stopped again
            char mnt[32];
            snprintf(mnt, sizeof(mnt), "/proc/%d/ns/mnt", nsholder_pid_);
            LOGI("The nsholder is cleaned and stopped, mnt_ns is %s", mnt);
            nsholder_mnt_ns_ = strdup(mnt);
            return;
        } else {
            LOGE("Unexpected status %d received from the nsholder", status);

            kill(nsholder_pid_, SIGKILL);
            nsholder_pid_ = -1;
            nsholder_mnt_ns_ = nullptr;
            use_nsholder_ = false;
        }
    }
}

bool MaybeSwitchMntNs() {
    if (!nsholder_mnt_ns_) return false;
    int fd = open(nsholder_mnt_ns_, O_RDONLY);
    if (fd < 0) { // Maybe the nsholder died...
        LOGE("Can't open %s: %s", nsholder_mnt_ns_, strerror(errno));
        return false;
    }
    int ret = setns(fd, 0);
    int err = errno;
    close(fd);
    if (ret != 0) {
        LOGE("Failed to switch ns: %s", strerror(err));
        return false;
    }
    LOGI("Hided magisk by switch ns");
    return true;
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
    if (!hide_isolated_ && !magic_handle_app_zygote_) return;
    xhook_enable_debug(1);
    xhook_enable_sigsegv_protection(0);
    bool failed = false;
#define UNHOOK(NAME) \
failed = failed || RegisterHook(#NAME, reinterpret_cast<void*>(orig_##NAME), nullptr)

    UNHOOK(fork);
    if (hide_isolated_) {
        UNHOOK(unshare);
    }
#undef UNHOOK

    if (failed || xhook_refresh(0)) {
        LOGE("Failed to clear hooks!");
        return;
    }
    xhook_clear();
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
            WriteIntAndClose(write_fd, new_pid);
        }
    } else {
        // parent process
        if (read_fd != -1 && write_fd != -1) {
            close(write_fd);
            pid = ReadIntAndClose(read_fd);
            LOGI("Zygote received new substitute pid %d", pid);
        }
    }
    return pid;
}

int UnshareReplace(int flags) {
    bool isolated_ns = (flags & CLONE_NEWNS) != 0 && in_child_ && (isolated_ || app_zygote_);
    bool cleaned = false;
    if (isolated_ns) {
        cleaned = MaybeSwitchMntNs();
    }
    int res = orig_unshare(flags);
    if (res == -1) return res;
    if (isolated_ns && !cleaned) { // If not in a cleaned ns, try hide directly again
        HideMagisk();
    }
    return res;
}

void RegisterHooks() {
    if (!hide_isolated_ && !magic_handle_app_zygote_) return;
    xhook_enable_debug(1);
    xhook_enable_sigsegv_protection(0);
    bool failed = false;
#define HOOK(NAME, REPLACE) \
failed = failed || RegisterHook(#NAME, reinterpret_cast<void*>(REPLACE), reinterpret_cast<void**>(&orig_##NAME))

    HOOK(fork, ForkReplace);
    if (hide_isolated_) {
        HOOK(unshare, UnshareReplace);
    }

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
    if (hide_isolated_) {
        EnsureSeparatedNamespace(mount_external);
        MaybeInitNsHolder(env);
    }
}

EXPORT int nativeForkAndSpecializePost(JNIEnv*, jclass, jint result) {
    ClearProcessState();
    if (result == 0) ClearHooks();
    return 0;
}

EXPORT void onModuleLoaded() {
    magisk_tmp_ = ReadMagiskTmp();
    LOGI("Magisk temp path is %s", magisk_tmp_);
    hide_isolated_ = Exists(kIsolated);
    magic_handle_app_zygote_ = Exists(kMagicHandleAppZygote);
    use_nsholder_ = Exists(kSetNs);
    RegisterHooks();
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
    if (hide_isolated_) {
        EnsureSeparatedNamespace(mountExternal);
        MaybeInitNsHolder(env);
    }
}

static void forkAndSpecializePost(JNIEnv*, jclass, jint res) {
    ClearProcessState();
    if (res == 0) {
        ClearHooks();
        AllowUnload();
    }
}

static void specializeAppProcessPre(
        JNIEnv *env, jclass clazz, jint *uid, jint *gid, jintArray *gids, jint *runtimeFlags,
        jobjectArray *rlimits, jint *mountExternal, jstring *seInfo, jstring *niceName,
        jboolean *startChildZygote, jstring *instructionSet, jstring *appDataDir,
        jboolean *isTopApp, jobjectArray *pkgDataInfoList, jobjectArray *whitelistedDataInfoList,
        jboolean *bindMountAppDataDirs, jboolean *bindMountAppStorageDirs) {
    InitProcessState(*uid, *startChildZygote);
    if (hide_isolated_) EnsureSeparatedNamespace(mountExternal);
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
                .shouldSkipUid = nullptr,
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
                module.moduleInfo.shouldSkipUid = shouldSkipUid;
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
