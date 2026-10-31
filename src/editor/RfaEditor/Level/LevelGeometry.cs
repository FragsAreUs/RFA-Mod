using System.Text.RegularExpressions;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using System.Windows.Media.Media3D;
using RfaFormats.Meshes;
using RfaFormats.Zones;

namespace RfaEditor;

/// <summary>
/// A zone's fixed level geometry in 3D: the stitch pieces its stitch list places (rock walls, cave
/// ceilings, ledges, mountains, roads, bridges, the Terraformer, ...), the decals it lays on them,
/// and its terrain (the ground of outdoor zones, its picture painted by <see cref="TerrainPainter"/>).
/// They are drawn with the zone's objects, but can't be picked or changed yet.
/// </summary>
static partial class LevelGeometry
{
    /// <summary>
    /// The names a zone or mission layer's own files go by, best first: world_08_02.rfgzone_pc and
    /// world_08_02_128.layer_pc both use world_08_02.stchlst_pc and world_08_02_0.ctmesh_pc, ...
    /// </summary>
    public static IEnumerable<string> ZoneNames(string zoneFileName)
    {
        string name = System.IO.Path.GetFileNameWithoutExtension(zoneFileName);
        yield return name;
        string zone = LayerNumber().Replace(name, "");
        if (zone != name) yield return zone;
    }

    /// <summary>
    /// The names of the zones around one on its grid (world_07_01 ... world_09_03 around
    /// world_08_02; dlc04_ zones likewise), whether or not the game has them; none for a zone
    /// that isn't on a grid (multiplayer maps, persistent zones).
    /// </summary>
    public static IEnumerable<string> Neighbours(string zone)
    {
        Match grid = GridName().Match(zone);
        if (!grid.Success) yield break;
        int column = int.Parse(grid.Groups[2].Value), row = int.Parse(grid.Groups[3].Value);
        for (int down = -1; down <= 1; down++)
            for (int across = -1; across <= 1; across++)
                if ((across != 0 || down != 0) && column + across >= 0 && row + down >= 0)
                    yield return $"{grid.Groups[1].Value}{column + across:D2}_{row + down:D2}";
    }

    /// <param name="model">A stitch piece's triangles and materials by its file name, or null if it can't be found.</param>
    /// <remarks>
    /// The pieces are joined into one mesh per texture (see <see cref="JoinedMeshes"/>): they can't
    /// be picked one at a time, and a zone's hundreds of pieces drawn separately cost WPF a draw
    /// call each, every frame.
    /// </remarks>
    public static Model3DGroup Build(StitchList list, Func<string, List<(MeshGeometry3D Mesh, Material Material)>?> model,
                                     out int drawn, out int missing)
    {
        var joined = new JoinedMeshes();
        drawn = missing = 0;
        foreach (StitchPiece piece in list.Pieces)
        {
            if (model(piece.Name + ".cstch_pc") is not { } parts)
            {
                missing++;
                continue;
            }
            joined.Add(parts, LevelScene.ViewMatrix(piece.Position, piece.Rotation));
            drawn++;
        }
        return joined.Build();
    }

    /// <summary>
    /// The decals a stitch list lays on surfaces (trash, blast marks, blood, signs): each drawn as
    /// its picture, see-through where the picture is, on a flat square across its right and
    /// forward directions, a few centimetres up from its middle so it shows above the surface it
    /// lies on. (The game projects them onto whatever is inside their box instead.)
    /// </summary>
    /// <param name="texture">A decal texture by name, with its see-through parts, or null if it can't be found.</param>
    public static Model3DGroup BuildDecals(StitchList list, Func<string, BitmapSource?> texture, out int drawn)
    {
        var decals = new Model3DGroup();
        var materials = new Dictionary<(string, float), Material?>();
        drawn = 0;
        foreach (StitchDecal decal in list.Decals)
        {
            float opacity = Math.Clamp(decal.Opacity, 0, 1);
            if (!materials.TryGetValue((decal.Texture, opacity), out Material? material))
            {
                BitmapSource? picture = texture(decal.Texture);
                material = picture == null ? null : ModelScene.Textured(picture, opacity);
                materials[(decal.Texture, opacity)] = material;
            }
            if (material == null) continue;

            // A square across right (x) and forward (z) in the decal's own space, lifted a little.
            float halfWidth = decal.Size.X / 2, halfLength = decal.Size.Z / 2;
            const float Lift = 0.03f;
            var mesh = new MeshGeometry3D();
            foreach ((float x, float z, double u, double v) in new[] { (-halfWidth, -halfLength, 0.0, 1.0), (halfWidth, -halfLength, 1.0, 1.0), (halfWidth, halfLength, 1.0, 0.0), (-halfWidth, halfLength, 0.0, 0.0) })
            {
                mesh.Positions.Add(new Point3D(x, Lift, -z));  // WPF's Z is the game's -Z
                mesh.TextureCoordinates.Add(new System.Windows.Point(u, v));
            }
            foreach (int index in new[] { 0, 1, 2, 0, 2, 3 }) mesh.TriangleIndices.Add(index);  // facing up
            mesh.Freeze();
            decals.Children.Add(new GeometryModel3D(mesh, material)
            {
                Transform = new MatrixTransform3D(LevelScene.ViewMatrix(decal.Position, decal.Rotation)),
            });
            drawn++;
        }
        if (decals.CanFreeze) decals.Freeze();
        return decals;
    }

    /// <summary>
    /// The corner of the zone the sub-zones are in (lowest x and z), where its blend and colour maps
    /// start. Zones sit on a 255 m grid, so it is the lowest sub-zone origin rounded down to a
    /// multiple of 255.
    /// </summary>
    public static System.Numerics.Vector2 ZoneCorner(IReadOnlyList<TerrainSubzone> subzones) => new(
        MathF.Floor(subzones.Min(s => s.Origin.X) / 255) * 255,
        MathF.Floor(subzones.Min(s => s.Origin.Y) / 255) * 255);

    /// <summary>A zone's terrain: the ground of its sub-zones that have any (caves mostly have none).</summary>
    /// <param name="picture">A picture of a sub-zone's ground over its box (x and z), or null to draw it plain.</param>
    /// <param name="inGameLight">For a sub-zone: what paints its picture for the Direct3D view's game light, if it differs (see TerrainPainter).</param>
    public static Model3DGroup BuildTerrain(IReadOnlyList<TerrainSubzone> subzones, Func<TerrainSubzone, BitmapSource?> picture,
                                            Func<TerrainSubzone, Func<BitmapSource>?>? inGameLight = null) =>
        BuildTerrain(subzones, subzone =>
        {
            System.Numerics.Vector3 min = subzone.BoundsMin, size = subzone.BoundsMax - subzone.BoundsMin;
            return (picture(subzone), p => new System.Windows.Point((p.X - min.X) / size.X, (p.Z - min.Z) / size.Z));
        }, inGameLight);

    /// <summary>
    /// A zone's terrain with one picture across its whole 255 m square from <paramref name="corner"/>
    /// (its colour map, which the game shows on far-away ground), or plain.
    /// </summary>
    public static Model3DGroup BuildTerrain(IReadOnlyList<TerrainSubzone> subzones, BitmapSource? zonePicture, System.Numerics.Vector2 corner) =>
        BuildTerrain(subzones, _ => (zonePicture, p => new System.Windows.Point((p.X - corner.X) / 255, (p.Z - corner.Y) / 255)));

    /// <param name="paint">For a sub-zone: its picture (or null, plain), and where a point of it is in the picture (0 to 1).</param>
    static Model3DGroup BuildTerrain(IReadOnlyList<TerrainSubzone> subzones,
                                     Func<TerrainSubzone, (BitmapSource? Picture, Func<System.Numerics.Vector3, System.Windows.Point> At)> paint,
                                     Func<TerrainSubzone, Func<BitmapSource>?>? inGameLight = null)
    {
        Material plain = ModelScene.Plain(Color.FromRgb(150, 138, 118));
        var terrain = new Model3DGroup();
        var materials = new Dictionary<BitmapSource, Material>();
        foreach (TerrainSubzone subzone in subzones)
        {
            (BitmapSource? image, var at) = paint(subzone);
            Material material = image == null ? plain : materials.TryGetValue(image, out Material? known) ? known : materials[image] = ModelScene.Textured(image);
            if (image != null && inGameLight?.Invoke(subzone) is Func<BitmapSource> paintInGameLight)
                material = materials[image] = Render.GameMaterial.WithPicture(paintInGameLight).Attach(material);
            var mesh = new MeshBuilder();
            bool withNormals = subzone.Normals.Length == subzone.Positions.Length;
            for (int i = 0; i < subzone.Positions.Length; i++)
            {
                System.Numerics.Vector3 p = subzone.Positions[i];
                var point = new Point3D(p.X, p.Y, -p.Z);  // WPF's Z is the game's -Z, as for models
                if (withNormals) mesh.AddVertex(point, new Vector3D(subzone.Normals[i].X, subzone.Normals[i].Y, -subzone.Normals[i].Z), at(p));
                else mesh.AddVertex(point, at(p));
            }
            // Flipping Z turns the triangles over: list each one's corners the other way round.
            for (int i = 0; i + 2 < subzone.Triangles.Length; i += 3)
                mesh.AddTriangle(subzone.Triangles[i], subzone.Triangles[i + 2], subzone.Triangles[i + 1]);
            terrain.Children.Add(new GeometryModel3D(mesh.ToMesh(), material) { BackMaterial = material });
        }
        terrain.Freeze();
        return terrain;
    }

    /// <summary>A mission layer's number after the zone's name, e.g. "_128" in world_08_02_128 (3 digits or more, so zone numbers stay).</summary>
    [GeneratedRegex(@"_\d{3,}$")]
    private static partial Regex LayerNumber();

    /// <summary>A zone on a grid: its prefix, column and row ("world_", "08", "02").</summary>
    [GeneratedRegex(@"^(.+_)(\d\d)_(\d\d)$")]
    private static partial Regex GridName();
}
