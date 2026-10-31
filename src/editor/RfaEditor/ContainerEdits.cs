using System.IO;
using RfaFormats;

namespace RfaEditor;

/// <summary>
/// Changing files that live inside a .str2_pc container. The game streams a container in whole,
/// so a changed file means a rebuilt container (Packfile.Rebuild) saved to overrides\. The game
/// sets memory aside for each container from the asset lists (.asm_pc), which record the size of
/// its compressed data and the size of every file in it, so every asset list entry for that
/// container (and every group entry naming its files) is updated to match and overridden too.
/// Used for textures (which keep their size)
/// and zones (which grow when objects are added or text gets longer: with the old sizes in the
/// asset list, the game stopped loading the level).
/// </summary>
static class ContainerEdits
{
    /// <summary>Replaces files in a container (as the game sees it now). Returns what was written, for the status bar.</summary>
    public static string Replace(Workspace workspace, string archive, string container, IReadOnlyDictionary<string, byte[]> files)
    {
        Packfile before = workspace.Container(archive, container);
        byte[] rebuilt = before.Rebuild(files);
        Packfile after = Packfile.FromBytes(container, rebuilt);
        List<string> lists = UpdateAssetLists(workspace, container, before, after);
        string path = workspace.WriteOverride(archive, container, rebuilt);
        return $"rebuilt {workspace.RelativeToMod(path)}" +
               (lists.Count > 0 ? $" and updated {string.Join(", ", lists)}." : ". No asset list mentions this container, so none was updated.");
    }

    /// <summary>
    /// Puts files into a container, like <see cref="Replace"/>; but if that makes every file in it
    /// the game's own again, goes back to the game's own container instead (<see cref="Restore"/>).
    /// </summary>
    public static string Put(Workspace workspace, string archive, string container, IReadOnlyDictionary<string, byte[]> files)
    {
        var replacing = new Dictionary<string, byte[]>(files, StringComparer.OrdinalIgnoreCase);
        Packfile current = workspace.Container(archive, container);
        Packfile original = Packfile.FromBytes(container, workspace.ReadOriginal(archive, container));
        bool allOriginal = original.Entries.All(e =>
            (replacing.TryGetValue(e.Name, out byte[]? replacement) ? replacement : current.Read(current.Find(e.Name)!))
            .AsSpan().SequenceEqual(original.Read(e)));
        if (!allOriginal) return Replace(workspace, archive, container, replacing);
        return workspace.OverrideFor(archive, container) == null ? $"{container} is the game's own." : Restore(workspace, archive, container);
    }

    /// <summary>Goes back to the game's own container, and puts its asset list entries back. Returns what was done.</summary>
    public static string Restore(Workspace workspace, string archive, string container)
    {
        Packfile current = workspace.Container(archive, container);
        Packfile original = Packfile.FromBytes(container, workspace.ReadOriginal(archive, container));
        List<string> lists = UpdateAssetLists(workspace, container, current, original);
        workspace.DeleteOverride(archive, container);
        return $"{container} is the game's own again" + (lists.Count > 0 ? $"; {string.Join(", ", lists)} put back." : ".");
    }

    /// <summary>
    /// Points every asset list entry that describes the container as it was (<paramref name="before"/>)
    /// at the container as it is now: where its data starts, its compressed size, and the sizes of
    /// its files. An entry's size list is the size of every file in the container, in the
    /// container's order, and each primitive gives its own file's size and its g-file's (as in all
    /// 799 entries of world_l0, mp_inf01 and missions checked), so they are all set from the
    /// container as it is now (which also mends a list saved before sizes were kept up to date).
    /// The same list's group entries, which have no container of their own (no size list) and
    /// name what a part of the world loads from many containers (1_c0812 names world_08_03's zone
    /// beside 1h_c0812, which holds it), follow too: the game sets memory aside for a file by them,
    /// so a zone grown without them overran it (and froze the game in its destruction links). In
    /// the game's lists a group gives a file the same sizes as a container holding it, every time
    /// (59,115 of 59,115), so a group's primitive naming one of the container's files is taken to
    /// mean this one unless another container holds that file at the group's sizes and this one
    /// didn't. An asset list that ends up the same as the game's own is no longer overridden.
    /// Returns the asset lists changed.
    /// </summary>
    static List<string> UpdateAssetLists(Workspace workspace, string containerFile, Packfile before, Packfile after)
    {
        string name = Path.GetFileNameWithoutExtension(containerFile);
        List<int> sizesAfter = after.Entries.Select(e => (int)e.Size).ToList();
        int? SizeIn(Packfile pack, string file) => file.Length > 0 && pack.Find(file) is { } entry ? (int)entry.Size : null;
        void SetSizes(AsmPrimitive primitive)  // to its file's size in the container as it is now, and its g-file's
        {
            if (SizeIn(after, primitive.Name) is int header) primitive.HeaderSize = header;
            if (primitive.DataSize != 0 && SizeIn(after, DataFileOf(primitive.Name)) is int dataSize) primitive.DataSize = dataSize;
        }
        var changed = new List<string>();
        foreach ((string archive, string list) in workspace.AssetLists())
        {
            AsmFile asm = AsmFile.Read(workspace.Read(archive, list));
            var entries = asm.Containers.Where(c => c.Name.Equals(name, StringComparison.OrdinalIgnoreCase) &&
                                                    c.DataOffset == before.DataStart &&
                                                    c.CompressedSize == before.CompressedDataSize).ToList();
            if (entries.Count == 0) continue;
            foreach (AsmContainer entry in entries)
            {
                entry.DataOffset = (uint)after.DataStart;
                entry.CompressedSize = after.CompressedDataSize;
                if (entry.Sizes.Count == sizesAfter.Count)
                {
                    entry.Sizes.Clear();
                    entry.Sizes.AddRange(sizesAfter);
                }
                foreach (AsmPrimitive primitive in entry.Primitives) SetSizes(primitive);
            }
            var elsewhere = asm.Containers.Where(c => c.Sizes.Count > 0 && !entries.Contains(c))
                               .SelectMany(c => c.Primitives).ToLookup(p => p.Name, StringComparer.OrdinalIgnoreCase);
            foreach (AsmContainer group in asm.Containers.Where(c => c.Sizes.Count == 0))
                foreach (AsmPrimitive primitive in group.Primitives)
                {
                    if (SizeIn(after, primitive.Name) == null) continue;  // not one of this container's files
                    bool wasThis = SizeIn(before, primitive.Name) == primitive.HeaderSize &&
                                   (primitive.DataSize == 0 || SizeIn(before, DataFileOf(primitive.Name)) == primitive.DataSize);
                    bool another = elsewhere[primitive.Name].Any(p => p.HeaderSize == primitive.HeaderSize && p.DataSize == primitive.DataSize);
                    if (wasThis || !another) SetSizes(primitive);
                }
            byte[] updated = asm.ToBytes();
            if (updated.AsSpan().SequenceEqual(workspace.ReadOriginal(archive, list)))
                workspace.DeleteOverride(archive, list);
            else
                workspace.WriteOverride(archive, list, updated);
            changed.Add(list);
        }
        return changed;
    }

    /// <summary>The file that holds a file's data: its g-file ("rock.cpeg_pc": "rock.gpeg_pc"); none for files without one.</summary>
    static string DataFileOf(string file)
    {
        string extension = Path.GetExtension(file);
        return extension.Length > 2 && extension[1] == 'c' ? Path.ChangeExtension(file, "g" + extension[2..]) : "";
    }
}
