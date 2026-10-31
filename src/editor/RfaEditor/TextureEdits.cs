using System.IO;
using RfaFormats;
using RfaFormats.Textures;

namespace RfaEditor;

/// <summary>Where a texture file pair is: directly in an archive, or inside one of its .str2_pc containers.</summary>
/// <param name="HeaderName">The .cpeg_pc / .cvbm_pc file.</param>
/// <param name="DataName">The .gpeg_pc / .gvbm_pc file that goes with it.</param>
sealed record PegLocation(string Archive, string? Container, string HeaderName, string DataName)
{
    /// <summary>The pair a selected file belongs to (it can be either half), or null if it isn't a texture file.</summary>
    public static PegLocation? Of(GameFile file)
    {
        string? header = PegFile.DataFileName(file.Name) != null ? file.Name : PegFile.HeaderFileName(file.Name);
        return header == null ? null : new PegLocation(file.Archive, file.Container, header, PegFile.DataFileName(header)!);
    }

    public string Where => Container == null ? Archive : $"{Archive} > {Container}";
}

/// <summary>
/// Reading textures as the game will see them, and replacing them through overrides. A texture
/// pair directly in an archive is replaced by overriding its data file; one inside a container by
/// rebuilding the container (see <see cref="ContainerEdits"/>). Textures keep their size and
/// format, so no other size changes.
/// </summary>
static class TextureEdits
{
    public static PegFile Load(Workspace workspace, PegLocation at)
    {
        if (at.Container == null)
            return PegFile.Read(workspace.Read(at.Archive, at.HeaderName), workspace.Read(at.Archive, at.DataName));
        Packfile container = workspace.Container(at.Archive, at.Container);
        return PegFile.Read(ReadFrom(container, at.HeaderName), ReadFrom(container, at.DataName));
    }

    /// <summary>Whether the game will use a replaced version of this texture pair.</summary>
    public static bool IsReplaced(Workspace workspace, PegLocation at) =>
        workspace.OverrideFor(at.Archive, at.Container ?? at.DataName) != null;

    /// <summary>Replaces one texture's pixels (all mip levels). Returns what was written, for the status bar.</summary>
    public static string Replace(Workspace workspace, PegLocation at, PegTexture texture, byte[] newData)
    {
        byte[] dataFile = Load(workspace, at).WithTextureData(texture, newData);
        if (at.Container == null)
        {
            string path = workspace.WriteOverride(at.Archive, at.DataName, dataFile);
            return $"Replaced {texture.Name}: saved {workspace.RelativeToMod(path)}.";
        }
        return $"Replaced {texture.Name}: " +
               ContainerEdits.Replace(workspace, at.Archive, at.Container, new Dictionary<string, byte[]> { [at.DataName] = dataFile });
    }

    /// <summary>Goes back to the game's own textures for the whole pair or container. Returns what was done.</summary>
    public static string Restore(Workspace workspace, PegLocation at)
    {
        if (at.Container != null) return ContainerEdits.Restore(workspace, at.Archive, at.Container);
        workspace.DeleteOverride(at.Archive, at.DataName);
        return $"{at.DataName} is the game's own again.";
    }

    static byte[] ReadFrom(Packfile pack, string fileName) =>
        pack.Read(pack.Find(fileName) ?? throw new FileNotFoundException($"{pack.Name} has no {fileName}"));
}
