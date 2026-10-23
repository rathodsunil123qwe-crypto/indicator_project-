#pragma once

#include <Arduino.h>
#include <stdint.h>
#include "../SCENE_STORAGE/scene_storage_v1.h"
#include "../ASSIGNMENT_CONFIG/assignment_config_v1.h"
#include "../SCENE_VM/scene_vm_v1_interface.h"
#include "../NO_PLATE/no_plate_button_config.h"
#include "scene_domain_adapters.h"

namespace IndicatorSceneRuntime {

enum class RuntimeResult : uint8_t {
    OK,
    NOT_READY,
    CONFIG_ERROR,
    SCENE_NOT_FOUND,
    SCENE_LOAD_FAILED,
    SCENE_START_FAILED,
    BUSY,
    NO_ASSIGNMENT
};

class SceneRuntimeManagerV1 {
public:
    bool begin(IndicatorSceneStorage::SceneStorageV1& storage,
               IndicatorSceneVM::SceneVM& noPlateVm,
               IndicatorSceneVM::SceneVM& indicatorVm,
               IndicatorSceneVM::MotionPort& indicatorMotion,
               IndicatorLED::LEDRuntimeV1& ledRuntime);

    RuntimeResult playWelcome();
    RuntimeResult playTurnSignal(IndicatorSceneVM::Side triggeredSide);
    RuntimeResult playClickMappedScene(uint8_t clicks);
    RuntimeResult stopIndicator();
    RuntimeResult stopAllIndicator();
    // Stops/discards only the runtime context affected by replacing this Scene ID.
    RuntimeResult prepareSceneForUpdate(const char* sceneId);

    // Automatic NO-PLATE runtime. GPIO17 selects which existing fixed
    // assignment is used; the selected scene repeats after completion.
    void beginNoPlate(IndicatorNoPlate::NoPlateButtonV1& button);

    void update(uint32_t nowMs);
    bool active() const;
    bool indicatorActive() const;
    bool indicatorPaused() const { return pausedIndicatorScene_; }
    bool noPlateActive() const;

    const char* lastError() const { return lastError_; }
    const char* assignedWelcomeId() const { return welcomeId_.c_str(); }
    const char* assignedTurnSignalId() const { return turnSignalId_.c_str(); }
    const char* assignedNoPlateId() const { return noPlateId_.c_str(); }
    IndicatorNoPlate::NoPlateButtonV1* noPlateButton() const { return noPlateButton_; }
    IndicatorNoPlate::SideSelection activeNoPlateSelection() const { return noPlateSelection_; }

    // Returns the validated active assignment.config exactly as stored on
    // LittleFS. This is the ESP-side source of truth for Android/device sync.
    bool readActiveAssignmentConfig(String& out) const;
    IndicatorSceneVM::Side activeTurnSignalSide() const { return turnSignalSide_; }


    static const char* resultName(RuntimeResult result);

private:
    IndicatorSceneStorage::SceneStorageV1* storage_ = nullptr;
    IndicatorSceneVM::SceneVM* noPlateVm_ = nullptr;
    IndicatorSceneVM::SceneVM* indicatorVm_ = nullptr;
    IndicatorSceneVM::SceneVM* pausedIndicatorVm_ = nullptr;
    IndicatorSceneVM::MotionPort* indicatorMotion_ = nullptr;
    IndicatorLED::LEDRuntimeV1* ledRuntime_ = nullptr;
    NoPlateLEDPortAdapter* noPlateLedPort_ = nullptr;
    IndicatorLEDPortAdapter* indicatorLedPort_ = nullptr;
    String welcomeId_;
    String turnSignalId_;
    IndicatorSceneVM::Side turnSignalSide_ = IndicatorSceneVM::Side::NONE;
    IndicatorNoPlate::NoPlateButtonV1* noPlateButton_ = nullptr;
    IndicatorNoPlate::SideSelection noPlateSelection_ = IndicatorNoPlate::SideSelection::NORMAL;
    String noPlateId_;
    bool noPlateEnabled_ = false;
    bool noPlateRunning_ = false;
    bool noPlatePending_ = false;
    bool noPlateLoopOnComplete_ = true;

    // SYNCED scenes temporarily borrow NO_PLATE LED ownership while the
    // existing NO_PLATE VM is paused. The NO_PLATE VM execution context and
    // framebuffer are restored when the SYNCED scene finishes or is replaced.
    bool syncedSceneActive_ = false;
    bool syncedNoPlateWasPaused_ = false;
    IndicatorLED::NoPlateOutputSnapshot syncedSavedNoPlateOutputs_{};

    // SYNCED follows normal-scene lifecycle: it repeats after completion and
    // can be parked underneath TURN SIGNAL exactly like a NORMAL scene.
    bool repeatSyncedScene_ = false;
    String repeatSyncedSceneText_;
    IndicatorSceneVM::SceneContext repeatSyncedContext_{};
    bool pausedSyncedScene_ = false;
    IndicatorLED::NoPlateOutputSnapshot pausedSyncedNoPlateOutputs_{};

    bool repeatIndicatorScene_ = false;
    String repeatIndicatorSceneText_;
    IndicatorSceneVM::SceneContext repeatIndicatorContext_{};

    // One additional VM is used only as the parked execution context for an
    // interrupted INDICATOR scene. It is not a third hardware domain.
    bool pausedIndicatorScene_ = false;
    bool pausedRepeatIndicatorScene_ = false;
    String pausedRepeatIndicatorSceneText_;
    IndicatorSceneVM::SceneContext pausedRepeatIndicatorContext_{};
    IndicatorSceneVM::Side pausedTurnSignalSide_ = IndicatorSceneVM::Side::NONE;
    IndicatorLED::IndicatorOutputSnapshot pausedIndicatorOutputs_{};

    const char* lastError_ = "";

    // TURN SIGNAL AUTO-OFF timer. This belongs to Runtime Manager, not
    // SceneVM, so scene WAIT commands remain purely scene timing.
    bool turnSignalAutoOffActive_ = false;
    uint32_t turnSignalAutoOffStartMs_ = 0;
    uint32_t turnSignalAutoOffDurationMs_ = 0;


    void setError(const char* error) { lastError_ = error; }

    RuntimeResult playNoPlateSelection(IndicatorNoPlate::SideSelection selection);
    RuntimeResult playNoPlateCurrentSelection();

    static bool sceneHasCapability(const String& sceneText, const char* capability);
    RuntimeResult startSyncedScene(const String& sceneText,
                                   const IndicatorSceneVM::SceneContext& context);
    RuntimeResult finishSyncedScene();

    // Replace the currently active INDICATOR scene in one atomic runtime flow:
    // stop old VM -> cancel repeat/context -> clear old INDICATOR outputs -> load/start new scene.
    RuntimeResult replaceIndicatorScene(const String& sceneText,
                                        const IndicatorSceneVM::SceneContext& context,
                                        bool preservePausedIndicator = false);
    RuntimeResult discardPausedIndicatorForReplacement();
    RuntimeResult pauseIndicatorForTurn();
    RuntimeResult resumePausedIndicatorAfterTurn();

    void cancelTurnSignalAutoOffTimer();
    bool startTurnSignalAutoOffTimer(const String& config);
    bool turnSignalAutoOffExpired(uint32_t nowMs) const;

};

} // namespace IndicatorSceneRuntime
