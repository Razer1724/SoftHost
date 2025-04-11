#include "../JuceLibraryCode/JuceHeader.h"

// This provides the required symbol for standalone applications
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return nullptr;
}