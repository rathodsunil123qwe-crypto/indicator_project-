INDICATOR SCENE LANGUAGE V9.1 — BUILDER CONTRACT

COMMENTS
--------
A `#` starts a line comment. Everything from `#` to end-of-line is ignored by the Builder and SceneVM. Comments are for developer readability and do not alter execution.

CASE
----
Language keywords, targets, zones, function names, and variable lookup are case-insensitive.
Identifiers may be written in mixed case. Color-slot/default spelling is preserved in .scene output.

SCENE FILE
----------
SCENE
ID       = <identifier>
NAME     = <text>
DESC     = <text>
CATEGORY = NORMAL | NO_PLATE
CAPABILITIES = WELCOME_COMPATIBLE,TURN_SIGNAL_COMPATIBLE,SYNCED_COMPATIBLE   # optional

COLORS
------
<color_slot> = <R>,<G>,<B>
Only declared color slots are assignment-editable. Literal RGB is fixed.

SCRIPT
------
MOVE <LEFT|RIGHT|BOTH> POS <number-expression> SPEED <number-expression>
WAIT TARGET
STOP <LEFT|RIGHT|BOTH>
WAIT <integer-ms> | WAIT <integer>ms | WAIT <integer>sec
SET <TARGET> <ZONE>[selector] COLOR <color-expression> [BRIGHTNESS <0..255>]
CLEAR <TARGET> <ZONE>[selector]
SHOW
LET <name> = <number|color|boolean-expression>
IF <boolean-expression> ... [ELSE ...] END
WHILE <boolean-expression> ... END
FOR (<name> = <number-expression>; <boolean-expression>; <name> = <number-expression>) ... END

CONTROL FLOW / END
------------------
Each IF, WHILE, and FOR opens exactly one block.
Each block is closed by the nearest following END.
ELSE is legal only once inside its matching IF.
After all blocks are closed, one final END closes SCRIPT.
Nothing may appear after the final SCRIPT END.
The Builder uses a stack; END cannot be confused between nested constructs. Maximum control nesting is 64 levels, matching the SceneVM resource limit.

EXPRESSIONS
-----------
Numbers, variables, TRUE/FALSE, TIME, DELTA. TIME is scene-relative elapsed milliseconds. DELTA is milliseconds since the previous VM update; both are read-only.
Operators: + - * / % and comparisons == != > < >= <=.
Functions: RGB(3), ABS(1), MIN(2), MAX(2), CLAMP(3), MAP(5), LERP(3), SIN(1), COS(1), TAN(1), RANDOM(0), RANDOM_CHOICE(1+).
RGB(R,G,B) returns color. Numeric operators require numbers. IF/WHILE require boolean.

VARIABLES / TYPES
-----------------
LET variables are script-scope and may be reassigned.
Color slots are read-only declarations and cannot be redefined by LET.
FOR variables are numeric and block-scoped; the initializer creates/overrides the loop variable for the block, and an outer variable of the same name is restored after END. The initializer and final expression must assign the loop variable; the condition must be boolean. Execution order is initializer, condition, body, final expression, condition, and so on.
Color slot -> LET alias remains a color and ultimately originates from that editable slot.

SELECTORS
---------
Semantic zones: FRONTLEDS, MAINLEDS, LENS, BACKLEDS, NO_PLATELEDS.
Static selectors: [1], [1,3,5-7], [1-10]. Dynamic selector: [i] where i is a numeric variable.
LENS is a dedicated physical LED and does not accept [index].
Physical counts in `DEFAULT_LED_PROFILE` are authoring/validation data only. Runtime physical counts come from ESP `LED_CONFIG` through the VM `LEDPort::zoneCount()` interface. The Scene VM does not hard-code physical LED counts. Changing the profile does not change the language.
Old scenes remain valid when counts increase; new scenes may use the added indices.

CAPABILITIES
------------
WELCOME_COMPATIBLE -> NORMAL only.
TURN_SIGNAL_COMPATIBLE -> NORMAL; every motion/indicator LED target must be BOTH.
SYNCED_COMPATIBLE -> NORMAL; permits NO_PLATELEDS access so runtime can arbitrate/temporarily own that domain.
NO_PLATE -> LED-only, NO_PLATELEDS only, BOTH only, no motion/WAIT TARGET, no capabilities.

NOT IN LANGUAGE
---------------
PRIORITY category, SIGNAL keyword/target, controller click mapping, assignment/configuration data, native effect commands.


RESOURCE LIMITS
---------------
The V9.1 Builder and SceneVM share these authoring/runtime limits:
- SCRIPT: maximum 4096 source lines.
- COLORS: maximum 32 declared color slots.
- WAIT: maximum 3,600,000 ms (3,600 sec).
- Control nesting: maximum 64 IF/WHILE/FOR levels.
The VM also enforces a 10,000-instruction cooperative execution burst per update. This is a runtime safety guard, not a static loop-count limit, because WHILE/FOR execution counts can depend on runtime values.

WAIT TARGET
-----------
`WAIT TARGET` waits for the **current motion target**: the side or sides specified by the most recent `MOVE`. Therefore `MOVE BOTH ...` followed by `WAIT TARGET` waits for BOTH; after `MOVE LEFT ...`, the next `WAIT TARGET` waits only for LEFT. `SET`, `CLEAR`, `SHOW`, and timed `WAIT` do not change the current motion-target context.
