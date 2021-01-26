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
#include "external/riru/riru.h"
#include "external/magisk/magiskhide.h"

extern "C" {
int riru_api_version = 0;
RiruApiV9* riru_api_v9;
}

int uid_ = -1;
bool child_zygote_ = false;
bool forked_ = false;

void StartHide() {
    LOGI("Created isolated process or app zygote %d, starting magisk hide...", getpid());
    if (unshare(CLONE_NEWNS) == -1) {
        LOGE("Failed to create new namespace for current process: %s (%d)", strerror(errno), errno);
        return;
    }
    hide_unmount();
    LOGI("Unmounted magisk file system.");
}

void OnNewProcess() {
    if (forked_) return;
    forked_ = true;

    int app_id = uid_ % 100000;
    if (app_id >= 90000 // isolated process
          || (child_zygote_ && app_id >= 10000 && app_id <= 19999)) // app zygote
        StartHide();
}


EXPORT int shouldSkipUid(int uid) { return false; }

// Before Riru v22
EXPORT void nativeForkAndSpecializePre(JNIEnv* env, jclass, jint* uid_ptr, jint* gid_ptr,
                                       jintArray*, jint*, jobjectArray*, jint*, jstring*,
                                       jstring*, jintArray*, jintArray*,
                                       jboolean* is_child_zygote, jstring*, jstring*, jboolean*,
                                       jobjectArray*) {
    uid_ = *uid_ptr;
    child_zygote_ = *is_child_zygote;
}

EXPORT int nativeForkAndSpecializePost(JNIEnv*, jclass, jint result) {
    uid_ = -1;
    child_zygote_ = false;
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
    uid_ = *_uid;
    child_zygote_ = *is_child_zygote;
}

static void forkAndSpecializePost(JNIEnv*, jclass, jint res) {
    uid_ = -1;
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
