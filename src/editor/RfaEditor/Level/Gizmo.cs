using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;
using System.Windows.Media.Media3D;

namespace RfaEditor;

/// <summary>What dragging in the level view does: pick objects only, move them, or turn them.</summary>
enum LevelTool { Select, Move, Rotate }

/// <summary>The game's axes: X east-west, Y up, Z north-south.</summary>
enum Axis { X, Y, Z }

/// <summary>
/// The handles drawn on the selected object, as in other 3D editors: three arrows to drag it along
/// X (red), Y (green) or Z (blue), or three rings to turn it around those axes. They are drawn in a
/// view of their own on top of the level, so walls never hide them, and keep the same size on
/// screen however far away the object is.
/// </summary>
sealed class Gizmo
{
    readonly Dictionary<GeometryModel3D, Axis> handles = [];

    public Model3DGroup Model { get; } = new();

    /// <summary>An axis's direction in WPF space (the game's Z is WPF's -Z).</summary>
    public static Vector3D Direction(Axis axis) => axis switch
    {
        Axis.X => new Vector3D(1, 0, 0),
        Axis.Y => new Vector3D(0, 1, 0),
        _ => new Vector3D(0, 0, -1),
    };

    /// <summary>An axis's direction in the game's space.</summary>
    public static System.Numerics.Vector3 GameDirection(Axis axis) => axis switch
    {
        Axis.X => System.Numerics.Vector3.UnitX,
        Axis.Y => System.Numerics.Vector3.UnitY,
        _ => System.Numerics.Vector3.UnitZ,
    };

    /// <param name="hot">The handle under the mouse (or being dragged), drawn highlighted.</param>
    public void Show(LevelTool tool, Point3D centre, double size, Axis? hot)
    {
        Model.Children.Clear();
        handles.Clear();
        if (tool == LevelTool.Select) return;
        foreach (Axis axis in new[] { Axis.X, Axis.Y, Axis.Z })
        {
            MeshGeometry3D mesh = tool == LevelTool.Move
                ? Arrow(centre, Direction(axis), size)
                : Ring(centre, Direction(axis), size);
            Color colour = axis == hot ? Color.FromRgb(255, 225, 60) : axis switch
            {
                Axis.X => Color.FromRgb(230, 70, 70),
                Axis.Y => Color.FromRgb(80, 210, 90),
                _ => Color.FromRgb(70, 130, 255),
            };
            var material = new MaterialGroup();
            material.Children.Add(new DiffuseMaterial(new SolidColorBrush(colour)));
            material.Children.Add(new EmissiveMaterial(new SolidColorBrush(Color.FromArgb(150, colour.R, colour.G, colour.B))));
            var handle = new GeometryModel3D(mesh, material) { BackMaterial = material };
            handles[handle] = axis;
            Model.Children.Add(handle);
        }
    }

    public void Hide()
    {
        Model.Children.Clear();
        handles.Clear();
    }

    /// <summary>The handle under a point of the gizmo's view, if any.</summary>
    public Axis? HandleAt(Viewport3D view, Point point)
    {
        Axis? found = null;
        VisualTreeHelper.HitTest(view, null, result =>
        {
            if (result is RayMeshGeometry3DHitTestResult hit && hit.ModelHit is GeometryModel3D model && handles.TryGetValue(model, out Axis axis))
            {
                found = axis;
                return HitTestResultBehavior.Stop;
            }
            return HitTestResultBehavior.Continue;
        }, new PointHitTestParameters(point));
        return found;
    }

    // ---- Dragging ------------------------------------------------------------------------------

    /// <summary>
    /// How far along an axis line the mouse is: the point of the line closest to the line from the
    /// camera through the mouse (the ray), measured from <paramref name="through"/>. Null if the
    /// axis points at the camera. All in the game's space.
    /// </summary>
    public static double? AlongAxis(System.Numerics.Vector3 rayOrigin, System.Numerics.Vector3 rayDirection,
                                    System.Numerics.Vector3 through, System.Numerics.Vector3 axis)
    {
        System.Numerics.Vector3 between = through - rayOrigin;
        double b = System.Numerics.Vector3.Dot(axis, rayDirection);
        double d = System.Numerics.Vector3.Dot(axis, between), e = System.Numerics.Vector3.Dot(rayDirection, between);
        double denominator = 1 - b * b;
        if (denominator < 1e-4) return null;
        return (b * e - d) / denominator;
    }

    /// <summary>
    /// The angle of the mouse around an axis through a centre: where the ray meets the plane square
    /// to the axis, measured around the axis (counter-clockwise looking down it, as in
    /// Matrix4x4.CreateFromAxisAngle). Null if that plane is edge-on. All in the game's space.
    /// </summary>
    public static double? AngleAround(System.Numerics.Vector3 rayOrigin, System.Numerics.Vector3 rayDirection,
                                      System.Numerics.Vector3 centre, System.Numerics.Vector3 axis)
    {
        float facing = System.Numerics.Vector3.Dot(rayDirection, axis);
        if (Math.Abs(facing) < 1e-4f) return null;
        System.Numerics.Vector3 hit = rayOrigin + rayDirection * (System.Numerics.Vector3.Dot(centre - rayOrigin, axis) / facing);
        System.Numerics.Vector3 u = Math.Abs(axis.Y) > 0.9f ? System.Numerics.Vector3.UnitX : System.Numerics.Vector3.UnitY;
        u = System.Numerics.Vector3.Normalize(u - axis * System.Numerics.Vector3.Dot(u, axis));
        System.Numerics.Vector3 v = System.Numerics.Vector3.Cross(axis, u);
        System.Numerics.Vector3 offset = hit - centre;
        return Math.Atan2(System.Numerics.Vector3.Dot(offset, v), System.Numerics.Vector3.Dot(offset, u));
    }

    // ---- Shapes --------------------------------------------------------------------------------

    /// <summary>Two directions at right angles to an axis and to each other.</summary>
    static (Vector3D U, Vector3D V) Across(Vector3D axis)
    {
        Vector3D helper = Math.Abs(axis.Y) < 0.9 ? new Vector3D(0, 1, 0) : new Vector3D(1, 0, 0);
        Vector3D u = Vector3D.CrossProduct(axis, helper);
        u.Normalize();
        Vector3D v = Vector3D.CrossProduct(axis, u);
        v.Normalize();
        return (u, v);
    }

    /// <summary>An arrow from the centre: a square shaft and a pointed head.</summary>
    static MeshGeometry3D Arrow(Point3D centre, Vector3D axis, double size)
    {
        var mesh = new MeshGeometry3D();
        (Vector3D u, Vector3D v) = Across(axis);
        double shaft = size * 0.035, head = size * 0.09;
        Point3D start = centre + axis * size * 0.12, neck = centre + axis * size * 0.75, tip = centre + axis * size;

        // The shaft: a long box.
        Point3D[] ring(Point3D at, double r) => [at + (u + v) * r, at + (u - v) * r, at + (-u - v) * r, at + (-u + v) * r];
        Point3D[] a = ring(start, shaft), b = ring(neck, shaft);
        for (int i = 0; i < 4; i++) Quad(mesh, a[i], a[(i + 1) % 4], b[(i + 1) % 4], b[i]);
        // The head: a four-sided point.
        Point3D[] c = ring(neck, head);
        for (int i = 0; i < 4; i++) Triangle(mesh, c[i], c[(i + 1) % 4], tip);
        Quad(mesh, c[3], c[2], c[1], c[0]);
        return mesh;
    }

    /// <summary>A ring around the centre, at right angles to the axis.</summary>
    static MeshGeometry3D Ring(Point3D centre, Vector3D axis, double size)
    {
        var mesh = new MeshGeometry3D();
        (Vector3D u, Vector3D v) = Across(axis);
        const int around = 64, sides = 6;
        double radius = size * 0.85, tube = size * 0.03;
        Point3D At(int i, int j)
        {
            double a = 2 * Math.PI * i / around, b = 2 * Math.PI * j / sides;
            Vector3D outward = u * Math.Cos(a) + v * Math.Sin(a);
            return centre + outward * (radius + tube * Math.Cos(b)) + axis * (tube * Math.Sin(b));
        }
        for (int i = 0; i < around; i++)
            for (int j = 0; j < sides; j++)
                Quad(mesh, At(i, j), At(i + 1, j), At(i + 1, j + 1), At(i, j + 1));
        return mesh;
    }

    static void Triangle(MeshGeometry3D mesh, Point3D a, Point3D b, Point3D c)
    {
        int first = mesh.Positions.Count;
        mesh.Positions.Add(a);
        mesh.Positions.Add(b);
        mesh.Positions.Add(c);
        mesh.TriangleIndices.Add(first);
        mesh.TriangleIndices.Add(first + 1);
        mesh.TriangleIndices.Add(first + 2);
    }

    static void Quad(MeshGeometry3D mesh, Point3D a, Point3D b, Point3D c, Point3D d)
    {
        Triangle(mesh, a, b, c);
        Triangle(mesh, a, c, d);
    }
}
