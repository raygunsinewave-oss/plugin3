#include "PresetManager.h"
#include "GlobalSettings.h"
#include "BinaryData.h"

namespace mbx
{
PresetManager::PresetManager (juce::AudioProcessor& p, juce::AudioProcessorValueTreeState& s, Assets& a)
    : processor (p), state (s), assets (a)
{
    meta = juce::JSON::parse (GlobalSettings::get().getString ("presetMeta", "{}"));
    if (! meta.isObject())
        meta = juce::var (new juce::DynamicObject());
}

juce::String PresetManager::safeKey (const Entry& e)
{
    return (e.factory ? "f_" : "u_") + juce::String::toHexString ((e.category + "/" + e.name).hashCode64());
}

void PresetManager::loadFactory()
{
    if (! factoryData.isArray())
    {
        int size = 0;
        if (const char* data = BinaryData::getNamedResource ("factory_presets_json", size))
            factoryData = juce::JSON::parse (juce::String::fromUTF8 (data, size));
    }
    if (auto* arr = factoryData.getArray())
        for (int i = 0; i < arr->size(); ++i)
        {
            const auto& p = arr->getReference (i);
            Entry e;
            e.name = p["name"].toString();
            e.category = p["category"].toString();
            e.author = p["author"].toString();
            e.description = p["description"].toString();
            if (auto* tags = p["tags"].getArray())
                for (auto& t : *tags) e.tags.add (t.toString());
            e.factory = true;
            e.factoryIndex = i;
            e.key = "factory:" + e.category + "/" + e.name;
            entries.push_back (std::move (e));
        }
}

void PresetManager::scanUser()
{
    const auto dir = GlobalSettings::get().presetFolder();
    if (! dir.isDirectory())
        return;
    for (const auto& f : dir.findChildFiles (juce::File::findFiles, true, juce::String ("*") + kExtension))
    {
        const auto p = juce::JSON::parse (f);
        if (! p.isObject())
            continue;
        Entry e;
        e.name = p["name"].toString().isNotEmpty() ? p["name"].toString() : f.getFileNameWithoutExtension();
        e.category = p["category"].toString().isNotEmpty() ? p["category"].toString() : juce::String ("User");
        e.author = p["author"].toString();
        e.description = p["description"].toString();
        if (auto* tags = p["tags"].getArray())
            for (auto& t : *tags) e.tags.add (t.toString());
        e.tags.addIfNotAlreadyThere ("User");
        e.file = f;
        e.key = "user:" + f.getFullPathName();
        entries.push_back (std::move (e));
    }
}

void PresetManager::refresh()
{
    entries.clear();
    loadFactory();
    scanUser();
}

juce::var PresetManager::getPresetData (const Entry& e) const
{
    if (e.factory)
    {
        if (auto* arr = factoryData.getArray(); arr != nullptr && juce::isPositiveAndBelow (e.factoryIndex, arr->size()))
            return arr->getReference (e.factoryIndex);
        return {};
    }
    return juce::JSON::parse (e.file);
}

bool PresetManager::load (const Entry& e)
{
    const auto p = getPresetData (e);
    if (! p.isObject())
        return false;
    apply (p);
    currentName = e.name;
    for (size_t i = 0; i < entries.size(); ++i)
        if (entries[i].key == e.key) currentIndex = (int) i;
    touchRecent (e);
    return true;
}

void PresetManager::step (const std::vector<int>& order, int delta)
{
    if (order.empty())
        return;
    auto it = std::find (order.begin(), order.end(), currentIndex);
    int pos = it == order.end() ? (delta > 0 ? -1 : 0) : (int) (it - order.begin());
    pos = (pos + delta + (int) order.size()) % (int) order.size();
    loadIndex (order[(size_t) pos]);
}

juce::var PresetManager::capture (const juce::String& name, const juce::String& category, const juce::StringArray& tags,
                                  const juce::String& description, const juce::String& author) const
{
    auto* root = new juce::DynamicObject();
    root->setProperty ("format", "mysterybox-preset");
    root->setProperty ("version", 1);
    root->setProperty ("name", name);
    root->setProperty ("category", category);
    root->setProperty ("author", author);
    root->setProperty ("description", description);
    juce::Array<juce::var> tagArr;
    for (auto& t : tags) tagArr.add (t);
    root->setProperty ("tags", tagArr);

    auto* prm = new juce::DynamicObject();
    for (auto* p : processor.getParameters())
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (p))
        {
            if (r->paramID == "oversampling") continue; // a per-machine quality setting, not part of the sound
            const float v = r->convertFrom0to1 (r->getValue()), d = r->convertFrom0to1 (r->getDefaultValue());
            if (std::abs (v - d) > 1.0e-5f * std::max (1.0f, std::abs (d)))
                prm->setProperty (r->paramID, std::round (v * 100000.0f) / 100000.0f);
        }
    root->setProperty ("params", juce::var (prm));

    auto* as = new juce::DynamicObject();
    const auto tree = assets.toValueTree();
    for (int i = 0; i < tree.getNumProperties(); ++i)
    {
        const auto id = tree.getPropertyName (i);
        const auto v = tree.getProperty (id);
        if (v.toString().isNotEmpty())
            as->setProperty (id, v);
    }
    root->setProperty ("assets", juce::var (as));
    return juce::var (root);
}

void PresetManager::apply (const juce::var& preset)
{
    const auto prms = preset["params"];
    for (auto* p : processor.getParameters())
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (p))
        {
            if (r->paramID == "oversampling") continue;
            float norm = r->getDefaultValue();
            if (prms.hasProperty (r->paramID))
                norm = r->convertTo0to1 ((float) (double) prms[juce::Identifier (r->paramID)]);
            if (std::abs (r->getValue() - norm) > 1.0e-7f)
            {
                r->beginChangeGesture();
                r->setValueNotifyingHost (norm);
                r->endChangeGesture();
            }
        }

    // assets: start from defaults so a preset never inherits the previous one's shapes / names
    juce::ValueTree t (Assets::kTreeType);
    t.setProperty ("mseg", ShapeStore::serialise (dsp::Shape::defaultMseg()), nullptr);
    for (int l = 0; l < dsp::kNumLfos; ++l)
        t.setProperty ("lfo" + juce::String (l + 1) + "_custom", ShapeStore::serialise (dsp::Shape::defaultCustom()), nullptr);
    for (int m = 0; m < dsp::kNumMacros; ++m)
        t.setProperty ("macro" + juce::String (m + 1) + "_name", "MACRO " + juce::String (m + 1), nullptr);
    t.setProperty ("seq_pattern", Assets::serialisePattern (dsp::SeqPattern::makeDefault()), nullptr);
    if (auto* obj = preset["assets"].getDynamicObject())
        for (const auto& prop : obj->getProperties())
            t.setProperty (prop.name, prop.value, nullptr);
    assets.fromValueTree (t);
}

juce::File PresetManager::saveUser (const juce::String& name, const juce::String& category, const juce::StringArray& tags,
                                    const juce::String& description)
{
    const auto dir = GlobalSettings::get().presetFolder().getChildFile (juce::File::createLegalFileName (category));
    dir.createDirectory();
    const auto file = dir.getChildFile (juce::File::createLegalFileName (name) + kExtension);
    const auto json = juce::JSON::toString (capture (name, category, tags, description, "User"));
    if (file.replaceWithText (json))
    {
        currentName = name;
        refresh();
        for (size_t i = 0; i < entries.size(); ++i)
            if (entries[i].file == file) currentIndex = (int) i;
    }
    return file;
}

bool PresetManager::exportCurrent (const juce::File& file) const
{
    return file.replaceWithText (juce::JSON::toString (capture (currentName, "User", { "Exported" }, {}, "User")));
}

bool PresetManager::importFile (const juce::File& file)
{
    const auto p = juce::JSON::parse (file);
    if (! p.isObject() || ! p.hasProperty ("params"))
        return false;
    const auto category = p["category"].toString().isNotEmpty() ? p["category"].toString() : juce::String ("User");
    const auto dir = GlobalSettings::get().presetFolder().getChildFile (juce::File::createLegalFileName (category));
    dir.createDirectory();
    const auto ok = file.copyFileTo (dir.getChildFile (file.getFileNameWithoutExtension() + kExtension));
    refresh();
    return ok;
}

void PresetManager::storeAB (int slot) { ab[(size_t) slot] = capture (currentName, {}, {}, {}, {}); }

void PresetManager::recallAB (int slot)
{
    if (! ab[(size_t) slot].isVoid())
        apply (ab[(size_t) slot]);
    activeAB = slot;
}

void PresetManager::toggleAB()
{
    const int other = 1 - activeAB;
    storeAB (activeAB);                  // remember edits on the current side
    if (ab[(size_t) other].isVoid())     // first switch: B starts as a copy of A
        ab[(size_t) other] = ab[(size_t) activeAB];
    recallAB (other);
}

void PresetManager::setFavourite (const Entry& e, bool f)
{
    if (meta["fav"].getDynamicObject() == nullptr)
        meta.getDynamicObject()->setProperty ("fav", juce::var (new juce::DynamicObject()));
    meta["fav"].getDynamicObject()->setProperty (safeKey (e), f);
    saveMeta();
}

int PresetManager::getRating (const Entry& e) const
{
    if (auto* r = meta["rating"].getDynamicObject())
        return (int) r->getProperty (safeKey (e));
    return 0;
}

void PresetManager::setRating (const Entry& e, int stars)
{
    if (meta["rating"].getDynamicObject() == nullptr)
        meta.getDynamicObject()->setProperty ("rating", juce::var (new juce::DynamicObject()));
    meta["rating"].getDynamicObject()->setProperty (safeKey (e), juce::jlimit (0, 5, stars));
    saveMeta();
}

juce::StringArray PresetManager::getRecents() const
{
    juce::StringArray r;
    if (auto* arr = meta["recent"].getArray())
        for (auto& v : *arr) r.add (v.toString());
    return r;
}

void PresetManager::touchRecent (const Entry& e)
{
    auto r = getRecents();
    r.removeString (e.key);
    r.insert (0, e.key);
    while (r.size() > 30) r.remove (r.size() - 1);
    juce::Array<juce::var> arr;
    for (auto& s : r) arr.add (s);
    meta.getDynamicObject()->setProperty ("recent", arr);
    saveMeta();
}

void PresetManager::saveMeta()
{
    GlobalSettings::get().set ("presetMeta", juce::JSON::toString (meta, true));
}

} // namespace mbx
