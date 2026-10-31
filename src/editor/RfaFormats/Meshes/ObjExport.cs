using System.Globalization;
using System.Numerics;
using System.Text;

namespace RfaFormats.Meshes;

/// <summary>
/// Writes a model as Wavefront OBJ (+ MTL), which every 3D program opens (Blender, 3ds Max, Maya,
/// ...). The game's coordinates are flipped on Z for OBJ's right-handed convention, and V is turned
/// upside down for OBJ's texture coordinates; triangles keep facing outwards.
/// </summary>
public static class ObjExport
{
    /// <param name="submeshes">Which submeshes to write, each moved into place (e.g. <see cref="ModelFile.Intact"/>).</param>
    /// <param name="diffuseImages">Per material number, the image file to use as its colour texture (or null).</param>
    public static void Write(ModelFile model, IEnumerable<PlacedSubmesh> submeshes, string objPath, IReadOnlyDictionary<int, string?> diffuseImages)
    {
        var c = CultureInfo.InvariantCulture;
        string mtlPath = Path.ChangeExtension(objPath, ".mtl");
        var obj = new StringBuilder();
        obj.AppendLine($"# {model.Name} ({model.Kind}), exported by the RFA editor");
        obj.AppendLine($"mtllib {Path.GetFileName(mtlPath)}");

        // Each part gets its own copy of the vertices it uses, moved into place (parts can share a submesh).
        var materialsUsed = new SortedSet<int>();
        int number = 0, written = 0;
        foreach ((Submesh submesh, Matrix4x4 transform) in submeshes)
        {
            obj.AppendLine($"o part_{number++}");
            var index = new Dictionary<int, int>();
            int Vertex(int v)
            {
                if (index.TryGetValue(v, out int known)) return known;
                Vector3 p = Vector3.Transform(model.Positions[v], transform);
                Vector3 n = Vector3.Normalize(Vector3.TransformNormal(model.Normals[v], transform));
                Vector2 t = model.TextureCoordinates[v];
                obj.AppendLine(string.Format(c, "v {0} {1} {2}", p.X, p.Y, -p.Z));
                obj.AppendLine(string.Format(c, "vt {0} {1}", t.X, 1 - t.Y));
                obj.AppendLine(string.Format(c, "vn {0} {1} {2}", n.X, n.Y, -n.Z));
                return index[v] = ++written;
            }
            foreach (MeshPart part in submesh.Parts)
            {
                materialsUsed.Add(part.Material);
                var faces = new List<string>();
                for (int i = 0; i + 2 < part.Triangles.Length; i += 3)
                {
                    // Flipping Z mirrors the model, so each triangle's corners are listed the other way round.
                    int a = Vertex(part.Triangles[i]), b = Vertex(part.Triangles[i + 2]), d = Vertex(part.Triangles[i + 1]);
                    faces.Add($"f {a}/{a}/{a} {b}/{b}/{b} {d}/{d}/{d}");
                }
                obj.AppendLine($"usemtl material_{part.Material}");
                foreach (string face in faces) obj.AppendLine(face);
            }
        }

        var mtl = new StringBuilder();
        foreach (int material in materialsUsed)
        {
            mtl.AppendLine($"newmtl material_{material}");
            mtl.AppendLine("Kd 1 1 1");
            if (material < model.Materials.Count)
                foreach (var (_, texture) in model.Materials[material].Textures)
                    mtl.AppendLine($"# uses {texture}");
            if (diffuseImages.GetValueOrDefault(material) is string image) mtl.AppendLine($"map_Kd {image}");
            mtl.AppendLine();
        }
        File.WriteAllText(objPath, obj.ToString());
        File.WriteAllText(mtlPath, mtl.ToString());
    }
}
