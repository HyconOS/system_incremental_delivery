/*
 * Copyright (C) 2019 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef ANDROID_INCREMENTAL_FILE_SYSTEM_DATA_LOADER_NDK_H
#define ANDROID_INCREMENTAL_FILE_SYSTEM_DATA_LOADER_NDK_H

#include <incfs_ndk.h>
#include <jni.h>

__BEGIN_DECLS

#define DATALOADER_LIBRARY_NAME "libdataloader.so"

// Keep in sync with IncrementalConstants.java
typedef enum {
    INCREMENTAL_DATA_LOADER_SLOW_CONNECTION = 4,
    INCREMENTAL_DATA_LOADER_NO_CONNECTION = 5,
    INCREMENTAL_DATA_LOADER_CONNECTION_OK = 6,

    INCREMENTAL_DATA_LOADER_FIRST_STATUS = INCREMENTAL_DATA_LOADER_SLOW_CONNECTION,
    INCREMENTAL_DATA_LOADER_LAST_STATUS = INCREMENTAL_DATA_LOADER_CONNECTION_OK,
} DataLoaderStatus;

typedef struct {
    const char* name;
    int fd;
} DataLoaderNamedFd;

struct DataLoaderParams {
    const char* staticArgs;
    const char* packageName;

    const DataLoaderNamedFd* dynamicArgs;
    int dynamicArgsSize;
};

#ifdef __cplusplus

typedef class DataLoaderFilesystemConnector {
} * DataLoaderFilesystemConnectorPtr;
typedef class DataLoaderStatusListener {
} * DataLoaderStatusListenerPtr;

#else /* not __cplusplus */

typedef void* DataLoaderFilesystemConnectorPtr;
typedef void* DataLoaderStatusListenerPtr;

#endif /* not __cplusplus */

typedef JavaVM* DataLoaderServiceVmPtr;
typedef jobject DataLoaderServiceConnectorPtr;
typedef jobject DataLoaderServiceParamsPtr;

struct DataLoader {
    bool (*onStart)(struct DataLoader* self);
    void (*onStop)(struct DataLoader* self);
    void (*onDestroy)(struct DataLoader* self);
    void (*onFileCreated)(struct DataLoader* self, IncFsInode inode, const char* metadataBytes,
                          int metadataLength);

    void (*onPendingReads)(struct DataLoader* self, const IncFsPendingReadInfo pendingReads[],
                           int pendingReadsCount);
    void (*onPageReads)(struct DataLoader* self, const IncFsPageReadInfo pageReads[],
                        int pageReadsCount);
};

struct DataLoaderFactory {
    struct DataLoader* (*onCreate)(struct DataLoaderFactory* self, const struct DataLoaderParams*,
                                   DataLoaderFilesystemConnectorPtr, DataLoaderStatusListenerPtr,
                                   DataLoaderServiceVmPtr, DataLoaderServiceConnectorPtr,
                                   DataLoaderServiceParamsPtr);
};
void DataLoader_Initialize(struct DataLoaderFactory*);

int DataLoader_FilesystemConnector_writeBlocks(DataLoaderFilesystemConnectorPtr,
                                               const struct incfs_new_data_block blocks[],
                                               int blocksCount);
// INCFS_MAX_FILE_ATTR_SIZE
int DataLoader_FilesystemConnector_getRawMetadata(DataLoaderFilesystemConnectorPtr, IncFsInode ino,
                                                  char buffer[], size_t* bufferSize);

int DataLoader_StatusListener_reportStatus(DataLoaderStatusListenerPtr listener,
                                           DataLoaderStatus status);

// DataLoaderService JNI
bool DataLoaderService_OnCreate(JNIEnv* env, jobject service, jint storageId, jobject control,
                                jobject params, jobject listener);
bool DataLoaderService_OnStart(jint storageId);
bool DataLoaderService_OnStop(jint storageId);
bool DataLoaderService_OnDestroy(jint storageId);
bool DataLoaderService_OnFileCreated(jint storageId, jlong inode, jbyteArray metadata);

__END_DECLS

#endif // ANDROID_INCREMENTAL_FILE_SYSTEM_DATA_LOADER_NDK_H