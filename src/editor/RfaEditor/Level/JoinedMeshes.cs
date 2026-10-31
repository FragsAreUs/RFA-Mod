using System.Windows.Media;
using System.Windows.Media.Media3D;

namespace RfaEditor;

/// <summary>
/// Many placed copies of meshes joined into a few large ones, one per texture: what can't be picked
/// one piece at a time (the zone's level geometry, the zones around it) draws much quicker as a
/// handful of meshes than as thousands of small ones, as each piece costs WPF a draw call every
/// frame.
/// </summary>
sealed class JoinedMeshes
{
    const int MostVertices = 250_000;  // a mesh that big is started afresh, to keep each one easy for WPF to draw

    readonly Dictionary<object, (MeshBuilder Mesh, Material Material)> joined = [];
    readonly List<(MeshBuilder Mesh, Material Material)> full = [];

    /// <summary>How many triangles have been added.</summary>
    public int Triangles { get; private set; }

    /// <summary>Adds a placed copy of a model's meshes (as <see cref="ModelScene.Parts"/> makes them).</summary>
    public void Add(IEnumerable<(MeshGeometry3D Mesh, Material Material)> parts, Matrix3D placement)
    {
        foreach ((MeshGeometry3D part, Material material) in parts)
        {
            object key = TextureOf(material);
            if (!joined.TryGetValue(key, out var target) || target.Mesh.VertexCount + part.Positions.Count > MostVertices)
            {
                if (target.Mesh != null) full.Add(target);
                joined[key] = target = (new MeshBuilder(), material);
            }
            target.Mesh.Add(part, placement);
            Triangles += part.TriangleIndices.Count / 3;
        }
    }

    /// <summary>The joined meshes, frozen.</summary>
    public Model3DGroup Build()
    {
        var group = new Model3DGroup();
        // See-through ones last: drawn before a wall behind them, they would hide it (WPF draws in order).
        foreach ((MeshBuilder mesh, Material material) in full.Concat(joined.Values).OrderBy(j => Render.GameMaterial.Of(j.Material)?.SeeThrough == true))
            group.Children.Add(new GeometryModel3D(mesh.ToMesh(), material) { BackMaterial = material });
        group.Freeze();
        return group;
    }

    /// <summary>
    /// What makes two materials look the same: their picture (each texture is read once), or their
    /// colour, and the game's material (bumps, shine, glow) they carry.
    /// </summary>
    static object TextureOf(Material material) => (material switch
    {
        DiffuseMaterial { Brush: ImageBrush { ImageSource: { } picture } } => (object)picture,
        DiffuseMaterial { Brush: SolidColorBrush { Color: var colour } } => colour,
        _ => material,
    }, Render.GameMaterial.Of(material));
}
