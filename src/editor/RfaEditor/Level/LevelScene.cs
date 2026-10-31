using System.Numerics;
using System.Windows.Media;
using System.Windows.Media.Media3D;
using RfaFormats.Zones;

namespace RfaEditor;

/// <summary>
/// A zone in 3D: each object that names a model (buildings and props by "chunk_name", items and
/// weapons through the game's tables; see <see cref="ObjectModels"/>) is drawn with that model
/// where the zone places it; every other object is a small cube coloured by its kind (see
/// <see cref="ObjectKind"/>). Clicking an object picks it, whole kinds can be hidden, and objects
/// can be moved, added and taken away.
///
/// For speed, objects are drawn in batches: all the objects of one kind drawn with one texture in
/// one 64 m square of the map are joined into one mesh, since WPF spends a draw call on every piece
/// every frame, and a zone has thousands of objects. A click on a batch finds the object by the
/// triangle hit.
/// An object that moves (or is added) leaves its batch: its triangles there are squashed to
/// nothing, and from then on it is drawn on its own.
/// </summary>
sealed class LevelScene
{
    const int MostVertices = 250_000;  // a batch that big is started afresh, to keep each mesh easy for WPF to draw
    const double BatchSquare = 64;     // batches are also split by 64 m squares of the map, so a click only looks through those near it

    readonly Dictionary<ZoneObject, Model3DGroup> pieceOf = [];          // every object's own geometry, placed
    readonly Dictionary<ZoneObject, ObjectKind> kindOf = [];
    readonly HashSet<ZoneObject> drawnWithModel = [];
    readonly HashSet<ZoneObject> loose = [];                             // drawn on their own (moved or added)
    readonly Dictionary<Model3D, ZoneObject> looseGeometry = [];         // a loose object's geometry -> the object
    readonly List<Batch> batches = [];
    readonly Dictionary<GeometryModel3D, Batch> batchOf = [];
    readonly Dictionary<ZoneObject, List<Batch>> batchesOf = [];
    readonly ZoneNames names;
    readonly bool showModels;
    readonly Func<ZoneObject, IReadOnlyList<ObjectPart>?> modelOf;
    readonly Func<string, uint?, List<(MeshGeometry3D Mesh, Material Material)>?> model;
    readonly Dictionary<ObjectKind, Material> kindMaterials = [];
    Func<ObjectKind, bool> shown = _ => true;

    public Model3DGroup Root { get; } = new();
    public int ModelsPlaced { get; private set; }
    public int ModelsMissing { get; private set; }

    static readonly MeshGeometry3D Cube = MakeCube();

    /// <summary>
    /// One batch: the objects of a kind drawn with one texture, joined into one mesh, and which
    /// object each run of its points belongs to.
    /// </summary>
    sealed class Batch
    {
        public required ObjectKind Kind { get; init; }
        public required bool Models { get; init; }  // drawn with models (else cubes)
        public required GeometryModel3D Geometry { get; init; }
        public readonly List<(int First, int Count, ZoneObject Object)> Runs = [];

        /// <summary>The object a point of the mesh belongs to.</summary>
        public ZoneObject? ObjectAt(int vertex)
        {
            int low = 0, high = Runs.Count - 1;
            while (low <= high)
            {
                int middle = (low + high) / 2;
                (int first, int count, ZoneObject obj) = Runs[middle];
                if (vertex < first) high = middle - 1;
                else if (vertex >= first + count) low = middle + 1;
                else return obj;
            }
            return null;
        }

        /// <summary>Takes an object out of the batch: its points all moved onto one, so its triangles have no area and can't be seen or hit.</summary>
        public void Squash(ZoneObject obj)
        {
            var mesh = (MeshGeometry3D)Geometry.Geometry;
            Point3D[] positions = [.. mesh.Positions];
            foreach ((int first, int count, ZoneObject owner) in Runs)
                if (owner == obj)
                    for (int i = first; i < first + count; i++) positions[i] = positions[first];
            var squashed = new MeshGeometry3D
            {
                Positions = new Point3DCollection(positions),
                Normals = mesh.Normals, TextureCoordinates = mesh.TextureCoordinates, TriangleIndices = mesh.TriangleIndices,
            };
            squashed.Freeze();
            Geometry.Geometry = squashed;
        }
    }

    /// <summary>The model an object places (its "chunk_name", a .cchk_pc file), if it places one.</summary>
    public static string? ModelOf(ZoneObject obj) =>
        obj.Property("chunk_name") is { Type: ZoneProperty.TextType } c && c.Text.EndsWith(".cchk_pc", StringComparison.OrdinalIgnoreCase)
            ? c.Text : null;

    /// <summary>Which object in its model file an object is (its "uid"), or null if it doesn't say.</summary>
    public static uint? UidOf(ZoneObject obj) => obj.Property("uid") is { Size: 4 } uid ? BitConverter.ToUInt32(uid.Value) : null;

    /// <param name="modelOf">The models an object is drawn with (most one; a turret three), or null if none.</param>
    /// <param name="model">
    /// The geometry of a model by its file name and, for files with several objects, which one
    /// (the zone object's uid); or null if it can't be found.
    /// </param>
    public LevelScene(ZoneFile zone, ZoneNames names, bool showModels, Func<ZoneObject, IReadOnlyList<ObjectPart>?> modelOf,
                      Func<string, uint?, List<(MeshGeometry3D Mesh, Material Material)>?> model)
    {
        this.names = names;
        this.showModels = showModels;
        this.modelOf = modelOf;
        this.model = model;
        foreach (ZoneObject obj in zone.Objects) MakePiece(obj);
        BuildBatches();
        ShowOnly(shown);
    }

    /// <summary>An object's own geometry, placed: its models (or a cube), in a group moved to where the zone puts it.</summary>
    void MakePiece(ZoneObject obj)
    {
        var piece = new Model3DGroup();
        ObjectKind kind = ObjectKind.Of(names.Class(obj.ClassHash), ModelOf(obj) != null);
        var drawnWith = showModels ? modelOf(obj) : null;
        var parts = drawnWith?.Select(part => (part.Placement, Geometry: model(part.File, part.Uid))).ToList();
        if (parts != null && parts.Count > 0 && parts.All(p => p.Geometry != null))
        {
            foreach ((Matrix4x4 placement, var geometry) in parts)
            {
                // Each model where it goes on the object (a turret's middle and top sit on its base).
                var group = placement.IsIdentity ? piece : new Model3DGroup { Transform = new MatrixTransform3D(ViewMatrix(placement)) };
                foreach ((MeshGeometry3D mesh, Material material) in geometry!)
                    group.Children.Add(new GeometryModel3D(mesh, material) { BackMaterial = material });
                if (group != piece) piece.Children.Add(group);
            }
            drawnWithModel.Add(obj);
            ModelsPlaced++;
        }
        else
        {
            if (drawnWith != null) ModelsMissing++;
            if (!kindMaterials.TryGetValue(kind, out Material? material))
                kindMaterials[kind] = material = ModelScene.Plain(kind.Colour);
            piece.Children.Add(new GeometryModel3D(Cube, material) { BackMaterial = material });
        }
        pieceOf[obj] = piece;
        kindOf[obj] = kind;
        SetPlacement(obj);
    }

    /// <summary>Joins every object that isn't loose into batches: one per kind, way of drawing (models or cubes) and texture.</summary>
    void BuildBatches()
    {
        var building = new Dictionary<(ObjectKind Kind, bool Models, object Texture, int X, int Z), (MeshBuilder Mesh, Material Material, List<(int, int, ZoneObject)> Runs)>();
        void Finish((ObjectKind Kind, bool Models, object Texture, int X, int Z) key)
        {
            (MeshBuilder mesh, Material material, var runs) = building[key];
            var geometry = new GeometryModel3D(mesh.ToMesh(), material) { BackMaterial = material };
            var batch = new Batch { Kind = key.Kind, Models = key.Models, Geometry = geometry };
            batch.Runs.AddRange(runs);
            batches.Add(batch);
            batchOf[geometry] = batch;
            foreach ((_, _, ZoneObject obj) in runs)
            {
                if (!batchesOf.TryGetValue(obj, out var list)) batchesOf[obj] = list = [];
                if (!list.Contains(batch)) list.Add(batch);
            }
            building.Remove(key);
        }
        foreach ((ZoneObject obj, Model3DGroup piece) in pieceOf)
        {
            if (loose.Contains(obj)) continue;
            bool models = drawnWithModel.Contains(obj);
            Vector3 at = obj.Placement.Position;
            int x = (int)Math.Floor(at.X / BatchSquare), z = (int)Math.Floor(at.Z / BatchSquare);
            foreach ((GeometryModel3D geometry, Matrix3D placement) in Placed(piece, Matrix3D.Identity))
            {
                var mesh = (MeshGeometry3D)geometry.Geometry;
                var key = (kindOf[obj], models, TextureOf(geometry.Material), x, z);
                if (building.TryGetValue(key, out var open) && open.Mesh.VertexCount + mesh.Positions.Count > MostVertices) Finish(key);
                if (!building.TryGetValue(key, out open)) building[key] = open = (new MeshBuilder(), geometry.Material, []);
                int first = open.Mesh.VertexCount;
                open.Mesh.Add(mesh, placement);
                open.Runs.Add((first, mesh.Positions.Count, obj));
            }
        }
        foreach (var key in building.Keys.ToList()) Finish(key);
        // See-through batches last: drawn before what is behind them, they would hide it (WPF draws in order).
        var ordered = batches.OrderBy(b => Render.GameMaterial.Of(b.Geometry.Material)?.SeeThrough == true).ToList();
        batches.Clear();
        batches.AddRange(ordered);
    }

    /// <summary>Every piece of geometry in a group, at any depth, with where it is in the world.</summary>
    static IEnumerable<(GeometryModel3D Geometry, Matrix3D Placement)> Placed(Model3D model, Matrix3D parent)
    {
        Matrix3D placement = model.Transform is { } transform && !transform.Value.IsIdentity ? transform.Value * parent : parent;
        if (model is GeometryModel3D geometry) yield return (geometry, placement);
        else if (model is Model3DGroup group)
            foreach (Model3D child in group.Children)
                foreach (var placed in Placed(child, placement)) yield return placed;
    }

    /// <summary>What makes two materials look the same: their picture (each texture is read once) or colour, and the game material they carry.</summary>
    static object TextureOf(Material material) => (material switch
    {
        DiffuseMaterial { Brush: ImageBrush { ImageSource: { } picture } } => (object)picture,
        DiffuseMaterial { Brush: SolidColorBrush { Color: var colour } } => colour,
        _ => material,
    }, Render.GameMaterial.Of(material));

    /// <summary>Takes an object out of its batches, to be drawn on its own (before it moves).</summary>
    void Loosen(ZoneObject obj)
    {
        if (!pieceOf.TryGetValue(obj, out Model3DGroup? piece) || !loose.Add(obj)) return;
        if (batchesOf.Remove(obj, out var inBatches))
            foreach (Batch batch in inBatches) batch.Squash(obj);
        foreach ((GeometryModel3D geometry, _) in Placed(piece, Matrix3D.Identity)) looseGeometry[geometry] = obj;
        if (shown(kindOf[obj])) Root.Children.Add(piece);
    }

    /// <summary>Draws an object added to the zone (or put back), on its own.</summary>
    public void Add(ZoneObject obj)
    {
        MakePiece(obj);
        Loosen(obj);
    }

    /// <summary>Stops drawing an object (one taken out of the zone).</summary>
    public void Remove(ZoneObject obj)
    {
        if (!pieceOf.ContainsKey(obj)) return;
        Loosen(obj);
        Model3DGroup piece = pieceOf[obj];
        Root.Children.Remove(piece);
        foreach ((GeometryModel3D geometry, _) in Placed(piece, Matrix3D.Identity)) looseGeometry.Remove(geometry);
        loose.Remove(obj);
        pieceOf.Remove(obj);
        if (drawnWithModel.Remove(obj)) ModelsPlaced--;
        kindOf.Remove(obj);
    }

    /// <summary>The objects drawn with their models (not as cubes), where they are now, shown or not.</summary>
    public IEnumerable<Model3D> Models =>
        batches.Where(b => b.Models).Select(b => (Model3D)b.Geometry).Concat(loose.Where(drawnWithModel.Contains).Select(obj => pieceOf[obj]));

    /// <summary>The object a click hit: by the geometry hit, or for a batch, by the triangle.</summary>
    public ZoneObject? ObjectOf(RayMeshGeometry3DHitTestResult hit)
    {
        if (looseGeometry.TryGetValue(hit.ModelHit, out ZoneObject? obj)) return obj;
        return hit.ModelHit is GeometryModel3D geometry && batchOf.TryGetValue(geometry, out Batch? batch) ? batch.ObjectAt(hit.VertexIndex1) : null;
    }

    public ObjectKind KindOf(ZoneObject obj) => kindOf[obj];

    /// <summary>How many objects of a kind the zone has.</summary>
    public int Count(ObjectKind kind) => kindOf.Values.Count(k => k == kind);

    /// <summary>Draws only the objects of the kinds chosen; the others can't be seen or clicked.</summary>
    public void ShowOnly(Func<ObjectKind, bool> shown)
    {
        this.shown = shown;
        Root.Children.Clear();
        foreach (Batch batch in batches)
            if (shown(batch.Kind)) Root.Children.Add(batch.Geometry);
        foreach (ZoneObject obj in loose)
            if (shown(kindOf[obj])) Root.Children.Add(pieceOf[obj]);
    }

    /// <summary>
    /// Puts an object where the zone now says (after it moved or turned, or a property changed),
    /// taking it out of its batch first.
    /// </summary>
    public void Place(ZoneObject obj)
    {
        Loosen(obj);
        SetPlacement(obj);
    }

    /// <summary>
    /// Draws an object somewhere else than the zone says (where the running game has it now),
    /// without changing the zone; <see cref="Place"/> puts it back.
    /// </summary>
    public void PlaceAt(ZoneObject obj, Vector3 position, Matrix4x4 rotation)
    {
        Loosen(obj);
        if (pieceOf.TryGetValue(obj, out Model3DGroup? piece)) piece.Transform = new MatrixTransform3D(ViewMatrix(position, rotation));
    }

    /// <summary>
    /// Puts an object's piece where the zone says: turned by its rotation (whose rows are its right,
    /// up and forward directions), then moved to its position. WPF is right-handed and the game
    /// isn't, so Z is flipped, as for the models themselves.
    /// </summary>
    void SetPlacement(ZoneObject obj)
    {
        if (!pieceOf.TryGetValue(obj, out Model3DGroup? piece)) return;
        (Vector3 p, Matrix4x4 r) = obj.Placement;
        piece.Transform = new MatrixTransform3D(ViewMatrix(p, r));
    }

    /// <summary>A placement in the game's space (rotation rows: right, up, forward) as a WPF transform, Z flipped.</summary>
    public static Matrix3D ViewMatrix(Vector3 p, Matrix4x4 r) => new(
        r.M11, r.M12, -r.M13, 0,
        r.M21, r.M22, -r.M23, 0,
        -r.M31, -r.M32, r.M33, 0,
        p.X, p.Y, -p.Z, 1);

    /// <summary>A placement as one matrix (rotation rows, then position in the last row) as a WPF transform, Z flipped.</summary>
    public static Matrix3D ViewMatrix(Matrix4x4 placement) => ViewMatrix(placement.Translation, placement with { M41 = 0, M42 = 0, M43 = 0 });

    /// <summary>A see-through yellow box around an object's bounding box, to show it is selected.</summary>
    public static Model3D SelectionBox(ZoneObject obj)
    {
        Vector3 min = obj.BoundsMin, max = obj.BoundsMax;
        var size = new Vector3D(Math.Max(0.3, max.X - min.X), Math.Max(0.3, max.Y - min.Y), Math.Max(0.3, max.Z - min.Z));
        var centre = new Vector3D((min.X + max.X) / 2, (min.Y + max.Y) / 2, -(min.Z + max.Z) / 2);
        var material = new MaterialGroup();
        material.Children.Add(new DiffuseMaterial(new SolidColorBrush(Color.FromArgb(70, 255, 220, 0))));
        material.Children.Add(new EmissiveMaterial(new SolidColorBrush(Color.FromArgb(90, 255, 200, 0))));
        var transform = new Transform3DGroup();
        transform.Children.Add(new ScaleTransform3D(size.X / 0.6, size.Y / 0.6, size.Z / 0.6));
        transform.Children.Add(new TranslateTransform3D(centre));
        return new GeometryModel3D(Cube, material) { BackMaterial = material, Transform = transform };
    }

    /// <summary>A 0.6 m cube around the origin.</summary>
    static MeshGeometry3D MakeCube()
    {
        const double s = 0.3;
        var mesh = new MeshGeometry3D();
        Point3D[] corners =
        [
            new(-s, -s, -s), new(s, -s, -s), new(s, s, -s), new(-s, s, -s),
            new(-s, -s, s), new(s, -s, s), new(s, s, s), new(-s, s, s),
        ];
        int[][] faces = [[0, 3, 2, 1], [4, 5, 6, 7], [0, 1, 5, 4], [2, 3, 7, 6], [1, 2, 6, 5], [0, 4, 7, 3]];
        foreach (int[] face in faces)
        {
            int first = mesh.Positions.Count;
            foreach (int corner in face) mesh.Positions.Add(corners[corner]);
            foreach (int index in new[] { 0, 1, 2, 0, 2, 3 }) mesh.TriangleIndices.Add(first + index);
        }
        mesh.Freeze();
        return mesh;
    }
}
