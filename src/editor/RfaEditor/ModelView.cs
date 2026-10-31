using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using System.Windows.Media.Media3D;
using RfaFormats.Meshes;

namespace RfaEditor;

/// <summary>Where a model's file pair is: directly in an archive, or inside one of its .str2_pc containers.</summary>
sealed record ModelLocation(string Archive, string? Container, string HeaderName, string DataName)
{
    /// <summary>The pair a selected file belongs to (either half), or null if it isn't a model.</summary>
    public static ModelLocation? Of(GameFile file)
    {
        if (ModelFile.KindOf(file.Name) == null) return null;
        string header = ModelFile.HeaderFileName(file.Name);
        return new ModelLocation(file.Archive, file.Container, header, ModelFile.DataFileName(header));
    }
}

/// <summary>Turns a model into WPF 3D geometry: one piece of geometry per material.</summary>
static class ModelScene
{
    /// <param name="diffuse">The colour texture for a material number, or null to draw it plain grey.</param>
    /// <param name="opacity">How solid it is drawn (less than 1 to see through it, e.g. to its skeleton).</param>
    public static Model3DGroup Build(ModelFile model, IEnumerable<PlacedSubmesh> submeshes, Func<int, BitmapSource?> diffuse, double opacity = 1)
    {
        var group = new Model3DGroup();
        foreach ((MeshGeometry3D mesh, Material shown) in Parts(model, submeshes, diffuse))
        {
            Material material = opacity >= 1 ? shown : SeeThrough(shown, opacity);
            var geometry = new GeometryModel3D(mesh, material) { BackMaterial = material };
            geometry.Freeze();
            group.Children.Add(geometry);
        }
        group.Freeze();
        return group;
    }

    /// <summary>
    /// The model's geometry, each submesh moved into place, to place (many times) in a scene: one
    /// mesh per material, joining every part drawn with it, so a model is a few pieces for WPF to
    /// draw rather than one per part (each piece costs a draw call every frame). Each material
    /// carries the game's own (<see cref="Render.GameMaterial"/>) for the Direct3D view.
    /// </summary>
    public static List<(MeshGeometry3D Mesh, Material Material)> Parts(ModelFile model, IEnumerable<PlacedSubmesh> submeshes, Func<int, BitmapSource?> diffuse)
    {
        var byMaterial = new Dictionary<int, MeshBuilder>();
        var order = new List<int>();  // materials in the order first used
        foreach ((Submesh submesh, System.Numerics.Matrix4x4 transform) in submeshes)
            foreach (MeshPart part in submesh.Parts)
            {
                if (part.Triangles.Length == 0) continue;
                if (!byMaterial.TryGetValue(part.Material, out MeshBuilder? builder))
                {
                    byMaterial[part.Material] = builder = new MeshBuilder();
                    order.Add(part.Material);
                }
                AddPart(builder, model, part, transform);
            }
        var parts = new List<(MeshGeometry3D, Material)>();
        foreach (int material in order)
        {
            BitmapSource? texture = diffuse(material);
            // Blended materials (see GameMaterial) also by how solid they are as a whole.
            double opacity = material < model.Materials.Count ? Render.GameMaterial.WholeOpacity(model.Materials[material]) : 1;
            Material shown = texture == null ? Plain(Color.FromRgb(170, 170, 170)) : Textured(texture, opacity);
            if (material < model.Materials.Count) shown = Render.GameMaterial.From(model.Materials[material]).Attach(shown);
            parts.Add((byMaterial[material].ToMesh(), shown));
        }
        return parts;
    }

    public static Material Plain(Color color) => Freeze(new DiffuseMaterial(Freeze(new SolidColorBrush(color))));

    /// <summary>A material drawn partly see-through.</summary>
    static Material SeeThrough(Material material, double opacity)
    {
        if (material is not DiffuseMaterial { Brush: { } brush }) return material;
        Brush faded = brush.Clone();
        faded.Opacity = opacity;
        return Freeze(new DiffuseMaterial(Freeze(faded)));
    }

    /// <summary>A material showing a texture, repeated, with texture coordinates 0 to 1 across it.</summary>
    public static Material Textured(BitmapSource texture, double opacity = 1) => Freeze(new DiffuseMaterial(Freeze(new ImageBrush(texture)
    {
        TileMode = TileMode.Tile, ViewportUnits = BrushMappingMode.Absolute, Viewport = new Rect(0, 0, 1, 1), Opacity = opacity,
    })));

    static T Freeze<T>(T freezable) where T : Freezable
    {
        freezable.Freeze();
        return freezable;
    }

    /// <summary>
    /// Adds the triangles of one part to a mesh, with only the vertices they use, moved into place.
    /// WPF is right-handed and the game isn't, so Z is flipped and each triangle's corners are
    /// listed the other way round.
    /// </summary>
    static void AddPart(MeshBuilder builder, ModelFile model, MeshPart part, System.Numerics.Matrix4x4 transform)
    {
        var used = new Dictionary<int, int>();
        int Vertex(int v)
        {
            if (used.TryGetValue(v, out int index)) return index;
            var p = System.Numerics.Vector3.Transform(model.Positions[v], transform);
            var n = System.Numerics.Vector3.Normalize(System.Numerics.Vector3.TransformNormal(model.Normals[v], transform));
            var t = model.TextureCoordinates[v];
            return used[v] = builder.AddVertex(new Point3D(p.X, p.Y, -p.Z), new Vector3D(n.X, n.Y, -n.Z), new Point(t.X, t.Y));
        }
        for (int i = 0; i + 2 < part.Triangles.Length; i += 3)
            builder.AddTriangle(Vertex(part.Triangles[i]), Vertex(part.Triangles[i + 2]), Vertex(part.Triangles[i + 1]));
    }
}

/// <summary>
/// The 3D view: a camera circling a point. Drag with the left mouse button to turn around it, with
/// the right one to move it, and use the wheel to come closer or move away. A click (a press
/// without dragging) is reported, so a level can select what was clicked.
/// </summary>
sealed class ModelViewer
{
    readonly Viewport3D viewport;
    readonly PerspectiveCamera camera = new() { FieldOfView = 45, NearPlaneDistance = 0.01 };
    readonly ModelVisual3D content = new();
    readonly ModelVisual3D overlay = new();  // e.g. the box around the selected object
    Point3D centre;
    double radius = 1, distance = 3, yaw = 0.8 * Math.PI, pitch = 0.35;
    bool frameWhenSized;  // framed before the view had a size: frame again when it gets one
    Point? turnFrom, panFrom, pressedAt;

    /// <summary>Raised when the view is clicked without dragging, with the point clicked.</summary>
    public event Action<Point>? Clicked;

    public ModelViewer(Viewport3D viewport, UIElement mouseArea)
    {
        this.viewport = viewport;
        viewport.Camera = camera;
        var lights = new Model3DGroup();
        lights.Children.Add(new AmbientLight(Color.FromRgb(85, 85, 85)));
        lights.Children.Add(new DirectionalLight(Colors.White, new Vector3D(-0.4, -0.8, -0.5)));
        lights.Children.Add(new DirectionalLight(Color.FromRgb(100, 100, 110), new Vector3D(0.6, 0.3, 0.7)));
        viewport.Children.Add(new ModelVisual3D { Content = lights });
        viewport.Children.Add(content);
        viewport.Children.Add(overlay);
        viewport.SizeChanged += (_, _) =>
        {
            if (!frameWhenSized) return;
            frameWhenSized = false;
            ResetView();
        };

        mouseArea.MouseLeftButtonDown += (_, e) => { turnFrom = pressedAt = e.GetPosition(mouseArea); mouseArea.CaptureMouse(); };
        mouseArea.MouseRightButtonDown += (_, e) => { panFrom = e.GetPosition(mouseArea); mouseArea.CaptureMouse(); };
        mouseArea.MouseLeftButtonUp += (_, e) =>
        {
            Point now = e.GetPosition(mouseArea);
            if (pressedAt is Point down && (now - down).Length < 4) Clicked?.Invoke(now);
            turnFrom = pressedAt = null;
            mouseArea.ReleaseMouseCapture();
        };
        mouseArea.MouseRightButtonUp += (_, _) => { panFrom = null; mouseArea.ReleaseMouseCapture(); };
        mouseArea.MouseMove += (_, e) =>
        {
            Point now = e.GetPosition(mouseArea);
            if (turnFrom is Point from)
            {
                yaw += (now.X - from.X) * 0.01;
                pitch = Math.Clamp(pitch + (now.Y - from.Y) * 0.01, -1.5, 1.5);
                turnFrom = now;
                UpdateCamera();
            }
            else if (panFrom is Point start)
            {
                // Move the point the camera circles, across the screen.
                Vector3D right = Vector3D.CrossProduct(camera.LookDirection, camera.UpDirection);
                right.Normalize();
                Vector3D up = Vector3D.CrossProduct(right, camera.LookDirection);
                up.Normalize();
                double step = distance * 0.0015;
                centre += -right * (now.X - start.X) * step + up * (now.Y - start.Y) * step;
                panFrom = now;
                UpdateCamera();
            }
        };
        mouseArea.MouseWheel += (_, e) =>
        {
            distance = Math.Clamp(distance * (e.Delta > 0 ? 0.85 : 1 / 0.85), 0.2, radius * 50);
            UpdateCamera();
        };
    }

    /// <summary>Moves the camera to look at a point from a distance that shows something this big.</summary>
    public void FocusOn(Point3D point, double size)
    {
        centre = point;
        distance = Math.Max(2, size * 2.5);
        UpdateCamera();
    }

    /// <summary>Shows something on top of the model (or nothing), such as a selection box.</summary>
    public void ShowOverlay(Model3D? model) => overlay.Content = model;

    /// <summary>The piece of geometry under a point of the view, if any.</summary>
    public Model3D? ModelAt(Point point)
    {
        Model3D? found = null;
        VisualTreeHelper.HitTest(viewport, null, result =>
        {
            if (result is RayMeshGeometry3DHitTestResult hit && hit.VisualHit == content)
            {
                found = hit.ModelHit;
                return HitTestResultBehavior.Stop;
            }
            return HitTestResultBehavior.Continue;
        }, new PointHitTestParameters(point));
        return found;
    }

    /// <summary>Shows a model. With <paramref name="frame"/> the camera is moved to see all of it.</summary>
    public void Show(Model3DGroup model, bool frame)
    {
        content.Content = model;
        if (!frame) return;
        Rect3D bounds = model.Bounds;
        if (bounds.IsEmpty) return;
        centre = new Point3D(bounds.X + bounds.SizeX / 2, bounds.Y + bounds.SizeY / 2, bounds.Z + bounds.SizeZ / 2);
        radius = Math.Max(0.01, new Vector3D(bounds.SizeX, bounds.SizeY, bounds.SizeZ).Length / 2);
        ResetView();
    }

    public void ResetView()
    {
        yaw = 0.8 * Math.PI;
        pitch = 0.35;
        // Far enough that a ball of the model's size fits the narrower of the view's two angles (WPF's
        // field of view is the side-to-side one; a wide view sees less from top to bottom). A view not
        // laid out yet (just made visible) is framed again once it has its size.
        frameWhenSized = viewport.ActualWidth <= 0 || viewport.ActualHeight <= 0;
        double aspect = frameWhenSized ? 1 : viewport.ActualWidth / viewport.ActualHeight;
        double across = camera.FieldOfView * Math.PI / 180, upDown = 2 * Math.Atan(Math.Tan(across / 2) / aspect);
        distance = 0.85 * radius / Math.Sin(Math.Min(across, upDown) / 2);  // the ball (round the corners of the model's box) is a generous size
        UpdateCamera();
    }

    public void Clear()
    {
        content.Content = null;
        overlay.Content = null;
    }

    void UpdateCamera()
    {
        var direction = new Vector3D(Math.Cos(pitch) * Math.Sin(yaw), Math.Sin(pitch), Math.Cos(pitch) * Math.Cos(yaw));
        camera.Position = centre + direction * distance;
        camera.LookDirection = -direction;
        camera.UpDirection = new Vector3D(0, 1, 0);
        camera.NearPlaneDistance = Math.Max(0.01, distance * 0.01);
        camera.FarPlaneDistance = distance + radius * 10;
    }
}
