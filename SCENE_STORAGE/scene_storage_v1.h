#pragma once

#include <Arduino.h>
#include <FS.h>
#include <LittleFS.h>
#include <stddef.h>
#include <stdint.h>

namespace IndicatorSceneStorage {

// Result of a scene storage operation.
enum class StoreResult : uint8_t {
    STORED,
    REPLACED,
    DUPLICATE_ID,
    INVALID_SCENE,
    READ_ERROR,
    WRITE_ERROR,
    VERIFY_ERROR,
    PATH_ERROR
};

// Persistent scene store.
//
// Responsibility:
//   STORE -> REJECT DUPLICATE ID -> MAKE UNIQUE SCENE AVAILABLE BY ID
//
// The upload source is deliberately transport-agnostic. Production callers
// (for example the Android HTTP layer) pass received scene content directly
// to installFromText(). No PC data file or source filename is involved.
class SceneStorageV1 {
public:
    static constexpr const char* SCENE_DIR = "/scenes";

    bool begin();
    bool mounted() const { return mounted_; }

    // Development-mode store policy: the internal Scene ID is the only
    // identity. A valid incoming scene with an existing ID replaces the old
    // scene transactionally. Invalid incoming content never destroys the old
    // committed scene.
    StoreResult installFromText(const char* sceneText,
                                size_t sceneLength,
                                String* storedId = nullptr);

    // Return whether a valid stored scene exists for this internal ID.
    bool existsById(const char* sceneId) const;

    // Read the complete stored scene content by internal ID for SceneVM or
    // another runtime consumer.
    bool readById(const char* sceneId, String& out) const;

    const char* lastError() const { return lastError_; }
    static const char* resultName(StoreResult result);

private:
    bool mounted_ = false;
    const char* lastError_ = "";

    static bool validId(const String& id);
    static bool extractId(const String& text, String& id);
    static bool readFile(fs::FS& fs, const char* path, String& out);
    static bool verifyStoredFile(const char* path,
                                 const char* expectedId,
                                 size_t expectedSize);
    bool findIdPath(const char* sceneId, String& path) const;
    void setError(const char* message) { lastError_ = message; }
};

} // namespace IndicatorSceneStorage
