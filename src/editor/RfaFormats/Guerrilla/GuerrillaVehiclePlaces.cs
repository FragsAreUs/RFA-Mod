using System.Text;
using RfaFormats.Zones;

namespace RfaFormats.Guerrilla;

/// <summary>
/// Red Faction: Guerrilla's roads and parked vehicles, as Armageddon's zone objects, for the world builder.
/// Both games have the classes, written the same way by Volition's tools:
///   - object_path_road: a road (its points, each a position, a curve handle and a width; then its
///     junctions, each another road by handle and the points joined) in path_road_struct and
///     path_road_data, its flags (ambient traffic, one way, special, no reinforcements, GPS only) in
///     path_road_flags, and the tools' own prev/NEXT, op, Outer_Radius and speed_limit. Guerrilla's 261
///     roads are kept as they are with their handles (their junctions name each other by handle, across
///     zones), and the world builder puts them in the zone's persistent p_ zone alone: Guerrilla has each in
///     both (its world keeps one object per handle), but Armageddon's made two objects of a road while its
///     zone was loaded, and its road search never reached the second (GuerrillaWorld, format 13).
///   - object_vehicle_spawn_node: a parked vehicle's spot: the vehicle spawn group it picks from
///     (spawn_set), its chance, and whether the player's chosen vehicle may come back there. The group is
///     Guerrilla's of that name, which the vehicles part writes into spawn_group_vehicle.xtbl as
///     <see cref="GroupPrefix"/> + its name (three of Guerrilla's names are Armageddon's too). Guerrilla's
///     night chance, special vehicle and no-parking flags aren't read by Armageddon's.
/// </summary>
public static class GuerrillaVehiclePlaces
{
    /// <summary>Guerrilla's vehicle spawn groups' names in Armageddon's table: this, then Guerrilla's name.</summary>
    public const string GroupPrefix = "rfg_";

    const uint None = 0xFFFFFFFF;
    static readonly uint Road = NameHashes.ClassHash("object_path_road"), Node = NameHashes.ClassHash("object_vehicle_spawn_node");

    /// <summary>The order Volition's tools wrote each class's properties in (Armageddon's own levels).</summary>
    static readonly string[] RoadOrder = ["prev", "NEXT", "path_road_data", "path_road_struct", "op", "Outer_Radius", "path_road_flags", "speed_limit"];
    static readonly string[] NodeOrder = ["spawn_set", "op", "spawn_prob", "player_vehicle_respawn"];

    /// <summary>
    /// A zone's roads and vehicle spawn nodes (from Guerrilla's zone, <paramref name="zone"/>) as Armageddon's
    /// objects. Roads keep Guerrilla's handles; one whose handle is taken (in <paramref name="taken"/>, which
    /// gains the handles used) is left out and named in <paramref name="problems"/>. Nodes keep Guerrilla's
    /// handles unless taken.
    /// </summary>
    /// <param name="corner">The zone's square: its corner (x, z), <paramref name="width"/> on each side.</param>
    /// <param name="moved">Gains each road whose own position was moved into the zone's square (see <see cref="InsideSquare"/>).</param>
    public static (List<ZoneObject> Roads, List<ZoneObject> Nodes) Convert(IReadOnlyDictionary<uint, GuerrillaZoneObject> zone, ZoneSchema schema,
                                                                          ISet<uint> taken, List<string> problems, (float X, float Z) corner, float width,
                                                                          List<string> moved)
    {
        var roads = new List<ZoneObject>();
        var nodes = new List<ZoneObject>();
        foreach (var o in zone.Values.Where(o => o.ClassHash == Road))
        {
            if (!taken.Add(o.Handle)) { problems.Add($"road {o.Handle:X8}: its handle is another object's"); continue; }
            var properties = RoadOrder.Where(name => o.Properties.ContainsKey(NameHashes.PropertyHash(name))).Select(name => Own(o, name)).ToList();
            InsideSquare(o.Handle, properties, corner, width, moved);
            roads.Add(Make(o, o.Handle, properties));
        }
        foreach (var o in zone.Values.Where(o => o.ClassHash == Node))
        {
            uint handle = o.Handle;
            while (!taken.Add(handle)) handle = (handle & 0xFFFF0000) | ((handle + 1) & 0xFFFF);
            nodes.Add(Make(o, handle, NodeOrder.Select(name => name switch
            {
                "spawn_set" => new NewProperty(ZoneProperty.TextType, NameHashes.PropertyHash(name), Text(GroupPrefix + (o.Text(name) ?? "Default Guerilla"))),
                _ when o.Properties.ContainsKey(NameHashes.PropertyHash(name)) => Own(o, name),
                _ => Default(schema, "object_vehicle_spawn_node", name),
            })));
        }
        return (roads, nodes);
    }

    /// <summary>
    /// A road's own position (op) kept inside its zone's square. When a zone streams out, Armageddon keeps each
    /// object that asks to be made again from the zone's file on its list for that only if the object's position
    /// lies in the zone its handle names (0x6CCCA0, the handle's top byte against the zone at its position; Guerrilla
    /// has no such check). Otherwise it saves the object's state instead; a road can't ("path roads serialize out not
    /// implemented"), so it's dropped, and isn't made again when the zone streams back in, nor in a save made after
    /// ("... is attempting to OSM_RESTORE despite not physically being in original zone"). 21 of Guerrilla's 261
    /// roads, those running to a zone's edge, have their position (their first point) 0.03-1.4 m over it; no other
    /// object of Guerrilla's world is outside its zone. Their position alone is moved 0.5 m inside; the road's
    /// points (world positions, path_road_data) and everything else stay as Guerrilla's.
    /// </summary>
    static void InsideSquare(uint handle, List<NewProperty> properties, (float X, float Z) corner, float width, List<string> moved)
    {
        const float Margin = 0.5f;
        int op = properties.FindIndex(p => p.NameHash == NameHashes.PropertyHash("op"));
        if (op < 0 || properties[op].Value.Length < 12) return;
        byte[] value = properties[op].Value;
        float x = BitConverter.ToSingle(value, 0), z = BitConverter.ToSingle(value, 8);
        if (x >= corner.X && x < corner.X + width && z >= corner.Z && z < corner.Z + width) return;  // in it: as Guerrilla's
        float inX = Math.Clamp(x, corner.X + Margin, corner.X + width - Margin), inZ = Math.Clamp(z, corner.Z + Margin, corner.Z + width - Margin);
        byte[] inside = (byte[])value.Clone();
        BitConverter.TryWriteBytes(inside.AsSpan(0, 4), inX);
        BitConverter.TryWriteBytes(inside.AsSpan(8, 4), inZ);
        properties[op] = new NewProperty(properties[op].Type, properties[op].NameHash, inside);
        moved.Add($"{handle:X8} {MathF.Sqrt((inX - x) * (inX - x) + (inZ - z) * (inZ - z)):0.##} m");
    }

    static ZoneObject Make(GuerrillaZoneObject o, uint handle, IEnumerable<NewProperty> properties) =>
        Detached(ZoneObject.Create(o.ClassHash, handle, o.Number, o.BoundsMin, o.BoundsMax, [.. properties], 0));

    static ZoneObject Detached(ZoneObject obj)
    {
        obj.Parent = obj.Sibling = obj.Child = None;
        return obj;
    }

    static NewProperty Own(GuerrillaZoneObject o, string name)
    {
        var (type, raw) = o.Properties[NameHashes.PropertyHash(name)];
        return new NewProperty(type, NameHashes.PropertyHash(name), raw);
    }

    static NewProperty Default(ZoneSchema schema, string className, string name)
    {
        var property = schema.Class(className)?.Property(name) ?? throw new InvalidDataException($"the schema's {className} has no {name}");
        return new NewProperty(property.ZoneType, NameHashes.PropertyHash(property.ZoneName),
                               property.DefaultValue() ?? throw new InvalidDataException($"the schema has no default for {className}'s {name}"));
    }

    static byte[] Text(string text) => [.. Encoding.Latin1.GetBytes(text), 0];
}
