using System.Numerics;
using System.Text;

namespace RfaFormats.Meshes;

/// <summary>
/// The parts of a vehicle (.ccar_pc): wheels, suspension, doors, a walker's legs. Submesh 0 is the
/// body; every other part's submesh is stored around the part's own origin, and the part table
/// says where each goes, as a tree: each part is placed relative to its parent part, or to the
/// vehicle if it has none (the LAV's wheels sit on hubs on swing arms; a walker's toe on its bill,
/// foot, calf, thigh and hip).
///
/// The table starts at 0x148, right after the number of parts (at 0x144); 264 bytes per part:
///   +0x00 a type hash, +0x0C flags, +0x10 the parent part (−1: the vehicle), +0x1C rotation (9
///   floats, rows right, up, forward), +0x40 position (relative to the parent), +0x4C the name
///   ("wheel_fl", "thigh_rb"; NUL-terminated, then padding), +0xCC the submesh drawn (−1: none)
/// The type hash is the part type's name hashed like zone property names (lower-case CRC-32):
/// "piston" is 0x68CB5BB2, "door" 0x0196EBB2; the others aren't named yet.
///
/// The parts are in their rest pose; walkers move theirs with a skeleton (a .rig_pc), not read here.
/// Not every part is seen on the whole vehicle (see <see cref="VehiclePart.Seen"/>).
/// </summary>
public sealed class VehicleParts
{
    public const int TableStart = 0x148, RecordSize = 0x108;

    public IReadOnlyList<VehiclePart> Parts { get; }

    VehicleParts(IReadOnlyList<VehiclePart> parts) => Parts = parts;

    /// <summary>A vehicle's parts. Throws <see cref="InvalidDataException"/> if the table doesn't look right.</summary>
    /// <param name="submeshCount">How many submeshes the model has, to check the numbers read.</param>
    public static VehicleParts Read(byte[] data, int submeshCount)
    {
        int count = BitConverter.ToInt32(data, 0x144);
        if (count < 0 || count > 1000 || TableStart + count * RecordSize > data.Length) throw new InvalidDataException("bad vehicle part count");
        var records = new List<(string Name, uint Type, int Parent, Matrix4x4 Local, int Submesh)>();
        for (int i = 0; i < count; i++)
        {
            int r = TableStart + i * RecordSize;
            int parent = BitConverter.ToInt32(data, r + 0x10);
            int submesh = BitConverter.ToInt32(data, r + 0xCC);
            if (parent < -1 || parent >= count || parent == i) throw new InvalidDataException($"vehicle part {i} has parent {parent}");
            if (submesh < -1 || submesh >= submeshCount) throw new InvalidDataException($"vehicle part {i} uses submesh {submesh} of {submeshCount}");
            int end = Array.IndexOf(data, (byte)0, r + 0x4C, 64);
            string name = Encoding.ASCII.GetString(data, r + 0x4C, (end < 0 ? r + 0x4C + 64 : end) - (r + 0x4C));
            var local = new Matrix4x4(
                F32(data, r + 0x1C), F32(data, r + 0x20), F32(data, r + 0x24), 0,
                F32(data, r + 0x28), F32(data, r + 0x2C), F32(data, r + 0x30), 0,
                F32(data, r + 0x34), F32(data, r + 0x38), F32(data, r + 0x3C), 0,
                F32(data, r + 0x40), F32(data, r + 0x44), F32(data, r + 0x48), 1);
            records.Add((name, BitConverter.ToUInt32(data, r), parent, local, submesh));
        }

        // Each part's place in the vehicle: its own placement, then its parent's, and so on up.
        var placed = new Matrix4x4?[count];
        Matrix4x4 Place(int i, int depth)
        {
            if (placed[i] is Matrix4x4 known) return known;
            if (depth > count) throw new InvalidDataException("the vehicle's parts loop");
            Matrix4x4 m = records[i].Parent < 0 ? records[i].Local : records[i].Local * Place(records[i].Parent, depth + 1);
            placed[i] = m;
            return m;
        }
        return new VehicleParts(records.Select((p, i) => new VehiclePart(p.Name, p.Type, p.Parent, p.Submesh, Place(i, 0))).ToList());
    }

    static float F32(byte[] data, int at) => BitConverter.ToSingle(data, at);
}

/// <param name="Type">The part type's name hash ("piston" is 0x68CB5BB2).</param>
/// <param name="Parent">The part it hangs from, or −1 for the vehicle itself.</param>
/// <param name="Submesh">The submesh drawn for it, or −1 for none.</param>
/// <param name="Transform">Where it goes in the vehicle (points as rows: p × Transform).</param>
public sealed record VehiclePart(string Name, uint Type, int Parent, int Submesh, Matrix4x4 Transform)
{
    public const uint PistonType = 0x68CB5BB2;

    /// <summary>
    /// Whether the part is seen on the whole vehicle, in its stored place. Left out: suspension
    /// pistons (their stored place is about 1.7 m out to the side of the wheel they hang from; the
    /// game sets them from the suspension), parts named as collision shapes ("veh_proxy",
    /// "wheel_proxy", "wheelhulls", "chassiscollision"), and the tank's "treadsegment" (one link
    /// of its track, which the game repeats along it; stored 1.2 m away from everything). Parts
    /// named "debris" stay: on the Marauder flyer they are the hull's panels (which come off when
    /// it is destroyed), and they meet each other and the cockpit within a few centimetres.
    /// </summary>
    public bool Seen => Submesh >= 0 && Type != PistonType &&
                        !new[] { "proxy", "hull", "collision", "treadsegment" }.Any(word => Name.Contains(word, StringComparison.OrdinalIgnoreCase));
}
