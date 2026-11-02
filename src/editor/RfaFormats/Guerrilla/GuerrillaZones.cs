using System.Numerics;
using System.Text;
using RfaFormats.Zones;

namespace RfaFormats.Guerrilla;

/// <summary>
/// The objects in Red Faction: Guerrilla's zone files (.rfgzone_pc, "ZONE" version 36, in its
/// zonescript_terr01.vpp_pc). Laid out as Armageddon's (version 46) but for the header: "ZONE", the
/// version, the object count, the handle count, a word, flags (1: no relation table), then a handle
/// each, the relation table (87,368 bytes, unless flags say none), and the objects. An object is the
/// same as Armageddon's: class name hash, handle, box (6 floats), flags, size, parent, sibling, child,
/// number, property count at +52, properties from +56 (type, value size, name hash, value, padding to
/// 4), named and hashed the same way.
/// </summary>
public static class GuerrillaZones
{
    public const uint Signature = 0x454E4F5A;  // "ZONE"
    public const uint Version = 36;
    const int RelationTableSize = 87368, HeaderSize = 56;

    /// <summary>The objects of one of Guerrilla's zone files.</summary>
    public static List<GuerrillaZoneObject> Objects(byte[] zone)
    {
        if (zone.Length < 24 || BitConverter.ToUInt32(zone, 0) != Signature || BitConverter.ToUInt32(zone, 4) != Version)
            throw new InvalidDataException("not a Guerrilla zone file (ZONE version 36)");
        int count = BitConverter.ToInt32(zone, 8), handles = BitConverter.ToInt32(zone, 12);
        uint flags = BitConverter.ToUInt32(zone, 20);
        int at = 24 + handles * 4 + ((flags & 1) == 0 ? RelationTableSize : 0);
        var objects = new List<GuerrillaZoneObject>();
        for (int i = 0; i < count; i++)
        {
            if (at + HeaderSize > zone.Length) throw new InvalidDataException("the zone's objects run past its end");
            int size = BitConverter.ToUInt16(zone, at + 34), propertyCount = BitConverter.ToUInt16(zone, at + 52);
            var properties = new Dictionary<uint, (int Type, byte[] Value)>();
            int p = at + HeaderSize;
            for (int k = 0; k < propertyCount; k++)
            {
                int type = BitConverter.ToUInt16(zone, p), valueSize = BitConverter.ToUInt16(zone, p + 2);
                properties.TryAdd(BitConverter.ToUInt32(zone, p + 4), (type, zone.AsSpan(p + 8, valueSize).ToArray()));
                p = (p + 8 + valueSize + 3) & ~3;
            }
            Vector3 V(int o) => new(BitConverter.ToSingle(zone, o), BitConverter.ToSingle(zone, o + 4), BitConverter.ToSingle(zone, o + 8));
            uint U(int o) => BitConverter.ToUInt32(zone, at + o);
            objects.Add(new GuerrillaZoneObject(U(0), U(4), V(at + 8), V(at + 20), BitConverter.ToUInt16(zone, at + 32), U(36), U(40), U(44), U(48), properties));
            at += size;
        }
        return objects;
    }
}

/// <summary>
/// An object in one of Guerrilla's zones: its class (hashed as Armageddon's), handle, box (world
/// coordinates), flags (as Armageddon's in their low byte: 0x01 persistent, only in the p_ zones;
/// 0x08 not made in the game; 0x10 streaming_fixed, set on movers attached to nothing, which the game
/// keeps among Havok's fixed objects; 0x04 the tools', which neither game's loader reads; the high
/// byte is left over from the tools, 0x3F, 0x7F or 0xFF), the objects it is attached to (parent, the
/// next sibling, its first child: 0xFFFFFFFF for none), its number in Volition's editor, and its
/// properties by name hash.
/// </summary>
public sealed record GuerrillaZoneObject(uint ClassHash, uint Handle, Vector3 BoundsMin, Vector3 BoundsMax, ushort Flags, uint Parent, uint Sibling, uint Child, uint Number,
                                        IReadOnlyDictionary<uint, (int Type, byte[] Value)> Properties)
{
    /// <summary>A property's value, or null.</summary>
    public byte[]? Value(string name) => Properties.TryGetValue(NameHashes.PropertyHash(name), out var p) ? p.Value : null;

    /// <summary>A text property, or null.</summary>
    public string? Text(string name) => Value(name) is { } v ? Encoding.Latin1.GetString(v).TrimEnd('\0') : null;
}
