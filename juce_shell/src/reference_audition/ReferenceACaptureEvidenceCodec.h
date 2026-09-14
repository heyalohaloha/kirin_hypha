#pragma once
#include "ReferenceACaptureModel.h"
namespace hypha::reference_audition
{
void writeCaptureEvidence(juce::MemoryOutputStream&,const ACaptureData&);
bool readCaptureEvidence(juce::MemoryInputStream&,ACaptureData&,bool requireExhausted=true);
}
