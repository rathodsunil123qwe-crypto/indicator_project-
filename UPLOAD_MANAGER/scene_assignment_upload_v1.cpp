#include "scene_assignment_upload_v1.h"
#include <ctype.h>

namespace IndicatorSceneUpload {
namespace {

static constexpr const char* TXN_DIR = "/upload_txn";
static constexpr const char* TXN_JOURNAL = "/upload_txn/journal";
static constexpr const char* TXN_JOURNAL_TMP = "/upload_txn/journal.tmp";
static constexpr const char* TXN_CONFIG_BACKUP = "/upload_txn/assignment.config.bak";

String scenePathFor(const String& id) {
    return String(IndicatorSceneStorage::SceneStorageV1::SCENE_DIR) + "/" + id + ".scene";
}

String sceneBackupPathFor(const String& id) {
    return String(TXN_DIR) + "/" + id + ".scene.bak";
}

bool parseAssignment(const String& line, String& key, String& value) {
    const int eq = line.indexOf('=');
    if (eq <= 0) return false;
    key = line.substring(0, eq);
    value = line.substring(eq + 1);
    key.trim();
    value.trim();
    return key.length() > 0;
}

bool extractIdFromScene(const char* text, size_t length, String& id) {
    id = "";
    if (!text || length == 0) return false;
    int start = 0;
    bool sawScene = false;
    while (start <= static_cast<int>(length)) {
        int end = -1;
        for (int i = start; i < static_cast<int>(length); ++i) {
            if (text[i] == '\n') { end = i; break; }
        }
        if (end < 0) end = static_cast<int>(length);
        String line;
        for (int i = start; i < end; ++i) line += text[i];
        line.replace("\r", "");
        line.trim();
        if (line.length() > 0 && line[0] != '#') {
            if (!sawScene) {
                if (!line.equalsIgnoreCase("SCENE")) return false;
                sawScene = true;
            } else {
                String key, value;
                if (parseAssignment(line, key, value) && key.equalsIgnoreCase("ID")) {
                    if (id.length() != 0) return false;
                    id = value;
                }
                if (line.equalsIgnoreCase("COLORS")) break;
            }
        }
        if (end >= static_cast<int>(length)) break;
        start = end + 1;
    }
    if (!sawScene || id.length() == 0) return false;
    if (!(isalpha(static_cast<unsigned char>(id[0])) || id[0] == '_')) return false;
    for (size_t i = 1; i < id.length(); ++i) {
        const unsigned char c = id[i];
        if (!(isalnum(c) || c == '_')) return false;
    }
    return true;
}

} // namespace

bool SceneAssignmentUploadV1::begin(IndicatorSceneStorage::SceneStorageV1& storage) {
    storage_ = &storage;
    lastError_ = "";
    if (!storage_->mounted()) {
        setError("scene storage is not mounted");
        return false;
    }
    if (!LittleFS.exists(CONFIG_DIR) && !LittleFS.mkdir(CONFIG_DIR)) {
        setError("cannot create config directory");
        return false;
    }

    if (!LittleFS.exists(TXN_DIR) && !LittleFS.mkdir(TXN_DIR)) {
        setError("cannot create upload transaction directory");
        return false;
    }
    if (!recoverTransaction()) {
        setError("upload transaction recovery failed; storage was not modified further");
        return false;
    }
    return true;
}

bool SceneAssignmentUploadV1::validSceneId(const String& id) {
    if (id.length() == 0) return false;
    if (!(isalpha(static_cast<unsigned char>(id[0])) || id[0] == '_')) return false;
    for (size_t i = 1; i < id.length(); ++i) {
        const unsigned char c = id[i];
        if (!(isalnum(c) || c == '_')) return false;
    }
    return true;
}

bool SceneAssignmentUploadV1::extractSceneId(const char* text, size_t length, String& id) {
    return extractIdFromScene(text, length, id);
}

bool SceneAssignmentUploadV1::readFile(const char* path, String& out) {
    out = "";
    File f = LittleFS.open(path, "r");
    if (!f || f.isDirectory()) return false;
    const size_t len = f.size();
    if (len == 0) { f.close(); return false; }
    out.reserve(len + 1);
    while (f.available()) {
        const int c = f.read();
        if (c < 0) { f.close(); return false; }
        out += static_cast<char>(c);
    }
    f.close();
    return out.length() == len;
}

bool SceneAssignmentUploadV1::stageConfig(const char* text, size_t length, String& tempPath) {
    tempPath = String(CONFIG_PATH) + ".tmp";
    if (LittleFS.exists(tempPath)) LittleFS.remove(tempPath);
    File f = LittleFS.open(tempPath, "w");
    if (!f || f.isDirectory()) {
        if (f) f.close();
        return false;
    }
    const size_t written = f.write(reinterpret_cast<const uint8_t*>(text), length);
    f.flush();
    f.close();
    return written == length;
}

bool SceneAssignmentUploadV1::commitConfig(const String& tempPath) {
    const String oldPath = String(CONFIG_PATH) + ".old";
    if (LittleFS.exists(oldPath)) LittleFS.remove(oldPath);

    // Journal the old file first. On reboot, begin() restores it if the new
    // file was not committed yet. Once the new file is in the active path,
    // the journal is simply discarded.
    if (LittleFS.exists(CONFIG_PATH) &&
        !LittleFS.rename(CONFIG_PATH, oldPath)) {
        return false;
    }

    if (!LittleFS.rename(tempPath, CONFIG_PATH)) {
        if (LittleFS.exists(oldPath)) LittleFS.rename(oldPath, CONFIG_PATH);
        return false;
    }

    if (LittleFS.exists(oldPath)) LittleFS.remove(oldPath);
    return true;
}

bool SceneAssignmentUploadV1::copyFile(const String& sourcePath,
                                             const String& destinationPath) {
    File source = LittleFS.open(sourcePath, "r");
    if (!source || source.isDirectory()) {
        if (source) source.close();
        return false;
    }
    if (LittleFS.exists(destinationPath) && !LittleFS.remove(destinationPath)) {
        source.close();
        return false;
    }
    File destination = LittleFS.open(destinationPath, "w");
    if (!destination || destination.isDirectory()) {
        if (destination) destination.close();
        source.close();
        return false;
    }
    uint8_t buffer[256];
    size_t total = 0;
    bool ok = true;
    while (source.available()) {
        const int count = source.read(buffer, sizeof(buffer));
        if (count <= 0) { ok = false; break; }
        if (destination.write(buffer, static_cast<size_t>(count)) != static_cast<size_t>(count)) {
            ok = false;
            break;
        }
        total += static_cast<size_t>(count);
    }
    destination.flush();
    destination.close();
    source.close();
    File check = LittleFS.open(destinationPath, "r");
    if (!check || check.isDirectory() || check.size() != total) ok = false;
    if (check) check.close();
    return ok;
}

bool SceneAssignmentUploadV1::writeJournal(const String* ids,
                                            const bool* existed,
                                            size_t count,
                                            bool configExisted) {
    if (LittleFS.exists(TXN_JOURNAL_TMP)) LittleFS.remove(TXN_JOURNAL_TMP);
    File file = LittleFS.open(TXN_JOURNAL_TMP, "w");
    if (!file || file.isDirectory()) {
        if (file) file.close();
        return false;
    }
    file.printf("CONFIG_EXISTS=%u\n", configExisted ? 1U : 0U);
    file.printf("SCENE_COUNT=%u\n", static_cast<unsigned>(count));
    for (size_t i = 0; i < count; ++i) {
        file.printf("SCENE=%s,%u\n", ids[i].c_str(), existed[i] ? 1U : 0U);
    }
    file.flush();
    file.close();
    if (!LittleFS.exists(TXN_JOURNAL_TMP)) return false;
    File verify = LittleFS.open(TXN_JOURNAL_TMP, "r");
    if (!verify || verify.isDirectory() || verify.size() == 0) {
        if (verify) verify.close();
        return false;
    }
    verify.close();
    if (LittleFS.exists(TXN_JOURNAL) && !LittleFS.remove(TXN_JOURNAL)) return false;
    return LittleFS.rename(TXN_JOURNAL_TMP, TXN_JOURNAL);
}

void SceneAssignmentUploadV1::cleanupTransaction(const String* ids, size_t count) {
    // Removing the journal is the transaction's final decision point. Once it
    // is gone, canonical files are considered committed/restored and leftover
    // backup cleanup is safe to repeat after reboot.
    if (LittleFS.exists(TXN_JOURNAL)) LittleFS.remove(TXN_JOURNAL);
    if (LittleFS.exists(TXN_JOURNAL_TMP)) LittleFS.remove(TXN_JOURNAL_TMP);
    for (size_t i = 0; i < count; ++i) {
        const String backup = sceneBackupPathFor(ids[i]);
        if (LittleFS.exists(backup)) LittleFS.remove(backup);
        const String scene = scenePathFor(ids[i]);
        const String temp = scene + ".tmp";
        const String localBackup = scene + ".bak";
        if (LittleFS.exists(temp)) LittleFS.remove(temp);
        if (LittleFS.exists(localBackup)) LittleFS.remove(localBackup);
    }
    if (LittleFS.exists(TXN_CONFIG_BACKUP)) LittleFS.remove(TXN_CONFIG_BACKUP);
    const String configTemp = String(CONFIG_PATH) + ".tmp";
    if (LittleFS.exists(configTemp)) LittleFS.remove(configTemp);
    if (LittleFS.exists(String(CONFIG_PATH) + ".old")) LittleFS.remove(String(CONFIG_PATH) + ".old");
    if (LittleFS.exists(TXN_JOURNAL_TMP)) LittleFS.remove(TXN_JOURNAL_TMP);
}

bool SceneAssignmentUploadV1::recoverTransaction() {
    if (!LittleFS.exists(TXN_JOURNAL)) {
        // No package transaction is active. Retain the legacy config backup
        // recovery behavior and discard abandoned staging artifacts only.
        const String legacyOld = String(CONFIG_PATH) + ".old";
        const String configTemp = String(CONFIG_PATH) + ".tmp";
        if (!LittleFS.exists(CONFIG_PATH) && LittleFS.exists(legacyOld)) {
            if (!LittleFS.rename(legacyOld, CONFIG_PATH)) return false;
        } else if (LittleFS.exists(CONFIG_PATH) && LittleFS.exists(legacyOld)) {
            if (!LittleFS.remove(legacyOld)) return false;
        }
        if (LittleFS.exists(TXN_JOURNAL_TMP)) LittleFS.remove(TXN_JOURNAL_TMP);
        if (LittleFS.exists(configTemp)) LittleFS.remove(configTemp);
        if (LittleFS.exists(TXN_CONFIG_BACKUP)) LittleFS.remove(TXN_CONFIG_BACKUP);
        return true;
    }

    String journal;
    if (!readFile(TXN_JOURNAL, journal)) return false;
    bool configExisted = false;
    bool sawConfig = false;
    String ids[MAX_UPLOAD_SCENES];
    bool existed[MAX_UPLOAD_SCENES] = {};
    size_t count = 0;
    size_t expectedCount = 0;
    bool sawCount = false;
    int pos = 0;
    while (pos <= journal.length()) {
        int end = journal.indexOf('\n', pos);
        if (end < 0) end = journal.length();
        String line = journal.substring(pos, end);
        line.trim();
        if (line.startsWith("CONFIG_EXISTS=")) {
            const String value = line.substring(14);
            if (value != "0" && value != "1") return false;
            configExisted = value == "1";
            sawConfig = true;
        } else if (line.startsWith("SCENE_COUNT=")) {
            const String value = line.substring(12);
            if (value.length() == 0) return false;
            const long parsed = value.toInt();
            if (parsed < 0 || parsed > MAX_UPLOAD_SCENES) return false;
            expectedCount = static_cast<size_t>(parsed);
            sawCount = true;
        } else if (line.startsWith("SCENE=")) {
            if (count >= MAX_UPLOAD_SCENES) return false;
            String value = line.substring(6);
            const int comma = value.lastIndexOf(',');
            if (comma <= 0) return false;
            ids[count] = value.substring(0, comma);
            const String existedText = value.substring(comma + 1);
            if (!validSceneId(ids[count]) || (existedText != "0" && existedText != "1")) return false;
            existed[count] = existedText == "1";
            ++count;
        }
        if (end >= journal.length()) break;
        pos = end + 1;
    }
    if (!sawConfig || !sawCount || count != expectedCount) return false;

    // Recovery policy is deliberately rollback: an interrupted transaction
    // never becomes active merely because some renames happened before power loss.
    for (size_t i = 0; i < count; ++i) {
        const String canonical = scenePathFor(ids[i]);
        const String backup = sceneBackupPathFor(ids[i]);
        const String localBackup = canonical + ".bak";
        const String temp = canonical + ".tmp";
        if (LittleFS.exists(canonical) && !LittleFS.remove(canonical)) return false;
        if (existed[i]) {
            if (!LittleFS.exists(backup) || !copyFile(backup, canonical)) {
                // A local storage backup may still contain the old scene if
                // failure occurred before the upload-manager copy was complete.
                if (LittleFS.exists(localBackup) && LittleFS.rename(localBackup, canonical)) {
                    // recovered from storage's own backup
                } else {
                    return false;
                }
            }
        }
        if (LittleFS.exists(temp)) LittleFS.remove(temp);
        if (LittleFS.exists(localBackup)) LittleFS.remove(localBackup);
    }

    if (configExisted) {
        if (!LittleFS.exists(TXN_CONFIG_BACKUP) ||
            !copyFile(TXN_CONFIG_BACKUP, String(CONFIG_PATH))) return false;
    } else if (LittleFS.exists(CONFIG_PATH) && !LittleFS.remove(CONFIG_PATH)) {
        return false;
    }
    cleanupTransaction(ids, count);
    return true;
}

UploadResult SceneAssignmentUploadV1::install(const UploadSceneText* scenes,
                                               size_t sceneCount,
                                               const char* configText,
                                               size_t configLength) {
    lastError_ = "";
    if (!storage_ || !storage_->mounted()) {
        setError("upload manager not ready");
        return UploadResult::NOT_READY;
    }
    if (!configText || configLength == 0) {
        setError("invalid assignment.config input");
        return UploadResult::INVALID_CONFIG;
    }
    if (sceneCount > MAX_UPLOAD_SCENES) {
        setError("too many scene files in upload");
        return UploadResult::TOO_MANY_SCENES;
    }

    // PRE-FLIGHT: parse every incoming scene ID first. This makes duplicate
    // decisions independent of filenames and lets the config validator see
    // the complete resulting scene set before anything is committed.
    String incomingIds[MAX_UPLOAD_SCENES];
    bool existedBefore[MAX_UPLOAD_SCENES] = {};
    for (size_t i = 0; i < sceneCount; ++i) {
        if (!scenes[i].text || scenes[i].length == 0 ||
            !extractSceneId(scenes[i].text, scenes[i].length, incomingIds[i])) {
            setError("invalid scene in upload");
            return UploadResult::SCENE_INSTALL_FAILED;
        }
        existedBefore[i] = storage_->existsById(incomingIds[i].c_str());
        for (size_t j = 0; j < i; ++j) {
            if (incomingIds[i].equals(incomingIds[j])) {
                setError("duplicate Scene ID inside upload");
                return UploadResult::DUPLICATE_UPLOAD_ID;
            }
        }
    }

    IndicatorAssignmentConfig::AssignmentConfigV1 parser;
    IndicatorAssignmentConfig::SceneReference refs[IndicatorAssignmentConfig::MAX_SCENE_REFERENCES];
    size_t refCount = 0;
    if (!parser.parseAndValidate(configText, configLength, refs, refCount)) {
        setError(parser.lastError());
        return UploadResult::INVALID_CONFIG;
    }

    // PRE-FLIGHT CONFIG REFERENCES: existing LittleFS scenes OR scenes in
    // this upload are enough. The upload order does not matter.
    for (size_t r = 0; r < refCount; ++r) {
        bool found = storage_->existsById(refs[r].sceneId.c_str());
        if (!found) {
            for (size_t i = 0; i < sceneCount; ++i) {
                if (incomingIds[i].equals(refs[r].sceneId)) { found = true; break; }
            }
        }
        if (!found) {
            setError("assignment.config references missing Scene ID");
            return UploadResult::MISSING_SCENE_REFERENCE;
        }
    }

    // STAGE every incoming scene and the config before touching canonical files.
    // Backups plus a persistent journal allow rollback after a reset at any point
    // in the multi-file commit sequence.
    String tempConfig;
    if (!stageConfig(configText, configLength, tempConfig)) {
        setError("assignment.config staging failed");
        return UploadResult::CONFIG_WRITE_FAILED;
    }
    String verifyConfig;
    if (!readFile(tempConfig.c_str(), verifyConfig) || verifyConfig.length() != configLength) {
        LittleFS.remove(tempConfig);
        setError("assignment.config staging verification failed");
        return UploadResult::VERIFY_FAILED;
    }

    // Copy the old config and every existing scene to transaction-owned backup
    // files. The journal is published only after all backups are verified.
    const bool configExisted = LittleFS.exists(CONFIG_PATH);
    if (configExisted && !copyFile(CONFIG_PATH, TXN_CONFIG_BACKUP)) {
        LittleFS.remove(tempConfig);
        setError("cannot back up active assignment.config");
        return UploadResult::CONFIG_WRITE_FAILED;
    }
    for (size_t i = 0; i < sceneCount; ++i) {
        const String canonical = scenePathFor(incomingIds[i]);
        const String backup = sceneBackupPathFor(incomingIds[i]);
        if (existedBefore[i] && !copyFile(canonical, backup)) {
            cleanupTransaction(incomingIds, sceneCount);
            setError("cannot back up an existing scene");
            return UploadResult::SCENE_INSTALL_FAILED;
        }
    }
    if (!writeJournal(incomingIds, existedBefore, sceneCount, configExisted)) {
        cleanupTransaction(incomingIds, sceneCount);
        setError("cannot create upload transaction journal");
        return UploadResult::CONFIG_WRITE_FAILED;
    }

    // Stop an affected runtime only after all input validation and rollback
    // backups are ready, but before replacing any canonical file.
    if (runtime_) {
        for (size_t i = 0; i < sceneCount; ++i) {
            const IndicatorSceneRuntime::RuntimeResult stopped =
                runtime_->prepareSceneForUpdate(incomingIds[i].c_str());
            if (stopped != IndicatorSceneRuntime::RuntimeResult::OK) {
                recoverTransaction();
                setError("affected scene could not be stopped safely; upload aborted");
                return UploadResult::SCENE_INSTALL_FAILED;
            }
        }
    }

    size_t storedCount = 0;
    for (size_t i = 0; i < sceneCount; ++i) {
        String storedId;
        const IndicatorSceneStorage::StoreResult result =
            storage_->installFromText(scenes[i].text, scenes[i].length, &storedId);
        if (result == IndicatorSceneStorage::StoreResult::STORED ||
            result == IndicatorSceneStorage::StoreResult::REPLACED) {
            ++storedCount;
            continue;
        }
        if (recoverTransaction()) {
            setError("scene installation failed; previous committed data restored");
        } else {
            setError("scene installation failed and rollback needs recovery on reboot");
        }
        return UploadResult::SCENE_INSTALL_FAILED;
    }

    if (!commitConfig(tempConfig)) {
        if (recoverTransaction()) {
            setError("assignment.config commit failed; previous committed data restored");
        } else {
            setError("assignment.config commit failed and rollback needs recovery on reboot");
        }
        return UploadResult::CONFIG_WRITE_FAILED;
    }

    // Commit is now complete. Removing the journal declares the new pair active.
    // If power fails before this cleanup, boot recovery conservatively rolls back.
    cleanupTransaction(incomingIds, sceneCount);
    setError("");
    return storedCount > 0 ? UploadResult::INSTALLED : UploadResult::DUPLICATE_ONLY;
}

bool SceneAssignmentUploadV1::readActiveConfig(String& out) const {
    return readFile(CONFIG_PATH, out);
}

const char* SceneAssignmentUploadV1::resultName(UploadResult result) {
    switch (result) {
        case UploadResult::INSTALLED: return "INSTALLED";
        case UploadResult::DUPLICATE_ONLY: return "DUPLICATE_ONLY";
        case UploadResult::CONFIG_INSTALLED: return "CONFIG_INSTALLED";
        case UploadResult::INVALID_CONFIG: return "INVALID_CONFIG";
        case UploadResult::MISSING_SCENE_REFERENCE: return "MISSING_SCENE_REFERENCE";
        case UploadResult::SCENE_INSTALL_FAILED: return "SCENE_INSTALL_FAILED";
        case UploadResult::CONFIG_WRITE_FAILED: return "CONFIG_WRITE_FAILED";
        case UploadResult::VERIFY_FAILED: return "VERIFY_FAILED";
        case UploadResult::NOT_READY: return "NOT_READY";
        case UploadResult::TOO_MANY_SCENES: return "TOO_MANY_SCENES";
        case UploadResult::DUPLICATE_UPLOAD_ID: return "DUPLICATE_UPLOAD_ID";
    }
    return "UNKNOWN";
}

} // namespace IndicatorSceneUpload
