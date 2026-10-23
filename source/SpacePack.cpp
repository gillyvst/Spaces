#include "SpacePack.h"

using namespace juce;

int RoomDef::indexOfIr (const String& irId) const
{
    for (int i = 0; i < irs.size(); ++i)
        if (irs.getReference (i).id == irId)
            return i;
    return -1;
}

int RoomDef::indexOfAmbience (const String& ambId) const
{
    for (int i = 0; i < ambiences.size(); ++i)
        if (ambiences.getReference (i).id == ambId)
            return i;
    return -1;
}

int SpacePack::indexOfRoom (const String& roomId) const
{
    for (int i = 0; i < rooms.size(); ++i)
        if (rooms.getReference (i).id == roomId)
            return i;
    return -1;
}

//==============================================================================
static NamedValueSet readNumberMap (const var& v)
{
    NamedValueSet result;

    if (auto* obj = v.getDynamicObject())
        for (auto& p : obj->getProperties())
            if (p.value.isDouble() || p.value.isInt() || p.value.isInt64() || p.value.isBool())
                result.set (p.name, (double) p.value);

    return result;
}

String SpacePack::load (const File& manifest, SpacePack& pack)
{
    var json;
    const auto parseResult = JSON::parse (manifest.loadFileAsString(), json);

    if (parseResult.failed())
        return manifest.getFullPathName() + ": " + parseResult.getErrorMessage();

    if (! json.isObject())
        return manifest.getFullPathName() + ": not a JSON object";

    const auto folder = manifest.getParentDirectory();
    const auto file = [&folder] (const var& v) { return v.toString().isEmpty() ? File() : folder.getChildFile (v.toString()); };

    pack.folder   = folder;
    pack.id       = json["id"].toString();
    pack.name     = json["name"].toString();
    pack.location = json["location"].toString();
    pack.defaults = readNumberMap (json["defaults"]);

    if (pack.id.isEmpty())
        pack.id = folder.getFileName().toLowerCase();

    if (pack.name.isEmpty())
        pack.name = folder.getFileName();

    StringArray problems;

    if (auto* rooms = json["rooms"].getArray())
    {
        for (auto& r : *rooms)
        {
            RoomDef room;
            room.id   = r["id"].toString();
            room.name = r["name"].toString();
            room.hero.still = file (r["image"]);

            if (auto frames = r["heroFrames"]; frames.isObject())
            {
                room.hero.frameFolder = file (frames["folder"]);
                room.hero.fps = frames.hasProperty ("fps") ? (double) frames["fps"] : 24.0;
            }

            if (auto* irs = r["irs"].getArray())
            {
                for (auto& i : *irs)
                {
                    IRDef ir;
                    ir.id     = i["id"].toString();
                    ir.name   = i["name"].toString();
                    ir.swapLR = (bool) i["swapLR"];
                    ir.type   = i["type"].toString() == "stereo" ? IRDef::Type::stereo : IRDef::Type::trueStereo;

                    bool ok = true;

                    if (ir.type == IRDef::Type::stereo)
                    {
                        ir.file = file (i["file"]);
                        ok = ir.file.existsAsFile();
                    }
                    else
                    {
                        ir.left  = file (i["left"]);
                        ir.right = file (i["right"]);
                        ok = ir.left.existsAsFile() && ir.right.existsAsFile();
                    }

                    if (ok)
                        room.irs.add (ir);
                    else
                        problems.add ("missing file for IR '" + ir.id + "' in " + room.id);
                }
            }

            if (auto* ambs = r["ambience"].getArray())
            {
                for (auto& a : *ambs)
                {
                    AmbienceDef amb;
                    amb.id     = a["id"].toString();
                    amb.name   = a["name"].toString();
                    amb.file   = file (a["file"]);
                    amb.gainDb = a.hasProperty ("gainDb") ? (float) (double) a["gainDb"] : 0.0f;

                    if (amb.file.existsAsFile())
                        room.ambiences.add (amb);
                    else
                        problems.add ("missing file for ambience '" + amb.id + "' in " + room.id);
                }
            }

            room.defaultIr       = jmax (0, room.indexOfIr (r["defaultIr"].toString()));
            room.defaultAmbience = jmax (0, room.indexOfAmbience (r["defaultAmbience"].toString()));

            if (room.irs.isEmpty())
                problems.add ("room '" + room.id + "' has no usable IRs");
            else
                pack.rooms.add (room);
        }
    }

    if (auto* presets = json["presets"].getArray())
    {
        for (auto& p : *presets)
        {
            PackPreset preset;
            preset.name       = p["name"].toString();
            preset.roomId     = p["room"].toString();
            preset.irId       = p["ir"].toString();
            preset.ambienceId = p["ambience"].toString();
            preset.params     = readNumberMap (p["params"]);
            preset.file       = file (p["file"]);
            preset.hidden     = (bool) p["hidden"];

            if (preset.file != File() && ! preset.file.existsAsFile())
                problems.add ("missing preset file " + p["file"].toString());
            else if (preset.name.isNotEmpty() && (preset.file.existsAsFile() || pack.indexOfRoom (preset.roomId) >= 0))
                pack.presets.add (preset);
        }
    }

    if (pack.rooms.isEmpty())
        problems.add ("no usable rooms");

    if (problems.isEmpty())
        return {};

    // Missing individual files are reported but don't stop the pack loading.
    const auto message = manifest.getFullPathName() + ": " + problems.joinIntoString ("; ");
    return pack.rooms.isEmpty() ? message : "warning: " + message;
}

//==============================================================================
File PackLibrary::getUserPacksFolder()
{
    return File::getSpecialLocation (File::userMusicDirectory).getChildFile ("Spaces").getChildFile ("Packs");
}

Array<File> PackLibrary::getSearchFolders()
{
    Array<File> folders;

    // Development override, also used by the test runner.
    if (auto env = SystemStats::getEnvironmentVariable ("SPACES_PACKS_DIR", {}); env.isNotEmpty())
        folders.add (File (env));

    folders.add (getUserPacksFolder());

    // Packs shipped inside the plugin bundle: <bundle>/Contents/Resources/Packs.
    // The binary lives in Contents/MacOS (macOS) or Contents/<arch>-linux (Linux).
    const auto binary = File::getSpecialLocation (File::currentExecutableFile);
    folders.add (binary.getParentDirectory().getSiblingFile ("Resources").getChildFile ("Packs"));

    return folders;
}

void PackLibrary::scan (const Array<File>& folders)
{
    packs.clear();
    errors.clear();

    for (auto& folder : folders)
    {
        if (! folder.isDirectory())
            continue;

        auto subfolders = folder.findChildFiles (File::findDirectories, false);
        subfolders.sort();

        for (auto& sub : subfolders)
        {
            const auto manifest = sub.getChildFile ("space.json");

            if (! manifest.existsAsFile())
                continue;

            SpacePack pack;
            const auto error = SpacePack::load (manifest, pack);

            if (error.isNotEmpty())
                errors.add (error);

            if (! pack.rooms.isEmpty() && indexOfPack (pack.id) < 0)
                packs.add (pack);
        }
    }
}

const RoomDef* PackLibrary::getRoom (int pack, int room) const noexcept
{
    if (auto* p = getPack (pack))
        if (isPositiveAndBelow (room, p->rooms.size()))
            return &p->rooms.getReference (room);

    return nullptr;
}

int PackLibrary::indexOfPack (const String& packId) const
{
    for (int i = 0; i < packs.size(); ++i)
        if (packs.getReference (i).id == packId)
            return i;
    return -1;
}
