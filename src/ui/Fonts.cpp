#include "Theme.h"
#include "BinaryData.h"

namespace mbx::ui::fonts
{
juce::Typeface::Ptr displayTypeface()
{
    static juce::Typeface::Ptr tf = juce::Typeface::createSystemTypefaceFor (BinaryData::Orbitron_ttf, (size_t) BinaryData::Orbitron_ttfSize);
    return tf;
}

juce::Typeface::Ptr monoTypeface()
{
    static juce::Typeface::Ptr tf = juce::Typeface::createSystemTypefaceFor (BinaryData::JetBrainsMonoRegular_ttf, (size_t) BinaryData::JetBrainsMonoRegular_ttfSize);
    return tf;
}
} // namespace mbx::ui::fonts
