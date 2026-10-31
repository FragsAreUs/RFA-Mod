using System.Numerics;
using System.Windows.Media;
using System.Windows.Media.Media3D;

namespace RfaEditor;

/// <summary>
/// Lines between linked objects (see <see cref="RfaFormats.Zones.ZoneLinks"/>): thin square tubes
/// from one object to the other, seen from any side, with an arrowhead three fifths of the way along
/// pointing at the object named. Each end is as thick as a few pixels at its distance from the
/// camera, so a line looks the same width on the screen near and far (the lines are made again as
/// the camera moves). Unlit, in flat colours, so they show in the dark of game light.
/// </summary>
static class LinkLines
{
    /// <param name="lines">Each line's ends (the game's space) and colour.</param>
    /// <param name="eye">The camera (the game's space).</param>
    /// <param name="perMetre">How many metres one pixel covers at one metre from the camera.</param>
    /// <param name="pixels">How wide the lines look, in pixels.</param>
    public static Model3D Build(IEnumerable<(Vector3 From, Vector3 To, Color Colour)> lines, Vector3 eye, double perMetre, double pixels)
    {
        var meshes = new Dictionary<Color, MeshGeometry3D>();
        float Half(Vector3 at) => (float)(pixels / 2 * perMetre * Math.Max(Vector3.Distance(at, eye), 0.2));
        foreach (var (from, to, colour) in lines)
        {
            float length = Vector3.Distance(from, to);
            if (length < 0.05f) continue;
            if (!meshes.TryGetValue(colour, out MeshGeometry3D? mesh)) meshes[colour] = mesh = new MeshGeometry3D();
            Vector3 along = (to - from) / length;
            Vector3 side = Vector3.Normalize(Vector3.Cross(along, MathF.Abs(along.Y) > 0.9f ? Vector3.UnitX : Vector3.UnitY));
            Vector3 other = Vector3.Cross(along, side);
            Tube(mesh, from, to, Half(from), Half(to), side, other);
            // The arrowhead: three fifths of the way, about four times as wide as the line, and longer than wide.
            Vector3 middle = from + along * (length * 0.6f);
            float width = Half(middle) * 4, head = Math.Min(width * 3, length * 0.25f);
            Pyramid(mesh, middle - along * (head / 2), middle + along * (head / 2), side * width, other * width);
        }
        var group = new Model3DGroup();
        foreach (var (colour, mesh) in meshes)
        {
            mesh.Freeze();
            var material = new MaterialGroup();
            material.Children.Add(new DiffuseMaterial(Brushes.Black));
            material.Children.Add(new EmissiveMaterial(new SolidColorBrush(colour)));
            material.Freeze();
            group.Children.Add(new GeometryModel3D(mesh, material) { BackMaterial = material });
        }
        group.Freeze();
        return group;
    }

    static Point3D View(Vector3 p) => new(p.X, p.Y, -p.Z);  // WPF's Z is the game's -Z

    /// <summary>Four long sides from one end to the other, each end its own thickness.</summary>
    static void Tube(MeshGeometry3D mesh, Vector3 from, Vector3 to, float halfFrom, float halfTo, Vector3 a, Vector3 b)
    {
        Vector3[] corners = [a + b, a - b, -a - b, -a + b];
        int first = mesh.Positions.Count;
        foreach (Vector3 corner in corners)
        {
            mesh.Positions.Add(View(from + corner * halfFrom));
            mesh.Positions.Add(View(to + corner * halfTo));
        }
        for (int i = 0; i < 4; i++)
        {
            int j = (i + 1) % 4;
            foreach (int index in new[] { 2 * i, 2 * i + 1, 2 * j + 1, 2 * i, 2 * j + 1, 2 * j })
                mesh.TriangleIndices.Add(first + index);
        }
    }

    /// <summary>A four-sided point from a square base to a tip.</summary>
    static void Pyramid(MeshGeometry3D mesh, Vector3 back, Vector3 tip, Vector3 a, Vector3 b)
    {
        Vector3[] corners = [a + b, a - b, -a - b, -a + b];
        int first = mesh.Positions.Count;
        foreach (Vector3 corner in corners) mesh.Positions.Add(View(back + corner));
        mesh.Positions.Add(View(tip));
        for (int i = 0; i < 4; i++)
        {
            mesh.TriangleIndices.Add(first + i);
            mesh.TriangleIndices.Add(first + (i + 1) % 4);
            mesh.TriangleIndices.Add(first + 4);
        }
        foreach (int index in new[] { 0, 2, 1, 0, 3, 2 }) mesh.TriangleIndices.Add(first + index);  // the base
    }
}
