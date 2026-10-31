using System.IO;
using RfaFormats;

namespace RfaEditor;

/// <summary>
/// Changing files that live inside a .str2_pc container. The game streams a container in whole,
/// so a changed file means a rebuilt container (Packfile.Rebuild) saved to overrides\. The game
/// sets memory aside for each container from the asset lists (.asm_pc), which record the size of
/// its compressed data, so every asset list entry for that container is updated to match and
/// overridden too. Used for textures and zones; files keep their size, so nothing else changes.
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
    /// at the container as it is now. An asset list that ends up the same as the game's own is no
    /// longer overridden. Returns the asset lists changed.
    /// </summary>
    static List<string> UpdateAssetLists(Workspace workspace, string containerFile, Packfile before, Packfile after)
    {
        string name = Path.GetFileNameWithoutExtension(containerFile);
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
}
