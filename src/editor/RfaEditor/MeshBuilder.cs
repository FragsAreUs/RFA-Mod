using System.Windows;
using System.Windows.Media;
using System.Windows.Media.Media3D;

namespace RfaEditor;

/// <summary>
/// Builds a mesh from many pieces. Points, normals, texture coordinates and triangles are gathered
/// in plain lists and made into WPF's collections once, at the end: adding to those one point at a
/// time is slow, as each addition is checked and announced to whatever watches the collection.
/// </summary>
sealed class MeshBuilder
{
    readonly List<Point3D> positions = [];
    readonly List<Vector3D> normals = [];
    readonly List<Point> textureCoordinates = [];
    readonly List<int> triangles = [];

    public int VertexCount => positions.Count;

    /// <summary>Adds a point with its normal and texture coordinates; returns its number, for <see cref="AddTriangle"/>.</summary>
    public int AddVertex(Point3D position, Vector3D normal, Point textureCoordinate)
    {
        positions.Add(position);
        normals.Add(normal);
        textureCoordinates.Add(textureCoordinate);
        return positions.Count - 1;
    }

    /// <summary>Adds a point without a normal (for meshes that have none: WPF works them out).</summary>
    public int AddVertex(Point3D position, Point textureCoordinate)
    {
        positions.Add(position);
        textureCoordinates.Add(textureCoordinate);
        return positions.Count - 1;
    }

    public void AddTriangle(int a, int b, int c)
    {
        triangles.Add(a);
        triangles.Add(b);
        triangles.Add(c);
    }

    /// <summary>Adds a placed copy of a mesh (one with a normal for every point).</summary>
    public void Add(MeshGeometry3D part, Matrix3D placement)
    {
        int first = positions.Count;
        foreach (Point3D p in part.Positions) positions.Add(placement.Transform(p));
        foreach (Vector3D n in part.Normals)
        {
            Vector3D turned = placement.Transform(n);
            turned.Normalize();
            normals.Add(turned);
        }
        foreach (Point uv in part.TextureCoordinates) textureCoordinates.Add(uv);
        foreach (int index in part.TriangleIndices) triangles.Add(first + index);
    }

    /// <summary>
    /// The mesh, frozen (so WPF can keep it on the graphics card and share it), or with
    /// <paramref name="changing"/> left open, for meshes whose points move (animated characters).
    /// </summary>
    public MeshGeometry3D ToMesh(bool changing = false)
    {
        var mesh = new MeshGeometry3D
        {
            Positions = new Point3DCollection(positions),
            TextureCoordinates = new PointCollection(textureCoordinates),
            TriangleIndices = new Int32Collection(triangles),
        };
        if (normals.Count == positions.Count) mesh.Normals = new Vector3DCollection(normals);
        if (!changing) mesh.Freeze();
        return mesh;
    }
}
