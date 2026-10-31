using System.Windows;
using System.Windows.Media.Media3D;

namespace RfaEditor;

/// <summary>
/// Finds the surface below a point among what is drawn (the terrain and the level geometry), the
/// way the game sets foliage down: straight down from a point, the first surface hit. A zone has
/// millions of triangles, so only those over the places asked about later are kept, sorted into
/// 4 m squares of the ground. Works in WPF's space (Z flipped), like the models it is given.
/// </summary>
sealed class SurfaceFinder
{
    const double SquareSize = 4;

    readonly Dictionary<(int X, int Z), List<Triangle>> squares = [];
    readonly double lowest, highest;

    readonly record struct Triangle(Point3D A, Point3D B, Point3D C);

    /// <param name="models">What to look through: groups, transforms and meshes, as drawn.</param>
    /// <param name="places">The places (x and z) that will be looked below; triangles over none of them are skipped.</param>
    /// <param name="lowest">The lowest height that will be looked at.</param>
    /// <param name="highest">The highest.</param>
    public SurfaceFinder(IEnumerable<Model3D?> models, IEnumerable<Point> places, double lowest, double highest)
    {
        this.lowest = lowest;
        this.highest = highest;
        foreach (Point place in places) squares.TryAdd(SquareOf(place.X, place.Y), []);
        foreach (Model3D? model in models)
            if (model != null) Add(model, Matrix3D.Identity);
    }

    /// <summary>
    /// The highest surface at (x, z) from <paramref name="top"/> down to <paramref name="bottom"/>:
    /// its height and which way it faces (up, whichever side was drawn), or null if there is none.
    /// </summary>
    public (double Height, Vector3D Normal)? Below(double x, double z, double top, double bottom)
    {
        if (!squares.TryGetValue(SquareOf(x, z), out List<Triangle>? triangles)) return null;
        (double Height, Triangle Triangle)? found = null;
        foreach (Triangle triangle in triangles)
            if (HeightAt(triangle, x, z) is double y && y <= top && y >= bottom && (found == null || y > found.Value.Height)) found = (y, triangle);
        if (found is not var (height, t)) return null;
        Vector3D normal = Vector3D.CrossProduct(t.B - t.A, t.C - t.A);
        normal.Normalize();
        return (height, normal.Y < 0 ? -normal : normal);
    }

    void Add(Model3D model, Matrix3D toWorld)
    {
        if (model.Transform is { } transform && !transform.Value.IsIdentity) toWorld = transform.Value * toWorld;
        if (model is Model3DGroup group)
        {
            foreach (Model3D child in group.Children) Add(child, toWorld);
        }
        else if (model is GeometryModel3D { Geometry: MeshGeometry3D mesh })
        {
            Rect3D bounds = new MatrixTransform3D(toWorld).TransformBounds(mesh.Bounds);
            if (bounds.IsEmpty || bounds.Y > highest || bounds.Y + bounds.SizeY < lowest || !AnySquareUnder(bounds)) return;
            var points = mesh.Positions.ToArray();
            toWorld.Transform(points);
            for (int i = 0; i + 2 < mesh.TriangleIndices.Count; i += 3)
                AddTriangle(new Triangle(points[mesh.TriangleIndices[i]], points[mesh.TriangleIndices[i + 1]], points[mesh.TriangleIndices[i + 2]]));
        }
    }

    void AddTriangle(Triangle t)
    {
        if (Math.Max(t.A.Y, Math.Max(t.B.Y, t.C.Y)) < lowest || Math.Min(t.A.Y, Math.Min(t.B.Y, t.C.Y)) > highest) return;
        (int x0, int z0) = SquareOf(Math.Min(t.A.X, Math.Min(t.B.X, t.C.X)), Math.Min(t.A.Z, Math.Min(t.B.Z, t.C.Z)));
        (int x1, int z1) = SquareOf(Math.Max(t.A.X, Math.Max(t.B.X, t.C.X)), Math.Max(t.A.Z, Math.Max(t.B.Z, t.C.Z)));
        for (int x = x0; x <= x1; x++)
            for (int z = z0; z <= z1; z++)
                if (squares.TryGetValue((x, z), out List<Triangle>? list)) list.Add(t);
    }

    bool AnySquareUnder(Rect3D bounds)
    {
        (int x0, int z0) = SquareOf(bounds.X, bounds.Z);
        (int x1, int z1) = SquareOf(bounds.X + bounds.SizeX, bounds.Z + bounds.SizeZ);
        return squares.Keys.Any(s => s.X >= x0 && s.X <= x1 && s.Z >= z0 && s.Z <= z1);
    }

    /// <summary>The height of a triangle at (x, z), or null if (x, z) isn't inside it seen from above.</summary>
    static double? HeightAt(Triangle t, double x, double z)
    {
        double d = (t.B.Z - t.C.Z) * (t.A.X - t.C.X) + (t.C.X - t.B.X) * (t.A.Z - t.C.Z);
        if (Math.Abs(d) < 1e-9) return null;  // standing on its edge
        double a = ((t.B.Z - t.C.Z) * (x - t.C.X) + (t.C.X - t.B.X) * (z - t.C.Z)) / d;
        double b = ((t.C.Z - t.A.Z) * (x - t.C.X) + (t.A.X - t.C.X) * (z - t.C.Z)) / d;
        double c = 1 - a - b;
        if (a < 0 || b < 0 || c < 0) return null;
        return a * t.A.Y + b * t.B.Y + c * t.C.Y;
    }

    static (int X, int Z) SquareOf(double x, double z) => ((int)Math.Floor(x / SquareSize), (int)Math.Floor(z / SquareSize));
}
