#pragma once

#include "HyphaLuReferenceFacts.h"
#include "HyphaObservatoryView.h"
#include "PluginProcessor.h"

// The editor's part of the VU's LU face: the saved choice, and what Reference gives the face.
// POST reads its own Reference and shares the 0 with the PRE of its chain; PRE reads that copy.
namespace hypha::lu_editor
{
inline void syncChoice (observatory::View& view, KirinHyphaProcessorBase& processor)
{
    const auto packed = processor.luChoicePreference();
    view.lu().restore (lu_needle::packedLuFace (packed), lu_needle::packedWindow (packed));
}

inline void configure (observatory::View& view, KirinHyphaProcessorBase& processor)
{
    syncChoice (view, processor);
    view.lu().onChoice = [&processor] (bool luFace, lu_needle::Window window)
    { processor.setLuChoicePreference (lu_needle::packChoice (luFace, window)); };
}

inline void refreshPost (observatory::View& view, KirinHyphaProcessorBase& processor,
                         const reference_audition::Snapshot& runtime)
{
    syncChoice (view, processor);
    // The same condition that lets B and C list Kirin OS's sets.
    const bool readable = processor.stereoWorkflowsSupported() && processor.licenseIsOs() && runtime.libraryReceived;
    const auto facts = lu_needle::referenceFacts (runtime, readable, view.lu().window());
    processor.shareLuZero (facts);
    view.lu().setFacts (facts);
}

inline void refreshPre (observatory::View& view, KirinHyphaProcessorBase& processor)
{
    syncChoice (view, processor);
    view.lu().setFacts (processor.sharedLuZero());
}
}
