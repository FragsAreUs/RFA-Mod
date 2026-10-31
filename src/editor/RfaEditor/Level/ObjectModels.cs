using System.IO;
using System.Numerics;
using RfaFormats.Meshes;
using RfaFormats.Tables;
using RfaFormats.Zones;

namespace RfaEditor;

/// <summary>
/// Which model a zone object is drawn with, by the way it names one:
///   buildings and props (general_mover, rfg_mover) name their model file ("chunk_name", a
///     destructible object file) and which object in it ("uid")
///   items lying in the level ("Item": ammo, salvage, audio logs, pods, banners) name an entry of
///     items_3d.xtbl ("item_type"), whose Mesh is a static mesh (x.smeshx is x.csmesh_pc) or whose
///     character_mesh is a character mesh (x.cmeshx is x.ccmesh_pc, drawn in its rest pose)
///   placed weapons ("weapon") name a weapons.xtbl entry ("weapon_type"), whose Inventory_Item is
///     an items_3d.xtbl entry too
///   squad spawns (object_squad_spawn_node) name a squads.xtbl entry ("squad_def"); its leader
///     ("squad_leader", "member_info") is a character.xtbl entry, whose first .cmeshx file is its
///     mesh, drawn in its rest pose. Only the leader is drawn: the game places the rest of the
///     squad around them. (Civilians wear outfits made of more meshes, not read yet, so they show
///     only a head, hands and a bag.)
///   exo spawns (object_exo_spawn_node) name an exo.xtbl entry ("exo_type"), whose Character is a
///     character.xtbl entry
///   vehicle spawns (object_vehicle_spawn_node) name a spawn_group_vehicle.xtbl entry
///     ("spawn_set"); its first vehicle ("vehicle_list", "vehicle_type") is a vehicles.xtbl entry,
///     whose Mesh_Name is a vehicle model (x.carx is x.ccar_pc), drawn with its parts in their
///     rest pose (see VehicleParts).
///   turret spawns (object_turret_spawn_node) name a turrets.xtbl entry ("turret_type"), whose
///     TurretBase, TurretMiddle and TurretTop are items_3d.xtbl entries (static meshes). Each part
///     sits on the one below it: the middle at the base's tag ("turret_swivel"), the top at the
///     middle's tag. (Every static mesh has one tag; the parts meet within a few centimetres.)
/// The DLC's own tables (dlc04_squads.xtbl, ...) are searched too.
/// </summary>
/// <param name="readModel">A model by its file name, to read turret parts' tags; null if it can't be found.</param>
sealed class ObjectModels(GameAssets assets, ZoneNames names, Func<string, ModelFile?> readModel)
{
    readonly Dictionary<string, string?> found = new(StringComparer.OrdinalIgnoreCase);  // "table/entry" → model file

    /// <summary>
    /// The models an object is drawn with (one for most; a turret's base, middle and top), each
    /// with which object in its file and where it goes on the object; or null if it has none (or
    /// they can't be found yet).
    /// </summary>
    public IReadOnlyList<ObjectPart>? Of(ZoneObject obj)
    {
        if (LevelScene.ModelOf(obj) is string chunk) return [new ObjectPart(chunk, LevelScene.UidOf(obj), Matrix4x4.Identity)];
        if (names.Class(obj.ClassHash).Equals("object_turret_spawn_node", StringComparison.OrdinalIgnoreCase))
            return Text(obj, "turret_type") is string turret ? TurretModels(turret) : null;
        string? model = names.Class(obj.ClassHash).ToLowerInvariant() switch
        {
            "item" when Text(obj, "item_type") is string item => ItemModel(item),
            "weapon" when Text(obj, "weapon_type") is string weapon => WeaponModel(weapon),
            "object_squad_spawn_node" when Text(obj, "squad_def") is string squad => SquadModel(squad),
            "object_exo_spawn_node" when Text(obj, "exo_type") is string exo => ExoModel(exo),
            "object_vehicle_spawn_node" when Text(obj, "spawn_set") is string set => VehicleSpawnModel(set),
            _ => null,
        };
        return model == null ? null : [new ObjectPart(model, null, Matrix4x4.Identity)];
    }

    /// <summary>A turret's base, middle and top, each on the tag of the one below; null if any can't be found.</summary>
    IReadOnlyList<ObjectPart>? TurretModels(string turret)
    {
        if (assets.Table("turrets.xtbl") == null) return null;
        string[] items = ["TurretBase", "TurretMiddle", "TurretTop"];
        var parts = new List<ObjectPart>();
        Matrix4x4 at = Matrix4x4.Identity;  // where the next part goes
        foreach (string field in items)
        {
            string? model = Lookup("turrets.xtbl/" + turret + "/" + field, "turrets.xtbl", turret, entry =>
                entry.Element(field)?.ValueOf("Item") is { Length: > 0 } item ? ItemModel(item.Trim()) : null);
            if (model == null) continue;  // a turret without a middle, say
            if (readModel(model) is not { } mesh) return null;
            parts.Add(new ObjectPart(model, null, at));
            if (mesh.Tags.Count > 0) at = mesh.Tags[0].Placement * at;
        }
        return parts.Count > 0 ? parts : null;
    }

    /// <summary>An items_3d.xtbl entry's model file (a character mesh if it has one, else its mesh), or null.</summary>
    string? ItemModel(string item) => Lookup("items_3d.xtbl", item, entry =>
    {
        var files = Filenames(entry).ToList();
        string? characterMesh = files.FirstOrDefault(f => f.EndsWith(".cmeshx", StringComparison.OrdinalIgnoreCase));
        string? mesh = files.FirstOrDefault(f => f.EndsWith(".smeshx", StringComparison.OrdinalIgnoreCase));
        return characterMesh != null ? Path.ChangeExtension(characterMesh, ".ccmesh_pc")
             : mesh != null ? Path.ChangeExtension(mesh, ".csmesh_pc")
             : null;
    });

    /// <summary>A weapons.xtbl entry's model: its inventory item's, or null.</summary>
    string? WeaponModel(string weapon) => Lookup("weapons.xtbl", weapon, entry =>
        entry.ValueOf("Inventory_Item") is { Length: > 0 } item ? ItemModel(item.Trim()) : null);

    /// <summary>A squads.xtbl entry's model: its leader's character's, or null.</summary>
    string? SquadModel(string squad) => Lookup("squads.xtbl", squad, entry =>
        entry.Element("squad_leader")?.ValueOf("member_info") is { Length: > 0 } leader ? CharacterModel(leader.Trim()) : null);

    /// <summary>An exo.xtbl entry's model: its character's, or null.</summary>
    string? ExoModel(string exo) => Lookup("exo.xtbl", exo, entry =>
        entry.ValueOf("Character") is { Length: > 0 } character ? CharacterModel(character.Trim()) : null);

    /// <summary>A character.xtbl entry's mesh (its first .cmeshx file), or null.</summary>
    string? CharacterModel(string character) => Lookup("character.xtbl", character, entry =>
        Filenames(entry).FirstOrDefault(f => f.EndsWith(".cmeshx", StringComparison.OrdinalIgnoreCase)) is string mesh
            ? Path.ChangeExtension(mesh, ".ccmesh_pc")
            : null);

    /// <summary>A spawn_group_vehicle.xtbl entry's model: its first vehicle's, or null.</summary>
    string? VehicleSpawnModel(string set) => Lookup("spawn_group_vehicle.xtbl", set, entry =>
        entry.Element("vehicle_list")?.ValueOf("vehicle_type") is { Length: > 0 } vehicle ? VehicleModel(vehicle.Trim()) : null);

    /// <summary>A vehicles.xtbl entry's model (its Mesh_Name file), or null.</summary>
    string? VehicleModel(string vehicle) => Lookup("vehicles.xtbl", vehicle, entry =>
        entry.Element("Mesh_Name")?.ValueOf("Filename") is { Length: > 0 } mesh ? Path.ChangeExtension(mesh.Trim(), ".ccar_pc") : null);

    /// <summary>
    /// A table entry's model, worked out once. The DLCs add entries in tables of their own
    /// (dlc04_squads.xtbl next to squads.xtbl, ...), which are searched too. Null if there is none,
    /// or if the table can't be read yet (the editor is still reading the game's files), in which
    /// case it is asked again later.
    /// </summary>
    string? Lookup(string tableName, string entryName, Func<XtblElement, string?> model) =>
        Lookup(tableName + "/" + entryName, tableName, entryName, model);

    /// <param name="key">What the answer is kept under (a turret's three parts come from one entry).</param>
    string? Lookup(string key, string tableName, string entryName, Func<XtblElement, string?> model)
    {
        if (found.TryGetValue(key, out string? known)) return known;
        if (assets.Table(tableName) is not { } table) return null;
        IEnumerable<XtblFile> tables = new[] { "", "dlc01_", "dlc02_", "dlc03_", "dlc04_" }
            .Select(prefix => prefix.Length == 0 ? table : assets.Table(prefix + tableName))
            .OfType<XtblFile>();
        XtblElement? entry = tables.SelectMany(t => t.Entries).FirstOrDefault(e => XtblFile.EntryName(e).Equals(entryName, StringComparison.OrdinalIgnoreCase));
        return found[key] = entry == null ? null : model(entry);
    }

    /// <summary>Every file an entry names (its Filename fields, at any depth), in order.</summary>
    static IEnumerable<string> Filenames(XtblElement entry) =>
        Descendants(entry).Where(e => e.Name.Equals("Filename", StringComparison.OrdinalIgnoreCase) && !e.HasElements)
                          .Select(e => e.Value.Trim()).Where(f => f.Length > 0);

    static string? Text(ZoneObject obj, string property) =>
        obj.Property(property) is { Type: ZoneProperty.TextType } p && p.Text.Length > 0 ? p.Text : null;

    static IEnumerable<XtblElement> Descendants(XtblElement element) =>
        element.Elements.SelectMany(child => Descendants(child).Prepend(child));
}

/// <summary>A model an object is drawn with: its file, which object in it, and where it goes on the object (points as rows: p × Placement).</summary>
readonly record struct ObjectPart(string File, uint? Uid, Matrix4x4 Placement);
