using System.Diagnostics;
using System.IO;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using System.Windows.Media.Media3D;
using Microsoft.Win32;
using RfaFormats;
using RfaFormats.Meshes;
using RfaFormats.Tables;
using RfaFormats.Textures;

namespace RfaEditor;

/// <summary>
/// The preview tab: whatever is picked in the explorer. Text files as text, textures as pictures
/// (export, replace, restore), models in 3D (export as OBJ), anything else as a hex dump. Its
/// inspector says what the file is and has the buttons for it: extract, copy to overrides, open in
/// the table or level editor. The game folder is only ever read.
/// </summary>
public partial class FilePreview : UserControl, IEditorDocument
{
    readonly EditorContext context;
    readonly ModelViewer modelViewer;
    GameFile? selectedFile;          // the file shown, if a file is
    object? selectedNode;            // the archive or container shown, for "Extract all"
    PegLocation? texturesAt;         // the texture file pair shown, if any
    PegFile? peg;                    // ...and its textures
    ModelLocation? modelAt;          // the model shown, if any
    ModelFile? model;                // ...and the model
    Rig? rig;                        // a character's skeleton, or the skeleton previewed
    string? rigName;                 // ...and its file
    CharacterTables? characterTables;
    Animation? animation;            // the animation playing, if any
    string? animationName;           // ...and its file
    AnimatedScene? animatedScene;    // the character (or skeleton) it moves
    float animationFrame;            // where in it we are
    bool playing;
    TimeSpan lastRendered;           // when the last frame was drawn, to move on by the time since
    bool showingFrame;               // moving the slider ourselves, not the user
    List<Cast> cast = [];            // for an animation file: who can play it
    string title = "Preview";

    internal FilePreview(EditorContext context)
    {
        InitializeComponent();
        this.context = context;
        Inspector = DocumentParts.Detach(InspectorPart);
        modelViewer = new ModelViewer(ModelViewport, ModelViewArea);
        context.Assets.IndexBecameReady += () =>
        {
            if (model != null) Try(() => ShowModelScene(frame: false));  // textures in other containers can be found now
        };
        ShowNothing();
    }

    Workspace Workspace => context.Workspace;

    CharacterTables Characters => characterTables ??= new CharacterTables(context.Assets);

    public string Title => title;
    public string Icon => Icons.Preview;
    public FrameworkElement View => this;
    public FrameworkElement? Outliner => null;
    public FrameworkElement? Inspector { get; }
    public event Action? TitleChanged;
    public bool ConfirmClose() => true;

    void SetTitle(string text)
    {
        title = text;
        TitleChanged?.Invoke();
    }

    // ---- What is shown -------------------------------------------------------------------------

    public void ShowNothing()
    {
        selectedFile = null;
        selectedNode = null;
        SetTitle("Preview");
        ShowKind(null);
        TitleText.Text = "Nothing picked";
        DetailsText.Text = "Pick an archive or a file in the explorer, or search for a file by name. The game's files are only " +
                           "read, never changed: changes go to overrides\\.";
        PreviewText.Text = "";
        PreviewText.Visibility = Visibility.Collapsed;
        EmptyHint.Visibility = Visibility.Visible;
        EmptyHintText.Text = "Pick a file in the explorer, or search for one by name, to see it here.";
        TexturePanel.Visibility = Visibility.Collapsed;
        ModelPanel.Visibility = Visibility.Collapsed;
        texturesAt = null;
        modelAt = null;
        model = null;
        StopAnimation();
        AnimationList.Visibility = CastList.Visibility = Visibility.Collapsed;
        modelViewer.Clear();
        SetButtons(extract: false, extractAll: false, copy: false, showOverride: false);
    }

    internal void ShowFolder(FolderNode folder)
    {
        ShowNothing();
        ShowKind(FileKind.Folder);
        TitleText.Text = folder.Label;
        EmptyHintText.Text = "Expand it in the explorer to see its archives.";
        DetailsText.Text = $"{folder.Children.Count} archives.";
    }

    internal void ShowArchive(ArchiveNode node)
    {
        ShowNothing();
        Try(() =>
        {
            Packfile pack = node.Open();
            long size = new FileInfo(Path.Combine(Workspace.GameFolder, node.Archive)).Length;
            selectedNode = node;
            SetTitle("Preview: " + node.Label);
            ShowKind(FileKind.Archive);
            TitleText.Text = node.Label;
            EmptyHintText.Text = "Expand it in the explorer to see its files.";
            DetailsText.Text = $"Archive {node.Archive}\n{pack.Entries.Count:N0} files, {size:N0} bytes on disk, {Storage(pack)}.";
            SetButtons(extract: false, extractAll: true, copy: false, showOverride: false);
        });
    }

    internal void ShowFile(GameFile file, FileNode? node)
    {
        ShowNothing();
        Try(() =>
        {
            selectedFile = file;
            selectedNode = node;
            SetTitle("Preview: " + file.Name);
            ShowKind(FileKind.Of(file.Name));
            TitleText.Text = file.Name;
            EmptyHint.Visibility = Visibility.Collapsed;
            PreviewText.Visibility = Visibility.Visible;
            ShowFileDetails(file);
            bool isContainer = file.Entry.IsContainer && file.Container == null;
            if (isContainer)
                PreviewText.Text = $"A .str2_pc container with {Workspace.Container(file.Archive, file.Name).Entries.Count} files. " +
                                   "Expand it in the explorer to see them.";
            else if (PegLocation.Of(file) is PegLocation at)
                ShowTextures(at, 0);
            else if (ModelLocation.Of(file) is ModelLocation modelLocation)
                ShowModel(modelLocation);
            else if (file.Entry.Extension == "rig_pc")
                ShowRig(file);
            else if (file.Entry.Extension == "anim_pc")
                ShowAnimationFile(file);
            else
                PreviewText.Text = Preview.Of(ReadAsGameSeesIt(file));
        });
    }

    /// <summary>The icon of the kind of thing picked, in a tinted square in front of its name (none if nothing is).</summary>
    void ShowKind(FileKind? kind)
    {
        KindBadge.Visibility = kind == null ? Visibility.Collapsed : Visibility.Visible;
        if (kind == null) return;
        Color colour = ((SolidColorBrush)kind.Colour).Color;
        KindBadge.Background = new SolidColorBrush(Color.FromArgb(45, colour.R, colour.G, colour.B));
        KindIcon.Text = kind.Icon;
        KindIcon.Foreground = kind.Colour;
    }

    /// <summary>Brings the details up to date (after files in overrides\ changed).</summary>
    public void RefreshDetails()
    {
        if (selectedFile != null) Try(() => ShowFileDetails(selectedFile));
    }

    /// <summary>Whether a file opens in another editor when double-clicked, and opens it.</summary>
    internal static bool OpenInEditor(GameFile file, EditorContext context)
    {
        if (IsTable(file)) context.OpenTable(file);
        else if (IsZone(file)) context.OpenLevel(file);
        else return false;
        return true;
    }

    /// <summary>A file as the game will see it: your override, or your rebuilt container's copy, if there is one.</summary>
    byte[] ReadAsGameSeesIt(GameFile file)
    {
        if (file.Container == null) return Workspace.Read(file.Archive, file.Name);
        Packfile container = Workspace.Container(file.Archive, file.Container);
        return container.Read(container.Find(file.Name) ?? throw new FileNotFoundException(file.Name));
    }

    /// <summary>Where a file is, its size, whether it's overridden, and the buttons that apply to it.</summary>
    void ShowFileDetails(GameFile file)
    {
        string stored = file.Source.IsCompressed && !file.Source.IsCondensed
            ? $" ({file.Entry.CompressedSize:N0} compressed)"
            : "";
        string details = $"In {file.Where}\n{file.Entry.Size:N0} bytes{stored}.\n";

        string? overridePath = file.CanOverride ? Workspace.OverrideFor(file.Archive, file.Name) : null;
        string? containerOverride = file.Container != null ? Workspace.OverrideFor(file.Archive, file.Container) : null;
        if (overridePath != null)
            details += $"Overridden: the game uses {Workspace.RelativeToMod(overridePath)} instead.";
        else if (file.CanOverride)
            details += "Not overridden.";
        else if (containerOverride != null)
            details += $"Its container is rebuilt with your changes: {Workspace.RelativeToMod(containerOverride)}. Shown as the game will see it.";
        else
            details += "Files inside a container are loaded together with the whole container, so they can't be copied to " +
                       "overrides one by one. Textures and zones can be changed: the editor rebuilds the container for you.";
        if (IsTable(file)) details += "\n\nThis is a table: open it in the table editor to change its entries field by field.";
        if (IsZone(file)) details += "\n\nThis is a zone (objects placed in a level): open it in the level editor to see and change them.";
        DetailsText.Text = details;

        bool isContainer = file.Entry.IsContainer && file.Container == null;
        SetButtons(extract: true, extractAll: isContainer, copy: file.CanOverride,
                   showOverride: overridePath != null || containerOverride != null, table: IsTable(file), level: IsZone(file));
    }

    static bool IsZone(GameFile file) => file.Entry.Extension is "rfgzone_pc" or "layer_pc";

    static bool IsTable(GameFile file) => file.CanOverride && file.Entry.Extension == "xtbl";

    static string Storage(Packfile pack) =>
        !pack.IsCompressed ? "not compressed"
        : pack.IsCondensed ? "compressed as one block"
        : "compressed file by file";

    void SetButtons(bool extract, bool extractAll, bool copy, bool showOverride, bool table = false, bool level = false)
    {
        OpenTableButton.Visibility = table ? Visibility.Visible : Visibility.Collapsed;
        OpenLevelButton.Visibility = level ? Visibility.Visible : Visibility.Collapsed;
        ExtractButton.Visibility = extract ? Visibility.Visible : Visibility.Collapsed;
        ExtractAllButton.Visibility = extractAll ? Visibility.Visible : Visibility.Collapsed;
        CopyToOverridesButton.Visibility = copy ? Visibility.Visible : Visibility.Collapsed;
        ShowOverrideButton.Visibility = showOverride ? Visibility.Visible : Visibility.Collapsed;
    }

    // ---- File buttons --------------------------------------------------------------------------

    void OnOpenTable(object sender, RoutedEventArgs e)
    {
        if (selectedFile is { } file && IsTable(file)) context.OpenTable(file);
    }

    void OnOpenLevel(object sender, RoutedEventArgs e)
    {
        if (selectedFile is { } file && IsZone(file)) context.OpenLevel(file);
    }

    void OnExtract(object sender, RoutedEventArgs e)
    {
        if (selectedFile == null) return;
        var dialog = new SaveFileDialog { FileName = selectedFile.Name, Title = "Save a copy of " + selectedFile.Name };
        if (dialog.ShowDialog() != true) return;
        Try(() =>
        {
            File.WriteAllBytes(dialog.FileName, ReadAsGameSeesIt(selectedFile));
            context.Log("Saved " + dialog.FileName);
        });
    }

    async void OnExtractAll(object sender, RoutedEventArgs e)
    {
        Packfile? pack;
        try
        {
            pack = selectedNode switch
            {
                ArchiveNode archive => archive.Open(),
                FileNode { File.Entry.IsContainer: true } container => container.OpenContainer(),
                _ => null,
            };
        }
        catch (Exception ex)
        {
            ShowError(ex);
            return;
        }
        if (pack == null) return;

        var dialog = new OpenFolderDialog { Title = $"Extract the files in {pack.Name} into a folder" };
        if (dialog.ShowDialog() != true) return;
        string target = Path.Combine(dialog.FolderName, pack.Name);
        SetBusy(true);
        try
        {
            context.Log($"Extracting {pack.Entries.Count} files from {pack.Name}...");
            await Task.Run(() =>
            {
                Directory.CreateDirectory(target);
                foreach (PackfileEntry entry in pack.Entries)
                    File.WriteAllBytes(Path.Combine(target, entry.Name), pack.Read(entry));
            });
            context.Log($"Extracted {pack.Entries.Count} files into {target}");
        }
        catch (Exception ex)
        {
            ShowError(ex);
        }
        finally
        {
            SetBusy(false);
        }
    }

    void OnCopyToOverrides(object sender, RoutedEventArgs e)
    {
        if (selectedFile is not { CanOverride: true } file) return;
        string target = Workspace.OverridePath(file.Archive, file.Name);
        string shown = Workspace.RelativeToMod(target);
        if (Workspace.OverrideFor(file.Archive, file.Name) is string elsewhere && !elsewhere.Equals(target, StringComparison.OrdinalIgnoreCase))
        {
            MessageBox.Show($"{file.Name} is already overridden by {Workspace.RelativeToMod(elsewhere)}. That archive's copy " +
                            "is identical to this one, so the one override already replaces both.",
                            "Copy to overrides", MessageBoxButton.OK, MessageBoxImage.Information);
            return;
        }
        if (File.Exists(target) &&
            MessageBox.Show($"{shown} already exists, maybe with your edits in it.\n\nReplace it with the game's original?",
                            "Copy to overrides", MessageBoxButton.YesNo, MessageBoxImage.Warning) != MessageBoxResult.Yes)
            return;
        Try(() =>
        {
            Workspace.WriteOverride(file.Archive, file.Name, Workspace.ReadOriginal(file.Archive, file.Name));
            context.OverridesChanged();
            context.Log($"Copied to {shown}. Edit that copy; the game uses it from the next start through RFAModLauncher.");
        });
    }

    void OnShowOverride(object sender, RoutedEventArgs e)
    {
        if (selectedFile == null) return;
        string? path = Workspace.OverrideFor(selectedFile.Archive, selectedFile.Container ?? selectedFile.Name);
        if (path != null) Process.Start("explorer.exe", $"/select,\"{path}\"");
    }

    // ---- Textures ------------------------------------------------------------------------------

    /// <summary>Shows the textures of a texture file pair, as the game will see them.</summary>
    void ShowTextures(PegLocation at, int index)
    {
        texturesAt = at;
        peg = TextureEdits.Load(Workspace, at);
        PreviewText.Visibility = Visibility.Collapsed;
        TexturePanel.Visibility = Visibility.Visible;
        TextureList.ItemsSource = peg.Textures.Select(t => t.Name).ToList();
        TextureList.IsEnabled = peg.Textures.Count > 1;
        TextureList.SelectedIndex = Math.Clamp(index, 0, peg.Textures.Count - 1);
        ShowTexture();
    }

    PegTexture? SelectedTexture => peg != null && TextureList.SelectedIndex >= 0 ? peg.Textures[TextureList.SelectedIndex] : null;

    void OnTextureChosen(object sender, SelectionChangedEventArgs e) => ShowTexture();

    void OnShowAlphaChanged(object sender, RoutedEventArgs e) => ShowTexture();

    void ShowTexture()
    {
        if (texturesAt == null || peg == null || SelectedTexture is not PegTexture texture) return;
        Try(() =>
        {
            bool replaced = TextureEdits.IsReplaced(Workspace, texturesAt);
            string where = texturesAt.Container == null
                ? "Replacing it saves its data file to overrides."
                : $"It is inside {texturesAt.Container}: replacing it rebuilds that container into overrides and " +
                  "updates the asset lists that describe it.";
            TextureInfo.Text = $"{texture.Name}: {texture.Description}. " +
                               (replaced ? "Replaced: the game uses your version (shown here). " : "") + where;
            RestoreTextureButton.Visibility = replaced ? Visibility.Visible : Visibility.Collapsed;
            ReplaceTextureButton.IsEnabled = texture.Format.IsKnown();
            TextureImage.Source = texture.Format.IsKnown()
                ? Bitmaps.FromRgba(TextureCodec.Decode(texture.Format, texture.Width, texture.Height, peg.TextureData(texture)),
                                   ShowAlphaBox.IsChecked == true)
                : null;
        });
    }

    void OnExportPng(object sender, RoutedEventArgs e)
    {
        if (peg == null || SelectedTexture is not PegTexture texture) return;
        var dialog = new SaveFileDialog { FileName = Path.ChangeExtension(texture.Name, ".png"), Filter = "PNG image|*.png" };
        if (dialog.ShowDialog() != true) return;
        Try(() =>
        {
            var encoder = new PngBitmapEncoder();
            encoder.Frames.Add(BitmapFrame.Create(Bitmaps.FromRgba(TextureCodec.Decode(texture.Format, texture.Width, texture.Height,
                                                                                       peg.TextureData(texture)), alpha: true)));
            using FileStream file = File.Create(dialog.FileName);
            encoder.Save(file);
            context.Log("Saved " + dialog.FileName);
        });
    }

    void OnExportDds(object sender, RoutedEventArgs e)
    {
        if (peg == null || SelectedTexture is not PegTexture texture) return;
        var dialog = new SaveFileDialog { FileName = Path.ChangeExtension(texture.Name, ".dds"), Filter = "DDS texture|*.dds" };
        if (dialog.ShowDialog() != true) return;
        Try(() =>
        {
            var dds = new DdsImage(texture.Format, texture.Width, texture.Height, texture.MipLevels, peg.TextureData(texture));
            File.WriteAllBytes(dialog.FileName, dds.ToBytes());
            context.Log($"Saved {dialog.FileName} ({texture.Format.Describe()}, {texture.MipLevels} mip levels)");
        });
    }

    async void OnReplaceTexture(object sender, RoutedEventArgs e)
    {
        if (texturesAt is not PegLocation at || SelectedTexture is not PegTexture texture) return;
        var dialog = new OpenFileDialog
        {
            Title = $"Replace {texture.Name} ({texture.Width} × {texture.Height})",
            Filter = "Images|*.png;*.dds;*.jpg;*.jpeg;*.bmp;*.tif;*.tiff|All files|*.*",
        };
        if (dialog.ShowDialog() != true) return;

        int index = TextureList.SelectedIndex;
        SetBusy(true);
        context.Log($"Replacing {texture.Name}...");
        try
        {
            // A DDS in the texture's own format, size and mip levels is used as it is; anything else
            // is decoded and encoded again, with fresh mip levels.
            DdsImage? dds = dialog.FileName.EndsWith(".dds", StringComparison.OrdinalIgnoreCase)
                ? DdsImage.Read(File.ReadAllBytes(dialog.FileName))
                : null;
            RgbaImage? image = dds == null ? LoadImage(dialog.FileName) : null;
            int width = dds?.Width ?? image!.Width, height = dds?.Height ?? image!.Height;
            if (width != texture.Width || height != texture.Height)
            {
                MessageBox.Show($"{texture.Name} is {texture.Width} × {texture.Height}, and the image is {width} × {height}. " +
                                "Resize the image to the texture's size first (changing a texture's size comes in a later stage).",
                                "Replace texture", MessageBoxButton.OK, MessageBoxImage.Information);
                context.Log("Nothing replaced.");
                return;
            }
            bool useAsIs = dds != null && dds.Format == texture.Format && dds.MipLevels >= texture.MipLevels;
            string message = await Task.Run(() =>
            {
                byte[] data = useAsIs
                    ? dds!.Data.AsSpan(0, texture.DataSize).ToArray()
                    : TextureCodec.EncodeWithMips(texture.Format, image ?? dds!.ToRgba(), texture.MipLevels);
                return TextureEdits.Replace(Workspace, at, texture, data);
            });
            context.OverridesChanged();
            ShowTextures(at, index);
            context.Log(message + " The game uses it from its next start through RFAModLauncher.");
        }
        catch (Exception ex)
        {
            ShowError(ex);
        }
        finally
        {
            SetBusy(false);
        }
    }

    async void OnRestoreTexture(object sender, RoutedEventArgs e)
    {
        if (texturesAt is not PegLocation at) return;
        string what = at.Container ?? at.DataName;
        if (MessageBox.Show($"Go back to the game's own {what}?\n\nThis undoes every texture you replaced in it.",
                            "Restore original", MessageBoxButton.YesNo, MessageBoxImage.Question) != MessageBoxResult.Yes)
            return;
        int index = TextureList.SelectedIndex;
        SetBusy(true);
        try
        {
            string message = await Task.Run(() => TextureEdits.Restore(Workspace, at));
            context.OverridesChanged();
            ShowTextures(at, index);
            context.Log(message);
        }
        catch (Exception ex)
        {
            ShowError(ex);
        }
        finally
        {
            SetBusy(false);
        }
    }

    /// <summary>Reads a PNG, JPG, BMP or TIFF with Windows' own image decoders.</summary>
    static RgbaImage LoadImage(string path)
    {
        BitmapDecoder decoder = BitmapDecoder.Create(new Uri(path), BitmapCreateOptions.PreservePixelFormat, BitmapCacheOption.OnLoad);
        var frame = new FormatConvertedBitmap(decoder.Frames[0], PixelFormats.Bgra32, null, 0);
        var image = new RgbaImage(frame.PixelWidth, frame.PixelHeight);
        frame.CopyPixels(image.Pixels, frame.PixelWidth * 4, 0);
        for (int i = 0; i < image.Pixels.Length; i += 4)
            (image.Pixels[i], image.Pixels[i + 2]) = (image.Pixels[i + 2], image.Pixels[i]);
        return image;
    }

    // ---- Models --------------------------------------------------------------------------------

    void ShowModel(ModelLocation at)
    {
        model = ModelFile.Read(at.HeaderName, ReadFrom(at.Archive, at.Container, at.HeaderName), ReadFrom(at.Archive, at.Container, at.DataName));
        modelAt = at;
        PreviewText.Visibility = Visibility.Collapsed;
        ModelPanel.Visibility = Visibility.Visible;
        ShowPiecesBox.IsChecked = false;
        (rig, rigName) = model.Kind == ModelKind.Character ? FindRig(model.Name, at.Archive, at.Container) : (null, null);
        ShowSkeletonBox.Visibility = rig != null ? Visibility.Visible : Visibility.Collapsed;
        ShowSkeletonBox.IsChecked = false;
        FillAnimationList(at.HeaderName);

        // A file with several objects: pick which one is shown.
        var objects = model.Objects?.Objects ?? [];
        showingList = true;
        ObjectList.ItemsSource = objects.Select(o => $"{(o.Name.Length > 0 ? o.Name : "(no name)")} — {(o.Breakable ? "breakable" : "solid")}, uid {o.Uid:X8}").ToList();
        ObjectList.SelectedIndex = objects.Count > 0 ? 0 : -1;
        ObjectList.Visibility = objects.Count > 1 ? Visibility.Visible : Visibility.Collapsed;
        showingList = false;
        ShowObject(frame: true);
    }

    bool showingList;  // filling the object list; its selection changes aren't the user's

    void OnObjectChosen(object sender, SelectionChangedEventArgs e)
    {
        if (model != null && !showingList) Try(() => ShowObject(frame: true));
    }

    /// <summary>The uid of the object picked in the list (for files with several), or null.</summary>
    uint? ChosenUid => model?.Objects is { } objects && ObjectList.SelectedIndex >= 0 && ObjectList.SelectedIndex < objects.Objects.Count
        ? objects.Objects[ObjectList.SelectedIndex].Uid
        : null;

    void ShowObject(bool frame)
    {
        if (model == null) return;
        ShowPiecesBox.Visibility = model.Broken(ChosenUid).Count > 0 ? Visibility.Visible : Visibility.Collapsed;
        if (ShowPiecesBox.Visibility != Visibility.Visible) ShowPiecesBox.IsChecked = false;
        ShowModelScene(frame);
    }

    void OnModelOptionChanged(object sender, RoutedEventArgs e)
    {
        if (animation != null && animatedScene != null && sender == ShowSkeletonBox)
            ShowFrame();  // the skeleton is drawn each frame anyway
        else if (model != null || animation != null) Try(() => ShowModelScene(frame: sender == ShowPiecesBox));
    }

    void OnResetView(object sender, RoutedEventArgs e) => modelViewer.ResetView();

    IReadOnlyList<PlacedSubmesh> ShownSubmeshes => model == null ? [] : ShowPiecesBox.IsChecked == true ? model.Broken(ChosenUid) : model.Intact(ChosenUid);

    void ShowModelScene(bool frame)
    {
        if (animation != null && rig != null)
        {
            ShowAnimatedScene(frame);
            return;
        }
        if (model == null || modelAt == null) return;
        var submeshes = ShownSubmeshes;
        var used = submeshes.SelectMany(s => s.Submesh.Parts).Select(p => p.Material).Distinct().ToList();
        bool textures = ShowTexturesBox.IsChecked == true;
        bool skeleton = rig != null && ShowSkeletonBox.IsChecked == true;
        int found = 0;
        Model3DGroup shown = ModelScene.Build(model, submeshes, material =>
        {
            BitmapSource? texture = textures ? ModelTexture(material) : null;
            if (texture != null) found++;
            return texture;
        }, opacity: skeleton ? 0.3 : 1);
        if (skeleton)
        {
            // The skeleton first, then the see-through model over it: WPF draws in order, and a
            // see-through surface drawn first would hide what is behind it.
            var both = new Model3DGroup();
            both.Children.Add(SkeletonScene.Build(rig!));
            both.Children.Add(shown);
            shown = both;
        }
        modelViewer.Show(shown, frame);

        string kind = model.Kind switch
        {
            ModelKind.Static => "Static mesh",
            ModelKind.Character => $"Character ({model.DetailLevels} detail levels; the most detailed is shown)",
            ModelKind.Destructible => (model.Objects?.Objects.Count > 1 ? $"Destructible object, one of {model.Objects.Objects.Count} in this file" : "Destructible object") +
                (ShowPiecesBox.IsChecked == true ? $": the {submeshes.Count} pieces it breaks into" : $", whole ({submeshes.Count} parts)"),
            ModelKind.Stitch => "Stitch piece (a fixed piece levels are built from; zones place them with their stitch list)",
            ModelKind.Foliage => "Foliage mesh (debris scattered over the ground; zones say where in their terrain file)",
            _ => $"Vehicle ({submeshes.Count} parts)",
        };
        int triangles = submeshes.Sum(s => s.Submesh.Parts.Sum(p => p.Triangles.Length / 3));
        string textureNote = !textures ? "Textures off."
            : $"Textures: {found} of {used.Count} found" +
              (found < used.Count && !context.Assets.IndexReady ? " (still looking through the other containers...)." : ".");
        string skeletonNote = model.Kind != ModelKind.Character ? ""
            : rig != null ? $" Skeleton: {rigName} ({rig.Bones.Count} bones, {rig.Tags.Count} tags; helper bones for items and the camera aren't drawn)."
            : " No skeleton found for it.";
        ModelInfo.Text = $"{kind}: {model.Positions.Length:N0} vertices, {triangles:N0} triangles, {model.Materials.Count} materials. " +
                         $"{textureNote}{skeletonNote} Drag to turn it around, right-drag to move, scroll to zoom.";
    }

    // ---- Skeletons -----------------------------------------------------------------------------

    /// <summary>
    /// A character's skeleton: the rig its entry in character.xtbl (or a DLC's) names for its mesh
    /// ("x.rigx" is the file x.rig_pc), else one with the mesh's own name. Null if none is found.
    /// </summary>
    (Rig?, string?) FindRig(string meshFile, string nearArchive, string? nearContainer)
    {
        string mesh = Path.GetFileNameWithoutExtension(meshFile);
        var names = new List<string>();
        foreach (string table in new[] { "character.xtbl", "dlc01_character.xtbl", "dlc02_character.xtbl", "dlc03_character.xtbl", "dlc04_character.xtbl" })
            foreach (XtblElement entry in context.Assets.Table(table)?.Entries ?? [])
                if (entry.Element("Model")?.ValueOf("Filename") is { } model &&
                    Path.GetFileNameWithoutExtension(model.Trim()).Equals(mesh, StringComparison.OrdinalIgnoreCase) &&
                    entry.Element("Rig")?.ValueOf("Filename") is { Length: > 0 } rigFile)
                    names.Add(Path.ChangeExtension(rigFile.Trim(), ".rig_pc"));
        names.Add(mesh + ".rig_pc");
        foreach (string name in names.Distinct(StringComparer.OrdinalIgnoreCase))
        {
            if (context.Assets.ReadFile(name, nearArchive, nearContainer) is not { } file) continue;
            try { return (Rig.Read(file.Data), name.ToLowerInvariant()); }
            catch (InvalidDataException) { }
        }
        return (null, null);
    }

    /// <summary>A skeleton file on its own: its bones and tags in 3D, and what they are called.</summary>
    void ShowRig(GameFile file)
    {
        rig = Rig.Read(ReadAsGameSeesIt(file));
        rigName = file.Name;
        model = null;
        modelAt = null;
        PreviewText.Visibility = Visibility.Collapsed;
        ModelPanel.Visibility = Visibility.Visible;
        ObjectList.Visibility = ShowPiecesBox.Visibility = ShowSkeletonBox.Visibility = Visibility.Collapsed;
        modelViewer.Show(SkeletonScene.Build(rig), frame: true);
        var top = rig.Bones.Take(12).Select(b => b.Name);
        ModelInfo.Text = $"Skeleton: {rig.Bones.Count} bones ({string.Join(", ", top)}{(rig.Bones.Count > 12 ? ", ..." : "")}) and {rig.Tags.Count} tags " +
                         $"(points things attach to{(rig.Tags.Count > 0 ? ": " + string.Join(", ", rig.Tags.Take(6).Select(t => t.Name)) + (rig.Tags.Count > 6 ? ", ..." : "") : "")}), " +
                         $"in its rest pose. {rig.Bones.Count(SkeletonScene.IsHelper)} helper bones (where items are held, the camera) aren't drawn: they only get " +
                         "their place while animating. Characters name their skeleton in character.xtbl; open one's .ccmesh_pc to see it inside the character.";
    }

    // ---- Animations ----------------------------------------------------------------------------

    /// <summary>Who can play an animation: a character (its model and skeleton) or a skeleton alone.</summary>
    sealed record Cast(string Label, string? Model, string Rig);

    /// <summary>For a character's model: the animations its animation sets name, to play on it.</summary>
    void FillAnimationList(string modelFile)
    {
        var names = rig == null ? [] : Characters.WithModel(modelFile.ToLowerInvariant()).SelectMany(Characters.AnimationsOf)
                                                 .Distinct(StringComparer.OrdinalIgnoreCase).Order(StringComparer.OrdinalIgnoreCase).ToList();
        showingList = true;
        AnimationList.ItemsSource = names.Count == 0 ? null : names.Select(Path.GetFileNameWithoutExtension).Prepend("(rest pose: no animation)").ToList();
        AnimationList.SelectedIndex = names.Count == 0 ? -1 : 0;
        AnimationList.Tag = names;
        AnimationList.Visibility = names.Count > 0 ? Visibility.Visible : Visibility.Collapsed;
        showingList = false;
    }

    void OnAnimationChosen(object sender, SelectionChangedEventArgs e)
    {
        if (showingList || model == null || modelAt == null || rig == null || AnimationList.Tag is not List<string> names) return;
        Try(() =>
        {
            StopAnimation();
            int chosen = AnimationList.SelectedIndex - 1;
            if (chosen >= 0 && chosen < names.Count)
            {
                if (context.Assets.ReadFile(names[chosen], modelAt.Archive, modelAt.Container) is not { } file)
                    throw new FileNotFoundException($"{names[chosen]} wasn't found in the game's files");
                var read = Animation.Read(file.Data);
                if (read.SkeletonBones != rig.Bones.Count)
                    throw new InvalidDataException($"{names[chosen]} is made for a skeleton of {read.SkeletonBones} bones, and {rigName} has {rig.Bones.Count}");
                (animation, animationName) = (read, names[chosen]);
            }
            ShowModelScene(frame: false);
        });
    }

    /// <summary>An animation file: played on a character whose animation sets name it, or on a skeleton with its number of bones.</summary>
    void ShowAnimationFile(GameFile file)
    {
        animation = Animation.Read(ReadAsGameSeesIt(file));
        animationName = file.Name;
        PreviewText.Visibility = Visibility.Collapsed;
        ModelPanel.Visibility = Visibility.Visible;
        ObjectList.Visibility = ShowPiecesBox.Visibility = Visibility.Collapsed;
        ShowSkeletonBox.IsChecked = false;

        // Characters whose sets name it first, then any skeleton of its size. Civilians come after
        // the others: the game puts them together from a head, a body and an outfit, and only their
        // first mesh (head and hands) is shown here.
        int bones = animation.SkeletonBones;
        var users = Characters.UsersOf(file.Name.ToLowerInvariant())
            .Where(c => Characters.BoneCount(c.Rig, file.Archive, file.Container) == bones)
            .DistinctBy(c => (c.Model, c.Rig))
            .OrderBy(c => c.Model.Contains("_civ_") ? 1 : 0)
            .Select(c => new Cast($"{c.Name} ({Path.GetFileNameWithoutExtension(c.Model)})", c.Model, c.Rig));
        var skeletons = Characters.AllRigs.Order(StringComparer.OrdinalIgnoreCase)
            .Where(r => Characters.BoneCount(r, file.Archive, file.Container) == bones)
            .Select(r => new Cast($"Skeleton only: {r}", null, r));
        cast = users.Concat(skeletons).ToList();
        showingList = true;
        CastList.ItemsSource = cast.Select(c => c.Label).ToList();
        CastList.SelectedIndex = cast.Count > 0 ? 0 : -1;
        CastList.Visibility = cast.Count > 0 ? Visibility.Visible : Visibility.Collapsed;
        showingList = false;
        if (cast.Count == 0)
        {
            ShowSkeletonBox.Visibility = Visibility.Collapsed;
            ModelInfo.Text = $"{AnimationSummary()} No skeleton with {bones} bones was found" +
                             (context.Assets.IndexReady ? "." : " yet (still looking through the game's files; pick it again in a moment).");
            return;
        }
        PlayCast(cast[0], file.Archive, file.Container);
    }

    void OnCastChosen(object sender, SelectionChangedEventArgs e)
    {
        if (showingList || selectedFile == null || CastList.SelectedIndex < 0 || CastList.SelectedIndex >= cast.Count) return;
        Try(() => PlayCast(cast[CastList.SelectedIndex], selectedFile.Archive, selectedFile.Container));
    }

    /// <summary>Loads a character (or a skeleton alone) and plays the animation file on it.</summary>
    void PlayCast(Cast who, string nearArchive, string? nearContainer)
    {
        if (context.Assets.ReadFile(who.Rig, nearArchive, nearContainer) is not { } rigFile)
            throw new FileNotFoundException($"{who.Rig} wasn't found in the game's files");
        rig = Rig.Read(rigFile.Data);
        rigName = who.Rig;
        model = null;
        modelAt = null;
        if (who.Model != null && context.Assets.PlacesOf(who.Model).FirstOrDefault() is var (archive, container) && archive != null)
        {
            model = context.Assets.Model(who.Model, archive, container);
            if (model != null) modelAt = new ModelLocation(archive, container, who.Model, ModelFile.DataFileName(who.Model));
        }
        ShowSkeletonBox.Visibility = model != null ? Visibility.Visible : Visibility.Collapsed;
        animatedScene = null;
        animationFrame = 0;
        ShowAnimatedScene(frame: true);
    }

    /// <summary>Builds the posed character (or skeleton) for the animation, shows it, and plays.</summary>
    void ShowAnimatedScene(bool frame)
    {
        if (animation == null || rig == null) return;
        bool textures = ShowTexturesBox.IsChecked == true;
        IReadOnlyList<PlacedSubmesh> submeshes = model?.Intact() ?? [];
        animatedScene = new AnimatedScene(rig, model, submeshes, material => textures ? ModelTexture(material) : null);
        if (frame)
        {
            // Framed on the rest pose, arms out, so that no moment of the animation is cut off.
            animatedScene.Show(null, 0, false, ShowSkeletonBox.IsChecked == true);
            modelViewer.Show(animatedScene.Group, frame: true);
        }
        else modelViewer.Show(animatedScene.Group, frame: false);
        ShowFrame();
        PlayBar.Visibility = Visibility.Visible;
        FrameSlider.Maximum = Math.Max(1, animation.Frames);
        string who = model != null ? $"Played by {Path.GetFileNameWithoutExtension(model.Name)} (skeleton {rigName})." : $"Played on the skeleton {rigName} (no model).";
        ModelInfo.Text = $"{AnimationSummary()} {who} Drag to turn it around, right-drag to move, scroll to zoom.";
        SetPlaying(true);
    }

    string AnimationSummary()
    {
        if (animation == null) return "";
        string moves = animation.Movement.Length() > 0.05f ? $"; it moves the character {animation.Movement.Length():0.00} m" : "";
        string extra = (animation.Tracks.Any(t => t.Weights.Count > 0) ? "; it has blend weights per bone" : "") +
                       (animation.Tracks.Any(t => t.Scales.Count > 0) ? "; it scales bones" : "");
        return $"Animation {Path.GetFileNameWithoutExtension(animationName)}: {animation.Frames} frames ({animation.Seconds:0.00} s at 60 a second), " +
               $"{animation.Tracks.Count} of its skeleton's {animation.SkeletonBones} bones animated{moves}{extra}.";
    }

    /// <summary>Poses the character at the current frame, and shows where we are.</summary>
    void ShowFrame()
    {
        if (animation == null || animatedScene == null) return;
        animatedScene.Show(animation, animationFrame, MoveAlongBox.IsChecked == true, ShowSkeletonBox.IsChecked == true);
        showingFrame = true;
        FrameSlider.Value = animationFrame;
        showingFrame = false;
        FrameText.Text = $"frame {animationFrame:0} of {animation.Frames} · {animationFrame / Animation.FramesPerSecond:0.00} s";
    }

    void SetPlaying(bool play)
    {
        if (play == playing) return;
        playing = play;
        PlayButton.Content = play ? "Pause" : "Play";
        if (play)
        {
            lastRendered = TimeSpan.Zero;
            CompositionTarget.Rendering += OnRendering;
        }
        else CompositionTarget.Rendering -= OnRendering;
    }

    /// <summary>Each time WPF draws: moves the animation on by the time since the last frame, looping.</summary>
    void OnRendering(object? sender, EventArgs e)
    {
        if (animation == null || animatedScene == null || !IsVisible || e is not RenderingEventArgs rendering) return;
        if (rendering.RenderingTime == lastRendered) return;  // called more than once for the same frame
        double seconds = lastRendered == TimeSpan.Zero ? 0 : (rendering.RenderingTime - lastRendered).TotalSeconds;
        lastRendered = rendering.RenderingTime;
        double speed = SpeedList.SelectedItem is ComboBoxItem { Tag: string tag } && double.TryParse(tag, System.Globalization.CultureInfo.InvariantCulture, out double s) ? s : 1;
        animationFrame += (float)(Math.Min(seconds, 0.25) * Animation.FramesPerSecond * speed);
        if (animation.Frames > 0) animationFrame %= animation.Frames;
        else animationFrame = 0;
        ShowFrame();
    }

    void OnPlayPause(object sender, RoutedEventArgs e) => SetPlaying(!playing);

    void OnPlayOptionChanged(object sender, RoutedEventArgs e)
    {
        if (!playing) ShowFrame();
    }

    void OnFrameSlid(object sender, RoutedPropertyChangedEventArgs<double> e)
    {
        if (showingFrame || animation == null) return;
        SetPlaying(false);
        animationFrame = (float)e.NewValue;
        ShowFrame();
    }

    void StopAnimation()
    {
        SetPlaying(false);
        animation = null;
        animationName = null;
        animatedScene = null;
        animationFrame = 0;
        PlayBar.Visibility = Visibility.Collapsed;
    }

    /// <summary>A material's colour texture, at most 1024 pixels wide.</summary>
    BitmapSource? ModelTexture(int material)
    {
        if (model == null || modelAt == null || material >= model.Materials.Count) return null;
        string? name = model.Materials[material].Diffuse;
        // Alpha kept for the materials the game draws see-through (glass, bullet holes, hair).
        return string.IsNullOrEmpty(name) ? null : context.Assets.Texture(name, modelAt.Archive, modelAt.Container, 1024,
                                                                            alpha: Render.GameMaterial.IsSeeThrough(model.Materials[material]));
    }

    byte[] ReadFrom(string archive, string? container, string fileName)
    {
        if (container == null) return Workspace.Read(archive, fileName);
        Packfile pack = Workspace.Container(archive, container);
        return pack.Read(pack.Find(fileName) ?? throw new FileNotFoundException($"{container} has no {fileName}"));
    }

    void OnExportObj(object sender, RoutedEventArgs e)
    {
        if (model == null || modelAt == null) return;
        var dialog = new SaveFileDialog { FileName = Path.ChangeExtension(model.Name, ".obj"), Filter = "OBJ model|*.obj" };
        if (dialog.ShowDialog() != true) return;
        Try(() =>
        {
            // The colour textures go next to the OBJ as PNG files, at full size.
            string folder = Path.GetDirectoryName(dialog.FileName)!;
            var submeshes = ShownSubmeshes;
            var images = new Dictionary<int, string?>();
            int saved = 0;
            foreach (int material in submeshes.SelectMany(s => s.Submesh.Parts).Select(p => p.Material).Distinct())
            {
                string? name = material < model.Materials.Count ? model.Materials[material].Diffuse : null;
                if (string.IsNullOrEmpty(name) || context.Assets.FindTexture(name, modelAt.Archive, modelAt.Container) is not var (texturePeg, texture)) continue;
                string image = Path.ChangeExtension(name, ".png");
                var encoder = new PngBitmapEncoder();
                encoder.Frames.Add(BitmapFrame.Create(Bitmaps.FromRgba(texturePeg.Decode(texture), alpha: true)));
                using (FileStream file = File.Create(Path.Combine(folder, image))) encoder.Save(file);
                images[material] = image;
                saved++;
            }
            ObjExport.Write(model, submeshes, dialog.FileName, images);
            context.Log($"Saved {dialog.FileName} with its .mtl and {saved} texture{(saved == 1 ? "" : "s")} as PNG.");
        });
    }

    // ---- Errors and waiting --------------------------------------------------------------------

    void SetBusy(bool busy)
    {
        IsEnabled = !busy;
        if (Inspector != null) Inspector.IsEnabled = !busy;
    }

    void Try(Action action)
    {
        try
        {
            action();
        }
        catch (Exception ex)
        {
            ShowError(ex);
        }
    }

    void ShowError(Exception ex)
    {
        DetailsText.Text = "Something went wrong: " + ex.Message;
        context.Log($"{ex.GetType().Name}: {ex.Message}");
    }
}
