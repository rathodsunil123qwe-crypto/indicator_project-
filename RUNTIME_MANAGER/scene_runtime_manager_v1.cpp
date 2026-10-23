#include "scene_runtime_manager_v1.h"
#include <utility>
#include "../SCENE_VM/scene_vm_v1_factory.h"

namespace IndicatorSceneRuntime {
namespace {

// NO_PLATE scenes are allowed to be static. A static scene must latch its
// output instead of being reloaded in a zero-time completion loop. A scene
// containing a timed WAIT is treated as an animation and keeps the existing
// NO_PLATE repeat behavior. WAIT TARGET is not valid for NO_PLATE scenes.
bool extractRuntimeSceneId(const String& sceneText, String& sceneId) {
    sceneId = "";
    int pos = 0;
    bool sawScene = false;
    while (pos <= sceneText.length()) {
        int end = sceneText.indexOf('\n', pos);
        if (end < 0) end = sceneText.length();
        String line = sceneText.substring(pos, end);
        line.replace("\r", "");
        line.trim();
        if (line.length() > 0 && line[0] != '#') {
            if (!sawScene) {
                if (!line.equalsIgnoreCase("SCENE")) return false;
                sawScene = true;
            } else if (line.equalsIgnoreCase("COLORS")) {
                break;
            } else {
                const int eq = line.indexOf('=');
                if (eq > 0) {
                    String key = line.substring(0, eq);
                    key.trim();
                    if (key.equalsIgnoreCase("ID")) {
                        if (sceneId.length() != 0) return false;
                        sceneId = line.substring(eq + 1);
                        sceneId.trim();
                    }
                }
            }
        }
        if (end >= sceneText.length()) break;
        pos = end + 1;
    }
    return sawScene && sceneId.length() > 0;
}

bool sceneHasTimedWait(const String& sceneText) {
    int script = sceneText.indexOf("SCRIPT");
    if (script < 0) return false;
    int pos = script + 6;
    while (pos < sceneText.length()) {
        int end = sceneText.indexOf('\n', pos);
        if (end < 0) end = sceneText.length();
        String line = sceneText.substring(pos, end);
        line.trim();
        if (line.startsWith("WAIT ") || line.startsWith("wait ")) {
            String arg = line.substring(5);
            arg.trim();
            if (!arg.equalsIgnoreCase("TARGET") && arg.length() > 0) return true;
        }
        if (end >= sceneText.length()) break;
        pos = end + 1;
    }
    return false;
}

} // namespace

bool SceneRuntimeManagerV1::begin(
    IndicatorSceneStorage::SceneStorageV1& storage,
    IndicatorSceneVM::SceneVM& noPlateVm,
    IndicatorSceneVM::SceneVM& indicatorVm,
    IndicatorSceneVM::MotionPort& indicatorMotion,
    IndicatorLED::LEDRuntimeV1& ledRuntime) {
    storage_ = &storage;
    noPlateVm_ = &noPlateVm;
    indicatorVm_ = &indicatorVm;
    pausedIndicatorVm_ = IndicatorSceneVM::createSceneVMV1();
    indicatorMotion_ = &indicatorMotion;
    ledRuntime_ = &ledRuntime;
    noPlateLedPort_ = new NoPlateLEDPortAdapter(ledRuntime);
    indicatorLedPort_ = new IndicatorLEDPortAdapter(ledRuntime);
    welcomeId_ = "";
    turnSignalId_ = "";
    turnSignalSide_ = IndicatorSceneVM::Side::NONE;
    lastError_ = "";
    noPlateButton_ = nullptr;
    noPlateSelection_ = IndicatorNoPlate::SideSelection::NORMAL;
    noPlateId_ = "";
    noPlateEnabled_ = false;
    noPlateRunning_ = false;
    noPlatePending_ = false;
    noPlateLoopOnComplete_ = true;
    pausedIndicatorScene_ = false;
    pausedRepeatIndicatorScene_ = false;
    pausedRepeatIndicatorSceneText_ = "";
    pausedRepeatIndicatorContext_ = IndicatorSceneVM::SceneContext{};
    pausedTurnSignalSide_ = IndicatorSceneVM::Side::NONE;
    pausedIndicatorOutputs_.valid = false;
    syncedSceneActive_ = false;
    syncedNoPlateWasPaused_ = false;
    syncedSavedNoPlateOutputs_.valid = false;
    repeatSyncedScene_ = false;
    repeatSyncedSceneText_ = "";
    repeatSyncedContext_ = IndicatorSceneVM::SceneContext{};
    pausedSyncedScene_ = false;
    pausedSyncedNoPlateOutputs_.valid = false;

    if (!storage_->mounted() || !noPlateVm_ || !indicatorVm_ || !pausedIndicatorVm_ || !indicatorMotion_ || !noPlateLedPort_ || !indicatorLedPort_) {
        setError("runtime domains are not ready");
        return false;
    }

    // Two independent SceneVM execution domains share only the proven LED
    // hardware runtime. NO_PLATE gets no MotionPort and only NO_PLATELEDS;
    // INDICATOR gets the proven motion port and only indicator LED zones.
    noPlateVm_->begin(nullptr, noPlateLedPort_);
    indicatorVm_->begin(indicatorMotion_, indicatorLedPort_);
    pausedIndicatorVm_->begin(indicatorMotion_, indicatorLedPort_);
    return true;
}

bool SceneRuntimeManagerV1::readActiveAssignmentConfig(String& out) const {
    out = "";
    if (!storage_ || !storage_->mounted()) return false;

    File f = LittleFS.open("/config/assignment.config", "r");
    if (!f || f.isDirectory()) {
        if (f) f.close();
        return false;
    }

    const size_t n = f.size();
    out.reserve(n + 1);
    while (f.available()) {
        const int c = f.read();
        if (c < 0) {
            f.close();
            out = "";
            return false;
        }
        out += static_cast<char>(c);
    }
    f.close();

    if (out.length() != n) {
        out = "";
        return false;
    }

    // Only expose a configuration that passes the same assignment validation
    // used by runtime resolution. The raw stored file remains unchanged.
    IndicatorAssignmentConfig::AssignmentConfigV1 parser;
    IndicatorAssignmentConfig::SceneReference refs[
        IndicatorAssignmentConfig::MAX_SCENE_REFERENCES];
    size_t refCount = 0;
    if (!parser.parseAndValidate(out.c_str(), out.length(), refs, refCount)) {
        out = "";
        return false;
    }
    return true;
}

bool SceneRuntimeManagerV1::sceneHasCapability(const String& sceneText, const char* capability) {
    if (!capability || !*capability) return false;

    const int colorsAt = sceneText.indexOf("COLORS");
    if (colorsAt < 0) return false;

    int pos = 0;
    while (pos < colorsAt) {
        int end = sceneText.indexOf('\n', pos);
        if (end < 0 || end > colorsAt) end = colorsAt;

        String line = sceneText.substring(pos, end);
        line.replace("\r", "");
        line.trim();

        if (line.startsWith("CAPABILITIES") || line.startsWith("capabilities") ||
            line.startsWith("Capabilities")) {
            const int eq = line.indexOf('=');
            if (eq < 0) return false;
            String values = line.substring(eq + 1);
            values.trim();

            int valueStart = 0;
            while (valueStart <= values.length()) {
                int comma = values.indexOf(',', valueStart);
                if (comma < 0) comma = values.length();
                String value = values.substring(valueStart, comma);
                value.trim();
                if (value.equalsIgnoreCase(capability)) return true;
                if (comma >= values.length()) break;
                valueStart = comma + 1;
            }
            return false;
        }

        if (end >= colorsAt) break;
        pos = end + 1;
    }
    return false;
}

RuntimeResult SceneRuntimeManagerV1::startSyncedScene(
    const String& sceneText,
    const IndicatorSceneVM::SceneContext& context) {

    if (!indicatorVm_ || !noPlateVm_ || !indicatorMotion_ || !ledRuntime_ || !indicatorLedPort_) {
        setError("SYNCED runtime domain is not ready");
        return RuntimeResult::NOT_READY;
    }
    if (sceneText.length() == 0) {
        setError("SYNCED scene is empty");
        return RuntimeResult::SCENE_LOAD_FAILED;
    }
    if (!sceneHasCapability(sceneText, "SYNCED_COMPATIBLE")) {
        setError("scene is not SYNCED_COMPATIBLE");
        return RuntimeResult::SCENE_LOAD_FAILED;
    }

    // NORMAL and SYNCED are one INDICATOR scene body for scene-control.
    // A new SYNCED scene therefore replaces any paused underlying scene
    // instead of creating a second pause layer.
    if (pausedIndicatorScene_) {
        const RuntimeResult discarded = discardPausedIndicatorForReplacement();
        if (discarded != RuntimeResult::OK) return discarded;
    }

    // If the current foreground is another INDICATOR scene (including a TURN
    // SIGNAL), replace it directly. The NO_PLATE domain remains independent
    // until this SYNCED scene explicitly takes temporary ownership.
    cancelTurnSignalAutoOffTimer();
    if (indicatorVm_->active()) {
        indicatorVm_->stop();
    }
    repeatIndicatorScene_ = false;
    repeatIndicatorSceneText_ = "";
    repeatIndicatorContext_ = IndicatorSceneVM::SceneContext{};
    turnSignalSide_ = IndicatorSceneVM::Side::NONE;
    if (!ledRuntime_->clearIndicatorOutputs()) {
        setError(ledRuntime_->lastError() ? ledRuntime_->lastError() :
                 "INDICATOR LED clear failed before SYNCED replacement");
        return RuntimeResult::SCENE_START_FAILED;
    }

    // A new SYNCED request replaces an already-running SYNCED scene directly.
    // Do not create a second paused SYNCED context.
    if (syncedSceneActive_) {
        if (indicatorVm_->active()) indicatorVm_->stop();
        const RuntimeResult finished = finishSyncedScene();
        if (finished != RuntimeResult::OK) return finished;
        repeatSyncedScene_ = false;
        repeatSyncedSceneText_ = "";
        repeatSyncedContext_ = IndicatorSceneVM::SceneContext{};
    }

    // SYNCED is a foreground INDICATOR scene with one special privilege:
    // temporary ownership of NO_PLATELEDS. Preserve the NO_PLATE execution
    // context and its current framebuffer before handing that LED zone over.
    if (!ledRuntime_->captureNoPlateOutputs(syncedSavedNoPlateOutputs_)) {
        setError(ledRuntime_->lastError() ? ledRuntime_->lastError() :
                 "NO_PLATE LED snapshot failed before SYNCED");
        return RuntimeResult::SCENE_START_FAILED;
    }

    syncedNoPlateWasPaused_ = false;
    if (noPlateVm_->active()) {
        noPlateVm_->pause();
        if (!noPlateVm_->paused()) {
            syncedSavedNoPlateOutputs_.valid = false;
            setError("NO_PLATE scene could not be paused for SYNCED");
            return RuntimeResult::SCENE_START_FAILED;
        }
        syncedNoPlateWasPaused_ = true;
    }

    // SYNCED owns the NO_PLATE LED zone only while its foreground VM runs.
    indicatorLedPort_->setSyncedOwnership(true);

    // Clear only the INDICATOR visual domain. NO_PLATE is now owned by SYNCED.
    if (!ledRuntime_->clearIndicatorOutputs()) {
        indicatorLedPort_->setSyncedOwnership(false);
        if (syncedNoPlateWasPaused_) noPlateVm_->resume();
        syncedNoPlateWasPaused_ = false;
        syncedSavedNoPlateOutputs_.valid = false;
        setError(ledRuntime_->lastError() ? ledRuntime_->lastError() :
                 "INDICATOR LED clear failed before SYNCED");
        return RuntimeResult::SCENE_START_FAILED;
    }

    if (indicatorVm_->load(sceneText.c_str(), sceneText.length()) != IndicatorSceneVM::VMResult::OK ||
        indicatorVm_->start(context) != IndicatorSceneVM::VMResult::OK) {
        indicatorLedPort_->setSyncedOwnership(false);
        if (syncedNoPlateWasPaused_) noPlateVm_->resume();
        ledRuntime_->restoreNoPlateOutputs(syncedSavedNoPlateOutputs_);
        syncedNoPlateWasPaused_ = false;
        syncedSavedNoPlateOutputs_.valid = false;
        setError(indicatorVm_->lastError() ? indicatorVm_->lastError() :
                 "SYNCED scene start failed");
        return RuntimeResult::SCENE_START_FAILED;
    }

    syncedSceneActive_ = true;
    pausedSyncedScene_ = false;
    repeatSyncedScene_ = true;
    repeatSyncedSceneText_ = sceneText;
    repeatSyncedContext_ = context;

    Serial.println("[RUNTIME] SYNCED SCENE STARTED | NO_PLATE LED OWNERSHIP -> SYNCED");
    return RuntimeResult::OK;
}

RuntimeResult SceneRuntimeManagerV1::finishSyncedScene() {
    if (!syncedSceneActive_) return RuntimeResult::OK;
    if (!indicatorVm_ || !noPlateVm_ || !ledRuntime_ || !indicatorLedPort_) {
        setError("SYNCED runtime domain is not ready during finish");
        return RuntimeResult::NOT_READY;
    }

    indicatorLedPort_->setSyncedOwnership(false);
    if (syncedNoPlateWasPaused_) {
        if (noPlateVm_->resume() != IndicatorSceneVM::VMResult::OK) {
            setError(noPlateVm_->lastError() ? noPlateVm_->lastError() :
                     "NO_PLATE scene resume failed after SYNCED");
            return RuntimeResult::SCENE_START_FAILED;
        }
    }

    const bool restored = ledRuntime_->restoreNoPlateOutputs(syncedSavedNoPlateOutputs_);
    const char* restoreError = ledRuntime_->lastError();

    syncedSceneActive_ = false;
    syncedNoPlateWasPaused_ = false;
    syncedSavedNoPlateOutputs_.valid = false;

    // IMPORTANT: keep the SYNCED repeat source alive across a normal
    // completion. update() uses it immediately after finishSyncedScene().

    pausedSyncedScene_ = false;
    pausedSyncedNoPlateOutputs_.valid = false;

    if (!restored) {
        setError(restoreError ? restoreError :
                 "NO_PLATE LED restore failed after SYNCED");
        return RuntimeResult::SCENE_START_FAILED;
    }

    Serial.println("[RUNTIME] SYNCED SCENE COMPLETE | NO_PLATE LED OWNERSHIP -> NO_PLATE");
    return RuntimeResult::OK;
}

RuntimeResult SceneRuntimeManagerV1::discardPausedIndicatorForReplacement() {
    if (!pausedIndicatorScene_) return RuntimeResult::OK;
    if (!indicatorVm_ || !pausedIndicatorVm_ || !ledRuntime_ || !indicatorLedPort_) {
        setError("INDICATOR paused replacement runtime is not ready");
        return RuntimeResult::NOT_READY;
    }

    // The current foreground VM is normally the TURN SIGNAL. It is being
    // replaced by a new NORMAL/click-mapped scene, so discard it directly.
    if (indicatorVm_->active()) {
        indicatorVm_->stop();
    }

    if (!ledRuntime_->clearIndicatorOutputs()) {
        setError(ledRuntime_->lastError() ? ledRuntime_->lastError() :
                 "INDICATOR LED clear failed while replacing paused scene");
        return RuntimeResult::SCENE_START_FAILED;
    }

    // If the parked scene was SYNCED, its temporary NO_PLATE ownership was
    // already released when the scene was paused for TURN SIGNAL. At that
    // point NO_PLATE was resumed and its framebuffer was restored, so there
    // is NO second NO_PLATE restore to perform here. The paused SYNCED scene
    // is simply discarded as one INDICATOR scene body.
    if (pausedSyncedScene_) {
        indicatorLedPort_->setSyncedOwnership(false);

        // Normally these are already false/invalid after pauseIndicatorForTurn().
        // Keep this defensive cleanup so replacement cannot leave NO_PLATE
        // parked if a future path preserves these flags.
        if (syncedNoPlateWasPaused_) {
            if (noPlateVm_->resume() != IndicatorSceneVM::VMResult::OK) {
                setError(noPlateVm_->lastError() ? noPlateVm_->lastError() :
                         "NO_PLATE scene resume failed while replacing paused SYNCED");
                return RuntimeResult::SCENE_START_FAILED;
            }
        }

        syncedNoPlateWasPaused_ = false;
        syncedSavedNoPlateOutputs_.valid = false;
        pausedSyncedNoPlateOutputs_.valid = false;
        pausedSyncedScene_ = false;
        repeatSyncedScene_ = false;
        repeatSyncedSceneText_ = "";
        repeatSyncedContext_ = IndicatorSceneVM::SceneContext{};
    }

    // The parked VM is no longer a resumable scene. Stop and discard it.
    pausedIndicatorVm_->stop();

    pausedIndicatorScene_ = false;
    pausedRepeatIndicatorScene_ = false;
    pausedRepeatIndicatorSceneText_ = "";
    pausedRepeatIndicatorContext_ = IndicatorSceneVM::SceneContext{};
    pausedTurnSignalSide_ = IndicatorSceneVM::Side::NONE;
    pausedIndicatorOutputs_.valid = false;

    Serial.println("[RUNTIME] PAUSED INDICATOR SCENE DISCARDED -> DIRECT REPLACEMENT");
    return RuntimeResult::OK;
}

RuntimeResult SceneRuntimeManagerV1::replaceIndicatorScene(
    const String& sceneText,
    const IndicatorSceneVM::SceneContext& context,
    bool preservePausedIndicator) {

    if (!indicatorVm_ || !indicatorMotion_ || !ledRuntime_) {
        setError("INDICATOR runtime domain is not ready");
        return RuntimeResult::NOT_READY;
    }

    if (syncedSceneActive_) {
        if (indicatorVm_->active()) indicatorVm_->stop();
        const RuntimeResult finished = finishSyncedScene();
        if (finished != RuntimeResult::OK) return finished;
    }

    // A new NORMAL/click-mapped scene replaces an underlying paused scene
    // instead of creating another pause layer. This is especially important
    // for SYNCED: 4 -> L -> 3 must discard paused 4 completely.
    if (pausedIndicatorScene_ && !preservePausedIndicator) {
        const RuntimeResult discarded = discardPausedIndicatorForReplacement();
        if (discarded != RuntimeResult::OK) return discarded;
    }

    // Any new INDICATOR scene replacing a TURN SIGNAL cancels the TURN
    // SIGNAL-only auto-off timer. playTurnSignal() starts it again after its
    // own replacement succeeds.
    cancelTurnSignalAutoOffTimer();

    if (sceneText.length() == 0) {
        setError("replacement INDICATOR scene is empty");
        return RuntimeResult::SCENE_LOAD_FAILED;
    }

    // Scene replacement is a runtime concern only. Stop the current VM first;
    // SceneVM::stop() also stops any motion currently owned by that VM.
    if (indicatorVm_->active()) {
        indicatorVm_->stop();
    }

    // The old scene must never be allowed to repeat after replacement.
    repeatIndicatorScene_ = false;
    repeatIndicatorSceneText_ = "";
    repeatIndicatorContext_ = IndicatorSceneVM::SceneContext{};
    turnSignalSide_ = IndicatorSceneVM::Side::NONE;

    // Remove the old INDICATOR visual state before the new scene starts.
    // NO_PLATE output is preserved by clearIndicatorOutputs().
    if (!ledRuntime_->clearIndicatorOutputs()) {
        setError(ledRuntime_->lastError() ? ledRuntime_->lastError() :
                 "INDICATOR LED clear failed during scene replacement");
        return RuntimeResult::SCENE_START_FAILED;
    }

    if (indicatorVm_->load(sceneText.c_str(), sceneText.length()) != IndicatorSceneVM::VMResult::OK) {
        setError(indicatorVm_->lastError() ? indicatorVm_->lastError() :
                 "replacement INDICATOR scene load failed");
        return RuntimeResult::SCENE_LOAD_FAILED;
    }

    if (indicatorVm_->start(context) != IndicatorSceneVM::VMResult::OK) {
        setError(indicatorVm_->lastError() ? indicatorVm_->lastError() :
                 "replacement INDICATOR scene start failed");
        return RuntimeResult::SCENE_START_FAILED;
    }

    return RuntimeResult::OK;
}

RuntimeResult SceneRuntimeManagerV1::pauseIndicatorForTurn() {
    if (!indicatorVm_ || !pausedIndicatorVm_ || !ledRuntime_) {
        setError("INDICATOR pause runtime is not ready");
        return RuntimeResult::NOT_READY;
    }
    if (pausedIndicatorScene_) return RuntimeResult::OK;
    if (!indicatorVm_->active()) return RuntimeResult::OK;

    if (!ledRuntime_->captureIndicatorOutputs(pausedIndicatorOutputs_)) {
        setError(ledRuntime_->lastError() ? ledRuntime_->lastError() :
                 "INDICATOR LED snapshot failed");
        return RuntimeResult::SCENE_START_FAILED;
    }

    // SYNCED is paused exactly like a normal INDICATOR scene, but it also
    // owns NO_PLATELEDS. While TURN SIGNAL is in the foreground, give those
    // LEDs back to NO_PLATE and remember the SYNCED framebuffer for resume.
    const bool pausingSynced = syncedSceneActive_;
    if (pausingSynced) {
        if (!ledRuntime_->captureNoPlateOutputs(pausedSyncedNoPlateOutputs_)) {
            setError(ledRuntime_->lastError() ? ledRuntime_->lastError() :
                     "SYNCED NO_PLATE LED snapshot failed before TURN SIGNAL");
            return RuntimeResult::SCENE_START_FAILED;
        }

        indicatorLedPort_->setSyncedOwnership(false);

        if (syncedNoPlateWasPaused_) {
            if (noPlateVm_->resume() != IndicatorSceneVM::VMResult::OK) {
                indicatorLedPort_->setSyncedOwnership(true);
                setError(noPlateVm_->lastError() ? noPlateVm_->lastError() :
                         "NO_PLATE scene resume failed before TURN SIGNAL");
                return RuntimeResult::SCENE_START_FAILED;
            }
        }

        if (!ledRuntime_->restoreNoPlateOutputs(syncedSavedNoPlateOutputs_)) {
            if (syncedNoPlateWasPaused_) noPlateVm_->pause();
            indicatorLedPort_->setSyncedOwnership(true);
            setError(ledRuntime_->lastError() ? ledRuntime_->lastError() :
                     "NO_PLATE LED restore failed before TURN SIGNAL");
            return RuntimeResult::SCENE_START_FAILED;
        }

        syncedSceneActive_ = false;
        pausedSyncedScene_ = true;
        syncedNoPlateWasPaused_ = false;
        syncedSavedNoPlateOutputs_.valid = false;
    }

    pausedRepeatIndicatorScene_ = repeatIndicatorScene_;
    pausedRepeatIndicatorSceneText_ = repeatIndicatorSceneText_;
    pausedRepeatIndicatorContext_ = repeatIndicatorContext_;
    pausedTurnSignalSide_ = turnSignalSide_;

    indicatorVm_->pause();
    if (!indicatorVm_->paused()) {
        setError("INDICATOR scene could not be paused");
        return RuntimeResult::SCENE_START_FAILED;
    }

    IndicatorSceneVM::SceneVM* oldForeground = indicatorVm_;
    indicatorVm_ = pausedIndicatorVm_;
    pausedIndicatorVm_ = oldForeground;

    repeatIndicatorScene_ = false;
    repeatIndicatorSceneText_ = "";
    repeatIndicatorContext_ = IndicatorSceneVM::SceneContext{};
    turnSignalSide_ = IndicatorSceneVM::Side::NONE;
    pausedIndicatorScene_ = true;
    return RuntimeResult::OK;
}


void SceneRuntimeManagerV1::cancelTurnSignalAutoOffTimer() {
    turnSignalAutoOffActive_ = false;
    turnSignalAutoOffStartMs_ = 0;
    turnSignalAutoOffDurationMs_ = 0;
}

bool SceneRuntimeManagerV1::startTurnSignalAutoOffTimer(const String& config) {
    IndicatorAssignmentConfig::AssignmentConfigV1 parser;
    uint32_t seconds = 0;

    if (!parser.getTurnSignalAutoOffSeconds(
            config.c_str(), config.length(), seconds)) {
        setError(parser.lastError() ? parser.lastError() :
                 "TURN SIGNAL AUTO-OFF TIMER could not be read");
        cancelTurnSignalAutoOffTimer();
        return false;
    }

    // Use the current Arduino time as the timer origin. update() receives
    // the same millis() timebase, so wrap-safe elapsed-time comparison works.
    turnSignalAutoOffStartMs_ = millis();
    turnSignalAutoOffDurationMs_ = seconds * 1000UL;
    turnSignalAutoOffActive_ = true;

    Serial.printf("[RUNTIME] TURN SIGNAL AUTO-OFF TIMER STARTED | %lu sec\n",
                  (unsigned long)seconds);
    return true;
}

bool SceneRuntimeManagerV1::turnSignalAutoOffExpired(uint32_t nowMs) const {
    if (!turnSignalAutoOffActive_) return false;
    return (uint32_t)(nowMs - turnSignalAutoOffStartMs_) >=
           turnSignalAutoOffDurationMs_;
}

RuntimeResult SceneRuntimeManagerV1::resumePausedIndicatorAfterTurn() {
    if (!pausedIndicatorScene_ || !indicatorVm_ || !pausedIndicatorVm_ || !ledRuntime_) {
        return RuntimeResult::NO_ASSIGNMENT;
    }

    // The foreground TURN SIGNAL VM is discarded. The parked VM becomes the
    // foreground execution context again, preserving its exact PC/env/wait.
    indicatorVm_->stop();
    if (!ledRuntime_->clearIndicatorOutputs()) {
        setError(ledRuntime_->lastError() ? ledRuntime_->lastError() :
                 "INDICATOR LED clear failed before resume");
        return RuntimeResult::SCENE_START_FAILED;
    }

    const bool resumeSynced = pausedSyncedScene_;

    if (resumeSynced) {
        // Reclaim NO_PLATE ownership and park NO_PLATE again before restoring
        // the exact SYNCED NO_PLATE framebuffer.
        if (!ledRuntime_->captureNoPlateOutputs(syncedSavedNoPlateOutputs_)) {
            setError(ledRuntime_->lastError() ? ledRuntime_->lastError() :
                     "NO_PLATE LED snapshot failed before SYNCED resume");
            return RuntimeResult::SCENE_START_FAILED;
        }

        if (noPlateVm_->active()) {
            noPlateVm_->pause();
            if (!noPlateVm_->paused()) {
                syncedSavedNoPlateOutputs_.valid = false;
                setError("NO_PLATE scene could not be paused for SYNCED resume");
                return RuntimeResult::SCENE_START_FAILED;
            }
            syncedNoPlateWasPaused_ = true;
        } else {
            syncedNoPlateWasPaused_ = false;
        }

        indicatorLedPort_->setSyncedOwnership(true);
        if (!ledRuntime_->restoreNoPlateOutputs(pausedSyncedNoPlateOutputs_)) {
            indicatorLedPort_->setSyncedOwnership(false);
            if (syncedNoPlateWasPaused_) noPlateVm_->resume();
            syncedNoPlateWasPaused_ = false;
            syncedSavedNoPlateOutputs_.valid = false;
            setError(ledRuntime_->lastError() ? ledRuntime_->lastError() :
                     "SYNCED NO_PLATE LED restore failed on resume");
            return RuntimeResult::SCENE_START_FAILED;
        }
    }

    std::swap(indicatorVm_, pausedIndicatorVm_);
    if (indicatorVm_->resume() != IndicatorSceneVM::VMResult::OK) {
        if (resumeSynced) {
            indicatorLedPort_->setSyncedOwnership(false);
            if (syncedNoPlateWasPaused_) noPlateVm_->resume();
            ledRuntime_->restoreNoPlateOutputs(syncedSavedNoPlateOutputs_);
            syncedNoPlateWasPaused_ = false;
            syncedSavedNoPlateOutputs_.valid = false;
        }
        setError(indicatorVm_->lastError() ? indicatorVm_->lastError() :
                 "paused INDICATOR scene resume failed");
        return RuntimeResult::SCENE_START_FAILED;
    }

    if (!ledRuntime_->restoreIndicatorOutputs(pausedIndicatorOutputs_)) {
        setError(ledRuntime_->lastError() ? ledRuntime_->lastError() :
                 "paused INDICATOR LED restore failed");
        return RuntimeResult::SCENE_START_FAILED;
    }

    repeatIndicatorScene_ = pausedRepeatIndicatorScene_;
    repeatIndicatorSceneText_ = pausedRepeatIndicatorSceneText_;
    repeatIndicatorContext_ = pausedRepeatIndicatorContext_;
    turnSignalSide_ = pausedTurnSignalSide_;

    if (resumeSynced) {
        syncedSceneActive_ = true;
        pausedSyncedScene_ = false;
        repeatSyncedScene_ = true;
        // The repeat source is reconstructed from the parked context below
        // only through the already-running VM; no reload occurs on resume.
    }

    pausedIndicatorScene_ = false;
    pausedRepeatIndicatorScene_ = false;
    pausedRepeatIndicatorSceneText_ = "";
    pausedRepeatIndicatorContext_ = IndicatorSceneVM::SceneContext{};
    pausedTurnSignalSide_ = IndicatorSceneVM::Side::NONE;
    pausedIndicatorOutputs_.valid = false;
    pausedSyncedNoPlateOutputs_.valid = false;
    return RuntimeResult::OK;
}

RuntimeResult SceneRuntimeManagerV1::playWelcome() {
    welcomeId_ = "";
    lastError_ = "";

    if (!storage_ || !indicatorVm_ || !storage_->mounted()) {
        setError("runtime manager not ready");
        return RuntimeResult::NOT_READY;
    }

    File f = LittleFS.open("/config/assignment.config", "r");
    if (!f || f.isDirectory()) {
        if (f) f.close();
        setError("active assignment.config not found");
        return RuntimeResult::CONFIG_ERROR;
    }

    const size_t n = f.size();
    String config;
    config.reserve(n + 1);
    while (f.available()) {
        int c = f.read();
        if (c < 0) {
            f.close();
            setError("active assignment.config read failed");
            return RuntimeResult::CONFIG_ERROR;
        }
        config += (char)c;
    }
    f.close();

    if (config.length() != n) {
        setError("active assignment.config length mismatch");
        return RuntimeResult::CONFIG_ERROR;
    }

    IndicatorAssignmentConfig::AssignmentConfigV1 parser;

    if (!parser.getFixedSceneId(
            config.c_str(), config.length(),
            IndicatorAssignmentConfig::FixedAssignment::WELCOME,
            welcomeId_)) {
        setError(parser.lastError());
        return RuntimeResult::CONFIG_ERROR;
    }

    // Load the complete stored .scene file. Editable colours are already part
    // of that file; the runtime no longer applies assignment colour overrides.
    String scene;
    if (!storage_->readById(welcomeId_.c_str(), scene)) {
        setError("assigned WELCOME Scene ID is not available in Scene Storage");
        return RuntimeResult::SCENE_NOT_FOUND;
    }

    IndicatorSceneVM::SceneContext context;
    const RuntimeResult replacement = replaceIndicatorScene(scene, context);
    if (replacement != RuntimeResult::OK) return replacement;

    return RuntimeResult::OK;
}


RuntimeResult SceneRuntimeManagerV1::playNoPlateSelection(
    IndicatorNoPlate::SideSelection selection) {

    if (!storage_ || !noPlateVm_ || !storage_->mounted()) {
        setError("runtime manager not ready");
        return RuntimeResult::NOT_READY;
    }

    IndicatorAssignmentConfig::FixedAssignment assignment =
        selection == IndicatorNoPlate::SideSelection::GRAPHIC
            ? IndicatorAssignmentConfig::FixedAssignment::NO_PLATE_GRAPHIC
            : IndicatorAssignmentConfig::FixedAssignment::NO_PLATE_NORMAL;

    File f = LittleFS.open("/config/assignment.config", "r");
    if (!f || f.isDirectory()) {
        if (f) f.close();
        setError("active assignment.config not found");
        return RuntimeResult::CONFIG_ERROR;
    }

    const size_t n = f.size();
    String config;
    config.reserve(n + 1);
    while (f.available()) {
        const int c = f.read();
        if (c < 0) {
            f.close();
            setError("active assignment.config read failed");
            return RuntimeResult::CONFIG_ERROR;
        }
        config += static_cast<char>(c);
    }
    f.close();
    if (config.length() != n) {
        setError("active assignment.config length mismatch");
        return RuntimeResult::CONFIG_ERROR;
    }

    IndicatorAssignmentConfig::AssignmentConfigV1 parser;
    String sceneId;
    if (!parser.getFixedSceneId(config.c_str(), config.length(), assignment, sceneId)) {
        setError(parser.lastError());
        return RuntimeResult::CONFIG_ERROR;
    }

    String scene;
    if (!storage_->readById(sceneId.c_str(), scene)) {
        setError("assigned NO-PLATE Scene ID is not available in Scene Storage");
        return RuntimeResult::SCENE_NOT_FOUND;
    }

    if (noPlateVm_->active()) noPlateVm_->stop();

    if (noPlateVm_->load(scene.c_str(), scene.length()) != IndicatorSceneVM::VMResult::OK) {
        setError(noPlateVm_->lastError() ? noPlateVm_->lastError() : "NO-PLATE scene load failed");
        return RuntimeResult::SCENE_LOAD_FAILED;
    }

    if (noPlateVm_->start() != IndicatorSceneVM::VMResult::OK) {
        setError(noPlateVm_->lastError() ? noPlateVm_->lastError() : "NO-PLATE scene start failed");
        return RuntimeResult::SCENE_START_FAILED;
    }

    // A static NO_PLATE scene (no timed WAIT) is a persistent state scene: it
    // should complete once and leave its LED framebuffer latched. Animated
    // NO_PLATE scenes retain the established automatic repeat behavior.
    noPlateLoopOnComplete_ = sceneHasTimedWait(scene);

    noPlateId_ = sceneId;
    noPlateSelection_ = selection;
    noPlateRunning_ = true;
    noPlatePending_ = false;
    return RuntimeResult::OK;
}

RuntimeResult SceneRuntimeManagerV1::playNoPlateCurrentSelection() {
    if (!noPlateButton_) {
        setError("NO-PLATE button is not configured");
        return RuntimeResult::NOT_READY;
    }
    return playNoPlateSelection(noPlateButton_->selection());
}

void SceneRuntimeManagerV1::beginNoPlate(IndicatorNoPlate::NoPlateButtonV1& button) {
    noPlateButton_ = &button;
    noPlateEnabled_ = true;
    noPlateRunning_ = false;
    noPlateSelection_ = button.selection();
    noPlatePending_ = true;
}


RuntimeResult SceneRuntimeManagerV1::playClickMappedScene(uint8_t clicks) {
    lastError_ = "";

    if (clicks < IndicatorAssignmentConfig::MULTI_CLICK_MIN ||
        clicks > IndicatorAssignmentConfig::MULTI_CLICK_MAX) {
        setError("invalid click-mapped scene number");
        return RuntimeResult::CONFIG_ERROR;
    }
    if (!storage_ || !indicatorVm_ || !storage_->mounted()) {
        setError("runtime manager not ready");
        return RuntimeResult::NOT_READY;
    }

    File f = LittleFS.open("/config/assignment.config", "r");
    if (!f || f.isDirectory()) {
        if (f) f.close();
        setError("active assignment.config not found");
        return RuntimeResult::CONFIG_ERROR;
    }

    const size_t n = f.size();
    String config;
    config.reserve(n + 1);
    while (f.available()) {
        const int c = f.read();
        if (c < 0) {
            f.close();
            setError("active assignment.config read failed");
            return RuntimeResult::CONFIG_ERROR;
        }
        config += (char)c;
    }
    f.close();
    if (config.length() != n) {
        setError("active assignment.config length mismatch");
        return RuntimeResult::CONFIG_ERROR;
    }

    IndicatorAssignmentConfig::AssignmentConfigV1 parser;
    String sceneId;
    if (!parser.getClickSceneId(config.c_str(), config.length(), clicks, sceneId)) {
        // A click without an assignment is intentionally a no-op. Keep the
        // currently active INDICATOR scene exactly as it is, or remain idle.
        if (parser.lastError() && parser.lastError()[0] != '\0') {
            setError(parser.lastError());
            return RuntimeResult::CONFIG_ERROR;
        }
        return RuntimeResult::NO_ASSIGNMENT;
    }

    String scene;
    if (!storage_->readById(sceneId.c_str(), scene)) {
        setError("assigned click-mapped Scene ID is not available in Scene Storage");
        return RuntimeResult::SCENE_NOT_FOUND;
    }

    IndicatorSceneVM::SceneContext context;

    // SYNCED is still an INDICATOR-domain scene, but it temporarily owns the
    // NO_PLATE LED zone. Do not send it through the ordinary NORMAL path.
    if (sceneHasCapability(scene, "SYNCED_COMPATIBLE")) {
        return startSyncedScene(scene, context);
    }

    // Reuse the same universal INDICATOR scene replacement mechanism used by
    // TURN SIGNAL. The newly assigned scene replaces any active INDICATOR
    // scene; NO_PLATE remains independent.
    const RuntimeResult replacement = replaceIndicatorScene(scene, context);
    if (replacement != RuntimeResult::OK) return replacement;

    // Click-mapped INDICATOR scenes use the current development-stage repeat
    // behavior, exactly like the existing TURN SIGNAL path.
    repeatIndicatorSceneText_ = scene;
    repeatIndicatorContext_ = context;
    repeatIndicatorScene_ = true;

    return RuntimeResult::OK;
}

RuntimeResult SceneRuntimeManagerV1::prepareSceneForUpdate(const char* sceneId) {
    if (!sceneId || !*sceneId) {
        setError("invalid Scene ID for runtime update preparation");
        return RuntimeResult::CONFIG_ERROR;
    }
    const String target(sceneId);
    bool affectsIndicator = false;
    String activeId;
    if (repeatIndicatorSceneText_.length() &&
        extractRuntimeSceneId(repeatIndicatorSceneText_, activeId) && activeId == target) {
        affectsIndicator = true;
    }
    if (repeatSyncedSceneText_.length() &&
        extractRuntimeSceneId(repeatSyncedSceneText_, activeId) && activeId == target) {
        affectsIndicator = true;
    }
    if (pausedRepeatIndicatorSceneText_.length() &&
        extractRuntimeSceneId(pausedRepeatIndicatorSceneText_, activeId) && activeId == target) {
        affectsIndicator = true;
    }

    // A paused INDICATOR scene shares the same physical motion/LED domain as
    // its foreground replacement. Stop the VM(s) in place and discard their
    // repeat sources; do not command a home movement during a storage commit.
    // SceneVM::stop() calls MotionPort::stop(BOTH), so motion is stopped rather
    // than left running against a scene file that is about to be replaced.
    if (affectsIndicator) {
        cancelTurnSignalAutoOffTimer();
        if (syncedSceneActive_) {
            const RuntimeResult finished = finishSyncedScene();
            if (finished != RuntimeResult::OK) {
                setError("SYNCED scene could not release shared LED ownership safely");
                return finished;
            }
        }
        if (indicatorVm_ && (indicatorVm_->active() || indicatorVm_->paused())) indicatorVm_->stop();
        if (pausedIndicatorVm_ && (pausedIndicatorVm_->active() || pausedIndicatorVm_->paused())) pausedIndicatorVm_->stop();
        if (pausedIndicatorScene_) {
            // Drop any parked context; it must not resume after the update.
            pausedIndicatorScene_ = false;
            pausedRepeatIndicatorScene_ = false;
            pausedRepeatIndicatorSceneText_ = "";
            pausedRepeatIndicatorContext_ = IndicatorSceneVM::SceneContext{};
            pausedTurnSignalSide_ = IndicatorSceneVM::Side::NONE;
            pausedIndicatorOutputs_.valid = false;
            pausedSyncedNoPlateOutputs_.valid = false;
            pausedSyncedScene_ = false;
        }
        repeatIndicatorScene_ = false;
        repeatIndicatorSceneText_ = "";
        repeatIndicatorContext_ = IndicatorSceneVM::SceneContext{};
        repeatSyncedScene_ = false;
        repeatSyncedSceneText_ = "";
        repeatSyncedContext_ = IndicatorSceneVM::SceneContext{};
        turnSignalSide_ = IndicatorSceneVM::Side::NONE;
        if (!ledRuntime_ || !ledRuntime_->clearIndicatorOutputs()) {
            setError(ledRuntime_ && ledRuntime_->lastError() ? ledRuntime_->lastError() :
                     "affected INDICATOR outputs could not be cleared safely");
            return RuntimeResult::SCENE_START_FAILED;
        }
    }

    if (noPlateId_ == target && noPlateVm_ && (noPlateVm_->active() || noPlateRunning_)) {
        noPlateVm_->stop();
        if (noPlateLedPort_) {
            const IndicatorSceneVM::LEDSelector all{};
            if (!noPlateLedPort_->clear(IndicatorSceneVM::Side::BOTH,
                                        IndicatorSceneVM::Zone::NO_PLATELEDS, all) ||
                !noPlateLedPort_->show()) {
                setError("affected NO-PLATE scene stopped but its LED output could not be cleared");
                return RuntimeResult::SCENE_START_FAILED;
            }
        }
        noPlateRunning_ = false;
        noPlatePending_ = false;
    }
    return RuntimeResult::OK;
}

RuntimeResult SceneRuntimeManagerV1::stopIndicator() {
    // Manual STOP and timer expiry share this cleanup path. The timer must
    // never remain armed after STOP/resume has completed.
    cancelTurnSignalAutoOffTimer();

    if (!indicatorVm_ || !indicatorMotion_ || !ledRuntime_) {
        setError("INDICATOR runtime domain is not ready");
        return RuntimeResult::NOT_READY;
    }

    // SYNCED owns NO_PLATELEDS only temporarily. STOP therefore ends the
    // SYNCED foreground scene first, returns NO_PLATE ownership, and then
    // continues with the normal INDICATOR STOP cleanup.
    if (syncedSceneActive_) {
        if (indicatorVm_->active()) indicatorVm_->stop();
        const RuntimeResult finished = finishSyncedScene();
        if (finished != RuntimeResult::OK) return finished;
    }

    // If a scene was parked for TURN SIGNAL, STOP ends the foreground turn
    // signal and resumes that exact paused execution context.
    if (pausedIndicatorScene_) {
        const RuntimeResult resumed = resumePausedIndicatorAfterTurn();
        if (resumed == RuntimeResult::OK) return RuntimeResult::OK;
        return resumed;
    }

    // No paused scene exists: STOP ends the current INDICATOR scene.
    if (indicatorVm_->active()) {
        indicatorVm_->stop();
    }
    repeatIndicatorScene_ = false;
    repeatIndicatorSceneText_ = "";
    repeatIndicatorContext_ = IndicatorSceneVM::SceneContext{};
    turnSignalSide_ = IndicatorSceneVM::Side::NONE;

    // With no paused scene to resume, STOP returns both physical indicator
    // mechanisms to position 0 using speed 100.
    if (!indicatorMotion_->move(IndicatorSceneVM::Side::BOTH, 0, 100)) {
        setError(indicatorMotion_->faultText() ? indicatorMotion_->faultText() :
                 "INDICATOR return-to-zero command failed");
        return RuntimeResult::SCENE_START_FAILED;
    }

    // STOP also clears every INDICATOR LED output while deliberately preserving
    // the independent NO_PLATE LED domain.
    if (!ledRuntime_->clearIndicatorOutputs()) {
        setError(ledRuntime_->lastError() ? ledRuntime_->lastError() :
                 "INDICATOR LED clear failed");
        return RuntimeResult::SCENE_START_FAILED;
    }

    return RuntimeResult::OK;
}

RuntimeResult SceneRuntimeManagerV1::stopAllIndicator() {
    // Reserved 2-click system command. Unlike the normal STOP path, this
    // never resumes a paused INDICATOR scene. It clears the complete
    // INDICATOR scene hierarchy and returns both mechanisms to position 0.
    cancelTurnSignalAutoOffTimer();

    if (!indicatorVm_ || !indicatorMotion_ || !ledRuntime_) {
        setError("INDICATOR runtime domain is not ready");
        return RuntimeResult::NOT_READY;
    }

    // If the foreground scene is SYNCED, release its temporary NO_PLATE
    // ownership normally. NO_PLATE itself remains in its own domain.
    if (syncedSceneActive_) {
        if (indicatorVm_->active()) indicatorVm_->stop();
        const RuntimeResult finished = finishSyncedScene();
        if (finished != RuntimeResult::OK) return finished;
    }

    if (indicatorVm_->active()) indicatorVm_->stop();

    // Discard, rather than resume, the one parked INDICATOR scene.
    if (pausedIndicatorScene_) {
        const RuntimeResult discarded = discardPausedIndicatorForReplacement();
        if (discarded != RuntimeResult::OK) return discarded;
    }

    repeatIndicatorScene_ = false;
    repeatIndicatorSceneText_ = "";
    repeatIndicatorContext_ = IndicatorSceneVM::SceneContext{};
    turnSignalSide_ = IndicatorSceneVM::Side::NONE;

    if (!indicatorMotion_->move(IndicatorSceneVM::Side::BOTH, 0, 100)) {
        setError(indicatorMotion_->faultText() ? indicatorMotion_->faultText() :
                 "INDICATOR STOP ALL return-to-zero command failed");
        return RuntimeResult::SCENE_START_FAILED;
    }

    if (!ledRuntime_->clearIndicatorOutputs()) {
        setError(ledRuntime_->lastError() ? ledRuntime_->lastError() :
                 "INDICATOR STOP ALL LED clear failed");
        return RuntimeResult::SCENE_START_FAILED;
    }

    return RuntimeResult::OK;
}

RuntimeResult SceneRuntimeManagerV1::playTurnSignal(IndicatorSceneVM::Side triggeredSide) {
    // IMPORTANT: turnSignalSide_ describes the CURRENT foreground scene.
    // Do not clear it before deciding whether the current foreground is
    // already a TURN SIGNAL. Otherwise a second LEFT/RIGHT request would
    // incorrectly pause the first TURN SIGNAL into the parked VM.
    const bool replacingActiveTurnSignal =
        indicatorVm_ && indicatorVm_->active() &&
        turnSignalSide_ != IndicatorSceneVM::Side::NONE &&
        !pausedIndicatorScene_;

    turnSignalId_ = "";
    lastError_ = "";

    if (!storage_ || !indicatorVm_ || !storage_->mounted()) {
        setError("runtime manager not ready");
        return RuntimeResult::NOT_READY;
    }
    if (triggeredSide != IndicatorSceneVM::Side::LEFT &&
        triggeredSide != IndicatorSceneVM::Side::RIGHT) {
        setError("TURN SIGNAL requires LEFT or RIGHT context");
        return RuntimeResult::CONFIG_ERROR;
    }
    // Only pause a non-TURN foreground scene. Once TURN SIGNAL is active,
    // LEFT/RIGHT requests replace it directly while the original underlying
    // scene (if any) remains parked exactly where it was.
    if (!replacingActiveTurnSignal) {
        const RuntimeResult pauseResult = pauseIndicatorForTurn();
        if (pauseResult != RuntimeResult::OK) return pauseResult;
    }
    File f = LittleFS.open("/config/assignment.config", "r");
    if (!f || f.isDirectory()) {
        if (f) f.close();
        setError("active assignment.config not found");
        return RuntimeResult::CONFIG_ERROR;
    }
    const size_t n = f.size();
    String config;
    config.reserve(n + 1);
    while (f.available()) {
        int c = f.read();
        if (c < 0) { f.close(); setError("active assignment.config read failed"); return RuntimeResult::CONFIG_ERROR; }
        config += (char)c;
    }
    f.close();
    if (config.length() != n) { setError("active assignment.config length mismatch"); return RuntimeResult::CONFIG_ERROR; }

    IndicatorAssignmentConfig::AssignmentConfigV1 parser;
    if (!parser.getFixedSceneId(config.c_str(), config.length(),
                                IndicatorAssignmentConfig::FixedAssignment::TURN_SIGNAL,
                                turnSignalId_)) {
        setError(parser.lastError());
        return RuntimeResult::CONFIG_ERROR;
    }

    String scene;
    if (!storage_->readById(turnSignalId_.c_str(), scene)) {
        setError("assigned TURN SIGNAL Scene ID is not available in Scene Storage");
        return RuntimeResult::SCENE_NOT_FOUND;
    }

    IndicatorSceneVM::SceneContext context;
    context.triggeredSide = triggeredSide;

    const RuntimeResult replacement = replaceIndicatorScene(scene, context, true);
    if (replacement != RuntimeResult::OK) return replacement;

    turnSignalSide_ = triggeredSide;
    repeatIndicatorSceneText_ = scene;
    repeatIndicatorContext_ = context;
    repeatIndicatorScene_ = true;

    // Start/restart the configured TURN SIGNAL-only timer after the new
    // foreground scene has been installed successfully. L/R replacement
    // therefore always receives a fresh full countdown.
    if (!startTurnSignalAutoOffTimer(config)) {
        // Do not leave a half-started TURN SIGNAL timer behind.
        cancelTurnSignalAutoOffTimer();
        return RuntimeResult::CONFIG_ERROR;
    }

    return RuntimeResult::OK;
}

void SceneRuntimeManagerV1::update(uint32_t nowMs) {
    if (noPlateVm_) noPlateVm_->update(nowMs);
    if (indicatorVm_) indicatorVm_->update(nowMs);

    // SYNCED repeats without releasing NO_PLATE ownership. The NO_PLATE VM
    // stays paused and the SYNCED VM immediately reloads the same scene while
    // ownership remains active, so NO_PLATELEDS never pass through a
    // NO_PLATE-owned frame between repetitions.
    if (syncedSceneActive_ && indicatorVm_ && !indicatorVm_->active()) {
        const String repeatText = repeatSyncedSceneText_;
        const IndicatorSceneVM::SceneContext repeatContext = repeatSyncedContext_;

        const bool shouldRepeatSynced = repeatSyncedScene_ && repeatText.length();

        if (shouldRepeatSynced) {
            if (!ledRuntime_->clearIndicatorOutputs()) {
                Serial.printf("[FAIL] SYNCED repeat clear: %s\n",
                              ledRuntime_->lastError() ? ledRuntime_->lastError() : "unknown error");
                finishSyncedScene();
            } else if (indicatorVm_->load(repeatText.c_str(), repeatText.length()) != IndicatorSceneVM::VMResult::OK ||
                       indicatorVm_->start(repeatContext) != IndicatorSceneVM::VMResult::OK) {
                Serial.printf("[FAIL] SYNCED repeat start: %s\n",
                              indicatorVm_->lastError() ? indicatorVm_->lastError() : "unknown error");
                finishSyncedScene();
                repeatSyncedScene_ = false;
                repeatSyncedSceneText_ = "";
                repeatSyncedContext_ = IndicatorSceneVM::SceneContext{};
            } else {
                // Ownership and NO_PLATE pause remain continuously active.
                Serial.println("[RUNTIME] Active SYNCED scene completed -> repeating (NO_PLATE ownership retained)");
            }
        } else {
            const RuntimeResult finished = finishSyncedScene();
            if (finished != RuntimeResult::OK) {
                Serial.printf("[FAIL] SYNCED finish: %s\n",
                              lastError_ ? lastError_ : "unknown error");
            }
        }
    }

    // TURN SIGNAL auto-off is evaluated independently of SceneVM WAIT
    // execution. This means scene timing remains exact while the runtime
    // timer counts down in parallel.
    if (turnSignalAutoOffExpired(nowMs) &&
        indicatorVm_ && indicatorVm_->active() &&
        turnSignalSide_ != IndicatorSceneVM::Side::NONE) {
        Serial.println("[RUNTIME] TURN SIGNAL AUTO-OFF EXPIRED");
        const RuntimeResult stopped = stopIndicator();
        if (stopped == RuntimeResult::OK) {
            Serial.println("[RESULT] OK | TURN SIGNAL AUTO-OFF -> STOP / RESUME");
        } else {
            Serial.printf("[FAIL] TURN SIGNAL AUTO-OFF: %s\n",
                          lastError_ ? lastError_ : "unknown error");
        }
        // stopIndicator() cancels the timer and, when a paused scene exists,
        // restores its repeat context. Do not run the foreground repeat logic
        // again in the same update tick.
        if (noPlateEnabled_ && noPlateButton_ && noPlateVm_) {
            const auto selected = noPlateButton_->selection();
            if (selected != noPlateSelection_) {
                noPlateSelection_ = selected;
                noPlatePending_ = true;
            }
        }
    }

    // Current test-stage behavior: the active TURN SIGNAL/indicator scene
    // repeats after completion. NO-PLATE is updated independently below.
    if (indicatorVm_ && !indicatorVm_->active() && repeatIndicatorScene_ && repeatIndicatorSceneText_.length()) {
        if (indicatorVm_->load(repeatIndicatorSceneText_.c_str(), repeatIndicatorSceneText_.length()) == IndicatorSceneVM::VMResult::OK &&
            indicatorVm_->start(repeatIndicatorContext_) == IndicatorSceneVM::VMResult::OK) {
            Serial.println("[RUNTIME] Active INDICATOR scene completed -> repeating");
        } else {
            repeatIndicatorScene_ = false;
            setError(indicatorVm_->lastError() ? indicatorVm_->lastError() : "INDICATOR scene repeat failed");
            Serial.printf("[FAIL] INDICATOR scene repeat: %s\n", lastError_);
        }
    }

    if (!noPlateEnabled_ || !noPlateButton_ || !noPlateVm_) return;
    if (syncedSceneActive_) return;

    const auto selected = noPlateButton_->selection();
    if (selected != noPlateSelection_) {
        noPlateSelection_ = selected;
        noPlatePending_ = true;
        if (noPlateRunning_ && noPlateVm_->active()) {
            noPlateVm_->stop();
            playNoPlateSelection(noPlateSelection_);
            return;
        }
    }

    // Do not reload a static scene after it reaches COMPLETE. Its framebuffer
    // already contains the desired persistent NO_PLATE state. Only animated
    // scenes with a timed WAIT are restarted.
    if (!noPlateVm_->active()) {
        if (noPlatePending_ || (noPlateRunning_ && noPlateLoopOnComplete_)) {
            playNoPlateSelection(noPlateSelection_);
        }
    }
}

bool SceneRuntimeManagerV1::active() const {
    return (noPlateVm_ && noPlateVm_->active()) ||
           (indicatorVm_ && indicatorVm_->active());
}

bool SceneRuntimeManagerV1::indicatorActive() const {
    return indicatorVm_ && indicatorVm_->active();
}

bool SceneRuntimeManagerV1::noPlateActive() const {
    return noPlateVm_ && noPlateVm_->active();
}



const char* SceneRuntimeManagerV1::resultName(RuntimeResult result) {
    switch (result) {
        case RuntimeResult::OK: return "OK";
        case RuntimeResult::NOT_READY: return "NOT_READY";
        case RuntimeResult::CONFIG_ERROR: return "CONFIG_ERROR";
        case RuntimeResult::SCENE_NOT_FOUND: return "SCENE_NOT_FOUND";
        case RuntimeResult::SCENE_LOAD_FAILED: return "SCENE_LOAD_FAILED";
        case RuntimeResult::SCENE_START_FAILED: return "SCENE_START_FAILED";
        case RuntimeResult::BUSY: return "BUSY";
        case RuntimeResult::NO_ASSIGNMENT: return "NO_ASSIGNMENT";
    }
    return "UNKNOWN";
}

} // namespace IndicatorSceneRuntime
