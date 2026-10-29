// SPDX-License-Identifier: AGPL-3.0-or-later
#include "PluginEditor.h"

namespace pentagrit
{
PentagritAudioProcessorEditor::PentagritAudioProcessorEditor(PentagritAudioProcessor& processor)
    : AudioProcessorEditor(&processor), processor_(processor), generic_(processor)
{
    addAndMakeVisible(generic_);
    setResizable(true, true);
    setResizeLimits(400, 300, 3000, 2000);
    const auto size = processor_.getUiSize();
    setSize(size.x, size.y);
}

void PentagritAudioProcessorEditor::resized()
{
    generic_.setBounds(getLocalBounds());
    processor_.setUiSize(getWidth(), getHeight());
}
} // namespace pentagrit
