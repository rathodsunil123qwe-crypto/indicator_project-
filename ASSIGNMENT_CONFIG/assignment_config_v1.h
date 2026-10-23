#pragma once

#include <Arduino.h>
#include <stddef.h>
#include <stdint.h>

namespace IndicatorAssignmentConfig {

static constexpr uint8_t FIXED_ASSIGNMENT_COUNT = 4;
static constexpr uint8_t MULTI_CLICK_MIN = 3;
static constexpr uint8_t MULTI_CLICK_MAX = 32;
static constexpr uint8_t MAX_SCENE_REFERENCES = 36;

struct SceneReference {
    String section;
    String sceneId;
};


enum class FixedAssignment : uint8_t {
    WELCOME = 0,
    TURN_SIGNAL = 1,
    NO_PLATE_NORMAL = 2,
    NO_PLATE_GRAPHIC = 3
};

class AssignmentConfigV1 {
public:
    bool parseAndValidate(const char* text,
                          size_t length,
                          SceneReference* refs,
                          size_t& refCount);

    const char* lastError() const { return lastError_; }

    bool getFixedSceneId(const char* text, size_t length,
                         FixedAssignment assignment, String& sceneId);


    // Reads the configured TURN SIGNAL AUTO-OFF TIMER in seconds.
    // Returns false only when the configuration is invalid or the required
    // timer entry is missing.
    bool getTurnSignalAutoOffSeconds(const char* text, size_t length,
                                     uint32_t& seconds);

    // Resolves an optional multi-click assignment. Returns true when the
    // requested click count has an assigned Scene ID; false means either the
    // click has no assignment or the config is invalid.
    bool getClickSceneId(const char* text, size_t length,
                         uint8_t clicks, String& sceneId);


private:
    const char* lastError_ = "";

    static bool validSceneId(const String& id);
    static bool parseAssignment(const String& line, String& key, String& value);
    static bool isKnownSection(const String& line);
    static bool parseClickSection(const String& line, uint8_t& clicks);
    static bool isTimerLine(const String& key, const String& value);
    static const char* fixedSection(FixedAssignment assignment);
    void setError(const char* error) { lastError_ = error; }
};

} // namespace IndicatorAssignmentConfig
