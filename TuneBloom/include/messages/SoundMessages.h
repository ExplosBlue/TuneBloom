#pragma once

namespace messages
{

namespace playback
{

inline constexpr const char* cWaveFileNoChannels = "The Wave File has no channels";
inline constexpr const char* cUnsupportedSoundType = "The Sound Type is not supported";
inline constexpr const char* cNothingToPlay = "Nothing to play";
inline constexpr const char* cLastSoundFormat = "Last Sound '%s'";
inline constexpr const char* cStopAllVoices = "Middle-click to stop all voices";
inline constexpr const char* cNoSound = "No sound";
inline constexpr const char* cMasterVolume = "Master volume";
inline constexpr const char* cVolumeDbFormat = "%.1f dB";
inline constexpr const char* cVolumeSilent = "-inf dB";
inline constexpr const char* cVolumeWidest = "-00.0 dB";
inline constexpr const char* cShowRemainingTime = "Click to show the remaining time";
inline constexpr const char* cShowElapsedTime = "Click to show the elapsed time";
inline constexpr const char* cNoLoop = "No loop";
inline constexpr const char* cSequenceNoEnd = "The sequence has no end";
inline constexpr const char* cUnknownTime = "--:--.---";

inline constexpr const char* cOverridesPersist = "Changes also apply to other Sounds";
inline constexpr const char* cParametersOpen = "Playback Parameters";
inline constexpr const char* cVariablesOpen = "Sequence Variables";
inline constexpr const char* cParameterFormat = "%s: %.2f";
inline constexpr const char* cPanOffsetFormat = "Pan offset: %s";
inline constexpr const char* cPanOffsetNone = "Pan offset: none";
inline constexpr const char* cTempoFormat = "Tempo: %.2f x";
inline constexpr const char* cVolumeFormat = "Volume: %s";
inline constexpr const char* cTrackVolumeFormat = "Track %u Volume: %s";
inline constexpr const char* cTrackWaitFormat = "Track %u Volume: %s, Wait %02X/%02X";
inline constexpr const char* cTrackSwitchTipFormat = "Click to mute, %s+click to solo";
inline constexpr const char* cRightClickReset = "Right-click to reset";
inline constexpr const char* cOverridesReset = "Reset parameters and variables";
inline constexpr const char* cSeqVarOverrideFormat = "%s, set when the sequence starts\nRight-click to reset";
inline constexpr const char* cSeqVarUsedTooltipFormat = "%s, used by this sequence";
inline constexpr const char* cSeqVarUnusedTooltipFormat = "%s, not used by this sequence";

inline constexpr const char* cMeterResetTooltip = "Click to reset";
inline constexpr const char* cMeterMerged = "Merged";

inline constexpr const char* cPanelTrackVolume = "Track Volume";

inline constexpr const char* cKnobTempo        = "Tempo";
inline constexpr const char* cBiquadInherit    = "Inherit";

inline constexpr const char* cSeqVarGlobal      = "Global";
inline constexpr const char* cSeqVarTrackWidest = "Track 15";

inline constexpr const char* cSeqVarStatusNone         = "No Sequence Sound selected";
inline constexpr const char* cSeqVarStatusUnusedFormat = "%s uses no variables";
inline constexpr const char* cSeqVarStatusOneFormat    = "%s uses 1 variable";
inline constexpr const char* cSeqVarStatusManyFormat   = "%s uses %u variables";

inline constexpr const char* cMeterMono        = "M";

inline constexpr const char* cTrackNoWaveFileFormat = "Track %u has no Wave File attached";
inline constexpr const char* cTrackNoChannelsFormat = "Track %u's Wave File has no channels";

}

namespace sound
{

inline constexpr const char* c3DApplyTitle = "Apply To";
inline constexpr const char* c3DDistanceTitle = "Distance";
inline constexpr const char* cWaveFileMenuTip = "Wave File Actions";
inline constexpr const char* cWaveFileGoTo = "Go to Wave File";
inline constexpr const char* cPanelEnableDetail = "Off: the sound uses the default values";

inline constexpr const char* cWaveSourceImport = "Import WAV";
inline constexpr const char* cBrowseLabel = "Browse...";
inline constexpr const char* cNoImportFile = "No WAV file selected";
inline constexpr const char* cWillHaveNoWaveFile = "This Sound will have no Wave File";

inline constexpr const char* cStreamFileLabel = "Stream File";
inline constexpr const char* cNoStreamFile = "No stream file selected";
inline constexpr const char* cWillHaveNoTracks = "This Sound will have no Tracks";
inline constexpr const char* cImportFileCountFormat = "%u WAV files";
inline constexpr const char* cStreamNewPathDetail = "Created on save, in the archive's folder";

inline constexpr const char* cCreateNoPlayer = "Select a Player, or add one first.";
inline constexpr const char* cCreateNoImportFile = "Select a WAV file to import.";
inline constexpr const char* cCreateNoWaveFile = "Select a Wave File.";
inline constexpr const char* cCreateNoStreamFile = "Select a stream file.";
inline constexpr const char* cCreateNoStreamPath = "Enter a path for the new stream file.";
inline constexpr const char* cCreateStreamPathUsedFormat = "'%s' is already in use.\nPlease enter a different path.";

inline constexpr const char* cTypeSequenceDetail = "Notes played with Bank instruments";
inline constexpr const char* cTypeStreamDetail   = "Long audio streamed from storage";
inline constexpr const char* cTypeWaveDetail     = "One short Wave File";

inline constexpr const char* cSurroundPanDetail  = "Front to rear position";
inline constexpr const char* cMainSendDetail     = "Level to the main output";
inline constexpr const char* cFxSendDetailFormat = "Level to effect bus %c";
inline constexpr const char* cLpfDetail          = "Low-pass filter. 64 is off";
inline constexpr const char* cBiquadDetail       = "Biquad filter amount. 0 is off";

inline constexpr const char* cBiquadLpfDetail     = "Low-pass: cuts the highs";
inline constexpr const char* cBiquadHpfDetail     = "High-pass: cuts the lows";
inline constexpr const char* cBiquadBpf512Detail  = "Band-pass around 512 Hz";
inline constexpr const char* cBiquadBpf1024Detail = "Band-pass around 1024 Hz";
inline constexpr const char* cBiquadBpf2048Detail = "Band-pass around 2048 Hz";

inline constexpr const char* cPanModeDualDetail    = "Pans each channel like a mono sound";
inline constexpr const char* cPanModeBalanceDetail = "Only changes the left/right balance";

inline constexpr const char* cPanCurveDip3DbDetail    = "-3 dB at center, 0 dB at full pan";
inline constexpr const char* cPanCurveBoost3DbDetail  = "0 dB at center, +3 dB at full pan";
inline constexpr const char* cPanCurveClampDetail     = "0 dB at center and at full pan";
inline constexpr const char* cPanCurveLinearDetail    = "-6 dB at center, 0 dB at full pan";
inline constexpr const char* cPanCurveLinear0DbDetail = "0 dB at center, +6 dB at full pan";

inline constexpr const char* c3DSectionDetail     = "Changes with position in the game world";
inline constexpr const char* c3DVolumeDetail      = "Quieter with distance";
inline constexpr const char* c3DPriorityDetail    = "Lower priority with distance";
inline constexpr const char* c3DPanDetail         = "Pan follows the direction";
inline constexpr const char* c3DSurroundDetail    = "Surround pan follows the direction";
inline constexpr const char* c3DFilterDetail      = "Filtered with distance";
inline constexpr const char* c3DDecayRatioDetail  = "Volume drop per unit of distance";
inline constexpr const char* c3DDopplerDetail     = "Pitch shift from movement. 0 is off";
inline constexpr const char* c3DDecayLogDetail    = "Volume is multiplied by the ratio per unit";
inline constexpr const char* c3DDecayLinearDetail = "Volume drops by the ratio per unit";

inline constexpr const char* cPlayerPriorityDetail = "When the player is full, lower stops first";
inline constexpr const char* cActorPlayerDetail    = "Sound actor slot that plays this sound";

inline constexpr const char* cChannelPriorityDetail = "When voices run out, lower stops first";
inline constexpr const char* cFixAtReleaseDetail    = "Keeps the priority during release";

inline constexpr const char* cUserParamSectionDetail = "Values for the game, no effect on sound";

inline constexpr const char* cRemoteFilterDetail = "Muffles the sound on the Wii Remote";
inline constexpr const char* cRemoteFilterNoCtr  = "Not used on 3DS";
inline constexpr const char* cFrontBypassDetail  = "Plays the front channels without surround";

inline constexpr const char* cKnobDecayRatio   = "Decay Ratio";
inline constexpr const char* cKnobDoppler      = "Doppler";
inline constexpr const char* cKnobRemoteFilter = "Remote Filter";

inline constexpr const char* cBiquadHpf       = "HPF";
inline constexpr const char* cBiquadBpf512    = "BPF 512";
inline constexpr const char* cBiquadBpf1024   = "BPF 1024";
inline constexpr const char* cBiquadBpf2048   = "BPF 2048";

inline constexpr const char* cPanelStartLabel = "Start Label";

inline constexpr const char* cStartOfFile              = "(Start of File)";
inline constexpr const char* cStartLabelNotFoundFormat = "The start label could not be found in the Sequence File\n'%s'";

inline constexpr const char* cSectionPanMode         = "Pan Mode";
inline constexpr const char* cSection3D              = "3D";
inline constexpr const char* cSectionPlayerPriority  = "Player Priority";
inline constexpr const char* cSectionChannelPriority = "Channel Priority";
inline constexpr const char* cSectionUserParam       = "User Parameters";
inline constexpr const char* cSectionOther           = "Other";

inline constexpr const char* cPanModeLabel   = "Mode";
inline constexpr const char* cPanModeDual    = "Dual";
inline constexpr const char* cPanModeBalance = "Balance";

inline constexpr const char* cPanCurveSqrt           = "Sqrt";
inline constexpr const char* cPanCurveSqrt0Db        = "Sqrt0Db";
inline constexpr const char* cPanCurveSqrt0DbClamp   = "Sqrt0DbClamp";
inline constexpr const char* cPanCurveSinCos         = "SinCos";
inline constexpr const char* cPanCurveSinCos0Db      = "SinCos0Db";
inline constexpr const char* cPanCurveSinCos0DbClamp = "SinCos0DbClamp";
inline constexpr const char* cPanCurveLinear0Db      = "Linear0Db";
inline constexpr const char* cPanCurveLinear0DbClamp = "Linear0DbClamp";

inline constexpr const char* c3DDecayCurve  = "Decay Curve";
inline constexpr const char* c3DDecayLog    = "Logarithmic";

inline constexpr const char* cActorPlayer  = "Actor Player";
inline constexpr const char* cFixAtRelease = "Fix At Release";

inline constexpr const char* cUnknownOptions  = "Unknown Options";
inline constexpr const char* cOptionBitFormat = "Option Bit %u";
inline constexpr const char* cUserParamFormat = "Param %u";

inline constexpr const char* cTagLabel = "Tag";

inline constexpr const char* cPanCentered    = "Centered";
inline constexpr const char* cPanLeftFormat  = "%d%% Left";
inline constexpr const char* cPanRightFormat = "%d%% Right";

}

namespace player
{

inline constexpr const char* cSoundLimitDetail = "Maximum sounds playing at once";
inline constexpr const char* cHeapSizeDetail   = "Memory to load each sound's data";

inline constexpr const char* cSoundLimit = "Sound Limit";
inline constexpr const char* cHeapSize   = "Heap Size";

inline constexpr const char* cHeapSizeUnit = "bytes";

}

namespace playerframe
{

inline constexpr const char* cWindowTitle     = "TuneBloom Player";
inline constexpr const char* cFactSeparator   = " · ";
inline constexpr const char* cOpenInEditor    = "Open in Editor";
inline constexpr const char* cReplaceTrack    = "Replace Track";
inline constexpr const char* cFileDetails     = "File Details";

}

}
