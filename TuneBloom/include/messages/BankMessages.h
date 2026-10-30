#pragma once

#include <messages/CommonMessages.h>

namespace messages
{

namespace bank
{

inline constexpr const char* cFileLoadFailed = "The Bank File could not be loaded";
inline constexpr const char* cNoFileAttached = "The Bank has no Bank File attached";
inline constexpr const char* cWillHaveNoFile = "This Bank will have no Bank File";
inline constexpr const char* cOpenFileTip = "Open the Bank File";

inline constexpr const char* cNoRegionSelected = "Select a Region";
inline constexpr const char* cIgnoreNoteOff = "Ignore Note Off";
inline constexpr const char* cCoarseTooltipFormat = "%+.0f semitones (pitch ratio %.4f)";
inline constexpr const char* cFineTooltipFormat = "%+.0f cents (pitch ratio %.4f)";
inline constexpr const char* cProgramNumber = "Program Number";
inline constexpr const char* cEnvelopeTimeTooltipFormat = "%s: %s";
inline constexpr const char* cEnvelopeLevelTooltipFormat = "%s: %.1f dB";
inline constexpr const char* cEnvelopeCurveTooltipFormat = "Attack %s, Hold %s, Decay %s, Sustain %.1f dB, Release %s";
inline constexpr const char* cIgnoreNoteOffDetail = "Percussion mode: plays the wave to its end";

inline constexpr const char* cFileModeCreateNew = "Create New";

inline constexpr const char* cInstrumentWaveLinkFailedFormat = "Instrument %u: the Wave File could not be linked";

inline constexpr const char* cKnobAttack  = "Attack";
inline constexpr const char* cKnobDecay   = "Decay";
inline constexpr const char* cKnobSustain = "Sustain";
inline constexpr const char* cKnobRelease = "Release";

inline constexpr const char* cKeyRange      = "Key Range";
inline constexpr const char* cVelocityRange = "Vel. Range";
inline constexpr const char* cRootKey       = "Root Key";
inline constexpr const char* cKeyGroup      = "Key Group";

inline constexpr const char* cPanelSample   = "Sample";

inline constexpr const char* cInterpolationPolyphase = "Polyphase (4-point)";
inline constexpr const char* cInterpolationTypes[] = { cInterpolationPolyphase, common::cLinear };

inline constexpr const char* cPanCenter      = "C";
inline constexpr const char* cPanLeftFormat  = "L%d";
inline constexpr const char* cPanRightFormat = "R%d";

inline constexpr const char* cNoVelocityRegionFormat = "The key region has no velocity region for velocity %d";

inline constexpr const char* cInstrumentWaveFileLoadFailedFormat = "Instrument %u: the Wave File could not be loaded";
inline constexpr const char* cInstrumentWaveIdNullFormat = "Instrument %u: the wave ID could not be found";
inline constexpr const char* cVelocityRegionInvalidFormat = "Instrument %u: a velocity region is invalid";
inline constexpr const char* cInvalidKeyRegionTypeFormat = "Instrument %u: the key region type is invalid";
inline constexpr const char* cVelocityRegionEmptyFormat = "Instrument %u: the velocity region is empty";
inline constexpr const char* cKeyRegionInvalidFormat = "Instrument %u: a key region is invalid";
inline constexpr const char* cInvalidInstrumentTypeFormat = "Instrument %u: the instrument type is invalid";
inline constexpr const char* cKeyRegionEmptyFormat = "Instrument %u: the key region is empty";
inline constexpr const char* cInstrumentCountInvalidFormat = "The bank has an invalid number of instruments (%d)";

inline constexpr const char* cInvalidProgramNumberFormat = "Instrument %u: the program number must be 0 to 32766";
inline constexpr const char* cInstrumentNoKeyRegionFormat = "Instrument %u has no Key Region";
inline constexpr const char* cProgramNumberExistsFormat = "Instrument %u: the program number '%d' is already in use";
inline constexpr const char* cKeyRegionNoVelocityRegionFormat = "Instrument %u: a Key Region has no Velocity Region";
inline constexpr const char* cVelocityRegionNoWaveFileFormat = "Instrument %u: a Velocity Region has no Wave File attached";

inline constexpr const char* cDoubleClickToAdd = "Double-click to add";
inline constexpr const char* cNoInstrumentSelected = "Select an Instrument";

inline constexpr const char* cInstrumentNameFormat = "Instrument %d";
inline constexpr const char* cNextProgramNumberTooltip = "Use Next Free Program Number";
inline constexpr const char* cProgramNumberShared = "Already in use";
inline constexpr const char* cVelocityMinFixed = "The lowest Velocity Region always starts at 0";
inline constexpr const char* cVelocityMaxFixed = "The highest Velocity Region always ends at 127";

}

}
