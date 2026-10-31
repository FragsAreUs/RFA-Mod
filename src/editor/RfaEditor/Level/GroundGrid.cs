using System.Numerics;
using System.Windows.Media;
using System.Windows.Media.Media3D;
using RfaFormats.Zones;

namespace RfaEditor;

/// <summary>
/// Lines on the ground under the whole zone, to judge where things are and how far they move:
/// every 10 metres, brighter every 50 (every 50 and 250 for very large zones), with the X and Z
/// axes drawn in their colours where they cross the zone. Drawn at the height most of the zone's
/// objects stand at or above.
/// </summary>
static class GroundGrid
{
    public static Model3D? Build(ZoneFile zone)
    {
        List<Vector3> positions = zone.Objects.Select(o => o.Placement.Position).ToList();
        if (positions.Count == 0) return null;

        // The height: of the lowest few objects, the highest (so a few deep ones don't pull it down).
        List<float> heights = positions.Select(p => p.Y).Order().ToList();
        float height = heights[heights.Count / 20];

        float minX = positions.Min(p => p.X), maxX = positions.Max(p => p.X);
        float minZ = positions.Min(p => p.Z), maxZ = positions.Max(p => p.Z);
        float extent = Math.Max(maxX - minX, maxZ - minZ);
        float step = extent > 1500 ? 50 : 10, major = step * 5;
        double width = Math.Clamp(extent / 3000, 0.04, 0.3);

        // Whole major squares around everything, with some room.
        float x0 = MathF.Floor((minX - step) / major) * major, x1 = MathF.Ceiling((maxX + step) / major) * major;
        float z0 = MathF.Floor((minZ - step) / major) * major, z1 = MathF.Ceiling((maxZ + step) / major) * major;

        MeshGeometry3D minorLines = new(), majorLines = new(), xAxis = new(), zAxis = new();
        for (float x = x0; x <= x1; x += step)
        {
            MeshGeometry3D mesh = x == 0 ? zAxis : x % major == 0 ? majorLines : minorLines;
            Line(mesh, new Vector3(x, height, z0), new Vector3(x, height, z1), x % major == 0 ? width * 2 : width);
        }
        for (float z = z0; z <= z1; z += step)
        {
            MeshGeometry3D mesh = z == 0 ? xAxis : z % major == 0 ? majorLines : minorLines;
            Line(mesh, new Vector3(x0, height, z), new Vector3(x1, height, z), z % major == 0 ? width * 2 : width);
        }

        var grid = new Model3DGroup();
        grid.Children.Add(Lines(minorLines, Color.FromRgb(0x3A, 0x40, 0x4B)));
        grid.Children.Add(Lines(majorLines, Color.FromRgb(0x50, 0x57, 0x66)));
        grid.Children.Add(Lines(xAxis, Color.FromRgb(0x9A, 0x40, 0x40)));
        grid.Children.Add(Lines(zAxis, Color.FromRgb(0x40, 0x5C, 0xA0)));
        grid.Freeze();
        return grid;
    }

    /// <summary>A line on the ground: a thin flat strip from one point to another (the game's space).</summary>
    static void Line(MeshGeometry3D mesh, Vector3 from, Vector3 to, double width)
    {
        Vector3 along = Vector3.Normalize(to - from);
        Vector3 side = new Vector3(-along.Z, 0, along.X) * (float)(width / 2);
        int first = mesh.Positions.Count;
        foreach (Vector3 corner in new[] { from - side, from + side, to + side, to - side })
            mesh.Positions.Add(new Point3D(corner.X, corner.Y, -corner.Z));  // WPF's Z is the game's -Z
        foreach (int index in new[] { 0, 1, 2, 0, 2, 3 }) mesh.TriangleIndices.Add(first + index);
    }

    /// <summary>Lines in one flat colour, seen from above and below, not shaded by the lights.</summary>
    static GeometryModel3D Lines(MeshGeometry3D mesh, Color colour)
    {
        var material = new MaterialGroup();
        material.Children.Add(new DiffuseMaterial(Brushes.Black));
        material.Children.Add(new EmissiveMaterial(new SolidColorBrush(colour)));
        return new GeometryModel3D(mesh, material) { BackMaterial = material };
    }
}
