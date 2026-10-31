using System.Numerics;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using System.Windows.Media.Media3D;
using RfaFormats.Meshes;

namespace RfaEditor;

/// <summary>
/// A character posed by an animation, for the preview to show frame after frame: its model with
/// every vertex moved by its bones (see <see cref="ModelFile.SkinBones"/>), and its skeleton, or the
/// skeleton alone when there is no model. The model's geometry is built once; each frame only the
/// vertex positions and normals change.
///
/// Skinning: a bone's pose (from <see cref="Animation.Pose"/>) takes a point from the bone's own
/// space to the model's, so a vertex moves to the sum, over its bones, of weight × (vertex − the
/// bone's rest position) × pose. In the rest pose every bone's matrix comes out as no change.
/// </summary>
sealed class AnimatedScene
{
    readonly Rig rig;
    readonly ModelFile? model;
    readonly List<Piece> pieces = [];
    readonly Model3DGroup group = new();
    readonly int[] skinBones = [];       // the bones moving each vertex (4 each), as numbers in the skeleton
    readonly Vector3[] skinned;          // the model's vertices this frame
    readonly Vector3[] skinnedNormals;

    /// <summary>One material's mesh, and for each of its vertices the model vertex it shows and the submesh's placement.</summary>
    sealed record Piece(MeshGeometry3D Mesh, int[] Source, Matrix4x4[] Placement);

    /// <summary>The whole scene: the skeleton (when shown), then the model. Show it once; it changes in place.</summary>
    public Model3DGroup Group => group;

    /// <param name="model">The character's model, or null to show the skeleton alone.</param>
    /// <param name="diffuse">The colour texture for a material number, or null for plain grey.</param>
    public AnimatedScene(Rig rig, ModelFile? model, IReadOnlyList<PlacedSubmesh> submeshes, Func<int, BitmapSource?> diffuse)
    {
        this.rig = rig;
        if (model?.SkinBones(rig) is int[] bones)
            (this.model, skinBones) = (model, bones);
        skinned = new Vector3[this.model?.Positions.Length ?? 0];
        skinnedNormals = new Vector3[skinned.Length];
        group.Children.Add(new Model3DGroup());  // the skeleton's place
        if (this.model == null) return;

        // One mesh per material, as ModelScene does, remembering which model vertex each vertex is.
        var byMaterial = new Dictionary<int, (MeshBuilder Builder, List<int> Source, List<Matrix4x4> Placement)>();
        foreach ((Submesh submesh, Matrix4x4 placement) in submeshes)
            foreach (MeshPart part in submesh.Parts)
            {
                if (!byMaterial.TryGetValue(part.Material, out var piece))
                    byMaterial[part.Material] = piece = (new MeshBuilder(), [], []);
                var used = new Dictionary<int, int>();
                int Vertex(int v)
                {
                    if (used.TryGetValue(v, out int index)) return index;
                    piece.Source.Add(v);
                    piece.Placement.Add(placement);
                    Vector2 uv = this.model.TextureCoordinates[v];
                    return used[v] = piece.Builder.AddVertex(new Point3D(), new Vector3D(0, 1, 0), new System.Windows.Point(uv.X, uv.Y));
                }
                // WPF is right-handed and the game isn't: Z is flipped when shown, and corners listed the other way round.
                for (int i = 0; i + 2 < part.Triangles.Length; i += 3)
                    piece.Builder.AddTriangle(Vertex(part.Triangles[i]), Vertex(part.Triangles[i + 2]), Vertex(part.Triangles[i + 1]));
            }
        var shownModel = new Model3DGroup();
        foreach (var (material, (builder, source, placement)) in byMaterial)
        {
            MeshGeometry3D mesh = builder.ToMesh(changing: true);
            BitmapSource? texture = diffuse(material);
            Material shown = texture == null ? ModelScene.Plain(Color.FromRgb(170, 170, 170)) : ModelScene.Textured(texture);
            shownModel.Children.Add(new GeometryModel3D(mesh, shown) { BackMaterial = shown });
            pieces.Add(new Piece(mesh, source.ToArray(), placement.ToArray()));
        }
        group.Children.Add(shownModel);
    }

    /// <summary>
    /// Shows a moment of an animation (null: the rest pose), with the model see-through and the
    /// skeleton inside it when <paramref name="skeleton"/> is set (always the skeleton when there's no model).
    /// </summary>
    public void Show(Animation? animation, float frame, bool move, bool skeleton)
    {
        Matrix4x4[] pose = animation?.Pose(rig, frame, move) ?? rig.Bones.Select(b => Matrix4x4.CreateTranslation(b.Position)).ToArray();

        bool drawSkeleton = skeleton || model == null;
        group.Children[0] = drawSkeleton ? SkeletonScene.Build(rig, pose.Select(m => m.Translation).ToList()) : new Model3DGroup();
        if (model == null) return;

        // Each bone's matrix: from the rest pose to this frame.
        var moves = new Matrix4x4[pose.Length];
        for (int b = 0; b < pose.Length; b++)
            moves[b] = Matrix4x4.CreateTranslation(-rig.Bones[b].Position) * pose[b];
        int[] bones = skinBones;
        float[] weights = model.SkinWeights!;
        for (int v = 0; v < skinned.Length; v++)
        {
            Vector3 position = Vector3.Zero, normal = Vector3.Zero;
            for (int i = 0; i < 4; i++)
            {
                float weight = weights[v * 4 + i];
                int bone = bones[v * 4 + i];
                if (weight <= 0 || bone >= moves.Length) continue;
                position += weight * Vector3.Transform(model.Positions[v], moves[bone]);
                normal += weight * Vector3.TransformNormal(model.Normals[v], moves[bone]);
            }
            skinned[v] = position;
            skinnedNormals[v] = normal.LengthSquared() > 0 ? Vector3.Normalize(normal) : model.Normals[v];
        }

        double opacity = skeleton ? 0.3 : 1;
        var shownModel = (Model3DGroup)group.Children[1];
        for (int p = 0; p < pieces.Count; p++)
        {
            Piece piece = pieces[p];
            var positions = new Point3DCollection(piece.Source.Length);
            var normals = new Vector3DCollection(piece.Source.Length);
            for (int i = 0; i < piece.Source.Length; i++)
            {
                Vector3 at = Vector3.Transform(skinned[piece.Source[i]], piece.Placement[i]);
                Vector3 n = Vector3.TransformNormal(skinnedNormals[piece.Source[i]], piece.Placement[i]);
                positions.Add(new Point3D(at.X, at.Y, -at.Z));
                normals.Add(new Vector3D(n.X, n.Y, -n.Z));
            }
            positions.Freeze();
            normals.Freeze();
            piece.Mesh.Positions = positions;
            piece.Mesh.Normals = normals;
            var geometry = (GeometryModel3D)shownModel.Children[p];
            if (geometry.Material is DiffuseMaterial { Brush: { } brush } && Math.Abs(brush.Opacity - opacity) > 0.01)
            {
                Brush faded = brush.Clone();
                faded.Opacity = opacity;
                faded.Freeze();
                geometry.Material = geometry.BackMaterial = new DiffuseMaterial(faded);
            }
        }
    }
}
