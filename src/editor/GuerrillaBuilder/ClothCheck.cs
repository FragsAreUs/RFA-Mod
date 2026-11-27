using System.IO;
using RfaFormats;
using RfaFormats.Guerrilla;
using RfaFormats.Meshes;
using RfaFormats.Tables;

namespace GuerrillaBuilder;

/// <summary>
/// The cloth built (GuerrillaCloth), as the game will load and wear it:
/// - our container list over Armageddon's, within the game's limits (64 files, the 64 KB pool);
/// - each of our cloth containers there, its list entry matching it;
/// - its cloth file read Armageddon's way: its anchors flagged and counted, its parents, links and gravity links
///   naming what it has, each swinging node linked to its parent;
/// - and worn: an item of ours with the cloth_sim flag whose mesh has the cloth's name, its skeleton holding every
///   node's bone, and a character of ours wearing it (among its props) whose skeleton holds every capsule's bone.
/// </summary>
static class ClothCheck
{
    /// <summary>Checks the cloth and logs what it finds. False if any of it is wrong (true when the cloth isn't built).</summary>
    public static bool Run(Setup setup, BuildLog log)
    {
        string listFile = Path.Combine(setup.Overrides, GuerrillaCloth.AssetList);
        if (setup.Game == null || !File.Exists(listFile)) return true;
        var list = AsmFile.Read(File.ReadAllBytes(listFile));
        var gameArchive = Packfile.Open(Path.Combine(setup.Game, "build", "pc", "cache", GuerrillaCloth.ListArchive));
        var misc = Packfile.Open(Path.Combine(setup.Game, "build", "pc", "cache", "misc.vpp_pc"));
        bool good = true;
        long bytes = list.Containers.Sum(c => c.Sizes.Sum(s => (long)s));
        if (list.Containers.Count > GuerrillaCloth.MostFiles || bytes > GuerrillaCloth.PoolBytes)
        {
            log.Line($"Cloth check: {list.Containers.Count} cloth files of {bytes:N0} bytes, past the game's {GuerrillaCloth.MostFiles} files or {GuerrillaCloth.PoolBytes:N0} bytes");
            good = false;
        }

        // The items that wear cloth: ours with the cloth_sim flag, by their mesh's name.
        var wearers = new Dictionary<string, (string Item, string? Rig)>(StringComparer.OrdinalIgnoreCase);
        string itemsFile = Path.Combine(setup.WorldFolder, "items_3d.xtbl");
        if (File.Exists(itemsFile))
            foreach (var item in XtblFile.FromBytes(File.ReadAllBytes(itemsFile)).Entries)
            {
                if (item.Element("Item_Flags")?.ElementsNamed("Flag").Any(f => f.Value.Trim().Equals("cloth_sim", StringComparison.OrdinalIgnoreCase)) != true) continue;
                var mesh = item.Element("character_mesh");
                string? meshFile = mesh?.Element("character_mesh")?.ValueOf("Filename")?.Trim(), rigFile = mesh?.Element("rig")?.ValueOf("Filename")?.Trim();
                if (string.IsNullOrEmpty(meshFile)) continue;
                wearers[Path.GetFileNameWithoutExtension(meshFile)] = (XtblFile.EntryName(item), string.IsNullOrEmpty(rigFile) ? null : Path.ChangeExtension(Path.GetFileName(rigFile), ".rig_pc"));
            }

        // The bodies the items are worn on: our characters with the item among their props, by their skeleton.
        // The capsules sit on the body's bones.
        var bodies = new Dictionary<string, (string Character, string Rig)>(StringComparer.OrdinalIgnoreCase);
        string charactersFile = Path.Combine(setup.WorldFolder, "character.xtbl");
        if (File.Exists(charactersFile))
            foreach (var character in XtblFile.FromBytes(File.ReadAllBytes(charactersFile)).Entries)
            {
                string? rig = character.Element("Rig")?.ValueOf("Filename")?.Trim();
                if (string.IsNullOrEmpty(rig)) continue;
                foreach (var prop in character.Element("Props")?.ElementsNamed("Prop") ?? [])
                    foreach (var worn in prop.Element("Items")?.ElementsNamed("Item") ?? [])
                        if (worn.ValueOf("Name")?.Trim() is { Length: > 0 } itemName)
                            bodies.TryAdd(itemName, (XtblFile.EntryName(character), Path.ChangeExtension(Path.GetFileName(rig), ".rig_pc")));
            }

        // A skeleton's bone count: ours (in the cache) or the game's (misc.vpp_pc); null if neither has it.
        int? BoneCount(string rig)
        {
            string? rigPath = setup.FindNewFile(rig);
            byte[]? rigData = rigPath != null ? File.ReadAllBytes(rigPath) : misc.Find(rig) is { } inGame ? misc.Read(inGame) : null;
            return rigData == null ? null : Rig.Read(rigData).Bones.Count;
        }

        int ours = 0;
        foreach (var container in list.Containers.Where(c => gameArchive.Find(c.FileName) == null))
        {
            ours++;
            string path = setup.NewFile(RfaFormats.Guerrilla.OverrideLayout.Cloth, container.FileName);
            if (!File.Exists(path))
            {
                log.Line($"Cloth check: {container.FileName} is listed but isn't there");
                good = false;
                continue;
            }
            var pack = Packfile.FromBytes(container.FileName, File.ReadAllBytes(path));
            if (pack.Find(container.Name) is not { } entry)
            {
                log.Line($"Cloth check: {container.FileName} hasn't its cloth file {container.Name}");
                good = false;
                continue;
            }
            byte[] data = pack.Read(entry);
            if (container.Sizes.Count != 1 || container.Sizes[0] != data.Length || container.DataOffset != pack.DataStart || container.CompressedSize != pack.CompressedDataSize)
            {
                log.Line($"Cloth check: {container.FileName}'s list entry doesn't match it (size {string.Join("/", container.Sizes)} against {data.Length}, data at {container.DataOffset} against {pack.DataStart})");
                good = false;
                continue;
            }
            GuerrillaCloth.Cloth cloth;
            try { cloth = GuerrillaCloth.Read(data, guerrilla: false); }
            catch (InvalidDataException e)
            {
                log.Line($"Cloth check: {container.Name} can't be read: {e.Message}");
                good = false;
                continue;
            }
            if (!wearers.TryGetValue(cloth.Name, out var wearer))
            {
                log.Line($"Cloth check: {cloth.Name}: no item of ours with the cloth_sim flag has that mesh, so nothing wears it");
                good = false;
                continue;
            }
            int? bones = wearer.Rig is { } rig ? BoneCount(rig) : null;
            if (bones == null)
            {
                log.Line($"Cloth check: {cloth.Name}: {wearer.Item}'s skeleton {wearer.Rig ?? "(none named)"} isn't there to check its bones against");
                good = false;
                continue;
            }
            if (!bodies.TryGetValue(wearer.Item, out var body) || BoneCount(body.Rig) is not int bodyBones)
            {
                log.Line($"Cloth check: {cloth.Name}: no character of ours wears {wearer.Item} with a skeleton to check its capsules against");
                good = false;
                continue;
            }
            if (GuerrillaCloth.Problem(cloth, nodeBones: bones, capsuleBones: bodyBones) is { } problem)
            {
                log.Line($"Cloth check: {cloth.Name}: {problem}");
                good = false;
                continue;
            }
            log.Line($"Cloth check: {cloth.Name}, worn by {wearer.Item}: {cloth.Nodes} nodes ({cloth.Anchors} pinned to the body) on {wearer.Rig}'s {bones} bones, " +
                     $"{cloth.Links} links, {cloth.Capsules} capsules on {body.Character}'s body ({body.Rig}, {bodyBones} bones)");
        }
        if (good)
            log.Line($"Cloth check: {list.Containers.Count} cloth files listed ({ours} of ours) of the game's {GuerrillaCloth.MostFiles}, {bytes:N0} of its {GuerrillaCloth.PoolBytes:N0} bytes");
        return good;
    }
}
