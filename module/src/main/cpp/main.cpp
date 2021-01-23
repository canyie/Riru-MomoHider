//
// Created by canyie on 2021/1/1.
//

#include <malloc.h>
#include <string.h>
#include <sched.h>
#include <errno.h>
#include <pthread.h>
#include "log.h"
#include "external/riru/riru.h"
#include "external/magisk/magiskhide.h"

extern "C" {
int riru_api_version = 0;
RiruApiV9* riru_api_v9;
}

bool creating_isolated_process = false;

void OnForkIsolatedProcess() {
    LOGI("Created isolated process %d, starting magisk hide...", getpid());
    if (unshare(CLONE_NEWNS) == -1) {
        LOGE("Failed to create new namespace for current process: %s (%d)", strerror(errno), errno);
        return;
    }
    hide_unmount();
    LOGI("Unmounted magisk file system.");
}

void OnNewProcess() {
    if (creating_isolated_process)
        OnForkIsolatedProcess();
}

EXPORT int shouldSkipUid(int uid) { return false; }

bool IsIsolated(int uid) {
    int app_id = uid % 100000;
    return app_id >= 90000 && app_id <= 99999;
}

// Before Riru v22
EXPORT void nativeForkAndSpecializePre(JNIEnv* env, jclass, jint* uid_ptr, jint* gid_ptr,
                                jintArray*, jint*, jobjectArray*, jint*, jstring*,
                                jstring*, jintArray*, jintArray*,
                                jboolean*, jstring*, jstring*, jboolean*, jobjectArray*) {
    creating_isolated_process = IsIsolated(*uid_ptr);
}

EXPORT int nativeForkAndSpecializePost(JNIEnv*, jclass, jint result) {
    creating_isolated_process = false;
    return 0;
}

EXPORT void onModuleLoaded() {
    LOGI("Registering fork monitor");
    pthread_atfork(nullptr, nullptr, OnNewProcess);
}

// After Riru v22
static void forkAndSpecializePre(
        JNIEnv *env, jclass, jint *_uid, jint *gid, jintArray *gids, jint *runtimeFlags,
        jobjectArray *rlimits, jint *mountExternal, jstring *seInfo, jstring *niceName,
        jintArray *fdsToClose, jintArray *fdsToIgnore, jboolean *is_child_zygote,
        jstring *instructionSet, jstring *appDataDir, jboolean *isTopApp, jobjectArray *pkgDataInfoList,
        jobjectArray *whitelistedDataInfoList, jboolean *bindMountAppDataDirs, jboolean *bindMountAppStorageDirs) {
    creating_isolated_process = IsIsolated(*_uid);
}

static void forkAndSpecializePost(JNIEnv*, jclass, jint res) {
    creating_isolated_process = false;
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

    static void *_module;

    switch (step) {
        case 1: {
            int core_max_api_version = *static_cast<int*>(arg);
            riru_api_version = core_max_api_version <= RIRU_NEW_MODULE_API_VERSION ? core_max_api_version : RIRU_NEW_MODULE_API_VERSION;
            return &riru_api_version;
        }
        case 2: {
            switch (riru_api_version) {
                // RiruApiV10 and RiruModuleInfoV10 are equal to V9
                case 10:
                case 9: {
                    riru_api_v9 = (RiruApiV9 *) arg;

                    auto module = (RiruModuleInfoV9 *) malloc(sizeof(RiruModuleInfoV9));
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
