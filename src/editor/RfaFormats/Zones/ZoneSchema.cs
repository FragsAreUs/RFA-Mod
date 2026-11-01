using System.Globalization;
using System.Numerics;
using System.Text;
using RfaFormats.Tables;

namespace RfaFormats.Zones;

/// <summary>
/// What every kind of zone object is made of, from the game's own copy of the schema Volition's
/// level editor (CLOE) used: rfg_zonex_format.txt in misc.vpp_pc. It is loose XML, like the
/// tables (<see cref="XtblXml"/> reads it):
///
///   &lt;struct&gt;obj_light                 a class of object, named by the text after the tag
///     &lt;description&gt;Light&lt;/description&gt;
///     &lt;old_type&gt;...&lt;/old_type&gt;          what it was called before
///     &lt;flt&gt;atten_range               a property: its type is the tag, its name the text
///       &lt;default&gt;1.0&lt;/default&gt;
///       &lt;CLOE_ONLY&gt;CLOE_ONLY&lt;/CLOE_ONLY&gt;  markers (see <see cref="SchemaProperty"/>)
///     &lt;/flt&gt;
///   &lt;/struct&gt;
///
/// edit_obj is the base every class shares (name, pos, orient, persistent, ...); the others add
/// their own. The game's copy has 69 classes: edit_obj and 68 others. Its names hash as zone
/// files' do (<see cref="NameHashes"/>).
///
/// The schema is the editor's view of objects; Volition's tools wrote them into zone files a little
/// differently. Checked against every zone and layer in the game (313 files, 180,188 objects):
///   - Of the 294 property names (hashes) in the zones, the schema names 265; the other 29 are the
///     tools' own (oP, navpoint_data, chunk_name, uid, dynamic_links, ...). 39 of the zones' 42
///     classes are the schema's; the other 3 are written forms of its classes (below).
///   - No CLOE_ONLY property is in any zone, and every other property of a class that has objects
///     is in its objects, except two flags properties that are never set (trigger_flags).
///   - pos and orient (COMBINED_EXPORT) are written together as "oP": the position, then the
///     rotation's rows (48 bytes), in every object but navpoints, which have only "just_pos". A
///     navpoint's other COMBINED_EXPORT settings are packed into "navpoint_data" (40 bytes).
///   - obj_chunk_ref (a building or prop) is written as an rfg_mover (fixed) or a general_mover
///     (loose), with its chunk, properties, gameplay_properties and lod_properties named chunk_name,
///     Props, gameplay_props and lod_props, and its model's destruction data (uid, dest_checksum,
///     world_anchors, dynamic_links, ...). The three constraint classes are written as "constraint".
///   - Values: see <see cref="SchemaProperty.ZoneType"/>. A flags property with nothing set is left
///     out (most lights have no light_flags), as is a chunk's game_destroyed_pct at its default.
/// </summary>
public sealed class ZoneSchema
{
    public const string FileName = "rfg_zonex_format.txt";
    public const string ArchiveName = @"build\pc\cache\misc.vpp_pc";
    public const string BaseClassName = "edit_obj";

    /// <summary>Classes written into zones under other names (see the summary).</summary>
    static readonly Dictionary<string, string[]> WrittenAs = new(StringComparer.OrdinalIgnoreCase)
    {
        ["obj_chunk_ref"] = ["rfg_mover", "general_mover"],
        ["constraint_point"] = ["constraint"],
    };

    /// <summary>Properties written into zones under other names: class, the schema's name, the zone's.</summary>
    static readonly (string Class, string Name, string ZoneName)[] PropertiesWrittenAs =
    [
        ("obj_chunk_ref", "chunk", "chunk_name"),
        ("obj_chunk_ref", "properties", "Props"),
        ("obj_chunk_ref", "gameplay_properties", "gameplay_props"),
        ("obj_chunk_ref", "lod_properties", "lod_props"),
    ];

    /// <summary>
    /// Classes whose objects need data the schema doesn't describe, which Volition's tools made
    /// (each class's zone objects all have it), so they can't be made from the schema alone.
    /// </summary>
    static readonly Dictionary<string, string> NeedsToolData = new(StringComparer.OrdinalIgnoreCase)
    {
        ["navpoint"] = "its settings are packed into navpoint_data, whose layout isn't known yet",
        ["object_path_road"] = "its road is in path_road_data, made by Volition's tools",
        ["object_spline"] = "its curve is in object_spline_data, made by Volition's tools",
        ["object_anim_rig"] = "its bones are in anim_rig_children and bone_strength, made from its rig",
        ["object_logic_handler"] = "its script is in logic_handler_buffer, made by Volition's tools",
        ["obj_chunk_ref"] = "it is written as an rfg_mover or general_mover with its model's destruction data; add a copy of one instead",
        ["constraint_point"] = "it is written as a constraint with a template made by Volition's tools",
        ["constraint_hinge"] = "it is written as a constraint with a template made by Volition's tools",
        ["constraint_prism"] = "it is written as a constraint with a template made by Volition's tools",
    };

    /// <summary>
    /// Classes that can be made from the schema but that none of the game's zones and layers have
    /// (DLC included): the game may do nothing with them.
    /// </summary>
    static readonly HashSet<string> NoneInTheGame = new(StringComparer.OrdinalIgnoreCase)
    {
        "object_animation_spline", "object_cutscene_camera", "object_spawn_region", "object_ambient_behavior_region",
        "object_safehouse", "object_upgrade_node", "jump_pad", "ambush_region",
    };

    readonly Dictionary<uint, SchemaClass> byHash = [];

    public IReadOnlyList<SchemaClass> Classes { get; }

    /// <summary>
    /// Every name in the schema, for naming hashes: classes (with their old names and the names
    /// they are written under) and properties (likewise, and a list's fields).
    /// </summary>
    public IEnumerable<string> Names =>
        Classes.SelectMany(c => c.OldNames.Prepend(c.Name).Concat(c.ZoneClassNames))
               .Concat(Classes.SelectMany(c => c.OwnProperties.Concat(c.OwnProperties.SelectMany(p => p.Fields)))
                              .SelectMany(p => p.OldNames.Prepend(p.Name).Append(p.ZoneName)))
               .Distinct();

    /// <summary>edit_obj: the properties every object has.</summary>
    public SchemaClass? Base { get; }

    ZoneSchema(List<SchemaClass> classes)
    {
        Classes = classes;
        Base = classes.FirstOrDefault(c => c.Name.Equals(BaseClassName, StringComparison.OrdinalIgnoreCase));
        foreach ((string className, string name, string zoneName) in PropertiesWrittenAs)
            if (classes.FirstOrDefault(c => c.Name.Equals(className, StringComparison.OrdinalIgnoreCase))?.OwnProperties
                       .FirstOrDefault(p => p.Name.Equals(name, StringComparison.OrdinalIgnoreCase)) is { } property)
                property.ZoneName = zoneName;
        foreach (SchemaClass type in classes)
        {
            type.ZoneClassNames = WrittenAs.GetValueOrDefault(type.Name) ?? [type.Name];
            type.CantMake = type == Base ? "it is the base every class shares, not a class of objects"
                : type.EditorOnly ? "it is Volition's editor's alone: its tools turned such objects into other files, never zone objects"
                : type.Name.Equals("layer", StringComparison.OrdinalIgnoreCase) ? "it is a layer of Volition's editor, not an object"
                : NeedsToolData.GetValueOrDefault(type.Name);
            type.InTheGame = !NoneInTheGame.Contains(type.Name);
            if (type != Base) type.Inherit(Base);
        }
        // A class's own name first, then old names, then the names it is written under.
        foreach (SchemaClass type in classes)
            foreach (string name in type.OldNames.Prepend(type.Name)) byHash.TryAdd(NameHashes.ClassHash(name), type);
        foreach (SchemaClass type in classes)
            foreach (string name in type.ZoneClassNames) byHash.TryAdd(NameHashes.ClassHash(name), type);
    }

    public static ZoneSchema Parse(string text)
    {
        XtblElement document = XtblXml.Parse(text);
        XtblElement root = document.Elements.FirstOrDefault() ?? throw new InvalidDataException("the zone schema is empty");
        var classes = root.ElementsNamed("struct").Select(SchemaClass.Read).Where(c => c.Name.Length > 0).ToList();
        if (classes.Count == 0) throw new InvalidDataException("the zone schema has no classes (<struct>)");
        return new ZoneSchema(classes);
    }

    public static ZoneSchema FromBytes(byte[] data) => Parse(Encoding.Latin1.GetString(data));

    /// <summary>The schema in a copy of the game (its misc.vpp_pc); null if it isn't there.</summary>
    public static ZoneSchema? FromGame(string gameFolder)
    {
        string path = Path.Combine(gameFolder, ArchiveName);
        if (!File.Exists(path)) return null;
        Packfile pack = Packfile.Open(path);
        return pack.Find(FileName) is { } entry ? FromBytes(pack.Read(entry)) : null;
    }

    /// <summary>A class by the hash a zone stores (its name's, or an old name's), or null.</summary>
    public SchemaClass? Class(uint classHash) => byHash.GetValueOrDefault(classHash);

    public SchemaClass? Class(string name) => Class(NameHashes.ClassHash(name));

    /// <summary>
    /// A zone object's property, by its class and the property's name hash: the class's own, else
    /// edit_obj's (for classes the schema doesn't have, too), else null.
    /// </summary>
    public SchemaProperty? Property(uint classHash, uint propertyHash) =>
        Class(classHash)?.Property(propertyHash) ?? Base?.Property(propertyHash);
}

/// <summary>A class of zone object in the schema: its own properties and edit_obj's.</summary>
public sealed class SchemaClass
{
    readonly List<SchemaProperty> own;
    List<SchemaProperty> all;
    readonly Dictionary<uint, SchemaProperty> byHash = [];

    public string Name { get; }
    public uint Hash => NameHashes.ClassHash(Name);
    public string? Description { get; }
    public IReadOnlyList<string> OldNames { get; }

    /// <summary>Its objects belong in a zone itself, never in a mission layer (BASE_LAYER_ONLY).</summary>
    public bool BaseLayerOnly { get; }

    /// <summary>
    /// The editor's alone: such objects (stitch pieces, decals, ...) never went into zone files as
    /// objects; the game's tools turned them into other files (stitch lists, decal lists). The
    /// schema says so with a property named CLOE_ONLY whose default is true.
    /// </summary>
    public bool EditorOnly => own.Any(p => p.Name == "CLOE_ONLY" && p.Default == "true");

    /// <summary>The class names its objects have in zone files: its own, or rfg_mover and general_mover for obj_chunk_ref.</summary>
    public IReadOnlyList<string> ZoneClassNames { get; internal set; } = [];

    /// <summary>Why a new object of this class can't be made from the schema alone (see <see cref="NewObject"/>); null if it can.</summary>
    public string? CantMake { get; internal set; }

    /// <summary>False for the few classes that can be made but that none of the game's levels have, so the game may ignore them.</summary>
    public bool InTheGame { get; internal set; } = true;

    /// <summary>The class's own properties, as the schema lists them.</summary>
    public IReadOnlyList<SchemaProperty> OwnProperties => own;

    /// <summary>edit_obj's properties, then the class's own; one marked OVERRIDE takes edit_obj's place.</summary>
    public IReadOnlyList<SchemaProperty> Properties => all;

    SchemaClass(string name, string? description, List<string> oldNames, bool baseLayerOnly, List<SchemaProperty> properties)
    {
        Name = name;
        Description = description;
        OldNames = oldNames;
        BaseLayerOnly = baseLayerOnly;
        own = properties;
        all = properties;
        Index();
    }

    internal static SchemaClass Read(XtblElement element)
    {
        var properties = element.Elements.Select(SchemaProperty.Read).OfType<SchemaProperty>().ToList();
        return new SchemaClass(SchemaText.NameOf(element), element.ValueOf("description"),
                               element.ElementsNamed("old_type").Select(e => e.Value).Where(v => v.Length > 0).ToList(),
                               SchemaText.Marked(element, "BASE_LAYER_ONLY"), properties);
    }

    internal void Inherit(SchemaClass? baseClass)
    {
        if (baseClass == null) return;
        var names = own.Select(p => p.Name).ToHashSet(StringComparer.OrdinalIgnoreCase);
        all = [.. baseClass.own.Where(p => !names.Contains(p.Name)), .. own];
        Index();
    }

    void Index()
    {
        byHash.Clear();
        foreach (SchemaProperty property in all)
            foreach (string name in property.OldNames.Prepend(property.Name).Append(property.ZoneName))
                byHash[NameHashes.PropertyHash(name)] = property;
    }

    /// <summary>A property by the hash a zone stores (its name's, an old name's or the name it is written under), or null.</summary>
    public SchemaProperty? Property(uint propertyHash) => byHash.GetValueOrDefault(propertyHash);

    public SchemaProperty? Property(string name) => Property(NameHashes.PropertyHash(name));

    // ---- New objects ---------------------------------------------------------------------------

    static readonly uint PositionAndRotation = NameHashes.PropertyHash("oP");

    /// <summary>
    /// The properties of a new object of this class, as Volition's tools would write it into a zone
    /// with every value the schema's default: where it is and its rotation ("oP": unturned), then
    /// every property written into zones (<see cref="SchemaProperty.Exported"/>), in the schema's
    /// order. A force field also gets its box in its own space (object_space_bounding_box_min and
    /// _max, which are its bb in every force field in the game). Only for classes that can be made
    /// (<see cref="CantMake"/> is null).
    /// </summary>
    /// <param name="choicesOf">An enum's choices from its table (<see cref="SchemaPopulate"/>), for enums that have no default of their own.</param>
    public List<NewProperty> NewObject(Vector3 position, Func<SchemaPopulate, IReadOnlyList<string>>? choicesOf = null)
    {
        if (CantMake != null) throw new InvalidOperationException($"a new {Name} can't be made: {CantMake}");
        var placement = new byte[48];
        float[] numbers = [position.X, position.Y, position.Z, 1, 0, 0, 0, 1, 0, 0, 0, 1];
        for (int i = 0; i < numbers.Length; i++) BitConverter.TryWriteBytes(placement.AsSpan(i * 4), numbers[i]);
        var properties = new List<NewProperty> { new(ZoneProperty.ValueType, PositionAndRotation, placement) };
        foreach (SchemaProperty property in all.Where(p => p.Exported))
            if (property.DefaultValue(choicesOf) is { } value)
                properties.Add(new NewProperty(property.ZoneType, NameHashes.PropertyHash(property.ZoneName), value));
        if (Name.Equals("object_force_field", StringComparison.OrdinalIgnoreCase) && Property("bb")?.DefaultValue() is { } box)
        {
            properties.Add(new NewProperty(ZoneProperty.ValueType, NameHashes.PropertyHash("object_space_bounding_box_min"), box[..12]));
            properties.Add(new NewProperty(ZoneProperty.ValueType, NameHashes.PropertyHash("object_space_bounding_box_max"), box[12..]));
        }
        return properties;
    }

    /// <summary>
    /// A new object's bounding box (in the world): its box ("bb") where it is, as for trigger
    /// regions; otherwise a metre across, as the game's lights have.
    /// </summary>
    public (Vector3 Min, Vector3 Max) NewObjectBounds(Vector3 position)
    {
        if (Property("bb") is { Type: SchemaType.Box } box && box.DefaultNumbers() is { Length: 6 } corners)
            return (position + new Vector3(corners[0], corners[1], corners[2]), position + new Vector3(corners[3], corners[4], corners[5]));
        return (position - new Vector3(0.5f), position + new Vector3(0.5f));
    }

    public override string ToString() => Name;
}

/// <summary>A property for a new zone object: its type (<see cref="ZoneProperty.TextType"/> or <see cref="ZoneProperty.ValueType"/>), name hash and value.</summary>
public sealed record NewProperty(int Type, uint NameHash, byte[] Value);

/// <summary>A property's type, from its tag in the schema.</summary>
public enum SchemaType
{
    Unknown,
    Text,        // str: text
    Handle,      // obj_handle: another object's handle (-1 for none)
    Int,         // int: a whole number
    Float,       // flt: a number with a fraction
    Bool,        // bool: true or false
    Vector,      // vec: x, y, z
    Matrix,      // mat: a rotation, 3 rows of 3
    Color,       // color: red, green, blue, alpha (0 to 1)
    Box,         // box: the lowest corner, then the highest (6 numbers)
    Flags,       // flags: any of the names listed (<f>)
    Enum,        // enum: one of the names listed (<e>), or of a table's entries (<populate>)
    List,        // struct,N: up to N of the fields in it (the handles of other objects, say)
    RenderMesh,  // rendermesh: the model the editor drew the object with
}

/// <summary>
/// One property of a class in the schema. Markers: CLOE_ONLY (<see cref="EditorOnly"/>: the
/// editor's alone, not written into zone files), READ_ONLY (set by the editor, not by hand),
/// HIDDEN (not shown in the editor), COMBINED_EXPORT (written into the zone together with other
/// properties: pos and orient as "op", a navpoint's settings as its navpoint data), NO_LOAD,
/// OPEN_FILE (a file name, picked from a file dialog), OVERRIDE (replaces edit_obj's property of
/// the same name), SCRIPT_POINT.
/// </summary>
public sealed class SchemaProperty
{
    public string Name { get; }
    public uint Hash => NameHashes.PropertyHash(Name);

    /// <summary>The tag as written: "flt", "enum", "struct,16", ...</summary>
    public string Tag { get; }
    public SchemaType Type { get; }

    /// <summary>The default as written, whitespace and line breaks squeezed to single spaces; null if none is given.</summary>
    public string? Default { get; }

    public string? Description { get; }

    /// <summary>An enum's own choices (&lt;e&gt;).</summary>
    public IReadOnlyList<string> Choices { get; }

    /// <summary>Where an enum's further choices come from (&lt;populate&gt;), if anywhere.</summary>
    public SchemaPopulate? Populate { get; }

    /// <summary>A flags property's flags (&lt;f&gt;), in order: the first is bit 0.</summary>
    public IReadOnlyList<string> FlagNames { get; }

    /// <summary>For a list (struct,N): how many it holds at most, and its fields.</summary>
    public int Count { get; }
    public IReadOnlyList<SchemaProperty> Fields { get; }

    public IReadOnlyList<string> OldNames { get; }

    public bool EditorOnly { get; }
    public bool ReadOnly { get; }
    public bool Hidden { get; }
    public bool CombinedExport { get; }
    public bool NoLoad { get; }
    public bool OpenFile { get; }
    public bool Override { get; }
    public bool ScriptPoint { get; }

    /// <summary>The name it is written under in zone files: its own, except a few of obj_chunk_ref's ("chunk" is "chunk_name").</summary>
    public string ZoneName { get; internal set; }

    SchemaProperty(XtblElement element, string name, SchemaType type, int count)
    {
        Name = name;
        ZoneName = name;
        Tag = element.Name;
        Type = type;
        Count = count;
        Default = element.Element("default") is { } given ? SchemaText.Squeeze(given.Value) : null;
        Description = element.ValueOf("description");
        Choices = element.ElementsNamed("e").Select(e => e.Value).ToList();
        FlagNames = element.ElementsNamed("f").Select(e => e.Value).ToList();
        Populate = element.ValueOf("populate") is { Length: > 0 } populate ? SchemaPopulate.Parse(populate) : null;
        OldNames = element.ElementsNamed("old_param_name").Select(e => e.Value).Where(v => v.Length > 0).ToList();
        Fields = type == SchemaType.List ? element.Elements.Select(Read).OfType<SchemaProperty>().ToList() : [];
        EditorOnly = SchemaText.Marked(element, "CLOE_ONLY");
        ReadOnly = SchemaText.Marked(element, "READ_ONLY");
        Hidden = SchemaText.Marked(element, "HIDDEN");
        CombinedExport = SchemaText.Marked(element, "COMBINED_EXPORT");
        NoLoad = SchemaText.Marked(element, "NO_LOAD");
        OpenFile = SchemaText.Marked(element, "OPEN_FILE");
        Override = SchemaText.Marked(element, "OVERRIDE");
        ScriptPoint = SchemaText.Marked(element, "SCRIPT_POINT");
    }

    /// <summary>A property from its element, or null if the element isn't one (a marker, a description).</summary>
    internal static SchemaProperty? Read(XtblElement element)
    {
        string tag = element.Name.ToLowerInvariant();
        int count = 0;
        SchemaType type = tag switch
        {
            "str" => SchemaType.Text,
            "obj_handle" => SchemaType.Handle,
            "int" => SchemaType.Int,
            "flt" => SchemaType.Float,
            "bool" => SchemaType.Bool,
            "vec" => SchemaType.Vector,
            "mat" => SchemaType.Matrix,
            "color" => SchemaType.Color,
            "box" => SchemaType.Box,
            "flags" => SchemaType.Flags,
            "enum" => SchemaType.Enum,
            "rendermesh" => SchemaType.RenderMesh,
            _ when tag.StartsWith("struct,") && int.TryParse(tag[7..], NumberStyles.None, CultureInfo.InvariantCulture, out count) => SchemaType.List,
            _ => SchemaType.Unknown,
        };
        if (type == SchemaType.Unknown) return null;
        string name = SchemaText.NameOf(element);
        return name.Length == 0 ? null : new SchemaProperty(element, name, type, count);
    }

    // ---- In zone files --------------------------------------------------------------------------

    /// <summary>
    /// How it is stored in zone files (see <see cref="ZoneProperty"/>), the same in every zone in the
    /// game: str, enum and flags as text (flags: the names set, each followed by a space, as
    /// "player characters "); the rest as values of a fixed size (<see cref="ZoneSize"/>).
    /// </summary>
    public int ZoneType => Type is SchemaType.Text or SchemaType.Enum or SchemaType.Flags ? ZoneProperty.TextType : ZoneProperty.ValueType;

    /// <summary>
    /// A value's size in zone files: bool 1 byte; int, flt and obj_handle 4 (an int's -1 and a
    /// handle's "none" are FFFFFFFF); vec 12 (x, y, z); color 16 (red, green, blue, alpha); box 24
    /// (the lowest corner, then the highest); mat 36 (rows). 0 for text, which is any length.
    /// </summary>
    public int ZoneSize => Type switch
    {
        SchemaType.Bool => 1,
        SchemaType.Int or SchemaType.Float or SchemaType.Handle => 4,
        SchemaType.Vector => 12,
        SchemaType.Color => 16,
        SchemaType.Box => 24,
        SchemaType.Matrix => 36,
        _ => 0,
    };

    /// <summary>
    /// Whether Volition's tools wrote it into zone files as a property of its own: not CLOE_ONLY,
    /// not COMBINED_EXPORT (pos, orient, a navpoint's settings), and not the editor's model or lists
    /// (written as obj_links).
    /// </summary>
    public bool Exported => !EditorOnly && !CombinedExport && Name != "CLOE_ONLY" &&
                            Type is not (SchemaType.RenderMesh or SchemaType.List or SchemaType.Matrix or SchemaType.Unknown);

    /// <summary>
    /// The default as zone files store it (see <see cref="ZoneType"/>); null when Volition's tools
    /// left the property out: flags with none set. A default the schema doesn't give is the first
    /// choice for an enum (its own, else its table's from <paramref name="choicesOf"/>), and 0,
    /// false, empty text or none otherwise. An enum's default that isn't one of its choices gives
    /// way to the first choice, too: three are Guerrilla's, not in Armageddon's tables (a squad
    /// spawn's basic_squad_1, a turret spawn's turret_machinegun, an ambient behaviour's none).
    /// </summary>
    public byte[]? DefaultValue(Func<SchemaPopulate, IReadOnlyList<string>>? choicesOf = null)
    {
        float[] numbers = DefaultNumbers() ?? [];
        float Number(int i, float otherwise) => i < numbers.Length ? numbers[i] : otherwise;
        switch (Type)
        {
            case SchemaType.Text:
                return Text(Default ?? "");
            case SchemaType.Enum:
                IReadOnlyList<string> populated = Populate != null && choicesOf != null ? choicesOf(Populate) : [];
                var choices = Choices.Concat(populated).ToList();
                string? choice = Default is { Length: > 0 } given && (populated.Count == 0 || choices.Contains(given, StringComparer.OrdinalIgnoreCase))
                    ? given
                    : choices.FirstOrDefault() ?? Default;
                return Text(choice ?? "");
            case SchemaType.Flags:
                IReadOnlyList<string> set = FlagsIn(Default);
                return set.Count == 0 ? null : Text(FlagsText(set));
            case SchemaType.Bool:
                return [(byte)(Default?.Equals("true", StringComparison.OrdinalIgnoreCase) == true ? 1 : 0)];
            case SchemaType.Int:
                return BitConverter.GetBytes((int)Number(0, 0));
            case SchemaType.Handle:
                return BitConverter.GetBytes(numbers.Length > 0 && numbers[0] >= 0 ? (uint)numbers[0] : ZoneObject.None);
            case SchemaType.Float:
                return BitConverter.GetBytes(Number(0, 0));
            case SchemaType.Vector or SchemaType.Color or SchemaType.Box or SchemaType.Matrix:
                float[] otherwise = Type switch
                {
                    SchemaType.Color => [1, 1, 1, 1],
                    SchemaType.Box => [-0.5f, -0.5f, -0.5f, 0.5f, 0.5f, 0.5f],
                    SchemaType.Matrix => [1, 0, 0, 0, 1, 0, 0, 0, 1],
                    _ => [0, 0, 0],
                };
                var value = new byte[ZoneSize];
                for (int i = 0; i < otherwise.Length; i++)
                    BitConverter.TryWriteBytes(value.AsSpan(i * 4), numbers.Length == otherwise.Length ? numbers[i] : otherwise[i]);
                return value;
            default:
                return null;
        }
    }

    /// <summary>Text as zone files store it: Latin-1, then a NUL.</summary>
    static byte[] Text(string text) => [.. Encoding.Latin1.GetBytes(text), 0];

    /// <summary>The flags named in a flags value ("player characters ", or a default such as "player characters").</summary>
    public static IReadOnlyList<string> FlagsIn(string? text) => (text ?? "").Split(' ', StringSplitOptions.RemoveEmptyEntries);

    /// <summary>Flags as zone files store them: each name followed by a space.</summary>
    public static string FlagsText(IEnumerable<string> flags) => string.Concat(flags.Select(f => f + " "));

    /// <summary>
    /// The default's numbers (for numbers, vectors, rotations, colours and boxes), leniently: the
    /// file has "-1.0f" and "80.0>". Null when there is no default or it isn't numbers.
    /// </summary>
    public float[]? DefaultNumbers()
    {
        if (Default == null) return null;
        var numbers = new List<float>();
        foreach (string part in Default.Split(' ', StringSplitOptions.RemoveEmptyEntries))
        {
            string trimmed = part.TrimEnd('f', 'F', '>');
            if (!float.TryParse(trimmed, NumberStyles.Float, CultureInfo.InvariantCulture, out float number)) return null;
            numbers.Add(number);
        }
        return numbers.Count > 0 ? numbers.ToArray() : null;
    }

    public override string ToString() => $"{Tag} {Name}";
}

/// <summary>
/// Where an enum's choices come from: "squads.xtbl;table;Squads;Name" is the Name of every Squads
/// entry in squads.xtbl's table; "territories.xtbl;Table;Territory;Districts;District;Name" goes
/// further down, into each entry's Districts. Each step matches elements by name, ignoring case.
/// </summary>
public sealed record SchemaPopulate(string Table, IReadOnlyList<string> Path)
{
    public static SchemaPopulate Parse(string text)
    {
        string[] parts = text.Split(';', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries);
        return new SchemaPopulate(parts.Length > 0 ? parts[0] : "", parts.Skip(1).ToList());
    }

    /// <summary>The choices in a table: the text of every element at the end of the path, each once, in the table's order.</summary>
    public IReadOnlyList<string> Values(XtblFile table)
    {
        if (Path.Count == 0 || table.Root == null) return [];
        // The path starts at the root's children ("table"), or at the root itself in a file whose root is named so.
        IEnumerable<XtblElement> at = table.Root.Name.Equals(Path[0], StringComparison.OrdinalIgnoreCase) ? [table.Root] : table.Root.ElementsNamed(Path[0]);
        foreach (string step in Path.Skip(1)) at = at.SelectMany(e => e.ElementsNamed(step));
        return at.Select(e => e.Value).Where(v => v.Length > 0).Distinct(StringComparer.OrdinalIgnoreCase).ToList();
    }

    public override string ToString() => $"{Table};{string.Join(";", Path)}";
}

static class SchemaText
{
    /// <summary>The name after an element's tag: its own text before the first child element.</summary>
    public static string NameOf(XtblElement element)
    {
        var name = new StringBuilder();
        foreach (XtblPart part in element.Parts)
        {
            if (part is XtblElement) break;
            if (part is XtblText text) name.Append(text.Raw);
        }
        return XtblElement.Decode(name.ToString()).Trim();
    }

    /// <summary>Whether a marker element is there (&lt;HIDDEN&gt;HIDDEN&lt;/HIDDEN&gt;, &lt;NO_LOAD&gt;true&lt;/NO_LOAD&gt;); "false" says it isn't.</summary>
    public static bool Marked(XtblElement element, string marker) =>
        element.Element(marker) is { } found && !found.Value.Trim('>', ' ').Equals("false", StringComparison.OrdinalIgnoreCase);

    public static string Squeeze(string text) => string.Join(' ', text.Split((char[]?)null, StringSplitOptions.RemoveEmptyEntries));
}
