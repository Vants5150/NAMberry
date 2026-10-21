//
// namcal.h - input calibration of the NAM models: arithmetic only, tested on Linux (test_namcal.cpp)
//
#ifndef _namcal_h
#define _namcal_h

// Input gain in dB for a model: maximum input level of the interface (dBu of a sine at 0 dBFS, with the Gain at minimum)
// minus the capture's send level (input_level_dbu), plus the chosen offset (the historical ADELnam/PiPedal sound has +6 dB; the TONE3000 guide and the NeuralAudio
// library do not add it). Model without a send level: no compensation (0 dB).
static inline float NamInputDb (float fMaxInDbu, float fModelInDbu, bool bHasModel, float fOffsetDb)
{
	return bHasModel ? fMaxInDbu - fModelInDbu + fOffsetDb : 0.0f;
}

#endif
