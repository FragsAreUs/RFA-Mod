using System.Numerics;
using System.Windows.Media;
using System.Windows.Media.Media3D;
using RfaFormats.Meshes;

namespace RfaEditor;

/// <summary>
/// A zone's foliage in 3D: the debris its terrain file scatters (see <see cref="ZoneFoliage.Scatter"/>),
/// each piece set down on the surface below it and tilted to its slope as the game does, turned,
/// scaled and tinted. A zone has hundreds to thousands of pieces, so the pieces of one mesh and
/// about the same tint are joined into one mesh. They can't be picked or changed.
///
/// Pieces with no surface in their 1 m sub-cube are left out, as the game does. Some zones have many:
/// in world_08_02, 160 of 495 (most with the ground up to 1.75 m below their sub-cube); in the cave
/// world_05_00, 806 of 1,105 (nearly all just under the cave floor). The zone's mission layers and
/// the zones around it don't account for them.
/// </summary>
static class FoliageScene
{
    const int TintSteps = 8;  // tints are rounded to eighths, so pieces share a handful of materials

    /// <param name="model">A foliage mesh's triangles and materials by its file name, or null if it can't be found.</param>
    /// <param name="surfaces">What the pieces are set down on.</param>
    /// <param name="drawn">How many pieces were drawn.</param>
    /// <param name="missing">How many pieces weren't, as their mesh couldn't be found.</param>
    /// <param name="nothingBelow">How many had no surface in their sub-cube, and were left out.</param>
    public static Model3DGroup Build(IReadOnlyList<FoliageInstance> pieces, Func<string, List<(MeshGeometry3D Mesh, Material Material)>?> model,
                                     SurfaceFinder surfaces, out int drawn, out int missing, out int nothingBelow)
    {
        var joined = new Dictionary<(MeshGeometry3D Part, Color Tint), (MeshBuilder Mesh, Material Material)>();
        drawn = missing = nothingBelow = 0;
        foreach (FoliageInstance piece in pieces)
        {
            if (piece.Model == null || model(piece.Model.FileName) is not { } parts)
            {
                missing++;
                continue;
            }

            // Straight down from the top of its sub-cube to the first surface, as the game's ray
            // does (it reaches a millimetre past the sub-cube either way).
            Vector3 at = piece.Position;
            Matrix4x4 rotation = Matrix4x4.CreateScale(piece.Scale) * Matrix4x4.CreateRotationY(piece.Yaw);
            if (surfaces.Below(at.X, -at.Z, at.Y + 0.001, at.Y - piece.Drop - 0.001) is not var (height, normal))
            {
                nothingBelow++;  // the game leaves it out
                continue;
            }
            at.Y = (float)height;
            if (piece.Model.FollowsSlope) rotation *= Tilt(new Vector3((float)normal.X, (float)normal.Y, (float)-normal.Z));  // back to the game's Z
            Matrix3D placement = LevelScene.ViewMatrix(at, rotation);

            Color tint = Tint(piece.Tint);
            foreach ((MeshGeometry3D part, Material material) in parts)
            {
                if (!joined.TryGetValue((part, tint), out var target))
                    joined[(part, tint)] = target = (new MeshBuilder(), Tinted(material, tint));
                target.Mesh.Add(part, placement);
            }
            drawn++;
        }

        var foliage = new Model3DGroup();
        foreach ((MeshBuilder mesh, Material material) in joined.Values)
            foliage.Children.Add(new GeometryModel3D(mesh.ToMesh(), material) { BackMaterial = material });
        foliage.Freeze();
        return foliage;
    }

    /// <summary>The rotation that tips straight up over to a surface's normal (both in the game's space).</summary>
    static Matrix4x4 Tilt(Vector3 normal)
    {
        Vector3 axis = Vector3.Cross(Vector3.UnitY, normal);
        if (axis.Length() < 1e-6f) return Matrix4x4.Identity;
        float angle = MathF.Acos(Math.Clamp(Vector3.Dot(Vector3.UnitY, Vector3.Normalize(normal)), -1, 1));
        return Matrix4x4.CreateFromQuaternion(System.Numerics.Quaternion.CreateFromAxisAngle(Vector3.Normalize(axis), angle));
    }

    /// <summary>A tint as a colour, rounded to eighths. Its alpha isn't a see-through amount, so it is left out.</summary>
    static Color Tint(Vector4 tint)
    {
        static byte Step(float value) => (byte)(Math.Round(Math.Clamp(value, 0, 1) * TintSteps) / TintSteps * 255);
        return Color.FromRgb(Step(tint.X), Step(tint.Y), Step(tint.Z));
    }

    /// <summary>A material with its colour multiplied by a tint, as the game's foliage shader does.</summary>
    static Material Tinted(Material material, Color tint)
    {
        if (tint == Colors.White || material is not DiffuseMaterial diffuse) return material;
        var tinted = new DiffuseMaterial(diffuse.Brush) { Color = tint };
        tinted.Freeze();
        return tinted;
    }
}
