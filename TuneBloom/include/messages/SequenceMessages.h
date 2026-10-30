#pragma once

namespace messages
{

namespace sequence
{

inline constexpr const char* cCommandVersionFormat = "Command '{}' requires {} version >= 0x{:08X}";

inline constexpr const char* cNotCompiled = "The Sequence File is not compiled";
inline constexpr const char* cInvalidStartLabel = "The Start Label is invalid. Compile the Sequence File first";

inline constexpr const char* cPlayTooltip = "Play from Label at Cursor";
inline constexpr const char* cNoBanks = "No Bank";
inline constexpr const char* cBanksMoreFormat = "%s +%u";
inline constexpr const char* cFollowOffButton = "Follow: Off";
inline constexpr const char* cFollowTrackButtonFormat = "Follow: Track %d";
inline constexpr const char* cFollowTip = "Follow Playback";
inline constexpr const char* cTrailLabel = "Trail";
inline constexpr const char* cCompileButton = "Compile";
inline constexpr const char* cStateCompiled = "Compiled";
inline constexpr const char* cStateNotCompiled = "Not compiled";
inline constexpr const char* cStateError = "Error";
inline constexpr const char* cFoldAllTooltip = "Fold All Labels";
inline constexpr const char* cUnfoldAllTooltip = "Unfold All Labels";
inline constexpr const char* cNoInstrument = "no instrument";
inline constexpr const char* cMoreRegionsFormat = " +%d";
inline constexpr const char* cErrorLineFormat = "Line %d: %s";
inline constexpr const char* cErrorJumpTooltip = "Go to line";
inline constexpr const char* cManualFilterHint = "Filter commands";
inline constexpr const char* cManualNoMatch = "No commands match the filter";

inline constexpr const char* cAutoCompileLabel = "Auto Compile";
inline constexpr const char* cWhitespaceLabel = "Show Whitespace";
inline constexpr const char* cMinimapLabel = "Minimap";
inline constexpr const char* cSuggestLabel = "Suggest While Typing";
inline constexpr const char* cHintsLabel = "Parameter Hints";

inline constexpr const char* cFindNoResults = "No results";
inline constexpr const char* cFindPrevious = "Previous Match (Shift+F3)";
inline constexpr const char* cFindNext = "Next Match (F3)";
inline constexpr const char* cFindClose = "Close (Escape)";
inline constexpr const char* cShowReplace = "Show Replace";
inline constexpr const char* cHideReplace = "Hide Replace";

inline constexpr const char* cGoToLineHint = "Line or line:column";
inline constexpr const char* cGoToLineCountFormat = "of %d";
inline constexpr const char* cLabelFilterHint = "Go to label";
inline constexpr const char* cNoLabelMatch = "No matching labels";

inline constexpr const char* cStatusPositionFormat = "Line %d, Column %d";
inline constexpr const char* cStatusSelectedFormat = "%d selected";
inline constexpr const char* cStatusSelectedLinesFormat = "%d lines, %d selected";
inline constexpr const char* cStatusOverwrite = "Overwrite";
inline constexpr const char* cStatusPositionTooltip = "Go to line (Ctrl+G)";
inline constexpr const char* cStatusLabelsTooltip = "Go to label (Ctrl+Shift+O)";

inline constexpr const char* cMenuGoToDefinition = "Go to Label";
inline constexpr const char* cMenuCut = "Cut";
inline constexpr const char* cMenuCopy = "Copy";
inline constexpr const char* cMenuPaste = "Paste";
inline constexpr const char* cMenuToggleComment = "Toggle Comment";

inline constexpr const char* cBankSlotFormat = "Bank %u";
inline constexpr const char* cErrorAtLineFormat = "Error at line %d:";
inline constexpr const char* cFoldedLinesFormat = "  ... %d lines";

inline constexpr const char* cVariableIdentifier = "variable identifier";
inline constexpr const char* cNoteIdentifier = "note identifier";
inline constexpr const char* cModTypeIdentifier = "modulation type identifier";
inline constexpr const char* cTrackIdentifier = "track identifier";

inline constexpr const char* cErrorInternal = "Internal error";
inline constexpr const char* cErrorLabelEmpty = "The label is empty";
inline constexpr const char* cErrorLabelStart = "Labels must start with a letter";
inline constexpr const char* cErrorLabelInvalid = "The label is invalid";
inline constexpr const char* cErrorLocalLabelDefinedFormat = "Local label '{}' already defined in line {}";
inline constexpr const char* cErrorGlobalLabelDefinedFormat = "Global label '{}' already defined in line {}";
inline constexpr const char* cErrorLocalLabelNotFoundFormat = "Local label '{}' could not be found in '{}'";
inline constexpr const char* cErrorGlobalLabelNotFoundFormat = "Global label '{}' could not be found";
inline constexpr const char* cErrorJumpOutOfBounds = "Jump target is out of bounds";
inline constexpr const char* cErrorArgRangeFormat = "Argument {} must be in the range [{}, {}]";
inline constexpr const char* cErrorArgRangeOpenFormat = "Argument {} must be in the range [0, {})";
inline constexpr const char* cErrorArgEmptyFormat = "Argument {} is empty";
inline constexpr const char* cErrorArgNumberFormat = "Argument {} must be a number";
inline constexpr const char* cErrorArgNumberOrFormat = "Argument {} must be a number or {}";
inline constexpr const char* cErrorArgLabelFormat = "Argument {} is not a valid label";
inline constexpr const char* cErrorArgInternalFormat = "Argument {}: Internal error";
inline constexpr const char* cErrorArgPairInternalFormat = "Argument ({}, {}): Internal error";
inline constexpr const char* cErrorTooManyArgsFormat = "Too many arguments. Expected {}, got {}";
inline constexpr const char* cErrorMissingArgsFormat = "Missing arguments. Expected {}, got {}";
inline constexpr const char* cErrorTooManyExtensions = "Too many extensions";
inline constexpr const char* cErrorExtensionTwiceFormat = "'_{}' extension is used more than once";
inline constexpr const char* cErrorExtensionFirstFormat = "'_{}' extension must be first";
inline constexpr const char* cErrorExtensionLastFormat = "'_{}' extension must be last";
inline constexpr const char* cErrorExtensionNotAllowedFormat = "Command '{}' does not allow the '_{}' extension";
inline constexpr const char* cErrorExtensionUnknownFormat = "Unknown extension '_{}'";
inline constexpr const char* cErrorCurrentVersionFormat = " (Current: 0x{:08X})";
inline constexpr const char* cErrorUnknownCommandFormat = "Unknown command '{}'";

inline constexpr const char* cNoBytesError = "The Sequence File has no compiled data";
inline constexpr const char* cTrackMarkerFormat = "Track {}";
inline constexpr const char* cTrackMarkerNextFormat = ", Track {}";

enum class ManualRowKind
{
    Section,
    Syntax,
    Command,
};

struct ManualRow
{
    ManualRowKind kind;
    const char* name;
    const char* alt;
    const char* args;
    const char* desc;
    const char* range;
};

inline constexpr ManualRow cManualRows[] = {
    { ManualRowKind::Section, "Editor", nullptr, nullptr, nullptr, nullptr },
    { ManualRowKind::Syntax,  "Margin arrow",  nullptr, "", "Click to fold the label, drag to move it.", nullptr },
    { ManualRowKind::Syntax,  "Alt + Up/Down", nullptr, "", "Moves the label at the cursor.", nullptr },
    { ManualRowKind::Syntax,  "Play icon",     nullptr, "", "Plays from this label.", nullptr },
    { ManualRowKind::Syntax,  "Ctrl + F",      "Ctrl + H", "", "Find or replace.", nullptr },
    { ManualRowKind::Syntax,  "Ctrl + G",      nullptr, "", "Goes to a line.", nullptr },
    { ManualRowKind::Syntax,  "Ctrl + Shift + O", nullptr, "", "Lists the labels.", nullptr },
    { ManualRowKind::Syntax,  "Ctrl + Click",  "F12", "", "Goes to the label; Alt + Left goes back.", nullptr },
    { ManualRowKind::Syntax,  "Ctrl + Space",  nullptr, "", "Suggests commands and labels at the cursor.", nullptr },
    { ManualRowKind::Syntax,  "Ctrl + /",      nullptr, "", "Toggles comments.", nullptr },
    { ManualRowKind::Syntax,  "Ctrl + Shift + K", nullptr, "", "Deletes the selected lines.", nullptr },
    { ManualRowKind::Syntax,  "Shift + Alt + Down", nullptr, "", "Copies the selected lines down.", nullptr },

    { ManualRowKind::Section, "Syntax", nullptr, nullptr, nullptr, nullptr },
    { ManualRowKind::Syntax,  "; text",   nullptr, "", "Comment to the end of the line.", nullptr },
    { ManualRowKind::Syntax,  "name:",    nullptr, "", "Global label, for a Start Label, jump, call or opentrack.", nullptr },
    { ManualRowKind::Syntax,  "_name:",   nullptr, "", "Local label of the global label above it.", nullptr },
    { ManualRowKind::Syntax,  "100 0x64 0b110", nullptr, "", "Decimal, hex or binary number; can be negative.", nullptr },
    { ManualRowKind::Syntax,  "VAR_",     nullptr, "0 - 15", "Sequence variable.", nullptr },
    { ManualRowKind::Syntax,  "GVAR_",    nullptr, "0 - 15", "Global variable, shared by all sequences.", nullptr },
    { ManualRowKind::Syntax,  "TVAR_",    nullptr, "0 - 15", "Track variable.", nullptr },
    { ManualRowKind::Syntax,  "TRACK_",   nullptr, "0 - 15", "Track bit for alloctrack; join with |.", nullptr },

    { ManualRowKind::Section, "Suffixes", nullptr, nullptr, nullptr, nullptr },
    { ManualRowKind::Syntax,  "_r",  nullptr, "min, max",    "Random value from min to max.", "pan_r 32, 96" },
    { ManualRowKind::Syntax,  "_v",  nullptr, "var",         "Value from a variable.", "volume_v VAR_0" },
    { ManualRowKind::Syntax,  "_t",  nullptr, "x, time",     "Slides to x over time ticks (volume, volume2, main_volume, pan, span, pitchbend).", "volume_t 0, 96" },
    { ManualRowKind::Syntax,  "_tr", nullptr, "x, min, max", "_t with a random time.", nullptr },
    { ManualRowKind::Syntax,  "_tv", nullptr, "x, var",      "_t with the time from a variable.", nullptr },
    { ManualRowKind::Syntax,  "_if", nullptr, "",            "Runs only if the last cmp_ was true. Must be the last suffix.", "jump_if _loop" },

    { ManualRowKind::Section, "Notes", nullptr, nullptr, nullptr, nullptr },
    { ManualRowKind::Command, "cn4",            nullptr,          "velocity, length", "Plays a note. Name: key c to b, n (natural) or s (sharp), then octave m1 or 0 - 9. cn4 is key 60.", "velocity 0 - 127, length in ticks" },
    { ManualRowKind::Command, "wait",           nullptr,          "length",           "Waits for length ticks.", "48 = quarter note at timebase 48" },
    { ManualRowKind::Command, "notewait_on",    "notewait_off",   "",                 "On: a note waits for its length. Off: notes start together, for chords.", "default on" },
    { ManualRowKind::Command, "tieon",          "tieoff",         "",                 "Tie mode: a note continues the previous one.", "default off" },
    { ManualRowKind::Command, "monophonic_on",  "monophonic_off", "",                 "One note at a time on this track.", "default off" },
    { ManualRowKind::Command, "damper_on",      "damper_off",     "",                 "Damper (sustain) pedal.", "default off" },
    { ManualRowKind::Command, "fin",            nullptr,          "",                 "Stops the track.", nullptr },

    { ManualRowKind::Section, "Program & Tempo", nullptr, nullptr, nullptr, nullptr },
    { ManualRowKind::Command, "prg",         nullptr, "x", "Selects instrument x of the Bank.", "default 0" },
    { ManualRowKind::Command, "bank_select", nullptr, "x", "Selects one of the Sound's 4 Banks.", "0 - 3, default 0" },
    { ManualRowKind::Command, "tempo",       nullptr, "x", "Tempo in BPM.", "0 - 1023, default 120" },
    { ManualRowKind::Command, "timebase",    nullptr, "x", "Ticks per quarter note.", "1 - 255, default 48" },
    { ManualRowKind::Command, "prio",        nullptr, "x", "Voice priority.", "0 - 127, default 64" },

    { ManualRowKind::Section, "Volume & Pan", nullptr, nullptr, nullptr, nullptr },
    { ManualRowKind::Command, "volume",         nullptr, "x", "Track volume.", "0 - 255, default 127. Above 127 is louder" },
    { ManualRowKind::Command, "volume2",        nullptr, "x", "Second track volume, multiplied with volume.", "0 - 255, default 127. Above 127 is louder" },
    { ManualRowKind::Command, "main_volume",    nullptr, "x", "Sequence volume.", "0 - 255, default 127. Above 127 is louder" },
    { ManualRowKind::Command, "velocity_range", nullptr, "x", "Scales note velocity.", "0 - 127, default 127" },
    { ManualRowKind::Command, "pan",            nullptr, "x", "Track pan.", "0 = left, 64 = center, 127 = right" },
    { ManualRowKind::Command, "init_pan",       nullptr, "x", "Start pan of each note.", "0 = left, 64 = center, 127 = right" },
    { ManualRowKind::Command, "span",           nullptr, "x", "Surround pan. Ignored by the TuneBloom player.", "0 = front, 127 = rear" },

    { ManualRowKind::Section, "Pitch", nullptr, nullptr, nullptr, nullptr },
    { ManualRowKind::Command, "transpose",   nullptr,     "x",   "Transposes notes by x semitones.", "-128 - 127, default 0" },
    { ManualRowKind::Command, "pitchbend",   nullptr,     "x",   "Pitch bend; full bend is bendrange semitones.", "-128 - 127, default 0" },
    { ManualRowKind::Command, "bendrange",   nullptr,     "x",   "Pitch bend range in semitones.", "0 - 255, default 2" },
    { ManualRowKind::Command, "sweep_pitch", nullptr,     "x",   "Each note starts x / 64 semitones away, then slides to its key.", "default 0" },
    { ManualRowKind::Command, "porta",       nullptr,     "key", "The next note slides from key.", "note name or 0 - 127" },
    { ManualRowKind::Command, "porta_on",    "porta_off", "",    "Portamento: each note slides from the previous one.", "default off" },
    { ManualRowKind::Command, "porta_time",  nullptr,     "x",   "Portamento speed.", "0 - 255, default 0" },

    { ManualRowKind::Section, "Envelope", nullptr, nullptr, nullptr, nullptr },
    { ManualRowKind::Command, "attack",    nullptr, "x", "Attack rate, overriding the instrument.", "0 - 127" },
    { ManualRowKind::Command, "env_hold",  nullptr, "x", "Hold time.", "0 - 127" },
    { ManualRowKind::Command, "decay",     nullptr, "x", "Decay rate.", "0 - 127" },
    { ManualRowKind::Command, "sustain",   nullptr, "x", "Sustain level.", "0 - 127" },
    { ManualRowKind::Command, "release",   nullptr, "x", "Release rate.", "0 - 127" },
    { ManualRowKind::Command, "env_reset", nullptr, "",  "Restores the instrument's envelope.", nullptr },

    { ManualRowKind::Section, "Modulation", nullptr, nullptr, nullptr, nullptr },
    { ManualRowKind::Command, "mod_depth",  nullptr, "x",    "Depth.", "0 - 255, default 0" },
    { ManualRowKind::Command, "mod_range",  nullptr, "x",    "Range at full depth, in semitones for pitch.", "0 - 255, default 1" },
    { ManualRowKind::Command, "mod_speed",  nullptr, "x",    "Speed, in steps of 0.39 Hz.", "0 - 255, default 16 = 6.25 Hz" },
    { ManualRowKind::Command, "mod_period", nullptr, "x",    "Sets the speed to 100 / x Hz.", "0 = stopped" },
    { ManualRowKind::Command, "mod_delay",  nullptr, "x",    "Start delay, in steps of 5 ms.", "default 0" },
    { ManualRowKind::Command, "mod_type",   nullptr, "type", "Modulation target.", "MOD_TYPE_PITCH, MOD_TYPE_VOLUME, MOD_TYPE_PAN" },
    { ManualRowKind::Command, "mod_phase",  nullptr, "x",    "Start phase.", "0 - 127 for one cycle, version 0x00020000 or later" },
    { ManualRowKind::Command, "mod_curve",  nullptr, "x",    "Wave shape.", "0 = sine, 1 = triangle, 2 = saw, 3 = square, 4 = random, version 0x00020000 or later" },
    { ManualRowKind::Syntax,  "mod2_ mod3_ mod4_", nullptr, "", "The 8 mod_ commands for modulation 2 to 4.", "mod2_depth 64, version 0x00020000 or later" },

    { ManualRowKind::Section, "Filter & Sends", nullptr, nullptr, nullptr, nullptr },
    { ManualRowKind::Command, "lpf_cutoff",     nullptr,           "x",    "Low-pass filter; 64 is open, lower cuts more.", "0 - 127, default 64" },
    { ManualRowKind::Command, "biquad_type",    nullptr,           "type", "Biquad filter type.", "0 = off, 1 = LPF, 2 = HPF, 3 - 5 = BPF" },
    { ManualRowKind::Command, "biquad_value",   nullptr,           "x",    "Biquad filter amount.", "0 - 127, default 0" },
    { ManualRowKind::Command, "mainsend",       nullptr,           "x",    "Send level to the main bus.", "0 - 127, default 127" },
    { ManualRowKind::Command, "fxsend_a",       nullptr,           "x",    "Send level to aux bus A.", "0 - 127, default 0" },
    { ManualRowKind::Command, "fxsend_b",       nullptr,           "x",    "Send level to aux bus B.", "0 - 127, default 0" },
    { ManualRowKind::Command, "fxsend_c",       nullptr,           "x",    "Send level to aux bus C.", "0 - 127, default 0" },
    { ManualRowKind::Command, "frontbypass_on", "frontbypass_off", "",     "Front bypass. Ignored by the TuneBloom player.", "default off" },

    { ManualRowKind::Section, "Flow", nullptr, nullptr, nullptr, nullptr },
    { ManualRowKind::Command, "jump",       nullptr, "label",     "Jumps to label.", nullptr },
    { ManualRowKind::Command, "call",       nullptr, "label",     "Runs the commands at label, then returns at ret.", nullptr },
    { ManualRowKind::Command, "ret",        nullptr, "",          "Returns from call.", nullptr },
    { ManualRowKind::Command, "loop_start", nullptr, "count",     "Starts a loop.", "0 - 255, 0 = no end" },
    { ManualRowKind::Command, "loop_end",   nullptr, "",          "Ends a loop.", nullptr },
    { ManualRowKind::Command, "alloctrack", nullptr, "tracks",    "Reserves tracks; must be first at a start label.", "alloctrack TRACK_0 | TRACK_1" },
    { ManualRowKind::Command, "opentrack",  nullptr, "no, label", "Starts track no at label.", "no 0 - 15, reserved with alloctrack" },

    { ManualRowKind::Section, "Variables", nullptr, nullptr, nullptr, nullptr },
    { ManualRowKind::Command, "setvar",   nullptr, "var, x", "var = x", nullptr },
    { ManualRowKind::Command, "addvar",   nullptr, "var, x", "var = var + x", nullptr },
    { ManualRowKind::Command, "subvar",   nullptr, "var, x", "var = var - x", nullptr },
    { ManualRowKind::Command, "mulvar",   nullptr, "var, x", "var = var * x", nullptr },
    { ManualRowKind::Command, "divvar",   nullptr, "var, x", "var = var / x", nullptr },
    { ManualRowKind::Command, "modvar",   nullptr, "var, x", "var = var % x", nullptr },
    { ManualRowKind::Command, "shiftvar", nullptr, "var, x", "var = var << x (right shift when x < 0)", nullptr },
    { ManualRowKind::Command, "randvar",  nullptr, "var, x", "var = random 0 to x", nullptr },
    { ManualRowKind::Command, "andvar",   nullptr, "var, x", "var = var AND x", nullptr },
    { ManualRowKind::Command, "orvar",    nullptr, "var, x", "var = var OR x", nullptr },
    { ManualRowKind::Command, "xorvar",   nullptr, "var, x", "var = var XOR x", nullptr },
    { ManualRowKind::Command, "notvar",   nullptr, "var, x", "var = NOT x", nullptr },
    { ManualRowKind::Command, "cmp_eq",   nullptr, "var, x", "True if var = x", nullptr },
    { ManualRowKind::Command, "cmp_ne",   nullptr, "var, x", "True if var != x", nullptr },
    { ManualRowKind::Command, "cmp_lt",   nullptr, "var, x", "True if var < x", nullptr },
    { ManualRowKind::Command, "cmp_le",   nullptr, "var, x", "True if var <= x", nullptr },
    { ManualRowKind::Command, "cmp_gt",   nullptr, "var, x", "True if var > x", nullptr },
    { ManualRowKind::Command, "cmp_ge",   nullptr, "var, x", "True if var >= x", nullptr },
    { ManualRowKind::Command, "printvar", nullptr, "var",    "Prints var to the debug output.", nullptr },

    { ManualRowKind::Section, "Other", nullptr, nullptr, nullptr, nullptr },
    { ManualRowKind::Command, "mute",     nullptr, "mode", "Track mute. Ignored by the TuneBloom player.", nullptr },
    { ManualRowKind::Command, "userproc", nullptr, "id",   "Calls a game function. Ignored by the TuneBloom player.", nullptr },
};

inline constexpr const char* cDataCommandVersionFormat   = "Command %s needs a newer file version";
inline constexpr const char* cDataCommandExVersionFormat = "Command 0x%02X 0x%02X needs a newer file version";
inline constexpr const char* cDataCommandMissingFormat   = "Unknown command 0x%02X";
inline constexpr const char* cDataCommandExMissingFormat = "Unknown command 0x%02X 0x%02X";
inline constexpr const char* cDataCommandInvalidFormat   = "Command 0x%02X is invalid here";
inline constexpr const char* cDataCommandMismatchFormat  = "Command 0x%02X cannot be saved without changes";
inline constexpr const char* cDataLabelNotVisitedFormat  = "Label 0x%X could not be reached";
inline constexpr const char* cDataLabelTable             = "The label table is damaged";
inline constexpr const char* cDataLabelEntryFormat       = "Label %u is damaged";
inline constexpr const char* cDataLabelOffsetFormat      = "Label %u points outside the sequence data";

inline constexpr const char* cCompileFirst = "Compile the sequence first";

}

}
