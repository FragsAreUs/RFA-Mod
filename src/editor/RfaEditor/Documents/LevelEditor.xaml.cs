using System.IO;
using System.Numerics;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Controls.Primitives;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Media.Media3D;
using System.Windows.Shapes;
using System.Windows.Threading;
using RfaFormats;
using RfaFormats.Meshes;
using RfaFormats.Textures;
using RfaFormats.Zones;
using Point = System.Windows.Point;
using Vector = System.Windows.Vector;

namespace RfaEditor;

/// <summary>
/// The level editor: one zone (or mission layer) of a level in 3D. Its objects are in the
/// outliner by class, the picked object's position, rotation and properties in the inspector.
/// Objects are moved and turned with the mouse, with handles (the gizmo) as in other 3D editors, or
/// by typing or dragging the inspector's numbers; they can be duplicated, added (a copy of one of
/// the file's objects, where the view points) and deleted (unless something names them), and every
/// change can be undone. Whole kinds of objects can be hidden with the "Show" chips. The zone's
/// level geometry (its stitch pieces: rock, cave walls, roads, ...), decals, terrain (the ground)
/// and foliage (debris scattered over it) are drawn around them, but can't be changed yet. So are
/// the zone's other files (its mission layers, or for a layer the zone itself; see
/// <see cref="RelatedFile"/>), whose objects can be picked and opened, and the zones next to it.
/// Changes are saved to overrides\ like everything else (see <see cref="ZoneFile.ToBytes"/>).
///
/// Mouse and keys in the 3D view:
///   left click          pick an object          left drag on a handle   move or turn the object
///   right drag          look around             right drag + WASD/QE    fly (Shift: faster)
///   middle drag         slide the view          wheel                   move forwards and back
///   Alt + left drag     circle the picked object
///   Q / W / E           select / move / rotate tool      F  look at the picked object
///   Ctrl+D              duplicate the picked object       Del  delete it
///   Ctrl+Z / Ctrl+Y     undo / redo                       Ctrl+S  save
/// </summary>
public partial class LevelEditor : UserControl, IEditorDocument
{
    const double AngleStep = Math.PI / 12;  // snapping turns to 15 degrees

    readonly EditorContext context;
    readonly string archive;
    readonly string? container;  // the .str2_pc the zone is in, if any
    readonly string zoneName;
    readonly Dictionary<string, List<(MeshGeometry3D Mesh, Material Material)>?> modelParts = new(StringComparer.OrdinalIgnoreCase);
    readonly Dictionary<ZoneObject, TreeViewItem> treeItems = [];
    readonly FlyCamera camera = new();
    readonly PerspectiveCamera sceneCamera = new(), gizmoCamera = new();
    readonly ModelVisual3D sceneContent = new(), selectionBox = new(), gizmoContent = new(), gridContent = new(), geometryContent = new(),
                           terrainContent = new(), decalContent = new(), foliageContent = new(), relatedContent = new(), neighbourContent = new(),
                           linkContent = new(), pickedLinkContent = new(), navMeshVisual = new();
    readonly Gizmo gizmo = new();
    readonly GameLink gameLink;
    double? gameFov;     // the running game's vertical field of view, while following it
    Follow? follow;      // while following the game: where its camera is going
    readonly Stack<Change> undo = new(), redo = new();
    readonly HashSet<ObjectKind> hiddenKinds = [];
    ZoneFile zone;
    readonly ZoneNames names;
    readonly ZonePropertyRows propertyRows;  // the inspector's properties, as the game's zone schema describes them
    LevelScene? scene;
    List<RelatedFile>? related;  // the zone's other files, drawn with it (found once the index is ready)
    readonly Dictionary<ZoneObject, RelatedFile> relatedOf = [];  // ...and which of them each of their objects is in
    Model3D? grid, geometry, terrain, decals, foliage, neighbours;
    bool showGrid = true, showGeometry = true, showTerrain = true, showDecals = true, showFoliage = true, showNeighbours = true, showLinks;
    List<ZoneLink>? links;       // between the zone's objects and its other files' (see Links)
    int neighbourZones;          // zones around this one drawn
    int geometryPieces;          // stitch pieces drawn
    int decalCount;              // decals drawn
    int terrainParts;            // terrain sub-zones with ground drawn
    int foliagePieces;           // pieces of foliage drawn
    ZoneObject? selected;
    TransformFields? transform;  // the picked object's position and rotation in the inspector
    byte[]? editStart;           // the picked object's bytes when its current change began (for undo)
    byte[]? turnStart;           // its bytes when it began turning in this change (see TurnSelected)
    List<Carried> carried = [];  // what is attached to it, moved and turned with it (see StartEdit)
    LevelTool tool = LevelTool.Move;
    bool unsaved, selectingFromView;
    // Chunks brought in from elsewhere (another level, the Guerrilla world): their files go into the
    // zone's container on saving, for those the zone's objects still name then.
    readonly Dictionary<string, ChunkPackage> chunksToAdd = new(StringComparer.OrdinalIgnoreCase);

    /// <summary>A chunk to add, picked in the Add list.</summary>
    sealed record ChunkChoice(string Chunk);

    /// <summary>
    /// One change, which can be undone and redone: an edit of one object (its bytes before and
    /// after, and those of the objects that moved with it, <paramref name="Others"/>), or objects
    /// added or deleted (<paramref name="AddsOrRemoves"/>).
    /// </summary>
    sealed record Change(ZoneObject? Object, string What, Action Undo, Action Redo, bool AddsOrRemoves = false,
                         IReadOnlyList<ZoneObject>? Others = null);

    /// <summary>An object attached to the picked one, and its bytes when the change began and when it began turning.</summary>
    sealed class Carried(ZoneObject obj)
    {
        public ZoneObject Object { get; } = obj;
        public byte[] Start { get; } = obj.Snapshot();
        public byte[]? TurnStart { get; set; }
    }

    // What the mouse is doing in the 3D view
    enum Drag { None, Look, Slide, Circle, Handle, Sculpt, NavMesh }
    Drag drag;
    Point dragFrom;
    Point? pressedAt;
    Axis? hotAxis;                                        // the handle under the mouse, or being dragged
    (Vector3 Position, Matrix4x4 Rotation) dragStart;     // the object's placement when the drag began
    double dragStartParameter;                            // where along the axis, or at which angle, it began
    DateTime lastFrame;

    internal LevelEditor(EditorContext context, string archive, string? container, string zoneName)
    {
        InitializeComponent();
        this.context = context;
        this.archive = archive;
        this.container = container;
        this.zoneName = zoneName;
        Outliner = DocumentParts.Detach(OutlinerPart);
        Inspector = DocumentParts.Detach(InspectorPart);
        DetachWindowParts();  // its menus, status and Show chips, which the window shows (see LevelEditor.Layout.cs)
        names = context.Assets.ZoneNames;
        propertyRows = new ZonePropertyRows(names, context.Assets.ZoneSchema, context.Assets.Choices,
                                            () => context.Assets.LogicFunctions, ScriptObjectName, h => EditScript(h), AddLogicHandler);
        zone = ZoneFile.Read(ReadZone());
        NotePlacements();
        gameLink = new GameLink(Workspace.ModFolder);
        ViewArea.SizeChanged += (_, _) => UpdateCamera();  // the field of view depends on the view's shape while following the game

        // The level and the selection box in one view; the gizmo in another on top of it.
        SceneViewport.Camera = sceneCamera;
        GizmoViewport.Camera = gizmoCamera;
        var lights = new Model3DGroup();
        lights.Children.Add(new AmbientLight(Color.FromRgb(85, 85, 85)));
        lights.Children.Add(new DirectionalLight(Colors.White, new Vector3D(-0.4, -0.8, -0.5)));
        lights.Children.Add(new DirectionalLight(Color.FromRgb(100, 100, 110), new Vector3D(0.6, 0.3, 0.7)));
        SceneViewport.Children.Add(new ModelVisual3D { Content = lights });
        SceneViewport.Children.Add(gridContent);
        SceneViewport.Children.Add(neighbourContent);
        SceneViewport.Children.Add(terrainContent);
        SceneViewport.Children.Add(geometryContent);
        SceneViewport.Children.Add(sceneContent);
        SceneViewport.Children.Add(relatedContent);
        SceneViewport.Children.Add(foliageContent);  // after everything solid, as they are see-through
        SceneViewport.Children.Add(decalContent);
        SceneViewport.Children.Add(linkContent);
        SceneViewport.Children.Add(pickedLinkContent);
        SceneViewport.Children.Add(selectionBox);
        GizmoViewport.Children.Add(new ModelVisual3D { Content = new AmbientLight(Colors.White) });
        gizmoContent.Content = gizmo.Model;
        GizmoViewport.Children.Add(gizmoContent);

        SceneViewport.Children.Add(brushVisual);
        SceneViewport.Children.Add(magnetVisual);
        SceneViewport.Children.Add(helpersVisual);  // light and camera cones, invisible walls (LevelEditor.Helpers.cs)
        SceneViewport.Children.Add(navMeshVisual);
        SceneViewport.Children.Add(navMeshBrushVisual);
        SetTool(LevelTool.Move);
        CompositionTarget.Rendering += PadFrame;
        GamePanel.ControllerChanged += ShowControllerOwner;
        ShowControllerOwner();
        ShowSource();
        BuildObjectTree("");
        BuildGrid();
        BuildScene(frame: true);
        ShowProperties();

        // Opened before the editor finished reading every container: draw again once it has, as
        // models and textures in other containers and archives can be found then.
        if (!context.Assets.IndexReady) context.Assets.IndexBecameReady += OnIndexReady;
    }

    /// <summary>
    /// The file index was read again (after a new level's archive was written): what wasn't found
    /// before (the level's ground, level geometry, sky, its other files) is looked for again and drawn.
    /// </summary>
    public void FilesIndexedAgain()
    {
        copies = null;  // (the zone's other copies: the index now has the new archive's containers)
        related = null;
        relatedOf.Clear();
        OnIndexReady();
    }

    void OnIndexReady()
    {
        context.Assets.IndexBecameReady -= OnIndexReady;
        modelParts.Clear();
        links = null;  // the zone's other files are found now, and their links with them
        BuildScene(frame: false);
        BuildObjectTree(FilterBox.Text.Trim());  // with the zone's other files, found now
        ShowSource();  // the zone's other copies can be found now
        // The Direct3D view's textures and the game light (the mission layers' lamps, the lamps'
        // flicker curves, the sky) were looked for before, and many weren't found: again, now.
        gpuScene?.ForgetTextures();
        if (zoneLighting != null)
        {
            zoneLighting = null;
            if (GameLightBox.IsChecked == true) OnGameLightChanged(GameLightBox, new RoutedEventArgs());
        }
    }

    Workspace Workspace => context.Workspace;

    public string Title => (unsaved ? "* " : "") + zoneName;
    public string Icon => Icons.Zone;
    public FrameworkElement View => this;
    public FrameworkElement? Outliner { get; }
    public FrameworkElement? Inspector { get; }
    public event Action? TitleChanged;

    string Where => container == null ? archive : $"{archive} > {container}";

    byte[] ReadZone()
    {
        if (container == null) return Workspace.Read(archive, zoneName);
        Packfile pack = Workspace.Container(archive, container);
        return pack.Read(pack.Find(zoneName) ?? throw new FileNotFoundException($"{container} has no {zoneName}"));
    }

    void ShowSource()
    {
        TitleChanged?.Invoke();
        var places = context.Assets.IndexReady ? Copies() : [(archive, container)];
        bool changed = places.Any(p => Workspace.IsChanged(p.Archive, p.Container ?? zoneName)) || TerrainChanged() || StitchChanged();
        SourceText.Text = (changed
            ? $"Editing your version of {zoneName} (in {Where})."
            : Workspace.IsNewArchive(archive)
                ? $"{zoneName} as it was built (in {Where}, ours, not the game's). Saving puts your changes in edits\\ (the archive stays as built); if the world is built again, changes made to the old one are left out."
                : $"The game's own {zoneName} (in {Where}). Saving puts your changes in overrides\\; the game's files are never changed.") +
            (places.Count > 1
                ? $" The game has {places.Count} identical copies of this zone and may read any of them, so saving changes all of them."
                : container != null ? " It is inside a container, so saving rebuilds the container and updates the asset lists." : "");
        RestoreButton.IsEnabled = changed;
    }

    // ---- Tools ---------------------------------------------------------------------------------

    void SetTool(LevelTool chosen)
    {
        tool = chosen;
        SelectTool.IsChecked = tool == LevelTool.Select;
        MoveTool.IsChecked = tool == LevelTool.Move;
        RotateTool.IsChecked = tool == LevelTool.Rotate;
        SculptTool.IsChecked = tool == LevelTool.Sculpt;
        NavMeshTool.IsChecked = tool == LevelTool.NavMesh;
        SculptPanel.Visibility = tool == LevelTool.Sculpt ? Visibility.Visible : Visibility.Collapsed;
        NavMeshPanel.Visibility = tool == LevelTool.NavMesh ? Visibility.Visible : Visibility.Collapsed;
        ShowBrush();
        ShowGizmo();
    }

    void OnToolClicked(object sender, RoutedEventArgs e)
    {
        if (sender == SculptTool) { ChooseSculpt(); return; }
        if (sender == NavMeshTool) { EnsureNavMesh(); return; }
        SetTool(sender == SelectTool ? LevelTool.Select : sender == RotateTool ? LevelTool.Rotate : LevelTool.Move);
    }

    bool Snapping => SnapBox.IsChecked == true;

    double GridSize => double.TryParse((GridSizeBox.SelectedItem as ComboBoxItem)?.Content as string,
                                       System.Globalization.NumberStyles.Float, System.Globalization.CultureInfo.InvariantCulture,
                                       out double size) ? size : 1;

    void OnKey(object sender, KeyEventArgs e)
    {
        bool control = Keyboard.Modifiers.HasFlag(ModifierKeys.Control);
        bool shift = Keyboard.Modifiers.HasFlag(ModifierKeys.Shift);
        if (control && e.Key == Key.Z && shift) Redo();
        else if (control && e.Key == Key.Z) Undo();
        else if (control && e.Key == Key.Y) Redo();
        else if (control && e.Key == Key.S) Save();
        else if (Keyboard.FocusedElement is TextBoxBase) return;  // typing in a property
        else if (drag == Drag.Look) { }                             // flying: the keys move the camera
        else if (control && shift && e.Key == Key.C) CopyProperties();
        else if (control && shift && e.Key == Key.V) PasteProperties();
        else if (control && e.Key == Key.D) DuplicateSelected();
        else if (e.Key == Key.Delete) DeleteSelected();
        else if (e.Key == Key.Q) SetTool(LevelTool.Select);
        else if (e.Key == Key.W) SetTool(LevelTool.Move);
        else if (e.Key == Key.E) SetTool(LevelTool.Rotate);
        else if (e.Key == Key.T) ChooseSculpt();
        else if (e.Key == Key.N) EnsureNavMesh();
        else if (e.Key == Key.M) ToggleMagnets();
        else if (e.Key == Key.OemOpenBrackets && tool == LevelTool.Sculpt) ResizeBrush(1 / 1.25);
        else if (e.Key == Key.OemCloseBrackets && tool == LevelTool.Sculpt) ResizeBrush(1.25);
        else if (e.Key == Key.F) FocusOnSelected();
        else if (e.Key == Key.Escape) Select(null, focus: false);
        else return;
        e.Handled = true;
    }

    void OnFocusClicked(object sender, RoutedEventArgs e) => FocusOnSelected();

    void FocusOnSelected()
    {
        if (selected == null) return;
        var size = selected.BoundsMax - selected.BoundsMin;
        // The middle of its box: logic handlers and other objects without a position of their own
        // ("oP") have only their box where they are; their position reads 0, 0, 0.
        camera.LookAt(ToView(LinkEnd(selected)), Math.Max(size.X, Math.Max(size.Y, size.Z)));
        UpdateCamera();
    }

    // ---- Objects -------------------------------------------------------------------------------

    /// <summary>What an object is called in the outliner: the model it places, or its most telling text, or its number.</summary>
    static string Label(ZoneObject obj)
    {
        foreach (string name in new[] { "chunk_name", "script_name", "name", "mission_name", "Props", "vehicle_type", "item_type", "spawn_set" })
            if (obj.Property(name) is { Type: ZoneProperty.TextType } text && text.Text.Length > 0)
                return text.Text;
        return $"#{obj.Index} (handle {obj.Handle:X8})";
    }

    /// <summary>An object of this zone named in a logic handler's script: its class and outliner label; null if it isn't here.</summary>
    string? ScriptObjectName(uint handle) =>
        zone?.Objects.FirstOrDefault(o => o.Handle == handle) is { } obj
            ? Label(obj) is var label && !label.StartsWith('#') ? $"{names.Class(obj.ClassHash)} \"{label}\"" : names.Class(obj.ClassHash)
            : null;

    ObjectKind KindOf(ZoneObject obj) => ObjectKind.Of(names.Class(obj.ClassHash), LevelScene.ModelOf(obj) != null);

    /// <summary>The Scene panel's tree of the zone's objects and the level's parts (see LevelEditor.Scene.cs).</summary>
    void BuildObjectTree(string filter) => BuildSceneTree(filter);

    void OnFilterChanged(object sender, TextChangedEventArgs e)
    {
        FilterHint.Visibility = FilterBox.Text.Length == 0 ? Visibility.Visible : Visibility.Collapsed;
        BuildObjectTree(FilterBox.Text.Trim());
    }

    void OnObjectSelected(object sender, RoutedPropertyChangedEventArgs<object> e)
    {
        if ((e.NewValue as TreeViewItem)?.Tag is ZoneObject obj) Select(obj, focus: !selectingFromView);
    }

    /// <summary>Picks an object (or nothing): shows it outlined, its handles and its properties.</summary>
    void Select(ZoneObject? obj, bool focus)
    {
        CommitEdit();
        selected = obj;
        StartEdit();
        hotAxis = null;
        ShowSelection();
        ShowProperties();
        if (focus) FocusOnSelected();
    }

    /// <summary>The inspector: the picked object's kind, name and class, its position and rotation, and its properties.</summary>
    void ShowProperties()
    {
        PropertyPanel.Children.Clear();
        Visibility shown = selected == null ? Visibility.Collapsed : Visibility.Visible;
        KindBadge.Visibility = shown;
        TransformSection.Visibility = shown;
        PropertiesSection.Visibility = shown;
        LinksSection.Visibility = Visibility.Collapsed;  // shown below if it has any
        transform = null;
        TransformHost.Content = null;
        RelatedFile? elsewhere = selected != null ? relatedOf.GetValueOrDefault(selected) : null;
        DuplicateButton.IsEnabled = DeleteButton.IsEnabled = selected != null && elsewhere == null;
        UpdatePasteButton();
        ShowInGameButton.IsEnabled = selected != null;
        ElsewhereNote.Visibility = elsewhere != null ? Visibility.Visible : Visibility.Collapsed;
        TransformHost.IsEnabled = PropertyPanel.IsEnabled = elsewhere == null;  // another file's objects are changed in its own tab
        if (selected == null)
        {
            ObjectTitle.Text = "Nothing picked";
            ObjectInfo.Text = "Click an object in the view, or pick one in the outliner.";
            return;
        }
        if (elsewhere != null)
            ElsewhereText.Text = elsewhere.IsLayer
                ? $"Added by the mission {elsewhere.Label} ({elsewhere.Name}). Open the layer to change it."
                : $"One of the zone's own objects ({elsewhere.Name}). Open the zone to change it.";
        ObjectKind kind = KindOf(selected);
        KindBadge.Background = new SolidColorBrush(Color.FromArgb(45, kind.Colour.R, kind.Colour.G, kind.Colour.B));
        KindIcon.Text = kind.Icon;
        KindIcon.Foreground = kind.Brush;
        ShowObjectTitle();
        transform = new TransformFields(selected,
            metres => Snapping ? Math.Round(metres / GridSize) * GridSize : metres,
            degrees => Snapping ? Math.Round(degrees / 15) * 15 : degrees,
            position => MoveSelected(position, fromInspector: true),
            rotation => TurnSelected(rotation, fromInspector: true),
            () => CommitEdit());
        TransformHost.Content = transform.View;
        ShowPropertyRows();
        ShowLinkRows();
    }

    /// <summary>
    /// The picked object's properties (see <see cref="ZonePropertyRows"/>). Made again after a
    /// property was added or taken out (a flag ticked, a default set), as the object's properties
    /// are read afresh then; after the click that did it has finished.
    /// </summary>
    void ShowPropertyRows()
    {
        PropertyPanel.Children.Clear();
        if (selected == null) return;
        foreach (UIElement row in propertyRows.For(selected, ShowAllPropertiesBox.IsChecked == true, OnPropertyChanged, () => CommitEdit(),
                                                   () => Dispatcher.BeginInvoke(ShowPropertyRows)))
            PropertyPanel.Children.Add(row);
    }

    void OnShowAllPropertiesChanged(object sender, RoutedEventArgs e) => ShowPropertyRows();

    void ShowObjectTitle()
    {
        if (selected == null) return;
        ObjectTitle.Text = Label(selected);
        ObjectInfo.Text = $"{names.Class(selected.ClassHash)} · handle {selected.Handle:X8}" +
                          (selected.Parent != ZoneObject.None ? $" · attached to {selected.Parent:X8}" : "") +
                          (CloePosition(selected.Placement.Position) is Vector3 cloe
                              ? $"\nCLOE position {cloe.X:0.0}, {cloe.Y:0.0}, {cloe.Z:0.0} (from the zone's middle, as the game's slew-mode panel shows)"
                              : "");
    }

    /// <summary>
    /// A position as CLOE (Volition's level editor) gave it, and as the game's slew-mode panel and
    /// its warp_to_cloe command use it: from the middle of the zone's 255 m square (world_08_02's
    /// middle is 127.5, 1402.5). Null for zones not on the open world's grid.
    /// </summary>
    Vector3? CloePosition(Vector3 position) =>
        ZoneFoliage.GridCorner(LevelGeometry.ZoneNames(zoneName).Last()) is Vector3 corner
            ? position - corner - new Vector3(127.5f, 0, 127.5f)
            : null;

    /// <summary>Whether an object is in the file being edited (not one of the zone's other files drawn with it).</summary>
    bool Editable(ZoneObject obj) => !relatedOf.ContainsKey(obj);

    /// <summary>The zone's other files, drawn with it (see <see cref="RelatedFile"/>); none until the index is ready.</summary>
    List<RelatedFile> RelatedFiles()
    {
        if (related != null) return related;
        if (!context.Assets.IndexReady) return [];
        related = RelatedFile.Find(context.Assets, zoneName, archive, container, context.Log);
        foreach (RelatedFile file in related)
            foreach (ZoneObject obj in file.Zone.Objects)
                relatedOf[obj] = file;
        return related;
    }

    /// <summary>Opens the file the picked object is in, when it is one of the zone's other files.</summary>
    void OnOpenElsewhere(object sender, RoutedEventArgs e)
    {
        if (selected == null || !relatedOf.TryGetValue(selected, out RelatedFile? file)) return;
        Packfile pack = file.Container == null ? Workspace.Archive(file.Archive) : Workspace.Container(file.Archive, file.Container);
        if (pack.Find(file.Name) is { } entry) context.OpenLevel(new GameFile(pack, entry, file.Archive, file.Container));
    }

    void OnPropertyChanged()
    {
        MarkUnsaved();
        if (selected == null) return;
        scene?.Remodel(selected);  // a squad, vehicle set, item type... changed: its model too
        scene?.Place(selected);
        ShowSelection();
        ShowObjectTitle();  // its name may be the property changed
    }

    // ---- Moving and turning --------------------------------------------------------------------

    /// <summary>
    /// Begins a change of the picked object: keeps its bytes, and those of the objects attached to
    /// it (and to those, and so on: see <see cref="ZoneFile.WithAttached"/>), which move and turn
    /// with it, as a breakable object's pieces, a building's parts or a tank on its base must.
    /// </summary>
    void StartEdit()
    {
        bool editable = selected != null && Editable(selected);
        editStart = editable ? selected!.Snapshot() : null;
        turnStart = null;
        carried = editable ? zone.WithAttached(selected!).Skip(1).Select(o => new Carried(o)).ToList() : [];
    }

    /// <param name="fromInspector">True when the inspector's numbers did it: they already show the new values.</param>
    void MoveSelected(Vector3 position, bool fromInspector)
    {
        if (selected == null) return;
        Vector3 by = position - selected.Placement.Position;
        selected.MoveTo(position);
        foreach (Carried c in carried) c.Object.MoveTo(c.Object.Placement.Position + by);
        AfterMoveOrTurn(fromInspector);
    }

    /// <summary>
    /// Turns the picked object, and what is attached to it around it. Each turn of one change (a
    /// drag, or typing an angle) starts again from the objects as they were when they began turning:
    /// turning works out an object's new bounding box from its old one, and doing that from an
    /// already turned box at every step of a drag would make the box grow larger and larger.
    /// </summary>
    void TurnSelected(Matrix4x4 rotation, bool fromInspector)
    {
        if (selected == null) return;
        if (turnStart == null) turnStart = selected.Snapshot();
        else selected.Restore(turnStart);
        foreach (Carried c in carried)
        {
            if (c.TurnStart == null) c.TurnStart = c.Object.Snapshot();
            else c.Object.Restore(c.TurnStart);
        }
        (Vector3 pivot, Matrix4x4 old) = selected.Placement;
        selected.TurnTo(rotation);
        if (carried.Count > 0 && selected.CanTurn && Matrix4x4.Invert(old, out Matrix4x4 undoOld))
        {
            Matrix4x4 change = undoOld * rotation;  // as TurnTo turns the box: a point's offset from the pivot, times this
            foreach (Carried c in carried)
            {
                (Vector3 position, Matrix4x4 turned) = c.Object.Placement;
                c.Object.MoveTo(Vector3.Transform(position - pivot, change) + pivot);
                c.Object.TurnTo(turned * change);
            }
        }
        AfterMoveOrTurn(fromInspector);
    }

    void AfterMoveOrTurn(bool fromInspector)
    {
        RefreshHelpers();
        scene?.Place(selected!);
        foreach (Carried c in carried) scene?.Place(c.Object);
        ShowSelection();
        ShowObjectTitle();  // its CLOE position
        if (!fromInspector) transform?.Refresh();
    }

    // ---- The running game ----------------------------------------------------------------------

    /// <summary>
    /// Shows the picked object in the running game (see <see cref="GameLink"/>): the game's camera
    /// goes to it in slew mode, keeping the way it looks, far enough back to see it whole, and the
    /// game picks it (its debug display then names it, as Q does).
    /// </summary>
    void OnShowInGame(object sender, RoutedEventArgs e)
    {
        if (selected == null) return;
        GameView? game = gameLink.Read();
        if (!GameLink.IsRecent(game))
        {
            context.Log("Show in game: the game isn't running with the console mod. Start it with RFAModLauncher and go to this level, then try again.");
            return;
        }
        Vector3 size = selected.BoundsMax - selected.BoundsMin;
        // Some objects (regions, markers) have boxes far bigger than what can be seen: their position then.
        Vector3 middle = size.Length() < 200 ? (selected.BoundsMin + selected.BoundsMax) / 2 : selected.Placement.Position;
        float distance = Math.Clamp(Math.Min(size.Length(), 200) * 1.2f, 6, 80);
        gameLink.Send(GameLink.ShowCommands(middle, distance, game!.Forward, selected.Handle));
        context.Log($"Show in game: {Described(selected)}. The game's camera goes {distance:0} m back from it, in slew mode " +
                    "(the console's slew_mode command switches back to the player), and picks it.");
    }

    // ---- Props as in the game ------------------------------------------------------------------

    readonly System.Windows.Threading.DispatcherTimer settledTimer = new() { Interval = TimeSpan.FromSeconds(0.5) };
    readonly Dictionary<ZoneObject, (Vector3 Position, Matrix4x4 Rotation)> asInGame = [];  // drawn where the game has them
    LevelScene? asInGameScene;  // the scene they were moved in (a rebuilt scene starts over)
    DateTime askedWhere;
    bool toldWhere;

    /// <summary>
    /// Props as in the game: while on, the zone's clutter (general_mover: debris, barrels, crates,
    /// wreck pieces, which physics moves in the game) is drawn where the running game has it, asked
    /// for twice a second (see <see cref="GameLink.AskWhere"/>). Only the drawing changes; the
    /// picked object's box and the gizmo stay where the zone puts it.
    /// </summary>
    void OnSettledChanged(object sender, RoutedEventArgs e)
    {
        settledTimer.Tick -= SettledTick;
        settledTimer.Stop();
        if (SettledBox.IsChecked != true)
        {
            PutPropsBack();
            return;
        }
        toldWhere = false;
        askedWhere = DateTime.MinValue;
        settledTimer.Tick += SettledTick;
        settledTimer.Start();
        if (!GameLink.IsRecent(gameLink.Read()))
            context.Log("Props as in the game: once it runs (start it with RFAModLauncher and go to this level), clutter is drawn where the game has it.");
    }

    void PutPropsBack()
    {
        if (asInGameScene == scene)
            foreach (ZoneObject obj in asInGame.Keys) scene?.Place(obj);
        asInGame.Clear();
    }

    void SettledTick(object? sender, EventArgs e)
    {
        if (scene == null || !GameLink.IsRecent(gameLink.Read())) return;
        if (asInGameScene != scene)
        {
            asInGame.Clear();
            asInGameScene = scene;
        }
        uint moverClass = NameHashes.ClassHash("general_mover");
        var props = zone.Objects.Where(o => o.ClassHash == moverClass).ToList();
        // An answer that leaves out some of them was to an older question (one left from before
        // objects were added, say): it is dropped, and asked again.
        var answer = gameLink.ReadWhere();
        if (answer != null && props.All(o => answer.ContainsKey(o.Handle))) ShowWhere(answer, props);
        else if (answer == null && DateTime.Now - askedWhere < TimeSpan.FromSeconds(2)) return;  // still waiting for the answer
        gameLink.AskWhere(props.Select(o => o.Handle));
        askedWhere = DateTime.Now;
    }

    /// <summary>Draws each prop where the game has it, if that differs from the zone (by 2 cm, or a little turn).</summary>
    void ShowWhere(Dictionary<uint, (Vector3 Position, Matrix4x4 Rotation)?> answer, List<ZoneObject> props)
    {
        int found = 0, gone = 0;
        float most = 0;
        ZoneObject? movedMost = null;
        static bool Same((Vector3 Position, Matrix4x4 Rotation) a, (Vector3 Position, Matrix4x4 Rotation) b) =>
            Vector3.Distance(a.Position, b.Position) < 0.02f &&
            new[] { a.Rotation.M11 - b.Rotation.M11, a.Rotation.M12 - b.Rotation.M12, a.Rotation.M13 - b.Rotation.M13,
                    a.Rotation.M21 - b.Rotation.M21, a.Rotation.M22 - b.Rotation.M22, a.Rotation.M23 - b.Rotation.M23,
                    a.Rotation.M31 - b.Rotation.M31, a.Rotation.M32 - b.Rotation.M32, a.Rotation.M33 - b.Rotation.M33 }.All(d => MathF.Abs(d) < 0.01f);
        foreach (ZoneObject obj in props)
        {
            if (!answer.TryGetValue(obj.Handle, out var now)) continue;
            if (now is not { } inGame)
            {
                gone++;
                if (asInGame.Remove(obj)) scene!.Place(obj);
                continue;
            }
            found++;
            (Vector3 Position, Matrix4x4 Rotation) saved = obj.Placement;
            if (Same(inGame, saved))
            {
                if (asInGame.Remove(obj)) scene!.Place(obj);
                continue;
            }
            float distance = Vector3.Distance(inGame.Position, saved.Position);
            if (distance > most) (most, movedMost) = (distance, obj);
            if (asInGame.TryGetValue(obj, out var shown) && Same(shown, inGame)) continue;  // where it was last time
            asInGame[obj] = inGame;
            scene!.PlaceAt(obj, inGame.Position, inGame.Rotation);
        }
        if (toldWhere) return;
        toldWhere = true;
        context.Log($"Props as in the game: of the zone's {props.Count} clutter props, the game has {found} now" +
                    (gone > 0 ? $" ({gone} not loaded or broken up)" : "") + $"; {asInGame.Count} of them are elsewhere than the zone puts them" +
                    (movedMost != null ? $", the furthest {most:0.0} m ({Described(movedMost)})" : "") + ", and are drawn where the game has them.");
    }

    /// <summary>
    /// While following the game: the game's camera as it last showed it (the view eases toward it),
    /// the last game frame seen, the object last picked there, and when this view last moved.
    /// </summary>
    sealed class Follow
    {
        public Point3D? Position;
        public double Yaw, Pitch;
        public uint Frame;
        public uint? Picked;
        public DateTime LastMoved = DateTime.Now;
    }

    /// <summary>How quickly the view catches up with the game's camera: most of the way in this many seconds.</summary>
    const double FollowSeconds = 0.05;

    void OnFollowGameChanged(object sender, RoutedEventArgs e)
    {
        CompositionTarget.Rendering -= FollowFrame;
        follow = null;
        if (FollowGameBox.IsChecked != true)
        {
            gameFov = null;  // back to the editor's own
            UpdateCamera();
            return;
        }
        follow = new Follow();
        CompositionTarget.Rendering += FollowFrame;
        context.Log(GameLink.IsRecent(gameLink.Read())
            ? "Following the game: the view moves with its camera, with its field of view, and what you pick there (Q in slew mode) is picked here."
            : "Following the game once it runs: start it with RFAModLauncher (the console mod links it to the editor).");
    }

    /// <summary>
    /// Every time this view is drawn while following the game: the game's camera, field of view
    /// and picked object, as the game showed them in its last frame (see <see cref="GameLink"/>).
    /// The view eases toward the game's camera rather than jumping to it, so the game's frames
    /// (usually more per second than this view's, and not in step with them) make steady movement;
    /// a jump of more than 30 m (the camera teleported) is taken at once.
    /// </summary>
    void FollowFrame(object? sender, EventArgs e)
    {
        if (follow == null || gameLink.Read() is not { } game || !GameLink.IsRecent(game)) return;
        DateTime now = DateTime.Now;
        double seconds = Math.Min(0.25, (now - follow.LastMoved).TotalSeconds);
        follow.LastMoved = now;
        if (game.Frame != follow.Frame)
        {
            follow.Frame = game.Frame;
            var look = new Vector3D(game.Forward.X, game.Forward.Y, -game.Forward.Z);  // WPF's Z is the game's -Z
            follow.Yaw = Math.Atan2(look.X, look.Z);
            follow.Pitch = Math.Clamp(Math.Asin(Math.Clamp(look.Y, -1, 1)), -1.55, 1.55);
            bool first = follow.Position == null;
            follow.Position = ToView(game.Camera);
            if (first || (follow.Position.Value - camera.Position).Length > 30)
            {
                camera.Position = follow.Position.Value;
                camera.Yaw = follow.Yaw;
                camera.Pitch = follow.Pitch;
            }
            if (game.VerticalFov is float fov && fov != gameFov) gameFov = fov;
            if (game.Picked != follow.Picked)
            {
                follow.Picked = game.Picked;
                if (game.Picked is uint handle) PickedInGame(handle);
            }
        }
        if (follow.Position is not Point3D target) return;
        Vector3D gap = target - camera.Position;
        double yawGap = Math.IEEERemainder(follow.Yaw - camera.Yaw, 2 * Math.PI), pitchGap = follow.Pitch - camera.Pitch;
        bool fovChanged = Math.Abs(camera.FieldOfView - (gameFov is double vertical ? FlyCamera.SideToSide(vertical, ViewArea.RenderSize) : camera.FieldOfView)) > 0.01;
        if (gap.Length < 0.001 && Math.Abs(yawGap) < 1e-5 && Math.Abs(pitchGap) < 1e-5 && !fovChanged) return;  // caught up
        double blend = 1 - Math.Exp(-seconds / FollowSeconds);
        camera.Position += gap * blend;
        camera.Yaw += yawGap * blend;
        camera.Pitch += pitchGap * blend;
        UpdateCamera();
    }

    /// <summary>The object picked in the game, picked here too if it is in this zone or its other files.</summary>
    void PickedInGame(uint handle)
    {
        ZoneObject? obj = zone.Objects.FirstOrDefault(o => o.Handle == handle) ??
                          RelatedFiles().SelectMany(f => f.Zone.Objects).FirstOrDefault(o => o.Handle == handle);
        if (obj != null)
        {
            PickInOutliner(obj);
            context.Log($"Picked in the game: {Described(obj)}.");
        }
        else
        {
            context.Log($"Picked in the game: handle {handle:X8}, which isn't in {zoneName} or its mission layers.");
        }
    }

    /// <summary>When the tab closes: stop listening.</summary>
    void Closing()
    {
        context.Assets.IndexBecameReady -= OnIndexReady;
        CompositionTarget.Rendering -= FollowFrame;
        CompositionTarget.Rendering -= DrawGpuView;
        CompositionTarget.Rendering -= PadFrame;
        GamePanel.ControllerChanged -= ShowControllerOwner;
        CompositionTarget.Rendering -= SculptFrame;
        gpuScene?.Dispose();
        gpuView?.Dispose();
        settledTimer.Stop();
        liveTimer.Stop();
        gameLink.Dispose();
    }

    // ---- Adding and deleting objects -----------------------------------------------------------

    (System.Numerics.Vector2 Corner, float Width)? zoneSquare;
    bool zoneSquareKnown;
    Dictionary<uint, Vector3>? placedAtOpen;  // where each object is in the game's own copy (see NotePlacements)

    /// <summary>Whether an object was added or moved since the game's own copy (or the world as built).</summary>
    bool AddedOrMoved(ZoneObject obj)
    {
        placedAtOpen ??= [];
        return !placedAtOpen.TryGetValue(obj.Handle, out Vector3 was) || Vector3.DistanceSquared(was, obj.Placement.Position) > 1e-4f;
    }

    /// <summary>Notes where the objects of the game's own copy of the zone (or the Guerrilla world's as built) are: objects added or moved since are checked on saving.</summary>
    void NotePlacements()
    {
        ZoneFile own = zone;
        try { own = ZoneFile.Read(Original(archive, container)); }
        catch (Exception ex) when (ex is InvalidDataException or FileNotFoundException or NotSupportedException) { }
        placedAtOpen = own.Objects.GroupBy(o => o.Handle).ToDictionary(g => g.Key, g => g.First().Placement.Position);
    }

    /// <summary>
    /// The square of the map the zone covers (its lowest x and z, and its width), or null if it isn't
    /// known (a cave zone without ground, before the index is ready). The Guerrilla world's zones are
    /// on its 511 m grid by name (rfg_CC_RR); the game's by their ground's sub-zones.
    /// </summary>
    (System.Numerics.Vector2 Corner, float Width)? ZoneSquare()
    {
        if (zoneSquareKnown) return zoneSquare;
        string name = LevelGeometry.ZoneNames(zoneName).Last();
        if (name.StartsWith("p_", StringComparison.OrdinalIgnoreCase)) name = name[2..];
        var grid = System.Text.RegularExpressions.Regex.Match(name, @"^rfg_(\d+)_(\d+)$", System.Text.RegularExpressions.RegexOptions.IgnoreCase);
        if (grid.Success)
        {
            int col = int.Parse(grid.Groups[1].Value), row = int.Parse(grid.Groups[2].Value);
            zoneSquare = (new System.Numerics.Vector2((col - 8) * GuerrillaZone, (7 - row) * GuerrillaZone), GuerrillaZone);
        }
        else
        {
            if (!context.Assets.IndexReady) return null;
            List<TerrainSubzone> subzones = ReadSubzones(name);
            if (subzones.Count > 0) zoneSquare = (LevelGeometry.ZoneCorner(subzones), subzones[0].ZoneWidth);
        }
        zoneSquareKnown = true;
        return zoneSquare;
    }

    const float GuerrillaZone = 511f;  // the Guerrilla world's zones (RfaFormats.Guerrilla.GuerrillaWorld.ZoneWidth)

    bool InZone(Vector3 p) => ZoneSquare() is not { } square ||
        (p.X >= square.Corner.X && p.X < square.Corner.X + square.Width && p.Z >= square.Corner.Y && p.Z < square.Corner.Y + square.Width);

    /// <summary>Which zone a place outside this one is in, in words (the Guerrilla world's by name).</summary>
    string ZoneAt(Vector3 p)
    {
        if (ZoneSquare() is { } square && System.Math.Abs(square.Width - GuerrillaZone) < 0.5f && zoneName.Contains("rfg_", StringComparison.OrdinalIgnoreCase))
        {
            int col = (int)MathF.Floor(p.X / GuerrillaZone) + 8, row = 7 - (int)MathF.Floor(p.Z / GuerrillaZone);
            return $"rfg_{col:00}_{row:00}";
        }
        return "another zone";
    }

    /// <summary>
    /// Whether a new object may go at a place: inside this zone's square. The game drops a mover
    /// outside its zone when the level loads ("Mover is physically located outside its zone", then
    /// "Failed to create rfg_mover from stub"), with everything attached to it; so does the view
    /// pointing at the zones around. Says why if not.
    /// </summary>
    bool CanPlaceAt(Vector3 p, string what)
    {
        if (InZone(p)) return true;
        var square = ZoneSquare()!.Value;
        MessageBox.Show($"{what} would be at {p.X:0.#}, {p.Y:0.#}, {p.Z:0.#}, which is in {ZoneAt(p)}, not in {System.IO.Path.GetFileNameWithoutExtension(zoneName)} " +
                        $"(x {square.Corner.X:0} to {square.Corner.X + square.Width:0}, z {square.Corner.Y:0} to {square.Corner.Y + square.Width:0}). " +
                        "The game drops objects placed outside their zone.\n\nPoint the middle of the view at this zone's own ground, or open the other zone to put it there.",
                        "Level editor", MessageBoxButton.OK, MessageBoxImage.Information);
        return false;
    }

    void OnDuplicateClicked(object sender, RoutedEventArgs e) => DuplicateSelected();
    void OnDeleteClicked(object sender, RoutedEventArgs e) => DeleteSelected();

    /// <summary>Adds a copy of the picked object (with what makes one breakable object with it) beside it, and picks the copy.</summary>
    void DuplicateSelected()
    {
        if (selected != null && Editable(selected)) AddCopy(selected, at: null, "Duplicate");
    }

    /// <summary>
    /// Adds a copy of an object with the objects that make one breakable object with it (see
    /// <see cref="ZoneFile.PiecesWith"/> and <see cref="ZoneFile.AddCopies"/>: a loose piece comes
    /// with its fixed part, everything comes with what is attached to it, and the destruction links
    /// between them are copied too) at the end of the zone's list, and picks the copy of the object
    /// asked for. It goes at a place (<paramref name="at"/>, for the copy of the topmost), or else
    /// beside the original, to the camera's right: never inside it, as physics objects in one
    /// another fly apart, and explosive ones blow up. New handles must not be any the zone's other
    /// files use, so this waits for the editor to have read every container.
    /// </summary>
    void AddCopy(ZoneObject source, Vector3? at, string what)
    {
        if (!context.Assets.IndexReady)
        {
            context.Log("The editor is still reading the game's files (to find handles the zone's other files use). Try again in a few seconds.");
            return;
        }
        CommitEdit();
        IReadOnlyList<ZoneObject> group = zone.PiecesWith(source);
        Vector3 by = at is Vector3 position ? position - group[0].Placement.Position : Beside(group);
        if (!CanPlaceAt(group[0].Placement.Position + by, $"A copy of {Label(source)}")) return;
        IReadOnlyList<ZoneObject> copies;
        try
        {
            copies = zone.AddCopies(group, TakenHandles(), by);
        }
        catch (InvalidOperationException ex)
        {
            MessageBox.Show($"Couldn't add a copy of {Label(source)}: {ex.Message}.", "Level editor", MessageBoxButton.OK, MessageBoxImage.Information);
            return;
        }
        foreach (ZoneObject copy in copies) scene?.Add(copy);
        RemovedObjects? removed = null;
        Record(new Change(copies[0], what,
            Undo: () => { removed = zone.Remove(copies.ToList()); foreach (ZoneObject copy in copies) scene?.Remove(copy); },
            Redo: () => { if (removed != null) zone.PutBack(removed); foreach (ZoneObject copy in copies) scene?.Add(copy); },
            AddsOrRemoves: true));
        ObjectsChanged();
        ZoneObject picked = copies[Math.Max(0, group.ToList().IndexOf(source))];
        PickInOutliner(picked);
        int copiedLinks = copies.Sum(zone.DestructionLinksOf) / 2;  // each joins two of the copies, so is counted twice
        var others = copies.Where(c => c != picked).ToList();
        string pieces = others.Count > 0
            ? $" with the {others.Count} object{(others.Count > 1 ? "s" : "")} joined to it ({string.Join(", ", others.GroupBy(c => names.Class(c.ClassHash)).Select(g => $"{g.Count()} {g.Key}"))})" +
              (copiedLinks > 0 ? $" and the {copiedLinks} destruction links between them" : "")
            : "";
        Vector3 where = copies[0].Placement.Position;
        context.Log($"{what}: added {Described(picked)}{pieces}, " + (at == null ? "beside the original" : "where the view points") + $", at {where.X:0.#}, {where.Y:0.#}, {where.Z:0.#}.");
    }

    /// <summary>
    /// How far to move a copy so it stands beside the original: to the camera's right (level), by the
    /// width of all the objects' boxes that way and half a metre.
    /// </summary>
    Vector3 Beside(IReadOnlyList<ZoneObject> group)
    {
        Vector3 min = group.Select(o => o.BoundsMin).Aggregate(Vector3.Min), max = group.Select(o => o.BoundsMax).Aggregate(Vector3.Max);
        var right = new Vector3((float)camera.Right.X, 0, (float)-camera.Right.Z);  // the game's Z is the view's -Z
        right = right.LengthSquared() > 1e-6f ? Vector3.Normalize(right) : Vector3.UnitX;
        Vector3 size = max - min;
        float across = MathF.Abs(right.X) * size.X + MathF.Abs(right.Z) * size.Z;
        return right * (across + 0.5f);
    }

    /// <summary>
    /// Deletes the picked object with the objects attached to it (a building's lights, say), and
    /// their destruction links (see <see cref="ZoneFile.Remove(IReadOnlyCollection{ZoneObject})"/>).
    /// Refused, saying what, if anything else names one of them (another object's links or
    /// handles, or an object in the zone's other files): those would then point at nothing.
    /// </summary>
    void DeleteSelected()
    {
        if (selected == null || !Editable(selected)) return;
        CommitEdit();
        ZoneObject obj = selected;
        IReadOnlyList<ZoneObject> going = zone.WithAttached(obj);
        List<string> namedBy = WhatNames(going);
        if (namedBy.Count > 0)
        {
            MessageBox.Show($"{Label(obj)} can't be deleted: other things name it{(going.Count > 1 ? " or what is attached to it" : "")}, " +
                            "and would be left pointing at nothing:\n\n" +
                            string.Join("\n", namedBy.Take(8).Select(n => "•  " + n)) + (namedBy.Count > 8 ? $"\n…and {namedBy.Count - 8} more" : ""),
                            "Level editor", MessageBoxButton.OK, MessageBoxImage.Information);
            return;
        }
        int links = going.Sum(zone.DestructionLinksOf);
        RemovedObjects removed = zone.Remove(going);
        foreach (ZoneObject gone in going) scene?.Remove(gone);
        Record(new Change(obj, "Delete",
            Undo: () => { zone.PutBack(removed); foreach (ZoneObject gone in going) scene?.Add(gone); },
            Redo: () => { removed = zone.Remove(going); foreach (ZoneObject gone in going) scene?.Remove(gone); }, AddsOrRemoves: true));
        Select(null, focus: false);
        ObjectsChanged();
        var said = new List<string>();
        if (going.Count > 1)
            said.Add($"{going.Count - 1} object(s) attached to it ({string.Join(", ", going.Skip(1).GroupBy(o => names.Class(o.ClassHash)).Select(g => $"{g.Count()} {g.Key}"))})");
        if (links > 0) said.Add($"its {links} destruction link(s) to the objects next to it");
        context.Log($"Deleted {Described(obj)}" + (said.Count > 0 ? ", with " + string.Join(" and ", said) : "") + ".");
    }

    /// <summary>An object for the log: its name, class and handle.</summary>
    string Described(ZoneObject obj) => Label(obj).StartsWith('#')
        ? $"{names.Class(obj.ClassHash)} {Label(obj)}"
        : $"{Label(obj)} ({names.Class(obj.ClassHash)}, handle {obj.Handle:X8})";

    /// <summary>What names any of these objects, other than one another, in words: in this file and in the zone's other files. Empty if nothing does.</summary>
    List<string> WhatNames(IReadOnlyCollection<ZoneObject> going)
    {
        string Describe(ZoneReference reference, string? inFile) =>
            $"{(reference.From != null ? Label(reference.From) + " (" + names.Class(reference.From.ClassHash) : "(an object")}{(inFile != null ? ", in " + inFile : "")})" +
            (reference.PropertyHash is uint property ? $": its \"{names.Property(property)}\"" : ": it is attached to this one");
        return going.SelectMany(obj => zone.ReferencesTo(obj, going).Select(r => Describe(r, null))
                        .Concat(RelatedFiles().SelectMany(file => ZoneFile.ReferencesIn(file.Zone, obj.Handle).Select(r => Describe(r, file.Label)))))
                    .Distinct().ToList();
    }

    List<uint>? persistentHandles;

    /// <summary>
    /// The handles the zone's other files use (its mission layers, or for a layer its zone, and
    /// the persistent zone p_world_08_02, which holds copies of some of the zone's objects), so a
    /// new object doesn't take one of them.
    /// </summary>
    IEnumerable<uint> TakenHandles()
    {
        if (persistentHandles == null)
        {
            persistentHandles = [];
            string name = "p_" + LevelGeometry.ZoneNames(zoneName).Last() + ".rfgzone_pc";
            if (context.Assets.ReadFile(name, archive, container) is { } file)
            {
                try { persistentHandles = ZoneFile.Read(file.Data).Objects.Select(o => o.Handle).ToList(); }
                catch (InvalidDataException) { }
            }
        }
        return RelatedFiles().SelectMany(f => f.Zone.Objects).Select(o => o.Handle).Concat(persistentHandles);
    }

    /// <summary>After objects were added or deleted: the outliner and the chips' counts again.</summary>
    void ObjectsChanged()
    {
        RefreshHelpers();
        links = null;  // before the chips: they count the links
        BuildObjectTree(FilterBox.Text.Trim());
        BuildKindChips();
        LinksChanged();
    }

    /// <summary>
    /// The Add list: one of each kind of object the file has (by class and name), to add a copy of;
    /// then every class in the game's zone schema that can be made from it, to add a new one of.
    /// </summary>
    void OnAddClicked(object sender, RoutedEventArgs e)
    {
        if (context.Assets.IndexReady) _ = context.Assets.ChunkPlacements();  // (looked for in the background, for adding a chunk)
        AddFilterBox.Text = "";
        FillAddList("");
        AddPopup.IsOpen = true;
        AddFilterBox.Focus();
    }

    void OnAddFilterChanged(object sender, TextChangedEventArgs e)
    {
        AddFilterHint.Visibility = AddFilterBox.Text.Length == 0 ? Visibility.Visible : Visibility.Collapsed;
        FillAddList(AddFilterBox.Text.Trim());
    }

    void FillAddList(string filter)
    {
        AddList.Items.Clear();
        ShowAddPath();
        if (chunkPath.Count > 0)
        {
            // In the chunk browser: only chunks.
            FillChunkBrowser(filter);
            return;
        }
        var kinds = zone.Objects
            .GroupBy(o => (Class: names.Class(o.ClassHash), Label: Label(o).StartsWith('#') ? "" : Label(o)))
            .Select(g => (g.Key.Class, g.Key.Label, Example: g.First(), Count: g.Count(), Kind: KindOf(g.First())))
            .Where(k => filter.Length == 0 || k.Class.Contains(filter, StringComparison.OrdinalIgnoreCase) ||
                        k.Label.Contains(filter, StringComparison.OrdinalIgnoreCase))
            .OrderBy(k => ObjectKind.All.IndexOf(k.Kind)).ThenBy(k => k.Label.Length == 0 ? k.Class : k.Label, StringComparer.OrdinalIgnoreCase)
            .ToList();
        if (kinds.Count > 0) AddList.Items.Add(AddListHeading("COPY ONE OF THIS FILE'S"));
        foreach (var (className, label, example, count, kind) in kinds)
            AddList.Items.Add(AddListRow(kind, label.Length > 0 ? label : className, label.Length > 0 ? className : null, count.ToString("N0"),
                                         $"{className}: {count:N0} in this file. A copy of one is added where the middle of the view points.", example));

        // An invisible wall (see LevelEditor.Barriers.cs).
        if (barrierFile != null && (filter.Length == 0 || "invisible wall barrier collision".Contains(filter, StringComparison.OrdinalIgnoreCase)))
        {
            AddList.Items.Add(AddListHeading("INVISIBLE WALL"));
            AddList.Items.Add(AddListRow(ObjectKind.Regions, "Invisible wall", "invisible_barrier", "",
                                         "A wall the player can't go through and nobody sees: a box 10 m wide, 10 m high and 1 m thick, standing where the " +
                                         "middle of the view points (its bb resizes it). It goes into the zone's terrain file on saving.", new BarrierChoice()));
        }

        // Any chunk of the game's (or the Guerrilla world's): buildings and props from every level, to browse (see LevelEditor.ChunkBrowser.cs).
        FillChunkBrowser(filter);

        if (context.Assets.ZoneSchema is not { } schema) return;
        bool Matches(SchemaClass type) => filter.Length == 0 || type.Name.Contains(filter, StringComparison.OrdinalIgnoreCase) ||
                                          type.Description?.Contains(filter, StringComparison.OrdinalIgnoreCase) == true;
        var classes = schema.Classes.Where(c => c.CantMake == null && Matches(c))
            .Select(c => (Class: c, Kind: ObjectKind.Of(c.ZoneClassNames[0], false), Title: c.Description is { Length: > 0 } title ? title : c.Name))
            .OrderBy(c => ObjectKind.All.IndexOf(c.Kind)).ThenBy(c => c.Title, StringComparer.OrdinalIgnoreCase)
            .ToList();
        if (classes.Count > 0) AddList.Items.Add(AddListHeading("NEW, FROM THE GAME'S SCHEMA"));
        foreach (var (type, kind, title) in classes)
            AddList.Items.Add(AddListRow(kind, title, title != type.Name ? type.Name : null, type.InTheGame ? "" : "unused",
                                         $"A new {type.Name}{(type.Description != null ? $" ({type.Description})" : "")}, with every property the schema's " +
                                         "default, added where the middle of the view points." +
                                         (type.InTheGame ? "" : "\nNone of the game's levels have one, so the game may do nothing with it."), type));
        var cant = schema.Classes.Where(c => c.CantMake != null && Matches(c)).ToList();
        if (cant.Count > 0)
        {
            var note = new ListBoxItem
            {
                IsEnabled = false, Focusable = false,
                Content = new TextBlock { Text = $"{cant.Count} more can't be made from the schema alone (hover to see why)", FontSize = 11, Foreground = Theme.Brush("DimTextBrush") },
                ToolTip = string.Join("\n", cant.Select(c => $"{c.Name}: {c.CantMake}")),
            };
            ToolTipService.SetShowOnDisabled(note, true);
            AddList.Items.Add(note);
        }
        AddExampleRows(filter);
    }

    static ListBoxItem AddListHeading(string text) => new()
    {
        IsEnabled = false, Focusable = false,
        Content = new TextBlock { Text = text, Style = Theme.Style("SectionTitle"), Margin = new Thickness(0, 4, 0, 2) },
    };

    /// <summary>A row of the Add list: the kind's icon, a title and a dim second name, a note on the right; its tag is what to add.</summary>
    static ListBoxItem AddListRow(ObjectKind kind, string title, string? dim, string note, string tip, object tag)
    {
        var row = new DockPanel { ToolTip = tip };
        TextBlock icon = Theme.Icon(kind.Icon, 12, kind.Brush);
        icon.Width = 18;
        DockPanel.SetDock(icon, Dock.Left);
        row.Children.Add(icon);
        var noted = new TextBlock { Text = note, Foreground = Theme.Brush("DimTextBrush"), FontSize = 11, Margin = new Thickness(8, 0, 0, 0) };
        DockPanel.SetDock(noted, Dock.Right);
        row.Children.Add(noted);
        var text = new TextBlock { TextTrimming = TextTrimming.CharacterEllipsis };
        text.Inlines.Add(new System.Windows.Documents.Run(title));
        if (dim != null) text.Inlines.Add(new System.Windows.Documents.Run("  " + dim) { Foreground = Theme.Brush("DimTextBrush"), FontSize = 11 });
        row.Children.Add(text);
        return new ListBoxItem { Content = row, Tag = tag };
    }

    void OnAddListClicked(object sender, MouseButtonEventArgs e)
    {
        if ((e.OriginalSource as DependencyObject) is { } hit && ItemsControl.ContainerFromElement(AddList, hit) is ListBoxItem { Tag: { } chosen })
            AddFromList(chosen);
    }

    void OnAddListKey(object sender, KeyEventArgs e)
    {
        if (e.Key == Key.Enter && AddList.SelectedItem is ListBoxItem { Tag: { } chosen }) AddFromList(chosen);
        else if (e.Key == Key.Back && (sender != AddFilterBox || AddFilterBox.Text.Length == 0) && chunkPath.Count > 0) ChunkFolderUp();
        else if (e.Key == Key.Down && sender == AddFilterBox && AddList.Items.Count > 0)
        {
            int first = AddList.Items.Cast<ListBoxItem>().ToList().FindIndex(item => item.IsEnabled);
            if (first < 0) return;
            AddList.SelectedIndex = first;
            (AddList.ItemContainerGenerator.ContainerFromIndex(first) as ListBoxItem)?.Focus();
        }
        else return;
        e.Handled = true;
    }

    /// <summary>Adds what was picked in the Add list: a copy of one of the file's objects, or a new object of a schema class.</summary>
    void AddFromList(object chosen)
    {
        if (chosen is ChunkFolder folder)
        {
            OpenChunkFolder(folder);  // a group of chunks: into it
            return;
        }
        AddPopup.IsOpen = false;
        Vector3 at = PointAhead();
        if (chosen is ZoneObject example) AddCopy(example, at, "Add");
        else if (chosen is SchemaClass type) { if (CanPlaceAt(at, $"The new {type.Name}")) AddNew(type, at); }
        else if (chosen is ChunkChoice chunk) { if (CanPlaceAt(at, chunk.Chunk)) AddChunk(chunk.Chunk, at); }
        else if (chosen is GeometryChoice piece) AddGeometry(piece.Piece, at);
        else if (chosen is BarrierChoice) AddBarrier(at);
        else if (chosen is ObjectExample gameExample) AddExample(gameExample, at);
        ViewArea.Focus();
    }

    /// <summary>
    /// Adds a new object of a class from the game's zone schema at a place, with every property the
    /// schema's default, as Volition's tools would have written it (see
    /// <see cref="SchemaClass.NewObject"/> and <see cref="ZoneFile.AddNew"/>), and picks it. As for
    /// copies, its handle must not be one the zone's other files use, so this waits for the index.
    /// </summary>
    void AddNew(SchemaClass type, Vector3 at)
    {
        if (!context.Assets.IndexReady)
        {
            context.Log("The editor is still reading the game's files (to find handles the zone's other files use). Try again in a few seconds.");
            return;
        }
        CommitEdit();
        ZoneObject obj;
        try
        {
            (Vector3 min, Vector3 max) = type.NewObjectBounds(at);
            obj = zone.AddNew(NameHashes.ClassHash(type.ZoneClassNames[0]), type.NewObject(at, context.Assets.Choices), min, max, TakenHandles());
        }
        catch (InvalidOperationException ex)
        {
            MessageBox.Show($"Couldn't add a new {type.Name}: {ex.Message}.", "Level editor", MessageBoxButton.OK, MessageBoxImage.Information);
            return;
        }
        scene?.Add(obj);
        RemovedObjects? removed = null;
        Record(new Change(obj, "Add",
            Undo: () => { removed = zone.Remove(obj); scene?.Remove(obj); },
            Redo: () => { if (removed != null) zone.PutBack(removed); scene?.Add(obj); },
            AddsOrRemoves: true));
        ObjectsChanged();
        PickInOutliner(obj);
        context.Log($"Add: a new {type.Name} (handle {obj.Handle:X8}) with the schema's defaults, where the view points, at {at.X:0.#}, {at.Y:0.#}, {at.Z:0.#}." +
                    (type.InTheGame ? "" : " None of the game's levels have one, so the game may do nothing with it."));
    }

    /// <summary>The container (in an archive) holding this zone, where chunks brought in go; null if the zone is in none.</summary>
    (string Archive, string Container)? ChunkContainer() => ChunkContainers() is { Count: > 0 } all ? all[0] : null;

    List<(string Archive, string Container)>? chunkContainers;

    /// <summary>
    /// Every container a chunk brought into this zone goes into, so each level that loads the zone
    /// has it: the container of each copy of the zone, and for a copy loose in its archive (as in
    /// some of the game's levels) the archive's own container with the most chunks in it (the
    /// level's), else its first container.
    /// </summary>
    List<(string Archive, string Container)> ChunkContainers()
    {
        if (chunkContainers != null) return chunkContainers;
        var found = new List<(string Archive, string Container)>();
        var places = context.Assets.IndexReady ? Copies() : [(archive, container)];
        foreach (var (a, c) in places)
            if (c != null && !found.Contains((a, c))) found.Add((a, c));
        // Only loose copies (a level that keeps its zone in its archive): the archive's level container.
        if (found.Count == 0)
            foreach (var (a, _) in places)
                if (LevelContainer(a) is string level && !found.Contains((a, level))) found.Add((a, level));
        return chunkContainers = found;
    }

    /// <summary>An archive's container with the most chunks (.cchk_pc) in it, else its first container; null if it has none.</summary>
    string? LevelContainer(string inArchive)
    {
        var containers = Workspace.Archive(inArchive).Entries.Where(e => e.Extension == "str2_pc").Select(e => e.Name).ToList();
        if (containers.Count == 0) return null;
        string? best = null;
        int most = -1;
        foreach (string name in containers)
        {
            try
            {
                int chunks = Workspace.Container(inArchive, name).Entries.Count(e => e.Extension == "cchk_pc");
                if (chunks > most) { most = chunks; best = name; }
            }
            catch (InvalidDataException) { }
        }
        return best ?? containers[0];
    }

    /// <summary>Whether a container, or its world, has a chunk already (so it needn't be brought in there).</summary>
    bool HasChunk(string chunk, (string Archive, string Container) at) =>
        context.Assets.WorldHasChunk(chunk, at.Archive) || Workspace.Container(at.Archive, at.Container).Find(chunk + ".cchk_pc") != null;

    /// <summary>
    /// The objects of a chunk added where given (see <see cref="AddChunk"/>): a copy of where the
    /// game's levels place it, else a new mover; its files noted to bring in on saving (said in
    /// <paramref name="needs"/>). Not yet in the scene or the undo list. Throws InvalidOperationException
    /// if it can't be. (The chunk painter adds many with it.)
    /// </summary>
    readonly Dictionary<string, ZoneFile> chunkSourceZones = new(StringComparer.OrdinalIgnoreCase);  // zones copied from, read once (the chunk painter adds many)

    IReadOnlyList<ZoneObject> MakeChunkObjects(string chunk, Vector3 at, string targetContainer, List<string> needs, out string how)
    {
        IReadOnlyList<ZoneObject> added;
        // Where the game's levels place it, if they do: copy that object (and its pieces).
        ZoneFile? from = null;
        ZoneObject? template = null;
        ChunkPlacement? place = null;
        var placements = context.Assets.ChunkPlacements();
        if (!placements.IsCompleted)
        {
            context.Log($"Looking through the game's levels for where they place {chunk} (once)...");
            placements.Wait(TimeSpan.FromSeconds(60));
        }
        if (placements.IsCompletedSuccessfully && placements.Result.TryGetValue(chunk, out ChunkPlacement? found))
        {
            place = found;
            try
            {
                Packfile pack = place.Container == null ? Workspace.Archive(place.Archive) : Workspace.Container(place.Archive, place.Container);
                string sourceKey = $"{place.Archive}|{place.Container}|{place.Zone}";
                if (chunkSourceZones.TryGetValue(sourceKey, out ZoneFile? known)) from = known;
                else if (pack.Find(place.Zone) is { } entry) from = chunkSourceZones[sourceKey] = ZoneFile.Read(pack.Read(entry));
                template = from?.Objects.FirstOrDefault(o => o.Handle == place.Handle);
            }
            catch (InvalidDataException) { }
        }

        if (from != null && template != null)
        {
            IReadOnlyList<ZoneObject> group = from.PiecesWith(template);
            added = zone.AddCopiesFrom(from, group, TakenHandles(), at - group[0].Placement.Position);
            how = $"a copy of {place!.Zone}'s ({names.Class(template.ClassHash)}, handle {template.Handle:X8}" +
                  (group.Count > 1 ? $", with the {group.Count - 1} object(s) joined to it" : "") + ")";
        }
        else
        {
            string modelName = chunk + ".cchk_pc";
            byte[]? header = context.Assets.ReadAnywhere(modelName), data = context.Assets.ReadAnywhere(ModelFile.DataFileName(modelName));
            if (header == null || data == null) throw new InvalidOperationException($"{modelName} isn't in the game's files");
            if (context.Assets.ZoneSchema is not { } schema) throw new InvalidOperationException("the game's zone schema couldn't be read");
            ModelFile model = ModelFile.Read(modelName, header, data);
            IReadOnlyList<ChunkObject> parts = model.Objects?.Objects ?? [];
            bool breaks = parts.Any(o => o.Breakable);
            uint uid = parts.Count > 0 ? parts[0].Uid : 0;
            uint checksum = header.Length >= 0x28 ? BitConverter.ToUInt32(header, 0x24) : 0;
            (Vector3 min, Vector3 max) = ChunkBounds(model, parts.Count > 0 ? parts[0].Uid : null);
            string className = breaks ? "rfg_mover" : "general_mover";
            var properties = RfaFormats.Guerrilla.GuerrillaMovers.NewMover(schema, breaks, chunk, checksum, uid, at);
            added = [zone.AddNew(NameHashes.ClassHash(className), properties, at + min, at + max, TakenHandles())];
            how = $"a new {className} (no level of the game's places it to copy)";
        }

        NoteChunksToBring(added, targetContainer, needs);
        return added;
    }

    /// <summary>The chunks new objects name that this zone's world doesn't have: brought in on saving (said in <paramref name="needs"/>).</summary>
    void NoteChunksToBring(IEnumerable<ZoneObject> added, string targetContainer, List<string> needs)
    {
        foreach (string named in added.Select(o => o.Property("chunk_name")?.Text).OfType<string>()
                                      .Select(n => System.IO.Path.GetFileNameWithoutExtension(n)).Distinct(StringComparer.OrdinalIgnoreCase))
        {
            if (chunksToAdd.ContainsKey(named) || ChunkContainers().All(c => HasChunk(named, c)))
                continue;
            if (context.Assets.PackageOf(named, out string? why) is { } package)
            {
                chunksToAdd[named] = package;
                needs.Add($"{named} ({package.Files.Count} files, {package.Files.Sum(f => (long)f.Data.Length) / 1024:N0} KB)");
            }
            else
                context.Log($"Add: {named}'s files can't be brought into {targetContainer}: {why}. The game will show its stand-in for it.");
        }
    }

    /// <summary>
    /// Adds any chunk of the game's (a building or prop from another level, or from the Guerrilla
    /// world) where the view points, and picks it. If the game's levels place it somewhere, that
    /// object is copied (with its properties, and the pieces joined to it, with the destruction links
    /// between them: see <see cref="ZoneFile.AddCopiesFrom"/>); else a new mover is made for it
    /// (an rfg_mover if it breaks in pieces, else a general_mover; see
    /// <see cref="RfaFormats.Guerrilla.GuerrillaMovers.NewMover"/>). The chunks it needs that this
    /// zone's world doesn't have are noted, and their files (model and textures) go into the zone's
    /// container, with the asset lists' entries, when it is saved.
    /// </summary>
    void AddChunk(string chunk, Vector3 at)
    {
        if (!context.Assets.IndexReady)
        {
            context.Log("The editor is still reading the game's files (to find the chunk and the handles the zone's other files use). Try again in a few seconds.");
            return;
        }
        if (ChunkContainer() is not { } target)
        {
            MessageBox.Show($"{zoneName} isn't inside a container, and its archive has none, so there is nowhere to put {chunk}'s model for the game.",
                            "Level editor", MessageBoxButton.OK, MessageBoxImage.Information);
            return;
        }
        var (targetArchive, targetContainer) = target;
        CommitEdit();
        Mouse.OverrideCursor = Cursors.Wait;
        IReadOnlyList<ZoneObject> added;
        string how;
        var needs = new List<string>();
        try
        {
            // Snap to ground: adjust Y to the ground height at this X/Z position
            float groundY = GroundHeight(at.X, at.Z);
            at = new Vector3(at.X, groundY, at.Z);
            added = MakeChunkObjects(chunk, at, targetContainer, needs, out how);
        }
        catch (InvalidOperationException ex)
        {
            MessageBox.Show($"Couldn't add {chunk}: {ex.Message}.", "Level editor", MessageBoxButton.OK, MessageBoxImage.Information);
            return;
        }
        finally
        {
            Mouse.OverrideCursor = null;
        }

        foreach (ZoneObject obj in added) scene?.Add(obj);
        RemovedObjects? removed = null;
        var copies = added.ToList();
        Record(new Change(added[0], "Add",
            Undo: () => { removed = zone.Remove(copies); foreach (ZoneObject obj in copies) scene?.Remove(obj); },
            Redo: () => { if (removed != null) zone.PutBack(removed); foreach (ZoneObject obj in copies) scene?.Add(obj); },
            AddsOrRemoves: true));
        ObjectsChanged();
        PickInOutliner(added[0]);
        context.Log($"Add: {chunk} where the view points, at {at.X:0.#}, {at.Y:0.#}, {at.Z:0.#}: {how}." +
                    (needs.Count > 0 ? $" Brought in on saving, into {string.Join(", ", ChunkContainers().Select(c => c.Container))}: {string.Join(", ", needs)}." : " This zone's world has its model already."));
    }

    /// <summary>A chunk's box around its object (the parts of object <paramref name="uid"/>, or all), from its place; a metre each way if it has none.</summary>
    static (Vector3 Min, Vector3 Max) ChunkBounds(ModelFile model, uint? uid)
    {
        Vector3 min = new(float.MaxValue), max = new(float.MinValue);
        foreach (PlacedSubmesh placed in model.Intact(uid))
            foreach (Vector3 corner in Corners(placed.Submesh.BoundsMin, placed.Submesh.BoundsMax))
            {
                Vector3 p = Vector3.Transform(corner, placed.Transform);
                min = Vector3.Min(min, p);
                max = Vector3.Max(max, p);
            }
        return min.X <= max.X ? (min, max) : (new Vector3(-1), new Vector3(1));

        static IEnumerable<Vector3> Corners(Vector3 a, Vector3 b)
        {
            for (int i = 0; i < 8; i++)
                yield return new Vector3((i & 1) == 0 ? a.X : b.X, (i & 2) == 0 ? a.Y : b.Y, (i & 4) == 0 ? a.Z : b.Z);
        }
    }

    /// <summary>Where the middle of the view points: the first thing drawn there (within 300 m), or 15 m ahead.</summary>
    Vector3 PointAhead()
    {
        var middle = new Point(ViewArea.ActualWidth / 2, ViewArea.ActualHeight / 2);
        RayMeshGeometry3DHitTestResult? first = HitsAt(middle, sceneContent, relatedContent, geometryContent, terrainContent, decalContent, gridContent)
            .FirstOrDefault(hit => hit.DistanceToRayOrigin < 300);
        Point3D at = first?.PointHit ?? camera.Position + camera.Forward * 15;
        return new Vector3((float)at.X, (float)at.Y, (float)-at.Z);
    }

    /// <summary>
    /// What a line from the camera through a point of the view meets in these parts of the scene,
    /// nearest first. It asks the parts themselves, in 3D, rather than the view: the view is hidden
    /// while the Direct3D view shows the scene (see <see cref="OnGpuViewChanged"/>), and the zones around
    /// and the foliage are left out (they can't be picked, and their meshes are huge, so a click
    /// would be slow).
    /// </summary>
    List<RayMeshGeometry3DHitTestResult> HitsAt(Point point, params ModelVisual3D[] parts)
    {
        (Point3D origin, Vector3D direction) = camera.Ray(point, ViewArea.RenderSize);
        var ray = new RayHitTestParameters(origin, direction);
        var hits = new List<RayMeshGeometry3DHitTestResult>();
        foreach (ModelVisual3D part in parts)
            if (part.Content != null)
                VisualTreeHelper.HitTest(part, null, result =>
                {
                    if (result is RayMeshGeometry3DHitTestResult hit) hits.Add(hit);
                    return HitTestResultBehavior.Continue;
                }, ray);
        hits.Sort((a, b) => a.DistanceToRayOrigin.CompareTo(b.DistanceToRayOrigin));
        return hits;
    }

    // ---- Undo ----------------------------------------------------------------------------------

    /// <summary>
    /// Ends the picked object's current change (typing, a drag) as one step that can be undone,
    /// with the objects attached to it that moved with it.
    /// </summary>
    void CommitEdit(string what = "Edit")
    {
        RefreshHelpers();  // (a property such as a light's angle may have changed)
        if (selected == null || editStart == null) return;
        byte[] now = selected.Snapshot();
        var others = carried.Select(c => (c.Object, Before: c.Start, Now: c.Object.Snapshot()))
                            .Where(c => !c.Now.AsSpan().SequenceEqual(c.Before)).ToList();
        if (!now.AsSpan().SequenceEqual(editStart) || others.Count > 0)
        {
            ZoneObject obj = selected;
            byte[] before = editStart;
            Record(new Change(obj, what,
                () => { obj.Restore(before); foreach (var other in others) other.Object.Restore(other.Before); },
                () => { obj.Restore(now); foreach (var other in others) other.Object.Restore(other.Now); },
                Others: others.Select(o => o.Object).ToList()));
        }
        StartEdit();
        UpdateUndoButtons();
    }

    /// <summary>Keeps a change that has been made, to undo.</summary>
    void Record(Change change)
    {
        undo.Push(change);
        redo.Clear();
        MarkUnsaved();
        UpdateUndoButtons();
        LinksChanged();  // a property naming another object may have changed, or the object moved
        KeepSpawnResourcesStocked();  // a spawn node added, or its squad, vehicle set or exo changed
    }

    void Undo()
    {
        CommitEdit();
        if (!undo.TryPop(out Change? change)) return;
        change.Undo();
        redo.Push(change);
        AfterUndoOrRedo(change, "Undid");
    }

    void Redo()
    {
        CommitEdit();
        if (!redo.TryPop(out Change? change)) return;
        change.Redo();
        undo.Push(change);
        AfterUndoOrRedo(change, "Redid");
    }

    void AfterUndoOrRedo(Change change, string done)
    {
        RefreshHelpers();
        if (change.AddsOrRemoves)
        {
            if (selected != null && Editable(selected) && !zone.Objects.Contains(selected)) Select(null, focus: false);
            ObjectsChanged();
            // Deleting or putting back a building can lay out its neighbours' properties again
            // (their destruction links): the inspector shows the picked object's anew.
            if (selected != null)
            {
                StartEdit();
                ShowProperties();
            }
        }
        else if (change.Object != null)
        {
            scene?.Remodel(change.Object);  // (an undone property may have named another squad, vehicle...)
            scene?.Place(change.Object);
            foreach (ZoneObject other in change.Others ?? []) scene?.Place(other);
            LinksChanged();
        }
        if (change.Object == null) LiveNeedsReload();  // a change to no one object (the ground, many objects): the game may need the level again
        StartEdit();  // the picked object, or what is attached to it, may be as it was
        if (selected != null && (change.Object == selected || change.Others?.Contains(selected) == true)) ShowProperties();
        ShowSelection();
        MarkUnsaved();
        UpdateUndoButtons();
        context.Log(change.Object != null ? $"{done}: {change.What.ToLowerInvariant()} {Label(change.Object)}" : $"{done}: {change.What.ToLowerInvariant()}");
    }

    void UpdateUndoButtons()
    {
        UndoButton.IsEnabled = undo.Count > 0;
        RedoButton.IsEnabled = redo.Count > 0;
        UndoButton.ToolTip = undo.TryPeek(out Change? next) ? $"Undo: {Describe(next)} (Ctrl+Z)" : "Ctrl+Z";
        RedoButton.ToolTip = redo.TryPeek(out Change? back) ? $"Redo: {Describe(back)} (Ctrl+Y)" : "Ctrl+Y";
        // (A sculpting stroke changes no object.)
        static string Describe(Change change) => change.Object != null ? $"{change.What.ToLowerInvariant()} {Label(change.Object)}" : change.What.ToLowerInvariant();
    }

    void OnUndoClicked(object sender, RoutedEventArgs e) => Undo();
    void OnRedoClicked(object sender, RoutedEventArgs e) => Redo();

    void MarkUnsaved()
    {
        if (unsaved) return;
        unsaved = true;
        SaveButton.IsEnabled = true;
        TitleChanged?.Invoke();
    }

    // ---- 3D ------------------------------------------------------------------------------------

    /// <summary>A point in the game's space, in WPF's (Z flipped).</summary>
    static Point3D ToView(Vector3 p) => new(p.X, p.Y, -p.Z);

    /// <summary>A line from the camera through a point of the view, in the game's space.</summary>
    (Vector3 Origin, Vector3 Direction) GameRay(Point point)
    {
        (Point3D origin, Vector3D direction) = camera.Ray(point, ViewArea.RenderSize);
        return (new Vector3((float)origin.X, (float)origin.Y, (float)-origin.Z),
                new Vector3((float)direction.X, (float)direction.Y, (float)-direction.Z));
    }

    void BuildScene(bool frame)
    {
        Mouse.OverrideCursor = Cursors.Wait;
        try
        {
            // How long each part takes, for the log: where the wait goes when a zone opens.
            var took = new List<string>();
            var clock = System.Diagnostics.Stopwatch.StartNew();
            void Took(string what)
            {
                took.Add($"{what} {clock.Elapsed.TotalSeconds:0.0}");
                clock.Restart();
            }
            var objectModels = new ObjectModels(context.Assets, names, file => context.Assets.Model(file, archive, container));
            bool showModels = ShowModelsBox.IsChecked == true;
            // The level geometry as objects (see LevelEditor.Geometry.cs), before the scene is made of them.
            bool hadGeometry = geometryLoaded;
            bool hadBarriers = barriersLoaded;
            EnsureGeometryObjects();
            EnsureBarrierObjects();  // (the invisible walls, see LevelEditor.Barriers.cs)
            if ((geometryLoaded && !hadGeometry) || (barriersLoaded && !hadBarriers)) BuildObjectTree(FilterBox.Text.Trim());
            scene = new LevelScene(zone, names, showModels, objectModels.Of, (file, uid) => ModelParts(file, archive, container, uid: uid));
            RefreshHelpers();
            sceneContent.Content = scene.Root;
            foreach (RelatedFile file in RelatedFiles())
                file.Scene = new LevelScene(file.Zone, names, showModels, objectModels.Of, (model, uid) => ModelParts(model, file.Archive, file.Container, uid: uid));
            ShowKinds();
            ShowRelated();
            Took("objects");
            BuildLevelGeometry();
            Took("level geometry");
            BuildTerrain();
            Took("terrain");
            BuildNeighbours(objectModels);
            Took("zones around");
            BuildFoliage();
            Took("foliage");
            BuildKindChips();
            context.Log($"{zoneName}: drawn, in seconds: {string.Join(", ", took)}.");
            if (frame) camera.Frame(ZoneBounds());
            UpdateCamera();
            ShowSelection();
            context.Log($"{zoneName}: {zone.Objects.Count:N0} objects, {scene.ModelsPlaced:N0} drawn with their models" +
                        (scene.ModelsMissing > 0 ? $", {scene.ModelsMissing} models not found{(context.Assets.IndexReady ? "" : " yet (still reading the game's files)")}" : "") + ".");
            foreach (RelatedFile file in RelatedFiles())
                context.Log($"{zoneName}: also showing {(file.IsLayer ? "what the mission " + file.Label + " adds" : "the zone's own objects")}: " +
                            $"{file.Zone.Objects.Count:N0} objects from {file.Name}, {file.Scene!.ModelsPlaced:N0} drawn with their models.");
        }
        finally
        {
            Mouse.OverrideCursor = null;
        }
    }

    /// <summary>
    /// A model's geometry for the level (textures at most 256 pixels wide), read once. It and its
    /// textures are looked for next to <paramref name="nearArchive"/> / <paramref name="nearContainer"/> first.
    /// With <paramref name="seeThrough"/>, its textures' alpha is kept as see-through (foliage).
    /// For a file with several objects, <paramref name="uid"/> picks one (as zone objects do). With
    /// <paramref name="farAway"/>, it is the game's less detailed copy, if it has one (see <see cref="ModelFile.FarAway"/>).
    /// </summary>
    List<(MeshGeometry3D Mesh, Material Material)>? ModelParts(string chunk, string nearArchive, string? nearContainer, bool seeThrough = false,
                                                                uint? uid = null, bool farAway = false)
    {
        bool textures = ShowTexturesBox.IsChecked == true;
        string key = chunk + (uid is uint id ? $"#{id:X8}" : "") + (textures ? "+textures" : "") + (seeThrough ? "+alpha" : "") + (farAway ? "+far" : "");
        if (modelParts.TryGetValue(key, out var known)) return known;
        var model = context.Assets.Model(chunk, nearArchive, nearContainer);
        var parts = model == null ? null : ModelScene.Parts(model, farAway ? model.FarAway(uid) : model.Intact(uid), material =>
        {
            string? diffuse = textures && material < model.Materials.Count ? model.Materials[material].Diffuse : null;
            // Alpha kept for foliage, and for the materials the game draws see-through or cut out (glass, bullet holes over walls).
            bool alpha = seeThrough || (material < model.Materials.Count && Render.GameMaterial.UsesAlpha(model.Materials[material]));
            return string.IsNullOrEmpty(diffuse) ? null : context.Assets.Texture(diffuse, nearArchive, nearContainer, 256, alpha);
        });
        if (context.Assets.IndexReady) modelParts[key] = parts;  // before then, textures may still be missing
        return parts;
    }

    void UpdateCamera()
    {
        // While following the game, the same vertical field of view it draws with, whatever this view's shape.
        camera.FieldOfView = (gameFov is double vertical ? FlyCamera.SideToSide(vertical, ViewArea.RenderSize) : FlyCamera.DefaultFieldOfView) * padZoom;  // (padZoom: zoomed with the controller)
        camera.Apply(sceneCamera);
        camera.Apply(gizmoCamera);
        ShowGizmo();
        DrawAxisWidget();
        // Link lines keep their width on the screen: made again for where the camera is now.
        if (selected != null) ShowPickedLinks();
        if (showLinks) ShowAllLinks();
    }

    /// <summary>
    /// The zone's level geometry: the stitch pieces its stitch list places. The list is in the
    /// zone's level container, which may not be where the zone was opened from, so it is found by
    /// name (once the editor has read every container).
    /// </summary>
    void BuildLevelGeometry()
    {
        geometry = decals = null;
        geometryPieces = decalCount = 0;
        geometryContent.Content = decalContent.Content = null;
        foreach (string listName in LevelGeometry.ZoneNames(zoneName).Select(zone => zone + ".stchlst_pc"))
        {
            if (context.Assets.ReadFile(listName, archive, container) is not { } found) continue;
            try
            {
                StitchList list = StitchList.Read(found.Data);
                int missing = 0;
                if (geometryLoaded) geometryPieces = list.Pieces.Count;  // drawn as objects instead (see LevelEditor.Geometry.cs)
                else
                {
                    geometry = LevelGeometry.Build(list, piece => ModelParts(piece, found.Archive, found.Container), out geometryPieces, out missing);
                    geometryContent.Content = showGeometry ? geometry : null;
                }
                bool textures = ShowTexturesBox.IsChecked == true;
                decals = LevelGeometry.BuildDecals(list, name => textures ? context.Assets.Texture(name, found.Archive, found.Container, 256, alpha: true) : null, out decalCount);
                decalContent.Content = showDecals ? decals : null;
                context.Log($"{zoneName}: {geometryPieces:N0} stitch pieces of level geometry and {decalCount} of {list.Decals.Count} decals from {listName}" +
                            (missing > 0 ? $"; {missing} pieces not found" : "") + ".");
            }
            catch (Exception ex) when (ex is InvalidDataException or NotSupportedException)
            {
                context.Log($"{listName} couldn't be read: {ex.Message}");
            }
            return;
        }
    }

    /// <summary>
    /// The zone's terrain: the ground of its nine sub-zones (each in its own small container), found
    /// by name once the editor has read every container. Most cave zones have none.
    /// </summary>
    void BuildTerrain()
    {
        terrain = null;
        terrainParts = 0;
        terrainContent.Content = null;
        foreach (string zone in LevelGeometry.ZoneNames(zoneName))
        {
            List<TerrainSubzone> subzones = ReadSubzones(zone);
            if (subzones.Count == 0) continue;
            System.Numerics.Vector2 corner = LevelGeometry.ZoneCorner(subzones);
            (TerrainPainter? painter, string look) = ShowTexturesBox.IsChecked == true ? GroundPainter(zone, corner, subzones[0].ZoneWidth) : (null, "plain");
            static System.Numerics.Vector2 From(TerrainSubzone s) => new(s.BoundsMin.X, s.BoundsMin.Z);
            static System.Numerics.Vector2 To(TerrainSubzone s) => new(s.BoundsMax.X, s.BoundsMax.Z);
            var ground = LevelGeometry.BuildTerrain(subzones, subzone => painter?.Paint(From(subzone), To(subzone)),
                inGameLight: painter == null ? null : subzone => () => painter.PaintAsInGame(From(subzone), To(subzone)), freeze: false);
            var roads = LevelGeometry.BuildRoads(subzones, RoadTexture);
            var both = new Model3DGroup { Children = { ground, roads } };
            // (Not frozen: the sculptor gives the sub-zones new meshes; one model a sub-zone, in their order.)
            NoteGround(zone, ground, subzones.Select(s => s.Number));
            terrain = both;
            terrainParts = subzones.Count;
            terrainContent.Content = showTerrain ? terrain : null;
            int roadCount = subzones.Sum(s => s.Roads.Count);
            context.Log($"{zoneName}: terrain from {subzones.Count} of {zone}'s 9 sub-zones ({subzones.Sum(s => s.Triangles.Length / 3):N0} triangles), {look}" +
                        (roadCount > 0 ? $"; {roadCount} road{(roadCount == 1 ? "" : "s")} laid in it." : "."));
            return;
        }
    }

    /// <summary>A road's texture (see <see cref="LevelGeometry.BuildRoads"/>), or none while textures are off.</summary>
    System.Windows.Media.Imaging.BitmapSource? RoadTexture(string name) =>
        ShowTexturesBox.IsChecked == true ? context.Assets.Texture(name, archive, container, 512) : null;

    /// <summary>A zone's terrain sub-zones that have ground (each is in its own small container), found by name.</summary>
    List<TerrainSubzone> ReadSubzones(string zone)
    {
        var subzones = new List<TerrainSubzone>();
        for (int number = 0; number < 9; number++)
        {
            if (context.Assets.ReadFile(TerrainSubzone.HeaderFileName(zone, number), archive, container) is not { } header ||
                context.Assets.ReadFile(TerrainSubzone.DataFileName(zone, number), header.Archive, header.Container) is not { } data)
                continue;
            try
            {
                TerrainSubzone subzone = TerrainSubzone.Read(header.Data, data.Data);
                if (subzone.HasGround) subzones.Add(subzone);
            }
            catch (Exception ex) when (ex is InvalidDataException or NotSupportedException)
            {
                context.Log($"{TerrainSubzone.HeaderFileName(zone, number)} couldn't be read: {ex.Message}");
            }
        }
        return subzones;
    }

    /// <summary>
    /// The zones around this one (world_07_01 ... world_09_03 around world_08_02), so its edges
    /// meet what the game shows next to them: their terrain, coloured with their colour maps (as
    /// the game colours far-away ground), their level geometry and their objects' models. Each
    /// zone's meshes are joined into a few, one per texture, to keep the view quick, so they
    /// can't be picked: open a zone to work on it. Found once the editor has read every container.
    /// </summary>
    void BuildNeighbours(ObjectModels objectModels)
    {
        neighbours = null;
        neighbourZones = 0;
        neighbourContent.Content = null;
        if (!context.Assets.IndexReady) return;
        var drawnZones = new List<string>();
        DateTime started = DateTime.Now;
        bool textures = ShowTexturesBox.IsChecked == true, models = ShowModelsBox.IsChecked == true;
        var drawn = new Model3DGroup();
        var joined = new JoinedMeshes();
        int geometryTriangles = 0;  // of the level geometry; the rest of those joined are the objects' models
        foreach (string zone in LevelGeometry.Neighbours(LevelGeometry.ZoneNames(zoneName).Last()))
        {
            bool found = false;
            try
            {
                if (context.Assets.ReadFile(zone + ".stchlst_pc", archive, container) is { } list)
                {
                    found = true;
                    int before = joined.Triangles;
                    foreach (StitchPiece piece in StitchList.Read(list.Data).Pieces)
                        if (ModelParts(piece.Name + ".cstch_pc", list.Archive, list.Container, farAway: true) is { } parts)
                            joined.Add(parts, LevelScene.ViewMatrix(piece.Position, piece.Rotation));
                    geometryTriangles += joined.Triangles - before;
                }
                List<TerrainSubzone> subzones = ReadSubzones(zone);
                if (subzones.Count > 0)
                {
                    found = true;
                    System.Windows.Media.Imaging.BitmapSource? colourMap = textures ? context.Assets.Texture(zone + "comb.tga", archive, container, 512) : null;
                    // (Not frozen: the sculptor's strokes carry on into the zones around.)
                    Model3DGroup ground = LevelGeometry.BuildTerrain(subzones, colourMap, LevelGeometry.ZoneCorner(subzones), freeze: false);
                    drawn.Children.Add(ground);
                    NoteGround(zone, ground, subzones.Select(s => s.Number));
                    Model3DGroup roads = LevelGeometry.BuildRoads(subzones, RoadTexture);
                    if (roads.CanFreeze) roads.Freeze();
                    drawn.Children.Add(roads);
                }
                if (models && context.Assets.ReadFile(zone + ".rfgzone_pc", archive, container) is { } zoneFile)
                {
                    found = true;
                    foreach (ZoneObject obj in ZoneFile.Read(zoneFile.Data).Objects)
                    {
                        if (objectModels.Of(obj) is not { } objectParts) continue;  // markers aren't drawn around the zone
                        Matrix3D at = LevelScene.ViewMatrix(obj.Placement.Position, obj.Placement.Rotation);
                        foreach (ObjectPart part in objectParts)
                            if (ModelParts(part.File, zoneFile.Archive, zoneFile.Container, uid: part.Uid) is { } parts)
                                joined.Add(parts, LevelScene.ViewMatrix(part.Placement) * at);
                    }
                }
            }
            catch (Exception ex) when (ex is InvalidDataException or NotSupportedException)
            {
                context.Log($"{zone} couldn't be read: {ex.Message}");
            }
            if (found) drawnZones.Add(zone);
        }
        neighbourZones = drawnZones.Count;
        if (neighbourZones == 0) return;
        Model3D joinedModels = joined.Build();
        if (joinedModels.CanFreeze) joinedModels.Freeze();
        drawn.Children.Add(joinedModels);
        neighbours = drawn;  // (not frozen: the zones' ground may be sculpted)
        neighbourContent.Content = showNeighbours ? neighbours : null;
        context.Log($"{zoneName}: the zones around it drawn too, {string.Join(", ", drawnZones)} ({geometryTriangles:N0} triangles of level geometry, " +
                    $"{joined.Triangles - geometryTriangles:N0} of objects' models), " +
                    $"in {(DateTime.Now - started).TotalSeconds:0.0} s.");
    }

    /// <summary>
    /// The zone's foliage: the debris its terrain file scatters over the ground, set down on what
    /// is below it: the terrain, the level geometry and the objects' models (so it is built after
    /// them, and not moved when they are). 11 zones have any.
    /// </summary>
    void BuildFoliage()
    {
        foliage = null;
        foliagePieces = 0;
        foliageContent.Content = null;
        foreach (string zone in LevelGeometry.ZoneNames(zoneName))
        {
            if (context.Assets.ReadFile(zone + ".cterrain_pc", archive, container) is not { } file) continue;
            try
            {
                byte[] data = FoliageAsPainted(zone, file.Data);  // (with the foliage painter's strokes, see LevelEditor.Foliage.cs)
                if (ZoneFoliage.Read(data, zone) is not { } zoneFoliage) return;
                var pieces = zoneFoliage.Scatter().ToList();
                if (pieces.Count == 0) return;
                IEnumerable<Model3D> models = (scene?.Models ?? []).Concat(RelatedFiles().SelectMany(f => f.Scene?.Models ?? []));
                var surfaces = new SurfaceFinder([terrain, geometry, .. models], pieces.Select(p => new Point(p.Position.X, -p.Position.Z)),
                                                 pieces.Min(p => p.Position.Y - p.Drop) - 1, pieces.Max(p => p.Position.Y) + 1);
                foliage = FoliageScene.Build(pieces, mesh => ModelParts(mesh, file.Archive, file.Container, seeThrough: true), surfaces,
                                             out foliagePieces, out int missing, out int nothingBelow);
                foliageContent.Content = showFoliage ? foliage : null;
                context.Log($"{zoneName}: {foliagePieces:N0} pieces of foliage scattered as the game does from {zone}.cterrain_pc" +
                            (missing > 0 ? $"; {missing:N0} not drawn, as their meshes weren't found" : "") +
                            (nothingBelow > 0 ? $"; {nothingBelow:N0} left out because their 1 m cube has no surface (the game does the same)" : "") + ".");
            }
            catch (Exception ex) when (ex is InvalidDataException or NotSupportedException)
            {
                context.Log($"{zone}.cterrain_pc's foliage couldn't be read: {ex.Message}");
            }
            return;
        }
    }

    /// <summary>
    /// What paints the ground: its four texture layers mixed by its blend map, as the game does,
    /// from the zone's terrain file (world_08_02.cterrain_pc); or, if those can't be found, the
    /// zone's colour map; or nothing (plain). Also says which, for the log.
    /// </summary>
    (TerrainPainter? Painter, string Look) GroundPainter(string zone, System.Numerics.Vector2 corner, float zoneWidth)
    {
        RgbaImage? Decode(string texture, int maxSize) =>
            context.Assets.FindTexture(texture, archive, container) is var (peg, found) ? peg.Decode(found, maxSize) : null;
        RgbaImage? colourMap = Decode(zone + "comb.tga", 512);
        try
        {
            if (context.Assets.ReadFile(zone + ".cterrain_pc", archive, container) is { } file &&
                ZoneTerrain.Read(TerrainAsSwapped(zone, file.Data)) is { } ground && (PaintedBlend(zone) ?? Decode(ground.BlendMap, 4096)) is { } blend)
            {
                RgbaImage?[] layers = ground.Layers.Select(layer => Decode(layer.Texture, TerrainPainter.DetailNeeded(layer))).ToArray();
                if (layers.All(layer => layer != null))
                {
                    TerrainPainter painter = TerrainPainter.ForLayers(ground, blend, layers!, corner, zoneWidth, colourMap);
                    return (painter, "painted from " + string.Join(", ", ground.Layers.Select(l => System.IO.Path.GetFileNameWithoutExtension(l.Texture))) +
                                     $" ({painter.Curve})");
                }
            }
        }
        catch (Exception ex) when (ex is InvalidDataException or NotSupportedException)
        {
            context.Log($"{zone}.cterrain_pc couldn't be read: {ex.Message}");
        }
        return colourMap != null ? (TerrainPainter.ForColourMap(colourMap, corner, zoneWidth), "coloured with its colour map") : (null, "plain");
    }

    void BuildGrid()
    {
        grid = GroundGrid.Build(zone);
        gridContent.Content = showGrid ? grid : null;
    }

    /// <summary>Draws only the kinds of objects the Scene panel shows, and of them not the objects hidden there one by one, in this file and the zone's others.</summary>
    void ShowKinds()
    {
        scene?.ShowOnly(kind => !hiddenKinds.Contains(kind), hiddenObjects.Contains);
        foreach (RelatedFile file in RelatedFiles()) file.Scene?.ShowOnly(kind => !hiddenKinds.Contains(kind), hiddenObjects.Contains);
        ShowAllLinks();
    }

    /// <summary>Draws the zone's other files whose chips are on.</summary>
    void ShowRelated()
    {
        var shown = new Model3DGroup();
        foreach (RelatedFile file in RelatedFiles())
            if (file.Shown && file.Scene != null) shown.Children.Add(file.Scene.Root);
        relatedContent.Content = shown;
        ShowAllLinks();
    }

    /// <summary>
    /// The Scene panel's eyes: one for each kind of object the zone has (with how many, counting the
    /// zone's other files drawn with it), one for each of those files, and the level's parts. The
    /// panel's tree is made again with them (see LevelEditor.Scene.cs).
    /// </summary>
    void BuildKindChips()
    {
        showRows.Clear();
        if (scene == null)
        {
            BuildObjectTree(FilterBox.Text.Trim());
            return;
        }
        foreach (ObjectKind kind in ObjectKind.All)
        {
            int count = scene.Count(kind) + RelatedFiles().Sum(file => file.Scene?.Count(kind) ?? 0);
            if (count == 0) continue;
            showRows.Add(Row(kind.Name, kind.Brush, count, kind.Description, !hiddenKinds.Contains(kind), shown =>
            {
                if (shown) hiddenKinds.Remove(kind);
                else hiddenKinds.Add(kind);
                ShowKinds();
            }, kind: kind));
        }
        foreach (RelatedFile file in RelatedFiles())
            showRows.Add(Row(file.Label, Theme.Brush("RelatedBrush"), file.Zone.Objects.Count, file.Description, file.Shown, shown =>
            {
                file.Shown = shown;
                ShowRelated();
                if (!shown && selected != null && relatedOf.GetValueOrDefault(selected) == file) Select(null, focus: false);
            }, file: file));
        if (neighbourZones > 0)
            showRows.Add(Row("Zones around", Theme.Brush("NeighbourBrush"), neighbourZones,
                "The zones next to this one: their ground (coloured with their colour maps, as the game colours far-away ground), " +
                "level geometry and objects' models. They can't be picked; open a zone to work on it.", showNeighbours, shown =>
                {
                    showNeighbours = shown;
                    neighbourContent.Content = shown ? neighbours : null;
                }));
        if (terrainParts > 0)
            showRows.Add(Row("Terrain", Theme.Brush("TerrainBrush"), null,
                "The ground, painted with its texture layers as the game blends them. It can't be picked or changed yet.", showTerrain, shown =>
                {
                    showTerrain = shown;
                    terrainContent.Content = shown ? terrain : null;
                }));
        if (foliagePieces > 0)
            showRows.Add(Row("Foliage", Theme.Brush("FoliageBrush"), foliagePieces,
                "Debris, rubble and rocks the game scatters over the ground from the zone's terrain file. It can't be picked or changed yet.", showFoliage, shown =>
                {
                    showFoliage = shown;
                    foliageContent.Content = shown ? foliage : null;
                }));
        if (decalCount > 0)
            showRows.Add(Row("Decals", Theme.Brush("DecalBrush"), decalCount,
                "Pictures laid onto surfaces: trash, blast marks, blood, signs. They can't be picked or changed yet.", showDecals, shown =>
                {
                    showDecals = shown;
                    decalContent.Content = shown ? decals : null;
                }));
        if (geometryPieces > 0 && !geometryLoaded)  // loaded as objects, their kind's own chip shows and hides them
            showRows.Add(Row("Level geometry", Theme.Brush("LevelGeometryBrush"), geometryPieces,
                "Rock, cave walls, ledges, roads, bridges and other fixed pieces the zone is built from (its stitch pieces). " +
                "They can't be picked or changed yet.", showGeometry, shown =>
                {
                    showGeometry = shown;
                    geometryContent.Content = shown ? geometry : null;
                }));
        if (Links().Count > 0)
            showRows.Add(Row("Links", Theme.Brush("LinkBrush"), Links().Count,
                "Lines between objects that name each other, each in the colour of the one naming the other, its arrow " +
                "pointing at the one named: navpoints' paths, roads, patrols, logic handlers and their triggers, animated " +
                "rigs, objects attached to others. The picked object's links are always shown, and listed in the inspector.",
                showLinks, shown =>
                {
                    showLinks = shown;
                    ShowAllLinks();
                }));
        showRows.Add(Row("Grid", null, null, "Lines on the ground every 10 metres", showGrid, shown =>
        {
            showGrid = shown;
            gridContent.Content = shown ? grid : null;
        }));
        BuildObjectTree(FilterBox.Text.Trim());
    }

    /// <summary>A row of the Scene panel with an eye: what it shows and hides, and how many it holds.</summary>
    static ShowRow Row(string name, Brush? colour, int? count, string tip, bool on, Action<bool> toggled, ObjectKind? kind = null, RelatedFile? file = null) =>
        new(name, colour, count, tip, on, toggled, kind, file);

    /// <summary>The axis widget in the corner: which way X, Y and Z point as seen from the camera.</summary>
    void DrawAxisWidget()
    {
        AxisWidget.Children.Clear();
        const double centre = 32, length = 22;
        (Axis Axis, string Name, string Brush)[] axes = [(Axis.X, "X", "AxisXBrush"), (Axis.Y, "Y", "AxisYBrush"), (Axis.Z, "Z", "AxisZBrush")];
        // Those pointing away first, so the ones pointing out of the screen are drawn over them.
        foreach (var (axis, name, brushKey) in axes.OrderByDescending(a => Vector3D.DotProduct(Gizmo.Direction(a.Axis), camera.Forward)))
        {
            Vector3D direction = Gizmo.Direction(axis);
            var end = new Point(centre + Vector3D.DotProduct(direction, camera.Right) * length,
                                centre - Vector3D.DotProduct(direction, camera.Up) * length);
            double opacity = Vector3D.DotProduct(direction, camera.Forward) > 0.3 ? 0.5 : 1;  // pointing away: dimmer
            Brush brush = Theme.Brush(brushKey);
            AxisWidget.Children.Add(new Line { X1 = centre, Y1 = centre, X2 = end.X, Y2 = end.Y, Stroke = brush, StrokeThickness = 2, Opacity = opacity });
            var dot = new Border
            {
                Width = 16, Height = 16, CornerRadius = new CornerRadius(8), Background = brush, Opacity = opacity,
                Child = new TextBlock
                {
                    Text = name, FontSize = 10, FontWeight = FontWeights.Bold, Foreground = Theme.Brush("WindowBrush"),
                    HorizontalAlignment = HorizontalAlignment.Center, VerticalAlignment = VerticalAlignment.Center,
                },
            };
            Canvas.SetLeft(dot, end.X - 8);
            Canvas.SetTop(dot, end.Y - 8);
            AxisWidget.Children.Add(dot);
        }
    }

    void ShowSelection()
    {
        selectionBox.Content = selected == null ? null : LevelScene.SelectionBox(selected);
        ShowPickedLinks();
        ShowGizmo();
    }

    // ---- Links between objects -------------------------------------------------------------------

    static readonly Color NamesColour = Color.FromRgb(255, 196, 64), NamedByColour = Color.FromRgb(96, 200, 255);

    /// <summary>The links between the zone's objects and its other files' (see <see cref="ZoneLinks"/>), found when first wanted.</summary>
    List<ZoneLink> Links() => links ??= ZoneLinks.Find([zone, .. RelatedFiles().Select(file => file.Zone)]);

    /// <summary>After objects were added, removed or changed: the links are found again and drawn again.</summary>
    void LinksChanged()
    {
        links = null;
        ShowAllLinks();
        ShowPickedLinks();
    }

    /// <summary>Where a link line ends at an object: the middle of its box, where its model or marker is.</summary>
    static Vector3 LinkEnd(ZoneObject obj) => (obj.BoundsMin + obj.BoundsMax) / 2;

    /// <summary>Whether an object is drawn now: its kind's chip and its file's are on.</summary>
    bool Drawn(ZoneObject obj) =>
        !hiddenKinds.Contains(KindOf(obj)) && !hiddenObjects.Contains(obj) && (!relatedOf.TryGetValue(obj, out RelatedFile? file) || file.Shown);

    /// <summary>The camera, in the game's space, and how many metres a pixel covers one metre in front of it: for link lines' widths.</summary>
    (Vector3 Eye, double PerMetre) LineView() =>
        (new Vector3((float)camera.Position.X, (float)camera.Position.Y, (float)-camera.Position.Z),
         2 * Math.Tan(camera.FieldOfView * Math.PI / 360) / Math.Max(1, ViewArea.ActualWidth));

    /// <summary>With the Links chip on: every link between objects drawn, in the colour of the object naming the other.</summary>
    void ShowAllLinks()
    {
        if (!showLinks)
        {
            linkContent.Content = null;
            return;
        }
        (Vector3 eye, double perMetre) = LineView();
        linkContent.Content = LinkLines.Build(Links().Where(l => Drawn(l.From) && Drawn(l.To))
            .Select(l => (LinkEnd(l.From), LinkEnd(l.To), KindOf(l.From).Colour)), eye, perMetre, pixels: 2);
    }

    /// <summary>The picked object's links, always: those it names in yellow, those naming it in blue.</summary>
    void ShowPickedLinks()
    {
        if (selected == null)
        {
            pickedLinkContent.Content = null;
            return;
        }
        ZoneObject picked = selected;
        (Vector3 eye, double perMetre) = LineView();
        pickedLinkContent.Content = LinkLines.Build(Links().Where(l => l.From == picked || l.To == picked)
            .Select(l => (LinkEnd(l.From), LinkEnd(l.To), l.From == picked ? NamesColour : NamedByColour)), eye, perMetre, pixels: 3);
    }

    /// <summary>The inspector's links: what the picked object names, and what names it, each a button that picks it.</summary>
    void ShowLinkRows()
    {
        LinkPanel.Children.Clear();
        if (selected == null) return;
        ZoneObject picked = selected;
        List<ZoneLink> naming = Links().Where(l => l.From == picked).ToList(), namedBy = Links().Where(l => l.To == picked).ToList();
        LinksSection.Visibility = naming.Count + namedBy.Count > 0 ? Visibility.Visible : Visibility.Collapsed;
        void Group(string title, Color colour, List<ZoneLink> group, bool outgoing)
        {
            if (group.Count == 0) return;
            LinkPanel.Children.Add(new TextBlock
            {
                Text = $"{title} ({group.Count})", FontSize = 11, Margin = new Thickness(0, 6, 0, 2),
                Foreground = new SolidColorBrush(colour),
            });
            const int shown = 40;
            foreach (ZoneLink link in group.Take(shown))
            {
                ZoneObject other = outgoing ? link.To : link.From;
                string how = link.Property is uint hash ? names.Property(hash).Replace('_', ' ') : outgoing ? "attached to" : "attached here";
                var row = new Button { Style = Theme.Style("ListRow"), ToolTip = $"Pick {Described(other)}" };
                var content = new DockPanel();
                var label = new TextBlock { Text = how, Width = 120, TextTrimming = TextTrimming.CharacterEllipsis, Foreground = Theme.Brush("DimTextBrush") };
                DockPanel.SetDock(label, Dock.Left);
                content.Children.Add(label);
                string what = Label(other).StartsWith('#') ? $"{names.Class(other.ClassHash)} {other.Handle:X8}" : Label(other);  // unnamed: its class
                content.Children.Add(new TextBlock { Text = what, TextTrimming = TextTrimming.CharacterEllipsis });
                row.Content = content;
                row.Click += (_, _) =>
                {
                    PickInOutliner(other);
                    if (selected != other) Select(other, focus: false);  // not in the outliner (a file not shown)
                    FocusOnSelected();
                };
                LinkPanel.Children.Add(row);
            }
            if (group.Count > shown)
                LinkPanel.Children.Add(new TextBlock { Text = $"and {group.Count - shown} more", FontSize = 11, Foreground = Theme.Brush("DimTextBrush") });
        }
        Group("Names", NamesColour, naming, outgoing: true);
        Group("Named by", NamedByColour, namedBy, outgoing: false);
    }

    /// <summary>The handles on the picked object, sized to look the same however far away it is.</summary>
    void ShowGizmo()
    {
        LevelTool shown = selected == null || !Editable(selected) || tool == LevelTool.Sculpt || (tool == LevelTool.Rotate && !selected.CanTurn) ? LevelTool.Select : tool;
        if (shown == LevelTool.Select)
        {
            gizmo.Hide();
            return;
        }
        Point3D centre = ToView(selected!.Placement.Position);
        gizmo.Show(shown, centre, Math.Max(0.2, (camera.Position - centre).Length * 0.16), hotAxis);
    }

    void OnViewOptionChanged(object sender, RoutedEventArgs e)
    {
        if (scene != null) BuildScene(frame: false);
    }

    // ---- The Direct3D view -------------------------------------------------------------------
    // Step 1 of docs\renderer-plan.md: Direct3D 11 draws what SceneViewport holds. SceneViewport
    // stays in place, unseen, for picking objects; the gizmo, axis widget and hints stay on top.

    Render.RenderView? gpuView;
    Render.SceneRenderer? gpuScene;

    void OnGpuViewChanged(object sender, RoutedEventArgs e)
    {
        bool on = GpuViewBox.IsChecked == true;
        if (on && gpuView == null)
        {
            try
            {
                if (Render.GpuDevice.Shared is not Render.GpuDevice gpu)
                    throw new InvalidOperationException(Render.GpuDevice.Failure ?? "no Direct3D 11 device");
                gpuView = new Render.RenderView(gpu);
                gpuScene = new Render.SceneRenderer(gpu)
                {
                    // Normal, specular and glow maps and skies for game light, found like the colour textures.
                    LoadTexture = (name, largest) => context.Assets.Texture(name, archive, container, largest, alpha: true),
                    // The same, as stored, for the card: full size and still compressed.
                    FindTexture = name => context.Assets.FindTexture(name, archive, container),
                };
                GpuImage.Source = gpuView.Image;
                // The picture is lost when the PC is locked or the display driver restarts: draw it again.
                gpuView.Image.IsFrontBufferAvailableChanged += (_, _) => gpuScene?.ForgetLastFrame();
                context.Log($"{zoneName}: drawn with Direct3D 11 on {gpu.Adapter}.");
            }
            catch (Exception ex)
            {
                context.Log("The Direct3D view couldn't start: " + ex.Message);
                gpuView?.Dispose();
                gpuView = null;
                GpuViewBox.IsChecked = false;
                return;
            }
        }
        GpuImage.Visibility = on ? Visibility.Visible : Visibility.Collapsed;
        // Hidden, not just see-through, so WPF doesn't draw the scene a second time under the Direct3D
        // view. Picking asks the scene's parts along a ray (HitsAt), and the Direct3D view gathers
        // them from the hidden view all the same.
        SceneViewport.Visibility = on ? Visibility.Hidden : Visibility.Visible;
        if (!on) GameLightBox.IsChecked = false;  // game light is drawn by the Direct3D view
        if (on) CompositionTarget.Rendering += DrawGpuView;
        else CompositionTarget.Rendering -= DrawGpuView;
    }

    ZoneLighting? zoneLighting;  // the zone's lamps and environments, read when first wanted

    /// <summary>Game light: the Direct3D view lit as the game lights the zone (switching that view on if it is off).</summary>
    void OnGameLightChanged(object sender, RoutedEventArgs e)
    {
        bool on = GameLightBox.IsChecked == true;
        if (on && GpuViewBox.IsChecked != true) GpuViewBox.IsChecked = true;
        if (on && gpuScene == null)
        {
            GameLightBox.IsChecked = false;  // the Direct3D view couldn't start (the log says why)
            return;
        }
        if (on)
        {
            zoneLighting ??= ReadZoneLighting();
            string skies = string.Join(", ", zoneLighting.Skies);
            context.Log($"{zoneName}: lit as the game lights it: {zoneLighting.Lamps.Count} lamps, {zoneLighting.EnvironmentCount} environment boxes " +
                        "(the ambient light, exposure and fog follow the box the camera is in)" + (skies.Length > 0 ? $", sky {skies}." : ", no sky."));
        }
        if (gpuScene != null) gpuScene.GameLighting = on ? zoneLighting : null;
    }

    /// <summary>
    /// The lamps and environment boxes of this zone and the zones around it: a zone's boxes reach
    /// over its edges (the first mission's tunnel into the Terraformer, in world_08_03, takes its
    /// light from a box of world_08_02's), and the zones around are drawn too.
    /// </summary>
    ZoneLighting ReadZoneLighting()
    {
        var objects = new List<ZoneObject>(zone.Objects);
        foreach (string around in LevelGeometry.Neighbours(LevelGeometry.ZoneNames(zoneName).Last()))
        {
            try
            {
                if (context.Assets.ReadFile(around + ".rfgzone_pc", archive, container) is { } file)
                    objects.AddRange(ZoneFile.Read(file.Data).Objects);
            }
            catch (Exception ex) when (ex is InvalidDataException or NotSupportedException) { }
        }
        // Flicker and sway curves, found like any file ("x.lightmult" is the file x.lightmult_pc).
        var curves = new Dictionary<string, LightCurve?>(StringComparer.OrdinalIgnoreCase);
        LightCurve? Curve(string name)
        {
            if (curves.TryGetValue(name, out LightCurve? known)) return known;
            LightCurve? curve = null;
            try
            {
                if (context.Assets.ReadFile(name + "_pc", archive, container) is { } file) curve = LightCurve.Read(file.Data);
            }
            catch (InvalidDataException) { }
            return curves[name] = curve;
        }
        // Skies: the picture each sky effect draws, read from the effect's file ("env_sur_day_sky.cefct_pc").
        var skies = new Dictionary<string, string?>(StringComparer.OrdinalIgnoreCase);
        string? Sky(string effect)
        {
            if (!skies.TryGetValue(effect, out string? picture))
                skies[effect] = picture = context.Assets.ReadFile(effect + ".cefct_pc", archive, container) is { } file ? ZoneLighting.SkyPicture(file.Data) : null;
            return picture;
        }
        return ZoneLighting.Read(objects, Curve, Sky);
    }

    /// <summary>Each time WPF draws: the Direct3D view draws the scene again, at the view's size in pixels.</summary>
    void DrawGpuView(object? sender, EventArgs e)
    {
        if (gpuView == null || gpuScene == null || !IsVisible) return;
        DrawGpuFrame();
    }

    /// <summary>Draws a frame of the Direct3D view if anything changed (also used by the off-screen tests).</summary>
    void DrawGpuFrame()
    {
        if (gpuView == null || gpuScene == null) return;
        DpiScale dpi = VisualTreeHelper.GetDpi(ViewArea);
        gpuView.Resize((int)Math.Round(ViewArea.ActualWidth * dpi.DpiScaleX), (int)Math.Round(ViewArea.ActualHeight * dpi.DpiScaleY));
        gpuView.Show();  // the frame drawn last time, now the card has done it
        if (!gpuScene.Changed(SceneViewport, sceneCamera, gpuView.Width, gpuView.Height)) return;
        gpuView.Draw(context => gpuScene.Draw(context, sceneCamera, gpuView.Width, gpuView.Height),
                     first: context => gpuScene.DrawShadows(context, sceneCamera));
    }


    void OnShowEverything(object sender, RoutedEventArgs e)
    {
        if (scene == null) return;
        camera.Frame(ZoneBounds());
        UpdateCamera();
    }

    /// <summary>
    /// The part of the view the zone's objects are in, for "Show the whole zone": the box around
    /// all but the furthest few in each direction, as zones have a few objects (such as markers)
    /// kilometres away from the rest.
    /// </summary>
    Rect3D ZoneBounds()
    {
        List<Point3D> points = zone.Objects.Select(o => ToView(o.Placement.Position)).ToList();
        if (points.Count == 0) return Rect3D.Empty;
        int skip = points.Count / 50;  // 2% at each end
        double Low(Func<Point3D, double> axis) => points.Select(axis).Order().ElementAt(skip);
        double High(Func<Point3D, double> axis) => points.Select(axis).OrderDescending().ElementAt(skip);
        double x0 = Low(p => p.X), y0 = Low(p => p.Y), z0 = Low(p => p.Z);
        return new Rect3D(x0, y0, z0, High(p => p.X) - x0, High(p => p.Y) - y0, High(p => p.Z) - z0);
    }

    /// <summary>How far things are, for how fast the camera slides and moves.</summary>
    double ViewScale => selected != null ? Math.Max(3, (camera.Position - ToView(selected.Placement.Position)).Length) : 40;

    // ---- Mouse ---------------------------------------------------------------------------------

    void OnViewMouseDown(object sender, MouseButtonEventArgs e)
    {
        ViewArea.Focus();
        Point point = e.GetPosition(ViewArea);
        dragFrom = point;
        if (e.ChangedButton == MouseButton.Right)
        {
            drag = Drag.Look;
            lastFrame = DateTime.Now;
            CompositionTarget.Rendering += Fly;
        }
        else if (e.ChangedButton == MouseButton.Middle)
        {
            drag = Drag.Slide;
        }
        else if (e.ChangedButton == MouseButton.Left && tool == LevelTool.Sculpt && !Keyboard.Modifiers.HasFlag(ModifierKeys.Alt))
        {
            brushAt = point;
            if (BeginStroke(fromPad: false)) drag = Drag.Sculpt;
        }
        else if (e.ChangedButton == MouseButton.Left && tool == LevelTool.NavMesh && !Keyboard.Modifiers.HasFlag(ModifierKeys.Alt))
        {
            brushAt = point;
            if (BeginNavMeshStroke()) drag = Drag.NavMesh;
        }
        else if (e.ChangedButton == MouseButton.Left)
        {
            if (Keyboard.Modifiers.HasFlag(ModifierKeys.Alt)) drag = Drag.Circle;
            else if (selected != null && Editable(selected) && gizmo.HandleAt(GizmoViewport, point) is Axis axis) BeginHandleDrag(axis, point);
            else pressedAt = point;
        }
        ViewArea.CaptureMouse();
        e.Handled = true;
    }

    void OnViewMouseMove(object sender, MouseEventArgs e)
    {
        Point point = e.GetPosition(ViewArea);
        Vector moved = point - dragFrom;
        switch (drag)
        {
            case Drag.Look:
                camera.Turn(-moved.X * 0.004, -moved.Y * 0.004);
                UpdateCamera();
                break;
            case Drag.Slide:
                camera.Position += (-camera.Right * moved.X + camera.Up * moved.Y) * ViewScale * 0.0015;
                UpdateCamera();
                break;
            case Drag.Circle:
            {
                // Circle around the picked object (or a point ahead), keeping the distance.
                Point3D pivot = selected != null ? ToView(selected.Placement.Position) : camera.Position + camera.Forward * 30;
                double distance = (pivot - camera.Position).Length;
                camera.Turn(-moved.X * 0.006, -moved.Y * 0.006);
                camera.Position = pivot - camera.Forward * distance;
                UpdateCamera();
                break;
            }
            case Drag.Handle:
                DragHandle(point);
                break;
            case Drag.Sculpt:
            case Drag.NavMesh:
                brushAt = point;
                break;
            default:
                if (tool == LevelTool.Sculpt)
                {
                    brushAt = point;
                    ShowBrush();
                    break;
                }
                if (tool == LevelTool.NavMesh)
                {
                    brushAt = point;
                    ShowNavMeshBrush();
                    break;
                }
                Axis? hot = selected != null && Editable(selected) ? gizmo.HandleAt(GizmoViewport, point) : null;
                if (hot != hotAxis)
                {
                    hotAxis = hot;
                    ShowGizmo();
                }
                break;
        }
        dragFrom = point;
    }

    void OnViewMouseUp(object sender, MouseButtonEventArgs e)
    {
        if (drag == Drag.Handle) EndHandleDrag();
        else if (drag == Drag.Sculpt) EndStroke();
        else if (drag == Drag.NavMesh) EndNavMeshStroke();
        else if (e.ChangedButton == MouseButton.Left && pressedAt is Point down && (e.GetPosition(ViewArea) - down).Length < 4) PickAt(down);
        if (drag == Drag.Look) CompositionTarget.Rendering -= Fly;
        drag = Drag.None;
        pressedAt = null;
        ViewArea.ReleaseMouseCapture();
    }

    void OnViewMouseWheel(object sender, MouseWheelEventArgs e)
    {
        camera.Position += camera.Forward * Math.Sign(e.Delta) * ViewScale * 0.12;
        UpdateCamera();
    }

    /// <summary>While looking around with the right button: WASD to fly, Q/E down and up, Shift faster.</summary>
    void Fly(object? sender, EventArgs e)
    {
        DateTime now = DateTime.Now;
        double seconds = Math.Min(0.1, (now - lastFrame).TotalSeconds);
        lastFrame = now;
        Vector3D way = new();
        if (Keyboard.IsKeyDown(Key.W)) way += camera.Forward;
        if (Keyboard.IsKeyDown(Key.S)) way -= camera.Forward;
        if (Keyboard.IsKeyDown(Key.D)) way += camera.Right;
        if (Keyboard.IsKeyDown(Key.A)) way -= camera.Right;
        if (Keyboard.IsKeyDown(Key.E)) way += new Vector3D(0, 1, 0);
        if (Keyboard.IsKeyDown(Key.Q)) way -= new Vector3D(0, 1, 0);
        if (way.Length < 1e-9) return;
        way.Normalize();
        double speed = 25 * (Keyboard.IsKeyDown(Key.LeftShift) || Keyboard.IsKeyDown(Key.RightShift) ? 5 : 1);
        camera.Position += way * speed * seconds;
        UpdateCamera();
    }

    /// <summary>The object a click hit (by the geometry hit, or in a batch by the triangle), in this file or the zone's others; null if none.</summary>
    ZoneObject? ObjectOf(RayMeshGeometry3DHitTestResult hit) =>
        scene?.ObjectOf(hit) ?? RelatedFiles().Select(file => file.Scene?.ObjectOf(hit)).FirstOrDefault(obj => obj != null);

    /// <summary>Picks the object under a point of the view (or nothing).</summary>
    void PickAt(Point point)
    {
        ZoneObject? obj = null;
        foreach (RayMeshGeometry3DHitTestResult hit in HitsAt(point, sceneContent, relatedContent, geometryContent, terrainContent))
        {
            if (hit.VisualHit == geometryContent || hit.VisualHit == terrainContent) break;  // level geometry or ground in front: nothing behind it is picked
            if (ObjectOf(hit) is ZoneObject found)
            {
                obj = found;
                break;
            }
        }
        PickInOutliner(obj);
    }

    /// <summary>Picks an object (or nothing) without moving the camera, showing it in the outliner too.</summary>
    void PickInOutliner(ZoneObject? obj)
    {
        if (obj != null && treeItems.TryGetValue(obj, out TreeViewItem? item))
        {
            selectingFromView = true;
            for (var folder = item.Parent as TreeViewItem; folder != null; folder = folder.Parent as TreeViewItem) folder.IsExpanded = true;
            item.IsSelected = true;
            item.BringIntoView();
            selectingFromView = false;
            if (selected != obj) Select(obj, focus: false);  // the outliner may be filtered
        }
        else
        {
            Select(obj, focus: false);
        }
    }

    // ---- Dragging the handles ------------------------------------------------------------------

    void BeginHandleDrag(Axis axis, Point point)
    {
        CommitEdit();  // typing before the drag is its own step
        double? start = tool == LevelTool.Move ? AlongAxis(point, axis, selected!.Placement.Position) : AngleAround(point, axis, selected!.Placement.Position);
        if (start == null) return;  // the axis points straight at the camera
        drag = Drag.Handle;
        hotAxis = axis;
        dragStart = selected.Placement;
        dragStartParameter = start.Value;
        ShowGizmo();
    }

    void DragHandle(Point point)
    {
        if (selected == null || hotAxis is not Axis axis) return;
        Vector3 direction = Gizmo.GameDirection(axis);
        if (tool == LevelTool.Move)
        {
            if (AlongAxis(point, axis, dragStart.Position) is not double along) return;
            double by = along - dragStartParameter;
            // Magnets first (along the arrow), else Snap.
            Vector3? magnet = Magnets ? MagnetSnap(selected, dragStart.Position + direction * (float)by, selected.Placement.Rotation, direction) : null;
            if (magnet == null && Snapping) by = Math.Round(by / GridSize) * GridSize;
            MoveSelected(magnet ?? dragStart.Position + direction * (float)by, fromInspector: false);
        }
        else
        {
            if (AngleAround(point, axis, dragStart.Position) is not double angle) return;
            double by = angle - dragStartParameter;
            while (by > Math.PI) by -= 2 * Math.PI;
            while (by < -Math.PI) by += 2 * Math.PI;
            if (Snapping) by = Math.Round(by / AngleStep) * AngleStep;
            TurnSelected(dragStart.Rotation * Matrix4x4.CreateFromAxisAngle(direction, (float)by), fromInspector: false);
        }
    }

    void EndHandleDrag()
    {
        drag = Drag.None;
        ShowMagnet(null);
        CommitEdit(tool == LevelTool.Move ? "Move" : "Turn");
    }

    double? AlongAxis(Point mouse, Axis axis, Vector3 through)
    {
        (Vector3 origin, Vector3 ray) = GameRay(mouse);
        return Gizmo.AlongAxis(origin, ray, through, Gizmo.GameDirection(axis));
    }

    double? AngleAround(Point mouse, Axis axis, Vector3 centre)
    {
        (Vector3 origin, Vector3 ray) = GameRay(mouse);
        return Gizmo.AngleAround(origin, ray, centre, Gizmo.GameDirection(axis));
    }

    // ---- Saving: every identical copy ----------------------------------------------------------

    List<(string Archive, string? Container)>? copies;

    /// <summary>
    /// Every copy of this zone that is the same as the game's own of the one opened. 119 zones are
    /// both directly in a zonescript_*.vpp_pc archive and inside a level container, identical; the
    /// game may read either, so an edit is saved to all of them. Needs the index of every file.
    /// </summary>
    List<(string Archive, string? Container)> Copies()
    {
        if (copies != null) return copies;
        byte[] mine = Original(archive, container);
        copies = [(archive, container)];
        foreach ((string a, string? c) in context.Assets.PlacesOf(zoneName).Distinct())
            if (!copies.Contains((a, c)) && Original(a, c).AsSpan().SequenceEqual(mine))
                copies.Add((a, c));
        // In one of our own archives (a level New level made: its zone loose and in its ns_base container, where
        // the game reads it), its containers are looked in directly, whether or not the index has them yet.
        if (Workspace.IsNewArchive(archive))
            foreach (PackfileEntry entry in Workspace.Archive(archive).Entries.Where(e => e.IsContainer).ToList())
            {
                if (copies.Contains((archive, entry.Name))) continue;
                try
                {
                    if (Original(archive, entry.Name).AsSpan().SequenceEqual(mine)) copies.Add((archive, entry.Name));
                }
                catch (Exception ex) when (ex is FileNotFoundException or InvalidDataException) { }
            }
        return copies;
    }

    /// <summary>The game's own copy of the zone in an archive or container.</summary>
    byte[] Original(string inArchive, string? inContainer)
    {
        if (inContainer == null) return Workspace.ReadOriginal(inArchive, zoneName);
        Packfile pack = Packfile.FromBytes(inContainer, Workspace.ReadOriginal(inArchive, inContainer));
        return pack.Read(pack.Find(zoneName) ?? throw new FileNotFoundException($"{inContainer} has no {zoneName}"));
    }

    /// <summary>Puts the zone into one place: an override of the file, or a rebuilt container; back to the game's own if it is the same.</summary>
    string Put(string inArchive, string? inContainer, byte[] bytes) => PutZoneFile(zoneName, inArchive, inContainer, bytes);

    /// <summary>Puts a zone file into one place: an override of the file, or a rebuilt container; back to the game's own if it is the same.</summary>
    string PutZoneFile(string name, string inArchive, string? inContainer, byte[] bytes)
    {
        if (inContainer != null)  // its sentence ends with a full stop; the places are listed with "; "
            return ContainerEdits.Put(Workspace, inArchive, inContainer, new Dictionary<string, byte[]> { [name] = bytes }).TrimEnd('.');
        // Loose in its archive: the asset lists giving its size (the game sets memory aside by it) follow it.
        int sizeBefore = Workspace.Read(inArchive, name).Length;
        string sized = ContainerEdits.UpdateLooseSize(Workspace, name, sizeBefore, bytes.Length) is { Count: > 0 } lists
            ? $" (its size updated in {string.Join(", ", lists)})" : "";
        if (bytes.AsSpan().SequenceEqual(Workspace.ReadOriginal(inArchive, name)))
        {
            Workspace.DeleteOverride(inArchive, name);
            return $"{name} in {inArchive} is the game's own" + sized;
        }
        return "saved " + Workspace.RelativeToMod(Workspace.WriteOverride(inArchive, name, bytes)) + sized;
    }

    void OnSave(object sender, RoutedEventArgs e) => Save();

    bool Save()
    {
        CommitEdit();
        if (!unsaved) return true;
        if (!context.Assets.IndexReady)
        {
            MessageBox.Show("The editor is still reading the game's files, to find every copy of this zone. Try again in a few seconds.",
                            "Level editor", MessageBoxButton.OK, MessageBoxImage.Information);
            return false;
        }
        // Objects with a model outside the zone's square: the game drops them (and what's attached to them).
        var outside = zone.Objects.Where(o => o.Property("chunk_name") != null && !IsGeometry(o) && AddedOrMoved(o) && !InZone(o.Placement.Position)).ToList();
        if (outside.Count > 0 &&
            MessageBox.Show($"{outside.Count} object(s) with a model are outside this zone, where the game drops them when the level loads " +
                            "(\"Mover is physically located outside its zone\"), with what is attached to them:\n\n" +
                            string.Join("\n", outside.Take(8).Select(o => $"•  {Label(o)} at {o.Placement.Position.X:0}, {o.Placement.Position.Z:0} ({ZoneAt(o.Placement.Position)})")) +
                            (outside.Count > 8 ? $"\n…and {outside.Count - 8} more" : "") +
                            "\n\nMove them into the zone (or delete them) first? Choose No to save anyway.",
                            "Level editor", MessageBoxButton.YesNo, MessageBoxImage.Warning) == MessageBoxResult.Yes)
        {
            PickInOutliner(outside[0]);
            return false;
        }
        Mouse.OverrideCursor = Cursors.Wait;
        try
        {
            using IDisposable batch = Workspace.Batch();  // our own archives (the Guerrilla world's) written once each
            string message = "";
            // Chunks brought in (see AddChunk) that the zone's objects still name: into its container first.
            var named = zone.Objects.Select(o => o.Property("chunk_name")?.Text).OfType<string>()
                            .Select(n => System.IO.Path.GetFileNameWithoutExtension(n)).ToHashSet(StringComparer.OrdinalIgnoreCase);
            var bringing = chunksToAdd.Values.Where(p => named.Contains(p.Chunk)).ToList();
            // ...into every level's container that loads the zone, where its world hasn't it.
            foreach (var to in ChunkContainers())
            {
                var here = bringing.Where(p => !HasChunk(p.Chunk, to)).ToList();
                if (here.Count == 0) continue;
                message += "Brought " + string.Join(", ", here.Select(p => p.Chunk)) + $" into {to.Container}: " +
                           ContainerEdits.AddFiles(Workspace, to.Archive, to.Container,
                                                   here.SelectMany(p => p.Files).ToList(),
                                                   here.SelectMany(p => p.Primitives).ToList(),
                                                   here.Select(p => p.Group).ToList()).TrimEnd('.') + ". ";
            }
            message += BringEffects();
            message += BringRigAnimations();
            byte[] bytes = ZoneBytesWithoutGeometry();  // (the level geometry goes into the stitch list: SaveGeometry)
            message += "Saved: " + string.Join("; ", Copies().Select(p => Put(p.Archive, p.Container, bytes))) + ".";
            message += SaveGeometry();
            if (SculptChanged) message += " " + SaveSculpt() + MoveWhatStandsOnGround();
            message += SaveFoliage();
            message += SaveBarriers();
            message += SaveGroundPaint();
            message += SaveLayerSwaps();
            message += SaveNavMesh();
            batch.Dispose();
            foreach (TerrainSculpt sculpted in sculptZones) sculpted.Saved();
            chunksToAdd.Clear();
            unsaved = false;
            SaveButton.IsEnabled = false;
            ShowSource();
            context.Log(message + " The game uses it from its next start through RFAModLauncher.");
            context.OverridesChanged();
            // A running game looks for files saved for the first time now (a loose file new in
            // overrides\ isn't used until it does: the level then read the game's own, smaller stitch
            // list into the room its asset list gave the saved one, and never finished loading).
            if (GameLink.IsRecent(gameLink.Read())) gameLink.TrySend(["editor_rescan"]);
            return true;
        }
        catch (Exception ex)
        {
            MessageBox.Show("Couldn't save: " + ex.Message, "Level editor", MessageBoxButton.OK, MessageBoxImage.Error);
            return false;
        }
        finally
        {
            Mouse.OverrideCursor = null;
        }
    }

    void OnRestore(object sender, RoutedEventArgs e)
    {
        if (!context.Assets.IndexReady) return;
        if (MessageBox.Show($"Go back to the game's own {zoneName}? Your changes to it, its objects and its ground, are undone (in every copy). " +
                            "Other changes in the same containers, such as replaced textures, stay.",
                            "Restore original", MessageBoxButton.YesNo, MessageBoxImage.Question) != MessageBoxResult.Yes)
            return;
        try
        {
            using (Workspace.Batch())
            {
                context.Log(string.Join("; ", Copies().Select(p => Put(p.Archive, p.Container, Original(p.Archive, p.Container)))) + ".");
                if (RestoreTerrain() is { Length: > 0 } ground) context.Log(ground);
                if (RestoreGeometry() is { Length: > 0 } geometryBack) context.Log(geometryBack);
            }
            // The ground as the game has it again: sculpting starts afresh from it.
            if (strokeBefore != null) EndStroke();
            sculpt = null;
            sculptZones = [];
            if (tool == LevelTool.Sculpt) SetTool(LevelTool.Move);
            chunksToAdd.Clear();
            unsaved = false;
            SaveButton.IsEnabled = false;
            undo.Clear();
            redo.Clear();
            UpdateUndoButtons();
            zone = ZoneFile.Read(ReadZone());
            NotePlacements();
            selected = null;
            StartEdit();
            ShowProperties();
            ShowSource();
            BuildObjectTree(FilterBox.Text.Trim());
            BuildGrid();
            BuildScene(frame: false);
            context.OverridesChanged();
        }
        catch (Exception ex)
        {
            MessageBox.Show("Couldn't restore: " + ex.Message, "Level editor", MessageBoxButton.OK, MessageBoxImage.Error);
        }
    }

    public bool ConfirmClose()
    {
        CommitEdit();
        if (!unsaved)
        {
            Closing();
            return true;
        }
        MessageBoxResult answer = MessageBox.Show($"Save your changes to {zoneName} in overrides\\?", "Level editor",
                                                  MessageBoxButton.YesNoCancel, MessageBoxImage.Question);
        bool close = answer == MessageBoxResult.No || (answer == MessageBoxResult.Yes && Save());
        if (close) Closing();
        return close;
    }
}
