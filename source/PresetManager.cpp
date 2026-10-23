#include "PresetManager.h"
#include "PluginProcessor.h"

using namespace juce;

PresetManager::PresetManager (SpacesProcessor& p) : processor (p) {}

File PresetManager::getUserPresetFolder()
{
    return File::getSpecialLocation (File::userMusicDirectory).getChildFile ("Spaces").getChildFile ("Presets");
}

void PresetManager::refresh()
{
    entries.clear();
    const auto& lib = processor.getLibrary();

    for (int p = 0; p < lib.getNumPacks(); ++p)
    {
        const auto& pack = *lib.getPack (p);

        for (int i = 0; i < pack.presets.size(); ++i)
        {
            Entry e;
            e.name = pack.presets.getReference (i).name;
            e.isFactory = true;
            e.pack = p;
            e.packPreset = i;
            entries.add (e);
        }
    }

    auto files = getUserPresetFolder().findChildFiles (File::findFiles, false, String ("*") + fileExtension);
    files.sort();

    for (auto& f : files)
    {
        Entry e;
        e.name = f.getFileNameWithoutExtension();
        e.isFactory = false;
        e.file = f;
        entries.add (e);
    }

    setCurrentByName (currentName);
    sendChangeMessage();
}

String PresetManager::getCurrentName() const
{
    return currentName;
}

void PresetManager::setCurrentByName (const String& name)
{
    currentName = name;
    currentIndex = -1;

    for (int i = 0; i < entries.size(); ++i)
        if (entries.getReference (i).name == name)
            currentIndex = i;

    sendChangeMessage();
}

void PresetManager::load (int index)
{
    if (! isPositiveAndBelow (index, entries.size()))
        return;

    const auto entry = entries[index];
    auto& state = processor.getState();
    const auto& lib = processor.getLibrary();

    if (entry.isFactory)
    {
        const auto* pack = lib.getPack (entry.pack);

        if (pack == nullptr || ! isPositiveAndBelow (entry.packPreset, pack->presets.size()))
            return;

        const auto& preset = pack->presets.getReference (entry.packPreset);

        processor.resetParametersToDefaults (pack);

        for (auto& p : preset.params)
            if (auto* param = state.getParameter (p.name.toString()))
                param->setValueNotifyingHost (param->convertTo0to1 ((float) (double) p.value));

        Selection s;
        s.space = entry.pack;
        s.room = jmax (0, pack->indexOfRoom (preset.roomId));
        const auto& room = pack->rooms.getReference (s.room);
        s.ir = room.indexOfIr (preset.irId);
        s.ambience = room.indexOfAmbience (preset.ambienceId);

        if (s.ir < 0)       s.ir = room.defaultIr;
        if (s.ambience < 0) s.ambience = room.defaultAmbience;

        processor.applySelection (s);
    }
    else
    {
        auto xml = parseXML (entry.file);

        if (xml == nullptr)
            return;

        // A preset never switches the plugin's bypass.
        auto* bypassParam = state.getParameter (ParamIDs::bypass);
        const auto bypassValue = bypassParam->getValue();

        processor.restoreStateTree (ValueTree::fromXml (*xml));

        if (bypassParam->getValue() != bypassValue)
            bypassParam->setValueNotifyingHost (bypassValue);
    }

    currentIndex = index;
    currentName = entry.name;
    sendChangeMessage();
}

void PresetManager::step (int delta)
{
    if (entries.isEmpty())
        return;

    const auto n = entries.size();
    const auto from = currentIndex < 0 ? (delta > 0 ? -1 : 0) : currentIndex;
    load (((from + delta) % n + n) % n);
}

String PresetManager::saveUserPreset (const String& rawName)
{
    const auto name = File::createLegalFileName (rawName.trim());

    if (name.isEmpty())
        return "Please enter a name.";

    const auto folder = getUserPresetFolder();

    if (! folder.createDirectory())
        return "Couldn't create " + folder.getFullPathName();

    auto tree = processor.createStateTree();
    tree.removeProperty ("uiScale", nullptr);
    tree.setProperty ("preset", name, nullptr);

    const auto file = folder.getChildFile (name + fileExtension);

    if (auto xml = tree.createXml(); xml == nullptr || ! xml->writeTo (file))
        return "Couldn't write " + file.getFullPathName();

    currentName = name;
    refresh();
    return {};
}

bool PresetManager::deleteUserPreset (int index)
{
    if (! isPositiveAndBelow (index, entries.size()) || entries.getReference (index).isFactory)
        return false;

    const auto ok = entries.getReference (index).file.deleteFile();
    refresh();
    return ok;
}
