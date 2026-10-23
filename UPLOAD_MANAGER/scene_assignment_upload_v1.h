#pragma once

#include <Arduino.h>
#include <FS.h>
#include <LittleFS.h>
#include <stddef.h>
#include <stdint.h>

#include "../SCENE_STORAGE/scene_storage_v1.h"
#include "../ASSIGNMENT_CONFIG/assignment_config_v1.h"
#include "../RUNTIME_MANAGER/scene_runtime_manager_v1.h"

namespace IndicatorSceneUpload {

static constexpr uint8_t MAX_UPLOAD_SCENES = 36;

struct UploadSceneText {
    const char* text;
    size_t length;
};

enum class UploadResult : uint8_t {
    INSTALLED,
    DUPLICATE_ONLY,
    CONFIG_INSTALLED,
    INVALID_CONFIG,
    MISSING_SCENE_REFERENCE,
    SCENE_INSTALL_FAILED,
    CONFIG_WRITE_FAILED,
    VERIFY_FAILED,
    NOT_READY,
    TOO_MANY_SCENES,
    DUPLICATE_UPLOAD_ID
};

class SceneAssignmentUploadV1 {
public:
    static constexpr const char* CONFIG_DIR = "/config";
    static constexpr const char* CONFIG_PATH = "/config/assignment.config";

    bool begin(IndicatorSceneStorage::SceneStorageV1& storage);
    void setRuntimeManager(IndicatorSceneRuntime::SceneRuntimeManagerV1& runtime) { runtime_ = &runtime; }

    // Installs one complete logical upload: selected scene texts + one
    // assignment.config. Scene filenames are never supplied or consulted.
    // The config is committed only after its referenced IDs exist in the
    // current LittleFS scene store or are present in this upload.
    UploadResult install(const UploadSceneText* scenes,
                         size_t sceneCount,
                         const char* configText,
                         size_t configLength);

    // Read the currently active assignment configuration.
    bool readActiveConfig(String& out) const;

    const char* lastError() const { return lastError_; }
    static const char* resultName(UploadResult result);

private:
    IndicatorSceneStorage::SceneStorageV1* storage_ = nullptr;
    IndicatorSceneRuntime::SceneRuntimeManagerV1* runtime_ = nullptr;
    const char* lastError_ = "";

    void setError(const char* error) { lastError_ = error; }
    static bool extractSceneId(const char* text, size_t length, String& id);
    static bool validSceneId(const String& id);
    static bool readFile(const char* path, String& out);
    bool stageConfig(const char* text, size_t length, String& tempPath);
    bool commitConfig(const String& tempPath);
    bool copyFile(const String& sourcePath, const String& destinationPath);
    bool writeJournal(const String* ids, const bool* existed, size_t count,
                      bool configExisted);
    bool recoverTransaction();
    void cleanupTransaction(const String* ids, size_t count);
};

} // namespace IndicatorSceneUpload
