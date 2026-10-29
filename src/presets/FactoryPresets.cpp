// SPDX-License-Identifier: AGPL-3.0-or-later
#include "presets/FactoryPresets.h"

#include <PentagritPresetData.h>
#include <algorithm>

namespace pentagrit
{
namespace
{
int categoryRank(const juce::String& category)
{
    static const juce::StringArray order{"Init", "Drums", "Bass", "Mix Bus", "Creative"};
    const int i = order.indexOf(category);
    return i >= 0 ? i : order.size();
}
} // namespace

std::vector<FactoryPreset> loadFactoryPresets()
{
    struct Item
    {
        juce::String fileName;
        FactoryPreset preset;
    };
    std::vector<Item> items;

    for (int i = 0; i < PentagritPresetData::namedResourceListSize; ++i)
    {
        const char* resName = PentagritPresetData::namedResourceList[i];
        int size = 0;
        const char* data = PentagritPresetData::getNamedResource(resName, size);
        const juce::String fileName = PentagritPresetData::getNamedResourceOriginalFilename(resName);
        if (data == nullptr || !fileName.endsWithIgnoreCase(".xml"))
            continue;

        auto xml = juce::XmlDocument::parse(juce::String::fromUTF8(data, size));
        Item item;
        item.fileName = fileName;
        if (xml == nullptr ||
            PresetManager::parsePreset(*xml, item.preset.entry, item.preset.values).failed() ||
            item.preset.entry.name.isEmpty())
        {
            jassertfalse; // a broken embedded preset is a build error caught by the tests
            continue;
        }
        item.preset.entry.factory = true;
        if (item.preset.entry.category.isEmpty())
            item.preset.entry.category = PresetManager::kUnsortedCategory;
        items.push_back(std::move(item));
    }

    std::stable_sort(items.begin(), items.end(),
                     [](const Item& a, const Item& b)
                     {
                         const int ra = categoryRank(a.preset.entry.category);
                         const int rb = categoryRank(b.preset.entry.category);
                         if (ra != rb)
                             return ra < rb;
                         if (ra == 5 && a.preset.entry.category != b.preset.entry.category)
                             return a.preset.entry.category < b.preset.entry.category;
                         return a.fileName < b.fileName;
                     });

    std::vector<FactoryPreset> out;
    out.reserve(items.size());
    for (auto& i : items)
        out.push_back(std::move(i.preset));
    return out;
}
} // namespace pentagrit
