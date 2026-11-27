using System.Reflection;
using RfaFormats.Meshes;
using RfaFormats.Shaders;

namespace RfaFormats.Guerrilla;

/// <summary>
/// Guerrilla's lit model shaders rewritten for Armageddon's renderer, as effect files of the game's
/// own kind (see <see cref="EffectFile"/>): gw_standard0/1/2 (Guerrilla's standard shaders: Lambert,
/// Blinn and self-illumination), gw_metal0/1/2 (its metal: the metal lobe), gw_brdf0/1/2 (its BRDF
/// shaders: their fitted curves or look-up maps), gw_blendmetal0/1/2 (metal and standard mixed by
/// the specular map's alpha), gw_glass0 (its blended glass), gw_eosglass1 (its mirror glass) and
/// gw_composite1c (its composite panels), gw_vehiclepaint0/1 (its vehicles' paint), gw_clearcoat1
/// (its Oasis roofs) and gw_fuelgauge0c (rfg-mp_fuel_gauge: the backpacks' fuel gauges, skinned too), the number the UV sets past the first their meshes have, then
/// "c" for alpha-tested materials (cut-outs) and "b" for blended ones; each as _s (still meshes) and
/// _bs (instanced ones), and the glass also as _c (skinned: the game draws a pane's flying shards with
/// its material's _c effect, and won't shatter glass whose shader has none), and gw_brdf0,
/// gw_standard0 and gw_standard0c as _c for the people and the character-mesh items (see <see cref="WriteSkinned"/>). They go with
/// the world's other new files (overrides\build\pc\cache\), and the guerrilla_world mod has the game
/// load them after its own (the game only loads the effect files inside its archives).
///
/// Each takes its G-buffer passes (the normals, plain and stippled) and the engine's texture slots
/// from Armageddon's ir_default as they are (but gw_glass, which has no G-buffer pass: Guerrilla's
/// glass casts no shadow and Armageddon then skips a blended material's G-buffer and shadow passes,
/// nor the blended "b" ones; and the cut-outs, whose depth (shadow) and G-buffer passes are ours too,
/// so their holes are cut by Guerrilla's own alpha in every pass, as ir_at_default lays them out),
/// and adds its own material pass, compiled from
/// Guerrilla\Shaders\GuerrillaSurfaces.hlsl, which lights the surface with Guerrilla's maths from
/// what the mod gives it. The material pass of objects fading in or out (stippled) is the plain one.
/// The materials name Guerrilla's own texture slots and these constants, register by register (the
/// first five where ir_default's G-buffer shaders read theirs):
/// IR_Object_ID (the engine's), Normal_Map_Height, Specular_Power, the normal map's tiling U and V,
/// Diffuse_Color, Specular_Color, then four values each: gw_Tiling, gw_Decals, gw_Shine, gw_Glow,
/// gw_Scroll, gw_Extra, then BRDF_Diff_1-3, BRDF_Spec_1-3, gw_Brdf, gw_MetalSpecular,
/// gw_MetalSpecularPower (see the shader's Material buffer).
/// </summary>
public static class GuerrillaShaders
{
    public enum Family { Standard = 1, Metal = 2, Brdf = 3, BlendMetal = 4, Glass = 5, EosGlass = 6, Composite = 7, VehiclePaint = 8, Clearcoat = 9, FuelGauge = 10 }

    /// <summary>Opaque, alpha-tested (Guerrilla's material flags 2: holes cut by the alpha) or blended (flags 1).</summary>
    public enum Variant { Plain, CutOut, Blended }

    /// <summary>The UV sets past the first a family's effects are written for: Guerrilla's glass has one, its mirror glass and composite two.</summary>
    public static int[] LayersOf(Family family) => family switch
    {
        Family.Glass or Family.FuelGauge => [0],
        Family.EosGlass or Family.Composite or Family.Clearcoat => [1],
        Family.VehiclePaint => [0, 1],
        _ => [0, 1, 2],
    };

    /// <summary>The variants a family's effects are written as: glass is blended itself, the composite is always cut out.</summary>
    public static Variant[] VariantsOf(Family family) => family switch
    {
        Family.Glass or Family.EosGlass or Family.VehiclePaint or Family.Clearcoat => [Variant.Plain],
        Family.Composite or Family.FuelGauge => [Variant.CutOut],
        _ => [Variant.Plain, Variant.CutOut, Variant.Blended],
    };

    /// <summary>An effect's name (without its _s / _bs): its family and how many UV sets past the first its meshes have.</summary>
    public static string Name(Family family, int layers, Variant variant = Variant.Plain) =>
        $"gw_{family.ToString().ToLowerInvariant()}{layers}{variant switch { Variant.CutOut => "c", Variant.Blended => "b", _ => "" }}";

    /// <summary>The material constants in register order (0 is the engine's).</summary>
    public static readonly string[] Constants =
        ["IR_Object_ID", "Normal_Map_Height", "Specular_Power", "gw_NormalTilingU", "gw_NormalTilingV", "Diffuse_Color", "Specular_Color",
         "gw_Tiling", "gw_Decals", "gw_Shine", "gw_Glow", "gw_Scroll", "gw_Extra",
         "BRDF_Diff_1", "BRDF_Diff_2", "BRDF_Diff_3", "BRDF_Spec_1", "BRDF_Spec_2", "BRDF_Spec_3", "gw_Brdf", "gw_MetalSpecular", "gw_MetalSpecularPower"];

    /// <summary>The texture slots (Guerrilla's names) and their registers, as the shader's.</summary>
    public static readonly (string Slot, byte Register)[] Slots =
        [("Normal_Map", 0), ("Diffuse_Map", 1), ("Specular_Map", 2), ("Decal_Map_1", 3), ("Decal_Map_2", 4), ("Environment_Map", 5),
         ("BRDF_Diff_Map", 6), ("BRDF_Spec_Map", 7), ("reflection_map", 8), ("Alpha_Map", 10), ("BRDF_Mask_Map", 11)];

    /// <summary>
    /// The slots an effect samples: the standard shaders' decals at UV1 and UV2; the others' layer at UV1
    /// and decal at UV2, and the BRDF ones' two look-up maps.
    /// </summary>
    public static string[] SlotsOf(Family family, int layers)
    {
        if (family == Family.Glass) return ["Diffuse_Map", "Specular_Map"];
        if (family == Family.EosGlass) return ["Normal_Map", "Diffuse_Map", "Environment_Map", "reflection_map"];
        if (family == Family.Composite) return ["Normal_Map", "Diffuse_Map", "Decal_Map_1", "Alpha_Map"];
        if (family == Family.VehiclePaint) return ["Normal_Map", "Diffuse_Map", "Specular_Map", "Decal_Map_1", "BRDF_Mask_Map"];
        if (family == Family.Clearcoat) return ["Normal_Map", "Specular_Map", "Environment_Map", "Decal_Map_1"];
        if (family == Family.FuelGauge) return ["Diffuse_Map"];
        string[] uvSets = family == Family.Standard
            ? layers switch { 1 => ["Decal_Map_1"], 2 => ["Decal_Map_1", "Decal_Map_2"], _ => [] }
            : layers switch { 1 => ["Environment_Map"], 2 => ["Environment_Map", "Decal_Map_1"], _ => [] };
        string[] lookUps = family == Family.Brdf ? ["BRDF_Diff_Map", "BRDF_Spec_Map"] : [];
        return ["Normal_Map", "Diffuse_Map", "Specular_Map", .. uvSets, .. lookUps];
    }

    /// <summary>Armageddon's G-buffer shaders read these constants of ir_default's from these registers; checked before they are used.</summary>
    static readonly (string Name, byte Register)[] BaseConstants =
        [("IR_Object_ID", 0), ("Normal_Height", 1), ("Specular_Power", 2), ("Diffuse_TilingU", 3), ("Diffuse_TilingV", 4)];

    static readonly uint DepthTechnique = 0xD0A87291, GeometryTechnique = 0x6614AD7B, GeometryFadingTechnique = 0xB08EBA8C,
        MaterialTechnique = 0xEA49BBC0, MaterialFadingTechnique = 0xFE6B0E83;

    /// <summary>
    /// Writes every effect into the overrides' shader folder (build\pc\cache\guerrilla\shaders: see OverrideLayout), from
    /// Armageddon's ir_default in <paramref name="armageddonCache"/> (its build\pc\cache).
    /// Returns the files' names.
    /// </summary>
    public static List<string> Write(string armageddonCache, string overrides)
    {
        var misc = Packfile.Open(Path.Combine(armageddonCache, "misc.vpp_pc"));
        var written = WriteModels(misc, overrides, [("s", false, false), ("bs", true, false), ("c", false, true)]);
        written.AddRange(WriteTerrain(misc, overrides));
        return written;
    }

    /// <summary>
    /// Writes only the skinned (_c) effects into the overrides' shader folder, the ones the people's materials
    /// (character meshes, GuerrillaPeople) and the glass's flying shards name; the same files
    /// <see cref="Write"/> writes among the others. Returns the files' names.
    /// </summary>
    public static List<string> WriteSkinned(string armageddonCache, string overrides) =>
        WriteModels(Packfile.Open(Path.Combine(armageddonCache, "misc.vpp_pc")), overrides, [("c", false, true)]);

    /// <summary>
    /// The families written skinned: the glass (its shards), the BRDF ones (the people's clothes, skin and
    /// eyes) and the standard ones (rfg-nodecal: clothes, props, the character-mesh guns and gear;
    /// Guerrilla's skinned and still versions have the same pixel shader), with no UV set past the first
    /// (character meshes have one, mostly): opaque, and alpha-tested too (Mason's eyelashes, the people's skin
    /// cut out on some; their depth and G-buffer passes skinned as well, on Armageddon's ir_at_default_c's
    /// layout). Blended skinned materials aren't written yet.
    /// </summary>
    static bool Skinned(Family family) => family is Family.Glass or Family.EosGlass or Family.Brdf or Family.Standard or Family.FuelGauge;
    /// <summary>Whether <see cref="WriteSkinned"/> writes the rewrite a skinned material of this family, layers and variant is drawn with.</summary>
    internal static bool CanSkin(Family family, int layers, Variant variant) =>
        Skinned(family) && SkinnedLayersOf(family).Contains(layers) && SkinnedVariantsOf(family).Contains(variant);
    // The standard ones with their decal layers too (UV sets past the first): Guerrilla's weapon locker is a character
    // mesh on rfg-singledecal (its vertices carry three UV sets), and with no gw_standard1_c it was drawn red (the
    // user's 2026-11-10; the log's "Red: a material on gw_standard1 is drawn as a mesh of the kind _c").
    static int[] SkinnedLayersOf(Family family) => family is Family.Brdf ? [0] : LayersOf(family);
    static Variant[] SkinnedVariantsOf(Family family) => family switch
    {
        Family.Brdf or Family.Standard => [Variant.Plain, Variant.CutOut],
        _ => VariantsOf(family),
    };

    static List<string> WriteModels(Packfile misc, string overrides, (string Suffix, bool Instanced, bool Skinned)[] kinds)
    {
        string source = Source();
        var written = new List<string>();
        foreach (var (suffix, instanced, skinned) in kinds)
        {
            string baseName = $"ir_default_{suffix}.fxo_pc_dx11";
            var entry = misc.Find(baseName) ?? throw new InvalidDataException($"Armageddon's misc.vpp_pc has no {baseName}");
            var baseEffect = EffectFile.Read(misc.Read(entry));
            Check(baseEffect, baseName);
            string cutOutName = $"ir_at_default_{suffix}.fxo_pc_dx11";
            var cutOutBase = EffectFile.Read(misc.Read(misc.Find(cutOutName) ?? throw new InvalidDataException($"Armageddon's misc.vpp_pc has no {cutOutName}")));
            CheckCutOut(cutOutBase, cutOutName);
            foreach (var family in Enum.GetValues<Family>().Where(f => !skinned || Skinned(f)))
                foreach (int layers in skinned ? SkinnedLayersOf(family) : LayersOf(family))
                    foreach (var variant in skinned ? SkinnedVariantsOf(family) : VariantsOf(family))
                    {
                        string name = $"{Name(family, layers, variant)}_{suffix}.fxo_pc_dx11";
                        byte[] Compile(string entryPoint, string target, bool stippled = false) =>
                            HlslCompiler.Compile(source, entryPoint, target, "GuerrillaSurfaces.hlsl", new Dictionary<string, string>
                            {
                                ["FAMILY"] = ((int)family).ToString(), ["LAYERS"] = layers.ToString(), ["INSTANCED"] = instanced ? "1" : "0",
                                ["SKINNED"] = skinned ? "1" : "0",
                                ["CUTOUT"] = variant == Variant.CutOut ? "1" : "0", ["BLENDED"] = variant == Variant.Blended ? "1" : "0",
                                ["STIPPLED"] = stippled ? "1" : "0",
                            });
                        byte[] vertex = Compile("SurfaceVertex", "vs_4_0"), pixel = Compile("SurfacePixel", "ps_4_0");
                        var effect = variant == Variant.CutOut
                            ? CutOut(cutOutBase, baseEffect, family, layers, [Compile("DepthVertex", "vs_4_0"), Compile("GeometryVertex", "vs_4_0"), Compile("GeometryVertex", "vs_4_0", true), vertex],
                                     [Compile("DepthPixel", "ps_4_0"), Compile("GeometryPixel", "ps_4_0"), Compile("GeometryPixel", "ps_4_0", true), pixel])
                            : family == Family.Glass || variant == Variant.Blended ? MaterialPassOnly(baseEffect, family, layers, vertex, pixel)
                            : Effect(baseEffect, family, layers, vertex, pixel);
                        File.WriteAllBytes(OverrideLayout.NewFile(overrides, OverrideLayout.Shaders, name), effect.Write());
                        written.Add(name);
                    }
        }
        return written;
    }

    /// <summary>
    /// The terrain's effects (see GuerrillaTerrain.hlsl): Armageddon's own terrain effect for each, its
    /// vertex shaders and G-buffer pass as they are (the ground, cliffs and roads placed and depth-tested
    /// exactly as before), its material pass's pixel shader Guerrilla's. Each with the rows Guerrilla's
    /// shader adds: the ground its layers' shine power (c10), the two-texture cliff the zone's colour map
    /// (t10, which it fades to far off, as the ground does); the roads need none (Armageddon's road
    /// effects have every constant of Guerrilla's), and are written for still and instanced decals (_s,
    /// _bs). Our pixel shader must read what Armageddon's vertex shader writes, register by register:
    /// checked before the file is written.
    /// </summary>
    static List<string> WriteTerrain(Packfile misc, string overrides)
    {
        string source = Source("GuerrillaTerrain.hlsl");
        var written = new List<string>();
        var effects = new List<(string Name, string From, int Surface, int Road)>
        {
            (TerrainMaterials.GroundShader + "_t", "ir_terrain_height_mesh_t", 1, 0),
            (TerrainMaterials.GroundFadeShader + "_t", "ir_terrain_height_mesh_blend_t", 2, 0),
            (TerrainMaterials.CliffShader + "_t", "ir_terrain_sidemap_t", 3, 0),
            (TerrainMaterials.FarShader + "_t", "ir_terrain_height_mesh_lowlod_t", 6, 0),
        };
        foreach (int variant in new[] { 2, 3, 4 })
            foreach (string suffix in new[] { "_s", "_bs" })
                effects.Add((TerrainMaterials.RoadShaderName(variant) + suffix, $"ir_terrain_road_layer{variant}{suffix}", 5, variant));
        // The undergrowth: Armageddon's foliage effect (a depth, a G-buffer and a material pass).
        foreach (string suffix in new[] { "_s", "_bs" })
            effects.Add((GuerrillaFoliage.ShaderName + suffix, $"ir_foliage_clutter{suffix}", 7, 0));
        foreach (var (name, from, surface, road) in effects)
        {
            string file = from + ".fxo_pc_dx11";
            var entry = misc.Find(file) ?? throw new InvalidDataException($"Armageddon's misc.vpp_pc has no {file}");
            var e = EffectFile.Read(misc.Read(entry));
            // The material pass: the last vertex and pixel shaders (the terrain's second of two, the foliage's third of three).
            int material = surface == 7 ? 2 : 1;
            bool laidOut = surface == 7
                ? e.VertexShaders.Count == 3 && e.PixelShaders.Count == 3 && e.Techniques.Count == 5 &&
                  e.Techniques[3] == new EffectFile.Technique(2, 2, MaterialTechnique, 0x80) && e.Techniques[4] == new EffectFile.Technique(2, 2, MaterialFadingTechnique, 0x100)
                : e.VertexShaders.Count == 2 && e.PixelShaders.Count == 2 && e.Techniques.Count == 4 &&
                  e.Techniques[2] == new EffectFile.Technique(1, 1, MaterialTechnique, 0x80) && e.Techniques[3] == new EffectFile.Technique(1, 1, MaterialFadingTechnique, 0x100);
            if (!laidOut) throw new InvalidDataException($"Armageddon's {file} isn't laid out as Guerrilla's terrain shaders expect");
            if (surface == 7 && !FoliageConstants.Select((c, r) => e.Constants.Any(k => k.Name == H(c) && k.Register == r)).All(x => x))
                throw new InvalidDataException($"Armageddon's {file} hasn't the constants where Guerrilla's undergrowth shader reads them");
            if (surface == 5 && (RoadConstants.Where((c, r) => !e.Constants.Any(k => k.Name == H(c) && k.Register == r)).Any() ||
                                 RoadTextures.Any(t => !e.Textures.Any(k => k.Name == H(t.Slot) && k.Register == t.Register))))
                throw new InvalidDataException($"Armageddon's {file} hasn't the constants and textures where Guerrilla's road shader reads them");
            byte[] pixel = HlslCompiler.Compile(source, "TerrainPixel", "ps_4_0", "GuerrillaTerrain.hlsl",
                                                new Dictionary<string, string> { ["SURFACE"] = surface.ToString(), ["ROAD"] = road.ToString() });
            CheckSignatures(e.VertexShaders[material]!, pixel, file);
            e.PixelShaders[material] = pixel;
            if (surface <= 2)
                e.Constants.Add(new(H("Terrain_layer_spec_power"), 10, 0, 1, 1));
            if (surface == 3)
            {
                var colour = e.Textures.First(t => t.Name == H("Diffuse_Map_0"));
                e.Textures.Add(colour with { Name = H("Combine_map"), Register = 10 });
            }
            string written1 = name + ".fxo_pc_dx11";
            File.WriteAllBytes(OverrideLayout.NewFile(overrides, OverrideLayout.Shaders, written1), e.Write());
            written.Add(written1);
        }
        return written;
    }

    /// <summary>The foliage effect's constants in register order, as Armageddon's ir_foliage_clutter has them.</summary>
    static readonly string[] FoliageConstants = ["Diffuse_Color", "Specular_Color", "Foliage_color", "Foliage_alphaClamp", "Specular_Power"];

    /// <summary>The road effects' constants in register order, and the textures our road shader reads, as Armageddon's road effects have them.</summary>
    static readonly string[] RoadConstants =
        ["Specular_Power", "Terrain_layer1_scale_offset", "Terrain_layer2_scale_offset", "Terrain_layer0_scale_offset", "Tile", "Edge_fade__use_spec_map",
         "Diffuse_color_0", "Diffuse_color_1", "Diffuse_color_2", "Diffuse_color_3", "Terrain_layer_spec_alpha"];
    static readonly (string Slot, byte Register)[] RoadTextures =
        [("Alpha_Map_0", 4), ("Diffuse_Map_3", 5), ("Diffuse_Map_0", 6), ("Diffuse_Map_1", 7), ("Diffuse_Map_2", 8)];

    /// <summary>That every input of our pixel shader is an output of Armageddon's vertex shader at the same register, with no component it hasn't.</summary>
    static void CheckSignatures(byte[] vertex, byte[] pixel, string file)
    {
        var outputs = HlslCompiler.Signature(vertex, "Output");
        foreach (var input in HlslCompiler.Signature(pixel, "Input"))
        {
            var match = outputs.FirstOrDefault(o => o.Register == input.Register);
            if (match.Name == null || !match.Name.Equals(input.Name, StringComparison.OrdinalIgnoreCase) || match.Index != input.Index ||
                input.Mask.Any(c => c != ' ' && !match.Mask.Contains(c)))
                throw new InvalidDataException($"Guerrilla's terrain shader for {file} reads {input.Name}{input.Index} (register {input.Register}, {input.Mask}), " +
                                               $"which Armageddon's vertex shader doesn't write there ({match.Name}{match.Index} {match.Mask})");
        }
    }

    static string Source(string file = "GuerrillaSurfaces.hlsl")
    {
        using var stream = Assembly.GetExecutingAssembly().GetManifestResourceStream(file)
            ?? throw new InvalidOperationException($"{file} isn't in RfaFormats");
        using var reader = new StreamReader(stream);
        return reader.ReadToEnd();
    }

    /// <summary>That Armageddon's ir_default is still laid out as these effects take it.</summary>
    static void Check(EffectFile e, string name)
    {
        bool techniques = e.Techniques.Count == 4 && e.VertexShaders.Count == 4 && e.PixelShaders.Count == 4
            && e.Techniques[0] == new EffectFile.Technique(0, 0, GeometryTechnique, 0x20)
            && e.Techniques[1] == new EffectFile.Technique(1, 1, GeometryFadingTechnique, 0x40);
        bool constants = BaseConstants.All(c => e.Constants.Any(k => k.Name == H(c.Name) && k.Register == c.Register));
        if (!techniques || !constants) throw new InvalidDataException($"Armageddon's {name} isn't laid out as Guerrilla's shaders expect");
    }

    static EffectFile Effect(EffectFile from, Family family, int layers, byte[] vertex, byte[] pixel)
    {
        var e = new EffectFile { KindsPresent = from.KindsPresent };
        from.TechniqueMap.CopyTo(e.TechniqueMap, 0);
        // The G-buffer passes as Armageddon's; the material pass (plain and stippled) ours.
        e.VertexShaders.AddRange([from.VertexShaders[0], from.VertexShaders[1], vertex]);
        e.PixelShaders.AddRange([from.PixelShaders[0], from.PixelShaders[1], pixel]);
        e.Techniques.AddRange([from.Techniques[0], from.Techniques[1],
                               new(2, 2, MaterialTechnique, 0x80), new(2, 2, MaterialFadingTechnique, 0x100)]);
        // ir_default's slots, the engine's with them; our own layers with the colour map's sampling.
        var diffuse = from.Textures.First(t => t.Name == H("Diffuse_Map"));
        e.Textures.AddRange(from.Textures);
        foreach (string slot in SlotsOf(family, layers).Where(s => !from.Textures.Any(t => t.Name == H(s))))
            e.Textures.Add(diffuse with { Name = H(slot), Register = Slots.First(s => s.Slot == slot).Register });
        for (int r = 0; r < Constants.Length; r++)
        {
            var baseConstant = from.Constants.FirstOrDefault(c => c.Register == r);
            e.Constants.Add(new(H(Constants[r]), (byte)r, baseConstant.Flags, 1, 1));
        }
        return e;
    }

    /// <summary>
    /// An alpha-tested surface's effect (a "c" one): Armageddon's ir_at_default's layout (the depth
    /// pass, for shadows, then the G-buffer, plain and stippled, then the material pass, plain and for
    /// objects fading in or out), every shader ours (<paramref name="vertex"/> and <paramref name="pixel"/>
    /// in that order: GuerrillaSurfaces.hlsl's DepthVertex/DepthPixel, GeometryVertex/GeometryPixel and
    /// its STIPPLED one, SurfaceVertex/SurfacePixel), each cutting the material's holes by its own alpha;
    /// the engine's texture slots and our constants as <see cref="Effect"/>'s.
    /// </summary>
    static EffectFile CutOut(EffectFile cutOutBase, EffectFile from, Family family, int layers, byte[][] vertex, byte[][] pixel)
    {
        var full = Effect(from, family, layers, vertex[3], pixel[3]);
        var e = new EffectFile { KindsPresent = cutOutBase.KindsPresent };
        cutOutBase.TechniqueMap.CopyTo(e.TechniqueMap, 0);
        e.VertexShaders.AddRange(vertex);
        e.PixelShaders.AddRange(pixel);
        e.Techniques.AddRange([new(0, 0, DepthTechnique, 0x8), new(1, 1, GeometryTechnique, 0x20), new(2, 2, GeometryFadingTechnique, 0x40),
                               new(3, 3, MaterialTechnique, 0x80), new(3, 3, MaterialFadingTechnique, 0x100)]);
        e.Textures.AddRange(full.Textures);
        e.Constants.AddRange(full.Constants);
        return e;
    }

    /// <summary>That Armageddon's ir_at_default (whose technique layout the cut-outs take) is still laid out as they take it.</summary>
    static void CheckCutOut(EffectFile e, string name)
    {
        bool ok = e.Techniques.Count == 5
            && e.Techniques[0] == new EffectFile.Technique(0, 0, DepthTechnique, 0x8)
            && e.Techniques[1] == new EffectFile.Technique(1, 1, GeometryTechnique, 0x20)
            && e.Techniques[2] == new EffectFile.Technique(2, 2, GeometryFadingTechnique, 0x40)
            && e.Techniques[3] == new EffectFile.Technique(3, 3, MaterialTechnique, 0x80)
            && e.Techniques[4] == new EffectFile.Technique(4, 4, MaterialFadingTechnique, 0x100);
        if (!ok) throw new InvalidDataException($"Armageddon's {name} isn't laid out as Guerrilla's cut-outs expect");
    }

    /// <summary>
    /// A blended surface's effect with no G-buffer pass (Guerrilla's glass, and the blended "b" ones): only the material pass, plain
    /// and blended (kinds 6 and 7, Armageddon's alpha pass), so it neither writes the G-buffer nor casts a
    /// shadow; the engine's texture slots and our constants as <see cref="Effect"/>'s.
    /// </summary>
    static EffectFile MaterialPassOnly(EffectFile from, Family family, int layers, byte[] vertex, byte[] pixel)
    {
        var full = Effect(from, family, layers, vertex, pixel);
        var e = new EffectFile { KindsPresent = (1u << 6) | (1u << 7) };
        Array.Fill(e.TechniqueMap, (sbyte)-1);
        e.TechniqueMap[6] = 0;
        e.TechniqueMap[7] = 1;
        e.VertexShaders.Add(vertex);
        e.PixelShaders.Add(pixel);
        e.Techniques.AddRange([new(0, 0, MaterialTechnique, 0x80), new(0, 0, MaterialFadingTechnique, 0x100)]);
        e.Textures.AddRange(full.Textures);
        e.Constants.AddRange(full.Constants);
        return e;
    }

    static uint H(string name) => Zones.NameHashes.PropertyHash(name);
}
