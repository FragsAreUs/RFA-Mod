using System.Windows.Media;
using System.Windows.Media.Media3D;
using RfaFormats.Meshes;

namespace RfaEditor;

/// <summary>
/// A skeleton (see <see cref="Rig"/>) in 3D, in its rest pose or posed by an animation: a small
/// block at every bone, and a thin bar from each bone to its parent, in bright colours so they show
/// inside a see-through model. Points are in the model's space (WPF's Z is the game's -Z, as for models). Helper bones
/// (where items are held, the camera, cutscene props) are left out: in the rest pose they sit at
/// the model's feet, as the game only places them while animating.
/// </summary>
static class SkeletonScene
{
    /// <summary>Whether a bone is a helper rather than part of the body (by its name: item1, camera-lookat, cutscene-item2, ...).</summary>
    public static bool IsHelper(RigBone bone) =>
        bone.Name.StartsWith("item", StringComparison.OrdinalIgnoreCase) || bone.Name.StartsWith("camera", StringComparison.OrdinalIgnoreCase) ||
        bone.Name.StartsWith("cutscene", StringComparison.OrdinalIgnoreCase);

    /// <param name="pose">Where each bone is (from <see cref="Animation.Pose"/>), or null for the rest pose.</param>
    public static Model3DGroup Build(Rig rig, IReadOnlyList<System.Numerics.Vector3>? pose = null)
    {
        // Sizes from the skeleton's own size in its rest pose, so a 2 m soldier and a 15 m monolith
        // both look right, and the blocks keep their size while it moves.
        var rest = rig.Bones.Select(b => b.Position).ToList();
        double height = rest.Count == 0 ? 1 : rest.Max(p => p.Y) - rest.Min(p => p.Y);
        var positions = (pose ?? rest).Select(ToView).ToList();
        double joint = Math.Max(0.004, height * 0.008), thickness = joint * 0.45;

        var joints = new MeshBuilder();
        var bars = new MeshBuilder();
        for (int i = 0; i < rig.Bones.Count; i++)
        {
            if (IsHelper(rig.Bones[i])) continue;
            AddBox(joints, positions[i], joint);
            if (rig.Bones[i].Parent >= 0) AddBar(bars, positions[rig.Bones[i].Parent], positions[i], thickness);
        }
        var group = new Model3DGroup();
        group.Children.Add(new GeometryModel3D(bars.ToMesh(), Glowing(Color.FromRgb(255, 170, 40))));
        group.Children.Add(new GeometryModel3D(joints.ToMesh(), Glowing(Color.FromRgb(255, 235, 120))));
        group.Freeze();
        return group;
    }

    static Point3D ToView(System.Numerics.Vector3 p) => new(p.X, p.Y, -p.Z);

    static Material Glowing(Color colour)
    {
        var material = new MaterialGroup();
        material.Children.Add(new DiffuseMaterial(new SolidColorBrush(colour)));
        material.Children.Add(new EmissiveMaterial(new SolidColorBrush(Color.FromRgb((byte)(colour.R / 2), (byte)(colour.G / 2), (byte)(colour.B / 2)))));
        material.Freeze();
        return material;
    }

    /// <summary>A cube of a size around a point.</summary>
    static void AddBox(MeshBuilder mesh, Point3D centre, double size)
    {
        double s = size / 2;
        AddCuboid(mesh, centre, new Vector3D(s, 0, 0), new Vector3D(0, s, 0), new Vector3D(0, 0, s));
    }

    /// <summary>A thin square bar from one point to another.</summary>
    static void AddBar(MeshBuilder mesh, Point3D from, Point3D to, double thickness)
    {
        Vector3D along = to - from;
        if (along.Length < 1e-6) return;
        Vector3D side = Vector3D.CrossProduct(along, Math.Abs(along.Y) < 0.9 * along.Length ? new Vector3D(0, 1, 0) : new Vector3D(1, 0, 0));
        side.Normalize();
        Vector3D up = Vector3D.CrossProduct(side, along);
        up.Normalize();
        AddCuboid(mesh, from + along / 2, along / 2, side * thickness / 2, up * thickness / 2);
    }

    /// <summary>A box around a centre, with three half-edges (its six faces, each two triangles, seen from both sides).</summary>
    static void AddCuboid(MeshBuilder mesh, Point3D centre, Vector3D a, Vector3D b, Vector3D c)
    {
        var corners = new Point3D[8];
        for (int i = 0; i < 8; i++)
            corners[i] = centre + ((i & 1) == 0 ? -a : a) + ((i & 2) == 0 ? -b : b) + ((i & 4) == 0 ? -c : c);
        int[][] faces = [[0, 2, 3, 1], [4, 5, 7, 6], [0, 1, 5, 4], [2, 6, 7, 3], [0, 4, 6, 2], [1, 3, 7, 5]];
        foreach (int[] face in faces)
        {
            int[] v = face.Select(i => mesh.AddVertex(corners[i], new System.Windows.Point(0, 0))).ToArray();
            mesh.AddTriangle(v[0], v[1], v[2]);
            mesh.AddTriangle(v[0], v[2], v[3]);
            mesh.AddTriangle(v[0], v[2], v[1]);  // and from the other side, so no face is missing whichever way it was wound
            mesh.AddTriangle(v[0], v[3], v[2]);
        }
    }
}
