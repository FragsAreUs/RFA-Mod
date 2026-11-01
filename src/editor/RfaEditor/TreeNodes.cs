using System.Collections.ObjectModel;
using System.ComponentModel;
using System.IO;
using RfaFormats;

namespace RfaEditor;

/// <summary>
/// The archive tree: folders of the game (build\pc\cache, build\dlc04\pc\cache, ...), their
/// archives, the files in each archive, and the files in each .str2_pc container. Archives and
/// containers are only read when their node is first expanded. Each row shows an icon for its kind
/// of file, its name, a dim note, and a mark when you have changed it (it is in overrides\).
/// </summary>
abstract class TreeNode : INotifyPropertyChanged
{
    string label = "";
    bool changed;

    public event PropertyChangedEventHandler? PropertyChanged;
    public ObservableCollection<TreeNode> Children { get; } = [];

    public string Label
    {
        get => label;
        protected set
        {
            label = value;
            PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(Label)));
        }
    }

    public FileKind Kind { get; protected init; } = FileKind.Of("");

    /// <summary>Dim text after the name, e.g. how many archives a folder has.</summary>
    public string Note { get; protected init; } = "";

    /// <summary>True if you have changed this file: the game uses your copy in overrides\.</summary>
    public bool Changed
    {
        get => changed;
        protected set
        {
            changed = value;
            PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(Changed)));
        }
    }

    /// <summary>Until a node is expanded it has one placeholder child, so the tree shows an arrow.</summary>
    public bool ChildrenLoaded => !(Children.Count == 1 && Children[0] is PlaceholderNode);

    public void LoadChildren()
    {
        if (ChildrenLoaded) return;
        Children.Clear();
        foreach (TreeNode child in CreateChildren()) Children.Add(child);
    }

    /// <summary>This node and every node below it that has been loaded.</summary>
    public IEnumerable<TreeNode> LoadedNodes()
    {
        yield return this;
        if (!ChildrenLoaded) yield break;
        foreach (TreeNode child in Children)
            foreach (TreeNode node in child.LoadedNodes())
                yield return node;
    }

    protected virtual IEnumerable<TreeNode> CreateChildren() => [];

    protected void AddPlaceholder() => Children.Add(new PlaceholderNode());
}

sealed class PlaceholderNode : TreeNode
{
    public PlaceholderNode() => Label = "Loading...";
}

/// <summary>A folder in the game that holds archives, e.g. build\pc\cache.</summary>
sealed class FolderNode : TreeNode
{
    public FolderNode(string folder, IEnumerable<ArchiveNode> archives)
    {
        Label = folder;
        Kind = FileKind.Folder;
        foreach (ArchiveNode archive in archives) Children.Add(archive);
        Note = Children.Count == 1 ? "1 archive" : $"{Children.Count} archives";
    }
}

/// <summary>A .vpp_pc archive in the game folder, or a new one in overrides\ (noted "new").</summary>
sealed class ArchiveNode : TreeNode
{
    readonly Workspace workspace;

    public string Archive { get; }  // relative to the game folder

    public ArchiveNode(Workspace workspace, string archive)
    {
        this.workspace = workspace;
        Archive = archive;
        Label = Path.GetFileName(archive);
        Kind = FileKind.Archive;
        if (workspace.IsNewArchive(archive)) Note = "new, in overrides";
        AddPlaceholder();
    }

    public Packfile Open() => workspace.Archive(Archive);

    protected override IEnumerable<TreeNode> CreateChildren()
    {
        Packfile pack = Open();
        return pack.Entries
            .OrderBy(e => e.Name, StringComparer.OrdinalIgnoreCase)
            .Select(e => new FileNode(workspace, new GameFile(pack, e, Archive, null)));
    }
}

/// <summary>A file in an archive or container. A .str2_pc container can be expanded too.</summary>
sealed class FileNode : TreeNode
{
    readonly Workspace workspace;

    public GameFile File { get; }

    public FileNode(Workspace workspace, GameFile file)
    {
        this.workspace = workspace;
        File = file;
        Label = file.Name;
        Kind = FileKind.Of(file.Name);
        UpdateChangedMark();
        if (file.Entry.IsContainer && file.Container == null) AddPlaceholder();
    }

    public bool IsOverridden => File.CanOverride && workspace.OverrideFor(File.Archive, File.Name) != null;

    public void UpdateChangedMark() => Changed = IsOverridden;

    public Packfile OpenContainer() => workspace.Container(File.Archive, File.Name);

    protected override IEnumerable<TreeNode> CreateChildren()
    {
        Packfile container = OpenContainer();
        return container.Entries
            .OrderBy(e => e.Name, StringComparer.OrdinalIgnoreCase)
            .Select(e => new FileNode(workspace, new GameFile(container, e, File.Archive, File.Name)));
    }
}
