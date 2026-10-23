#include "assignment_config_v1.h"
#include <ctype.h>
#include <stdlib.h>

namespace IndicatorAssignmentConfig {
namespace {

bool parseSeconds(const String& value) {
    String v = value;
    v.trim();
    v.toUpperCase();
    if (!v.endsWith("SEC")) return false;
    v.remove(v.length() - 3);
    v.trim();
    if (v.length() == 0) return false;
    for (size_t i = 0; i < v.length(); ++i) {
        if (!isdigit(static_cast<unsigned char>(v[i]))) return false;
    }
    const unsigned long seconds = strtoul(v.c_str(), nullptr, 10);
    return seconds > 0 && seconds <= 86400UL;
}

} // namespace

bool AssignmentConfigV1::validSceneId(const String& id) {
    if (id.length() == 0) return false;
    if (!(isalpha(static_cast<unsigned char>(id[0])) || id[0] == '_')) return false;
    for (size_t i = 1; i < id.length(); ++i) {
        const unsigned char c = id[i];
        if (!(isalnum(c) || c == '_')) return false;
    }
    return true;
}

bool AssignmentConfigV1::parseAssignment(const String& line, String& key, String& value) {
    const int eq = line.indexOf('=');
    if (eq <= 0) return false;
    key = line.substring(0, eq);
    value = line.substring(eq + 1);
    key.trim();
    value.trim();
    return key.length() > 0;
}

bool AssignmentConfigV1::isKnownSection(const String& line) {
    return line.equalsIgnoreCase("WELCOME SCENE") ||
           line.equalsIgnoreCase("TURN SIGNAL SCENE") ||
           line.equalsIgnoreCase("NO-PLATE NORMAL SIDE SCENE") ||
           line.equalsIgnoreCase("NO-PLATE GRAPHIC SIDE SCENE");
}

bool AssignmentConfigV1::parseClickSection(const String& line, uint8_t& clicks) {
    clicks = 0;
    const int space = line.indexOf(' ');
    if (space <= 0) return false;
    String n = line.substring(0, space);
    n.trim();
    for (size_t i = 0; i < n.length(); ++i) {
        if (!isdigit(static_cast<unsigned char>(n[i]))) return false;
    }
    const unsigned long value = strtoul(n.c_str(), nullptr, 10);
    if (value < MULTI_CLICK_MIN || value > MULTI_CLICK_MAX) return false;
    String suffix = line.substring(space + 1);
    suffix.trim();
    if (!suffix.equalsIgnoreCase("CLICKS SCENE")) return false;
    clicks = static_cast<uint8_t>(value);
    return true;
}

bool AssignmentConfigV1::isTimerLine(const String& key, const String& value) {
    if (!key.equalsIgnoreCase("TURN SIGNAL AUTO-OFF TIMER") &&
        !key.equalsIgnoreCase("NO_PLATE LOCK TIMER")) {
        return false;
    }
    return parseSeconds(value);
}

const char* AssignmentConfigV1::fixedSection(FixedAssignment assignment) {
    switch (assignment) {
        case FixedAssignment::WELCOME: return "WELCOME SCENE";
        case FixedAssignment::TURN_SIGNAL: return "TURN SIGNAL SCENE";
        case FixedAssignment::NO_PLATE_NORMAL: return "NO-PLATE NORMAL SIDE SCENE";
        case FixedAssignment::NO_PLATE_GRAPHIC: return "NO-PLATE GRAPHIC SIDE SCENE";
    }
    return nullptr;
}

bool AssignmentConfigV1::parseAndValidate(const char* text,
                                           size_t length,
                                           SceneReference* refs,
                                           size_t& refCount) {
    refCount = 0;
    lastError_ = "";

    if (!text || !refs || length == 0) {
        setError("invalid config input");
        return false;
    }

    String section = "";
    bool fixedSeen[FIXED_ASSIGNMENT_COUNT] = {false, false, false, false};
    bool fixedIdSeen[FIXED_ASSIGNMENT_COUNT] = {false, false, false, false};
    bool clickSeen[MULTI_CLICK_MAX + 1] = {};
    bool clickIdSeen[MULTI_CLICK_MAX + 1] = {};
    uint8_t currentClicks = 0;
    bool timerSeen[2] = {false, false};

    int start = 0;
    while (start <= (int)length) {
        int end = -1;
        for (int i = start; i < (int)length; ++i) {
            if (text[i] == '\n') { end = i; break; }
        }
        if (end < 0) end = (int)length;

        String line;
        line.reserve((size_t)(end - start) + 1);
        for (int i = start; i < end; ++i) line += text[i];
        line.replace("\r", "");
        line.trim();

        if (line.length() > 0 && line[0] != '#') {
            if (isKnownSection(line)) {
                section = line;
                if (line.equalsIgnoreCase("WELCOME SCENE")) fixedSeen[0] = true;
                else if (line.equalsIgnoreCase("TURN SIGNAL SCENE")) fixedSeen[1] = true;
                else if (line.equalsIgnoreCase("NO-PLATE NORMAL SIDE SCENE")) fixedSeen[2] = true;
                else fixedSeen[3] = true;
            } else {
                uint8_t clicks = 0;
                if (parseClickSection(line, clicks)) {
                    section = line;
                    currentClicks = clicks;
                    clickSeen[clicks] = true;
                } else {
                    String key, value;
                    if (!parseAssignment(line, key, value)) {
                        if (!line.equalsIgnoreCase("COLORS")) {
                            setError("invalid config line");
                            return false;
                        }
                    } else if (isTimerLine(key, value)) {
                        if (key.equalsIgnoreCase("TURN SIGNAL AUTO-OFF TIMER")) timerSeen[0] = true;
                        else timerSeen[1] = true;
                    } else if (key.equalsIgnoreCase("ID")) {
                        if (!validSceneId(value)) {
                            setError("invalid Scene ID in config");
                            return false;
                        }

                        int fixedIndex = -1;
                        if (section.equalsIgnoreCase("WELCOME SCENE")) fixedIndex = 0;
                        else if (section.equalsIgnoreCase("TURN SIGNAL SCENE")) fixedIndex = 1;
                        else if (section.equalsIgnoreCase("NO-PLATE NORMAL SIDE SCENE")) fixedIndex = 2;
                        else if (section.equalsIgnoreCase("NO-PLATE GRAPHIC SIDE SCENE")) fixedIndex = 3;

                        if (fixedIndex >= 0) {
                            if (fixedIdSeen[fixedIndex]) {
                                setError("duplicate ID inside fixed assignment");
                                return false;
                            }
                            fixedIdSeen[fixedIndex] = true;
                        } else if (currentClicks >= MULTI_CLICK_MIN &&
                                   currentClicks <= MULTI_CLICK_MAX &&
                                   clickSeen[currentClicks]) {
                            if (clickIdSeen[currentClicks]) {
                                setError("duplicate ID inside click assignment");
                                return false;
                            }
                            clickIdSeen[currentClicks] = true;
                        } else {
                            setError("ID outside a valid assignment section");
                            return false;
                        }

                        if (refCount >= MAX_SCENE_REFERENCES) {
                            setError("too many scene assignments");
                            return false;
                        }
                        refs[refCount].section = section;
                        refs[refCount].sceneId = value;
                        ++refCount;
                    }
                }
            }
        }

        if (end >= (int)length) break;
        start = end + 1;
    }

    if (!timerSeen[0] || !timerSeen[1]) {
        setError("required timer missing");
        return false;
    }
    for (uint8_t i = 0; i < FIXED_ASSIGNMENT_COUNT; ++i) {
        if (!fixedSeen[i] || !fixedIdSeen[i]) {
            setError("required fixed assignment missing");
            return false;
        }
    }
    return true;
}


bool AssignmentConfigV1::getTurnSignalAutoOffSeconds(const char* text,
                                                     size_t length,
                                                     uint32_t& seconds) {
    seconds = 0;
    lastError_ = "";

    if (!text || length == 0) {
        setError("invalid config input");
        return false;
    }

    // Validate the complete active configuration first. This guarantees the
    // timer is read from the same valid assignment.config used elsewhere.
    SceneReference refs[MAX_SCENE_REFERENCES];
    size_t refCount = 0;
    if (!parseAndValidate(text, length, refs, refCount)) return false;

    int start = 0;
    while (start <= (int)length) {
        int end = -1;
        for (int i = start; i < (int)length; ++i) {
            if (text[i] == '\n') { end = i; break; }
        }
        if (end < 0) end = (int)length;

        String line;
        line.reserve((size_t)(end - start) + 1);
        for (int i = start; i < end; ++i) line += text[i];
        line.replace("\r", "");
        line.trim();

        if (line.length() > 0 && line[0] != '#') {
            String key, value;
            if (parseAssignment(line, key, value) &&
                key.equalsIgnoreCase("TURN SIGNAL AUTO-OFF TIMER")) {
                String v = value;
                v.trim();
                v.toUpperCase();

                if (!v.endsWith("SEC")) {
                    setError("invalid TURN SIGNAL AUTO-OFF TIMER");
                    return false;
                }

                v.remove(v.length() - 3);
                v.trim();
                if (v.length() == 0) {
                    setError("invalid TURN SIGNAL AUTO-OFF TIMER");
                    return false;
                }

                for (size_t i = 0; i < v.length(); ++i) {
                    if (!isdigit(static_cast<unsigned char>(v[i]))) {
                        setError("invalid TURN SIGNAL AUTO-OFF TIMER");
                        return false;
                    }
                }

                const unsigned long parsed = strtoul(v.c_str(), nullptr, 10);
                if (parsed == 0 || parsed > 86400UL) {
                    setError("invalid TURN SIGNAL AUTO-OFF TIMER");
                    return false;
                }

                seconds = static_cast<uint32_t>(parsed);
                return true;
            }
        }

        if (end >= (int)length) break;
        start = end + 1;
    }

    setError("TURN SIGNAL AUTO-OFF TIMER missing");
    return false;
}

bool AssignmentConfigV1::getFixedSceneId(const char* text, size_t length,
                                         FixedAssignment assignment, String& sceneId) {
    sceneId = "";
    SceneReference refs[MAX_SCENE_REFERENCES];
    size_t refCount = 0;
    if (!parseAndValidate(text, length, refs, refCount)) return false;

    const char* wanted = fixedSection(assignment);
    if (!wanted) { setError("invalid fixed assignment"); return false; }

    for (size_t i = 0; i < refCount; ++i) {
        if (refs[i].section.equalsIgnoreCase(wanted)) {
            sceneId = refs[i].sceneId;
            return true;
        }
    }
    setError("required fixed assignment missing");
    return false;
}


bool AssignmentConfigV1::getClickSceneId(const char* text, size_t length,
                                         uint8_t clicks, String& sceneId) {
    sceneId = "";
    lastError_ = "";

    if (clicks < MULTI_CLICK_MIN || clicks > MULTI_CLICK_MAX) {
        setError("invalid click assignment number");
        return false;
    }
    if (!text || length == 0) {
        setError("invalid config input");
        return false;
    }

    SceneReference refs[MAX_SCENE_REFERENCES];
    size_t refCount = 0;
    if (!parseAndValidate(text, length, refs, refCount)) return false;

    String wanted;
    wanted.reserve(24);
    wanted += String((unsigned)clicks);
    wanted += " CLICKS SCENE";

    for (size_t i = 0; i < refCount; ++i) {
        if (refs[i].section.equalsIgnoreCase(wanted)) {
            sceneId = refs[i].sceneId;
            return sceneId.length() > 0;
        }
    }

    // Optional assignment: no scene is assigned to this click count.
    lastError_ = "";
    return false;
}


} // namespace IndicatorAssignmentConfig
