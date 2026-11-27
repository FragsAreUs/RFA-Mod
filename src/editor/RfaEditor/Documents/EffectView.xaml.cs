using System.Diagnostics;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using RfaFormats.Effects;
using RfaFormats.Textures;

namespace RfaEditor;

/// <summary>
/// An effect (.cefct_pc, its mesh vertices in the .gefct_pc beside it), played: its emitters' particles simulated
/// (<see cref="EffectSimulation"/>) and drawn (<see cref="EffectPlayer"/>), with a camera you turn with the mouse. Then
/// how long it lasts and how big it is, its textures as pictures, and its parts (emitters, lights, coronas, meshes, filters) with what is known of each, plus
/// the effects.xtbl entries that use it (the name the game's tables and scripts give it, and its sound). Read only.
/// </summary>
public partial class EffectView : UserControl
{
    /// <summary>An entry of effects.xtbl whose visual is this effect.</summary>
    public sealed record TableEntry(string Name, string? Sound, string? Folder);

    sealed class TextureCard
    {
        public required string Label { get; init; }
        public required string Tip { get; init; }
        public BitmapSource? Picture { get; set; }
        public string Missing { get; set; } = "loading...";
    }

    // A part as listed; an emitter's can be hidden from the player.
    sealed class Part
    {
        public required string Kind { get; init; }
        public required string Name { get; init; }
        public required string What { get; init; }
        public required string Details { get; init; }
        public int Emitter { get; init; } = -1;
        public int Corona { get; init; } = -1;
        public int Mesh { get; init; } = -1;
        public int Light { get; init; } = -1;
        public int Filter { get; init; } = -1;
        public bool CanHide => Emitter >= 0 || Corona >= 0 || Mesh >= 0 || Light >= 0 || Filter >= 0;
        public bool Shown { get; set; } = true;
    }

    const uint ColourSampler = 0x79A00DFF, MaskSampler = 0xE3832402;

    readonly Action<string> log;
    readonly EffectPlayer player = new();
    readonly Stopwatch clock = new();
    int generation;  // which effect a background texture load is for
    EffectSimulation? simulation;
    IReadOnlyList<EffectMesh> meshes = [];
    IReadOnlyList<CoronaSettings> coronas = [];
    IReadOnlyList<LightSettings> lights = [];
    EffectLooks looks = new();
    List<Part> parts = [];
    bool playing, animating, dragging;
    Point dragFrom;
    TimeSpan lastFrame;

    public EffectView(Action<string> log)
    {
        InitializeComponent();
        this.log = log;
        IsVisibleChanged += (_, _) => Animate(IsVisible && simulation != null);
    }

    /// <param name="bytes">The effect file, to play it.</param>
    /// <param name="vertices">The .gefct_pc beside it (its meshes' vertices), or null.</param>
    /// <param name="texture">A texture by name (with .tga) as a picture, or null: run off the UI thread.</param>
    /// <param name="pixels">A texture by name as pixels (small), for the player, or null: run off the UI thread.</param>
    /// <param name="tiles">Each texture's flip-book tiles (across, down), by name; missing ones are 1 × 1.</param>
    /// <param name="shaderName">An effect shader's name for its hash (the .fxo files'), or null.</param>
    public void Show(EffectSummary effect, byte[] bytes, byte[]? vertices, string fileName, IReadOnlyList<TableEntry> entries, Func<string, BitmapSource?> texture,
                     Func<string, RgbaImage?> pixels, IReadOnlyDictionary<string, (int, int)> tiles, Func<uint, string?> shaderName)
    {
        generation++;
        StartPlayer(effect, bytes, vertices, pixels, tiles);
        string Bitmap(int index) => index >= 0 && index < effect.Bitmaps.Count ? effect.Bitmaps[index] : $"bitmap {index}";
        string Shader(uint hash) => shaderName(hash) ?? hash.ToString("X8");
        string MaterialText(EffectSummary.Material? m) => m == null ? "no material"
            : $"shader {Shader(m.Shader)}; textures: " + (m.Textures.Count == 0 ? "none" : string.Join(", ", m.Textures.Select(t => Bitmap(t.Bitmap))));
        static string Rgba(float[] c) => $"{c[0]:0.##} {c[1]:0.##} {c[2]:0.##} {c[3]:0.##}";

        // The summary: what it is and who names it.
        var counts = new List<string>();
        void Count(int n, string one, string many) { if (n > 0) counts.Add(n == 1 ? $"1 {one}" : $"{n} {many}"); }
        Count(effect.Emitters.Count, "emitter", "emitters");
        Count(effect.Lights.Count, "light", "lights");
        Count(effect.Coronas.Count, "corona", "coronas");
        Count(effect.Meshes.Count, "mesh", "meshes");
        Count(effect.Filters.Count, "filter", "filters");
        Count(effect.Expressions, "expression", "expressions");
        string used = entries.Count == 0
            ? "No entry of effects.xtbl uses it (a sky, or an effect only a level or another file names)."
            : "Used by effects.xtbl: " + string.Join(", ", entries.Select(e => e.Sound != null ? $"{e.Name} (sound {e.Sound})" : e.Name)) + ".";
        SummaryText.Text = $"Effect {effect.Name} ({fileName}): lasts {effect.Duration:0.###} s" +
                           (effect.Duration <= 0 ? " (loops until stopped)" : "") +
                           $", {effect.Radius:0.##} m across from its middle. " +
                           (counts.Count > 0 ? string.Join(", ", counts) + "." : "Nothing in it.") + "\n" + used;

        // Its parts.
        parts = [];
        int emitterIndex = 0;
        foreach (var e in effect.Emitters)
        {
            string type = EffectSummary.ParticleTypeName(e.ParticleType);
            var flags = new List<string>();
            if (e.HalfRes) flags.Add("half resolution");
            if (e.QuarterRes) flags.Add("quarter resolution");
            if (e.Soft) flags.Add("soft (fades into surfaces)");
            string what = $"{type} particles from a {e.Shape}, up to {e.MaxParticles}, living {e.Lifetime:0.##} s" +
                          (e.LifetimeVariance > 0 ? $" ± {e.LifetimeVariance:0.##}" : "");
            parts.Add(new Part
            {
                Kind = "Emitter", Name = e.Name, What = what, Emitter = emitterIndex++,
                Details = $"Emitter {e.Name}\n{what}\n" +
                (e.MeshName != null ? $"Its particles are the mesh {e.MeshName}.\n" : "") +
                $"Colour (red green blue alpha): {Rgba(e.ColourStart)} at birth, {Rgba(e.ColourEnd)} at the end\n" +
                (flags.Count > 0 ? "Drawn: " + string.Join(", ", flags) + "\n" : "") +
                (e.Parent >= 0 && e.Parent < effect.Emitters.Count ? $"Emits from the particles of {effect.Emitters[e.Parent].Name}\n" : "") +
                "Material: " + MaterialText(e.Material) +
                (e.ParticleType == 6 ? "\nA node emitter: its particles aren't drawn; other emitters send theirs from them as they fly." : ""),
            });
        }
        int lightIndex = 0;
        foreach (var l in effect.Lights)
        {
            string what = $"{EffectSummary.LightTypeName(l.Type)} light, fading from {l.AttenuationStart:0.##} m to nothing at {l.AttenuationEnd:0.##} m" +
                          (l.Type == 1 ? $", its cone {l.HotspotSize * 180 / MathF.PI:0}° inside, {l.HotspotFalloff * 180 / MathF.PI:0}° at its edge" : "");
            parts.Add(new Part { Kind = "Light", Name = l.Name, What = what, Light = lightIndex++,
                Details = $"Light {l.Name}\n{what}\nIts colour, brightness, place and direction change over the effect's time. The player " +
                          "lights the ground and solid meshes with it, and marks where it is with a small ring." });
        }
        int coronaIndex = 0, meshIndex = 0;
        foreach (var c in effect.Coronas)
            parts.Add(new Part { Kind = "Corona", Name = c.Name, What = "a glow facing the camera", Corona = coronaIndex++,
                Details = $"Corona {c.Name}: a glow facing the camera, drawn on top. Its size and colour change with the effect's time and " +
                          $"how far away you are.\nMaterial: {MaterialText(c.Material)}" });
        foreach (var m in effect.Meshes)
        {
            string what = $"a model ({EffectSummary.MeshTypeName(m.Type)})";
            bool particleShape = effect.Emitters.Any(e => e.ParticleType == 10 && string.Equals(e.MeshName, m.Name, StringComparison.OrdinalIgnoreCase));
            parts.Add(new Part { Kind = "Mesh", Name = m.Name, What = what + (particleShape ? ", the shape of mesh particles" : ""), Mesh = meshIndex++,
                Details = $"Mesh {m.Name}: {what}. Its vertices are in the .gefct_pc beside this file." +
                          (particleShape ? " Mesh particles are drawn as it; it isn't drawn on its own." : "") });
        }
        int filterIndex = 0;
        foreach (string f in effect.Filters)
        {
            FilterSettings? force = simulation != null && filterIndex < simulation.Filters.Count ? simulation.Filters[filterIndex] : null;
            string pushes = force == null || force.Affects.Length == 0 ? "no emitter"
                : string.Join(", ", force.Affects.Select(i => i >= 0 && i < effect.Emitters.Count ? effect.Emitters[i].Name : $"emitter {i}"));
            string what = force == null ? "a force on particles" : force.Type switch
            {
                FilterSettings.Magnet => "a magnet: pulls particles toward it",
                FilterSettings.Vortex => "a vortex: swirls particles around it",
                FilterSettings.Turbulence => "turbulence: shakes particles about",
                FilterSettings.Orientor => "an orientor: lays particles flat, turned its way",
                FilterSettings.Wind => "wind: blows particles along",
                _ => $"a force ({force.TypeName})",
            };
            parts.Add(new Part { Kind = "Filter", Name = f, What = what, Filter = filterIndex++,
                Details = $"Filter {f}: {what}.\nIt acts on: {pushes}." +
                          (force?.Type == FilterSettings.Wind ? "\nWind isn't played yet." : "") });
        }
        PartList.ItemsSource = parts;
        DetailsBox.Text = parts.Count > 0 ? "Pick a part to see what is known of it." : "";
        if (parts.Count > 0) PartList.SelectedIndex = 0;

        // Its textures, read off the UI thread.
        var cards = effect.Bitmaps.Select((name, i) => new TextureCard
        {
            Label = name.EndsWith(".tga", StringComparison.OrdinalIgnoreCase) ? name[..^4] : name,
            Tip = $"{name}: used by " + UsersOf(effect, i),
        }).ToList();
        TextureStrip.ItemsSource = cards;
        int mine = generation;
        Task.Run(() =>
        {
            var pictures = new List<BitmapSource?>();
            foreach (string name in effect.Bitmaps)
            {
                try { pictures.Add(texture(name)); }
                catch (Exception e) when (e is System.IO.IOException or System.IO.InvalidDataException or NotSupportedException)
                {
                    pictures.Add(null);
                    log($"Effect preview: {name} can't be read ({e.Message})");
                }
            }
            Dispatcher.BeginInvoke(() =>
            {
                if (mine != generation) return;
                for (int i = 0; i < cards.Count; i++)
                {
                    cards[i].Picture = pictures[i];
                    cards[i].Missing = pictures[i] == null ? "not found" : "";
                }
                TextureStrip.ItemsSource = null;
                TextureStrip.ItemsSource = cards;
            });
        });
    }

    // Which parts use bitmap number `index`.
    static string UsersOf(EffectSummary effect, int index)
    {
        var users = effect.Emitters.Where(e => e.Material?.Textures.Any(t => t.Bitmap == index) == true).Select(e => e.Name)
            .Concat(effect.Coronas.Where(c => c.Material?.Textures.Any(t => t.Bitmap == index) == true).Select(c => c.Name))
            .ToList();
        return users.Count == 0 ? "its meshes (or nothing listed)" : string.Join(", ", users);
    }

    public void Hide()
    {
        generation++;
        Animate(false);
        simulation = null;
        PlayerImage.Source = null;
        PartList.ItemsSource = null;
        TextureStrip.ItemsSource = null;
    }

    void OnPartChosen(object sender, SelectionChangedEventArgs e)
    {
        if (PartList.SelectedItem is Part part) DetailsBox.Text = part.Details;
    }

    // ---- The player -------------------------------------------------------------------------------------

    void StartPlayer(EffectSummary effect, byte[] bytes, byte[]? vertices, Func<string, RgbaImage?> pixels, IReadOnlyDictionary<string, (int, int)> tiles)
    {
        Animate(false);
        try
        {
            simulation = new EffectSimulation(bytes) { Loop = LoopBox.IsChecked == true };
        }
        catch (Exception e) when (e is System.IO.InvalidDataException or NotSupportedException or ArgumentException or IndexOutOfRangeException)
        {
            simulation = null;
            PlayText.Text = "It can't be played: " + e.Message;
            return;
        }
        meshes = EffectMesh.ReadAll(bytes, vertices);
        coronas = CoronaSettings.ReadAll(bytes);
        lights = LightSettings.ReadAll(bytes);
        looks = new EffectLooks
        {
            Emitters = new ParticleSprite?[simulation.Emitters.Count],
            Coronas = new ParticleSprite?[coronas.Count],
            EmittersShown = [.. Enumerable.Repeat(true, simulation.Emitters.Count)],
            CoronasShown = [.. Enumerable.Repeat(true, coronas.Count)],
            MeshesShown = [.. Enumerable.Repeat(true, meshes.Count)],
            LightsShown = [.. Enumerable.Repeat(true, lights.Count)],
        };
        // Frame it: the camera a few times its radius away, looking at its middle, the grid as wide as it.
        float radius = Math.Max(0.5f, effect.Radius);
        player.Target = new System.Numerics.Vector3(0, radius * 0.6f, 0);
        player.Distance = radius * 3.5f;
        player.GridSize = Math.Max(2, radius * 2);
        player.Resize(PlayerArea.ActualWidth, PlayerArea.ActualHeight);
        PlayerImage.Source = player.Bitmap;
        playing = true;
        PlayButton.Content = "Pause";

        // Each emitter's and corona's colour texture (and a masked shader's mask), and the meshes' textures, read off
        // the UI thread.
        var materials = effect.Emitters.Select(e => e.Material).ToList();
        var coronaMaterials = effect.Coronas.Select(c => c.Material).ToList();
        var meshTextures = meshes.SelectMany(m => m.Parts.Select(part => m.TextureOf(part.Material))).OfType<string>()
                                 .Distinct(StringComparer.OrdinalIgnoreCase).ToList();
        var names = effect.Bitmaps;
        EffectLooks forThis = looks;
        int mine = generation;
        Task.Run(() =>
        {
            var cache = new Dictionary<int, RgbaImage?>();
            RgbaImage? Load(int bitmap)
            {
                if (bitmap < 0 || bitmap >= names.Count) return null;
                if (cache.TryGetValue(bitmap, out var known)) return known;
                try { return cache[bitmap] = pixels(names[bitmap]); }
                catch (Exception e) when (e is System.IO.IOException or System.IO.InvalidDataException or NotSupportedException)
                {
                    log($"Effect player: {names[bitmap]} can't be read ({e.Message})");
                    return cache[bitmap] = null;
                }
            }
            // The colour map is the sampler every particle shader has (0x79A00DFF, slot 0); a masked shader's mask is
            // its 0xE3832402.
            (int, int) TilesOf(int bitmap) => bitmap >= 0 && bitmap < names.Count && tiles.TryGetValue(names[bitmap], out var t) ? t : (1, 1);
            ParticleSprite? SpriteOf(EffectSummary.Material? material)
            {
                if (material == null || material.Textures.Count == 0) return null;
                var colourMap = material.Textures.FirstOrDefault(t => t.Sampler == ColourSampler, material.Textures[0]);
                int maskBitmap = material.Textures.Any(t => t.Sampler == MaskSampler) ? material.Textures.First(t => t.Sampler == MaskSampler).Bitmap : -1;
                RgbaImage? image = Load(colourMap.Bitmap);
                return image == null ? null
                    : new ParticleSprite(image, TilesOf(colourMap.Bitmap), maskBitmap >= 0 ? Load(maskBitmap) : null, TilesOf(maskBitmap));
            }
            var emitterSprites = materials.Select(SpriteOf).ToArray();
            var coronaSprites = coronaMaterials.Select(SpriteOf).ToArray();
            var meshPictures = new Dictionary<string, RgbaImage?>(StringComparer.OrdinalIgnoreCase);
            foreach (string name in meshTextures)
            {
                try { meshPictures[name] = pixels(name); }
                catch (Exception e) when (e is System.IO.IOException or System.IO.InvalidDataException or NotSupportedException)
                {
                    log($"Effect player: {name} can't be read ({e.Message})");
                }
            }
            Dispatcher.BeginInvoke(() =>
            {
                if (mine != generation) return;
                forThis.Emitters = emitterSprites;
                forThis.Coronas = coronaSprites;
                forThis.MeshTextures = meshPictures;
            });
        });
        Animate(IsVisible);
    }

    // Drawing every frame while it shows.
    void Animate(bool on)
    {
        if (on == animating) return;
        animating = on;
        if (on)
        {
            clock.Restart();
            lastFrame = TimeSpan.Zero;
            CompositionTarget.Rendering += OnFrame;
        }
        else
            CompositionTarget.Rendering -= OnFrame;
    }

    void OnFrame(object? sender, EventArgs e)
    {
        if (simulation == null) return;
        TimeSpan now = clock.Elapsed;
        float seconds = (float)Math.Min(0.05, (now - lastFrame).TotalSeconds);
        lastFrame = now;
        double speed = SpeedList.SelectedItem is ComboBoxItem { Tag: string tag } &&
                       double.TryParse(tag, System.Globalization.NumberStyles.Float, System.Globalization.CultureInfo.InvariantCulture, out double s) ? s : 1;
        if (playing)
        {
            // In small steps, so fast particles don't jump.
            float left = seconds * (float)speed;
            while (left > 0)
            {
                float step = Math.Min(left, 1 / 60f);
                simulation.Step(step);
                left -= step;
            }
            // Played once: when its last particle is gone, from the start again.
            if (!simulation.Loop && !simulation.Emitting && simulation.Particles.Count == 0) simulation.Restart();
        }
        foreach (Part part in parts)
        {
            if (part.Emitter >= 0 && part.Emitter < looks.EmittersShown.Length) looks.EmittersShown[part.Emitter] = part.Shown;
            if (part.Corona >= 0 && part.Corona < looks.CoronasShown.Length) looks.CoronasShown[part.Corona] = part.Shown;
            if (part.Mesh >= 0 && part.Mesh < looks.MeshesShown.Length) looks.MeshesShown[part.Mesh] = part.Shown;
            if (part.Light >= 0 && part.Light < looks.LightsShown.Length) looks.LightsShown[part.Light] = part.Shown;
            if (part.Filter >= 0 && part.Filter < simulation.FiltersOn.Length) simulation.FiltersOn[part.Filter] = part.Shown;
        }
        player.Draw(simulation, meshes, coronas, lights, looks);
        PlayText.Text = $"{simulation.Time:0.00} of {simulation.Duration:0.00} s, {simulation.Particles.Count} particles";
    }

    void OnPlayPause(object sender, RoutedEventArgs e)
    {
        playing = !playing;
        PlayButton.Content = playing ? "Pause" : "Play";
    }

    void OnRestart(object sender, RoutedEventArgs e) => simulation?.Restart();

    void OnLoop(object sender, RoutedEventArgs e)
    {
        if (simulation != null) simulation.Loop = LoopBox.IsChecked == true;
    }

    void OnPlayerResized(object sender, SizeChangedEventArgs e)
    {
        player.Resize(PlayerArea.ActualWidth, PlayerArea.ActualHeight);
        PlayerImage.Source = player.Bitmap;
    }

    void OnPlayerDown(object sender, MouseButtonEventArgs e)
    {
        PlayerArea.Focus();
        dragging = true;
        dragFrom = e.GetPosition(PlayerArea);
        PlayerArea.CaptureMouse();
    }

    void OnPlayerMove(object sender, MouseEventArgs e)
    {
        if (!dragging) return;
        Point at = e.GetPosition(PlayerArea);
        player.Yaw -= (float)(at.X - dragFrom.X) * 0.01f;
        player.Pitch = Math.Clamp(player.Pitch + (float)(at.Y - dragFrom.Y) * 0.01f, -1.4f, 1.4f);
        dragFrom = at;
    }

    void OnPlayerUp(object sender, MouseButtonEventArgs e)
    {
        dragging = false;
        PlayerArea.ReleaseMouseCapture();
    }

    void OnPlayerWheel(object sender, MouseWheelEventArgs e)
    {
        player.Distance = Math.Clamp(player.Distance * (e.Delta > 0 ? 0.9f : 1.1f), 0.3f, 500);
        e.Handled = true;
    }

    void OnPlayerKey(object sender, KeyEventArgs e)
    {
        if (e.Key != Key.Space) return;
        OnPlayPause(sender, e);
        e.Handled = true;
    }
}
