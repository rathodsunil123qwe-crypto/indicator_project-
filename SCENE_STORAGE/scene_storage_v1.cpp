#include "scene_storage_v1.h"
#include <ctype.h>

namespace IndicatorSceneStorage {
namespace {

bool parseAssignment(const String& line, String& key, String& value) {
    const int eq = line.indexOf('=');
    if (eq <= 0) return false;
    key = line.substring(0, eq);
    value = line.substring(eq + 1);
    key.trim();
    value.trim();
    return key.length() > 0;
}

} // namespace

bool SceneStorageV1::begin() {
    mounted_ = false;
    lastError_ = "";

    // Never format automatically. Existing LittleFS contents must survive
    // firmware reboot/update and remain available to the application.
    if (!LittleFS.begin(false)) {
        setError("LittleFS mount failed");
        return false;
    }

    mounted_ = true;
    if (!LittleFS.exists(SCENE_DIR) && !LittleFS.mkdir(SCENE_DIR)) {
        setError("cannot create scene directory");
        mounted_ = false;
        return false;
    }
    return true;
}

bool SceneStorageV1::validId(const String& id) {
    if (id.length() == 0) return false;
    if (!(isalpha(static_cast<unsigned char>(id[0])) || id[0] == '_')) return false;

    for (size_t i = 1; i < id.length(); ++i) {
        const unsigned char c = static_cast<unsigned char>(id[i]);
        if (!(isalnum(c) || c == '_')) return false;
    }
    return true;
}

// Storage validates only the SCENE envelope and internal ID. Full language
// validation remains the responsibility of the locked SceneVM.
bool SceneStorageV1::extractId(const String& text, String& id) {
    id = "";
    if (text.length() == 0) return false;

    int start = 0;
    bool sawScene = false;
    bool sawColors = false;
    bool sawScript = false;
    String found;

    while (start <= static_cast<int>(text.length())) {
        int end = text.indexOf('\n', start);
        if (end < 0) end = static_cast<int>(text.length());

        String line = text.substring(start, end);
        line.replace("\r", "");
        line.trim();

        if (line.length() > 0 && line[0] != '#') {
            if (!sawScene) {
                if (!line.equalsIgnoreCase("SCENE")) return false;
                sawScene = true;
            } else if (!sawColors) {
                if (line.equalsIgnoreCase("COLORS")) {
                    sawColors = true;
                } else {
                    String key, value;
                    if (!parseAssignment(line, key, value)) return false;
                    if (key.equalsIgnoreCase("ID")) {
                        if (found.length() != 0) return false;
                        found = value;
                    }
                }
            } else if (!sawScript && line.equalsIgnoreCase("SCRIPT")) {
                sawScript = true;
            }
        }

        if (end >= static_cast<int>(text.length())) break;
        start = end + 1;
    }

    if (!sawScene || !sawColors || !sawScript || !validId(found)) return false;
    id = found;
    return true;
}

bool SceneStorageV1::readFile(fs::FS& fs, const char* path, String& out) {
    out = "";
    File f = fs.open(path, "r");
    if (!f || f.isDirectory()) return false;

    const size_t len = f.size();
    if (len == 0) {
        f.close();
        return false;
    }

    out.reserve(len + 1);
    while (f.available()) {
        const int c = f.read();
        if (c < 0) {
            f.close();
            return false;
        }
        out += static_cast<char>(c);
    }
    f.close();
    return out.length() == len;
}

bool SceneStorageV1::verifyStoredFile(const char* path,
                                      const char* expectedId,
                                      size_t expectedSize) {
    File f = LittleFS.open(path, "r");
    if (!f || f.isDirectory() || f.size() != expectedSize) {
        if (f) f.close();
        return false;
    }
    f.close();

    String text;
    String id;
    return readFile(LittleFS, path, text) &&
           extractId(text, id) &&
           id.equals(String(expectedId));
}

bool SceneStorageV1::findIdPath(const char* sceneId, String& path) const {
    path = "";
    if (!mounted_ || !sceneId || !validId(String(sceneId))) return false;

    // The store controls committed filenames, so the canonical path is the
    // deterministic lookup mechanism. Identity is still verified from the
    // scene content before the file is accepted as a match.
    const String canonicalPath = String(SCENE_DIR) + "/" + sceneId + ".scene";
    if (!LittleFS.exists(canonicalPath)) return false;

    String text;
    String storedId;
    if (!readFile(LittleFS, canonicalPath.c_str(), text)) return false;
    if (!extractId(text, storedId)) return false;
    if (!storedId.equals(String(sceneId))) return false;

    path = canonicalPath;
    return true;
}

bool SceneStorageV1::existsById(const char* sceneId) const {
    String path;
    return findIdPath(sceneId, path);
}

StoreResult SceneStorageV1::installFromText(const char* sceneText,
                                            size_t sceneLength,
                                            String* storedId) {
    if (storedId) *storedId = "";

    if (!mounted_ || !sceneText) {
        setError("invalid storage/scene state");
        return StoreResult::PATH_ERROR;
    }
    if (sceneLength == 0) {
        setError("empty scene");
        return StoreResult::INVALID_SCENE;
    }

    // Copy only the received content into an ESP-side String. The source can
    // come from HTTP, Bluetooth, serial transport, or another future input.
    String text;
    text.reserve(sceneLength + 1);
    text += sceneText;
    if (text.length() != sceneLength) {
        setError("scene input length mismatch");
        return StoreResult::READ_ERROR;
    }

    String id;
    if (!extractId(text, id)) {
        setError("scene ID/SCENE envelope invalid");
        return StoreResult::INVALID_SCENE;
    }

    // Internal ID is the unique identity. No upload filename is consulted.
    // DEVELOPMENT POLICY: an existing ID is replaced after the new content
    // has been fully written and verified. Invalid content has already
    // returned above, so the existing committed scene remains untouched.
    const String finalPath = String(SCENE_DIR) + "/" + id + ".scene";
    const String tempPath = finalPath + ".tmp";
    const String backupPath = finalPath + ".bak";
    const bool replacing = LittleFS.exists(finalPath);

    if (LittleFS.exists(tempPath)) LittleFS.remove(tempPath);

    // Transaction: write -> flush -> verify -> rename into the committed path.
    File out = LittleFS.open(tempPath, "w");
    if (!out || out.isDirectory()) {
        if (out) out.close();
        LittleFS.remove(tempPath);
        setError("transactional scene write failed");
        return StoreResult::WRITE_ERROR;
    }

    const size_t written = out.write(
        reinterpret_cast<const uint8_t*>(sceneText), sceneLength);
    out.flush();
    out.close();

    if (written != sceneLength ||
        !verifyStoredFile(tempPath.c_str(), id.c_str(), sceneLength)) {
        LittleFS.remove(tempPath);
        setError("stored scene verification failed");
        return StoreResult::VERIFY_ERROR;
    }

    // Commit transaction. When replacing, move the old committed file to a
    // backup first. If the new commit fails, restore the old file.
    if (LittleFS.exists(backupPath)) LittleFS.remove(backupPath);

    if (replacing && !LittleFS.rename(finalPath, backupPath)) {
        LittleFS.remove(tempPath);
        setError("existing scene backup failed");
        return StoreResult::WRITE_ERROR;
    }

    if (!LittleFS.rename(tempPath, finalPath)) {
        LittleFS.remove(tempPath);
        if (replacing) {
            LittleFS.remove(finalPath);
            LittleFS.rename(backupPath, finalPath);
        }
        setError("scene commit rename failed");
        return StoreResult::WRITE_ERROR;
    }

    if (replacing) LittleFS.remove(backupPath);

    if (storedId) *storedId = id;
    setError("");
    return replacing ? StoreResult::REPLACED : StoreResult::STORED;
}

bool SceneStorageV1::readById(const char* sceneId, String& out) const {
    out = "";
    String path;
    if (!findIdPath(sceneId, path)) return false;
    return readFile(LittleFS, path.c_str(), out);
}

const char* SceneStorageV1::resultName(StoreResult result) {
    switch (result) {
        case StoreResult::STORED: return "STORED";
        case StoreResult::REPLACED: return "REPLACED";
        case StoreResult::DUPLICATE_ID: return "DUPLICATE_ID";
        case StoreResult::INVALID_SCENE: return "INVALID_SCENE";
        case StoreResult::READ_ERROR: return "READ_ERROR";
        case StoreResult::WRITE_ERROR: return "WRITE_ERROR";
        case StoreResult::VERIFY_ERROR: return "VERIFY_ERROR";
        case StoreResult::PATH_ERROR: return "PATH_ERROR";
    }
    return "UNKNOWN";
}

} // namespace IndicatorSceneStorage
