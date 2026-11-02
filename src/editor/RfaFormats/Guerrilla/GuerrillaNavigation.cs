using System.Numerics;
using RfaFormats.Meshes;

namespace RfaFormats.Guerrilla;

/// <summary>
/// Red Faction: Guerrilla's navigation meshes (in its zones' terrain files, after the cliff
/// materials: see <see cref="GuerrillaTerrain.PathfindingAt"/>) as Armageddon's navigation files (see
/// <see cref="NavigationFile"/>). Both games keep the same meshes: Guerrilla's layers are version
/// 0x00020000, with 32-byte cells and no pools. Each layer is ported as it is: its vertices, its cells
/// (neighbours, vertices, hull reference, heap index, zone, flags), its spare cell slots, outer edges
/// and box, and its Havok tree (the same MOPP block, keyed by the same cell numbers). Armageddon's
/// four bytes on each cell (its island and the zone's place) and its pools are made from the mesh
/// (<see cref="NavigationLayer.SwitchedOnIslands"/> for the people's layer, whose islands the game
/// checks: Guerrilla's cells that its missions switch on can split them;
/// <see cref="NavigationLayer.Islands"/> for the vehicles'; <see cref="NavigationLayer.PlainPools"/>),
/// as Volition's tools made them for the campaign's zones with nothing computed in the pools. A zone without
/// navigation (the scenery around the playable map) gets a file with none.
/// </summary>
public static class GuerrillaNavigation
{
    const int GuerrillaCellSize = 32;

    /// <summary>The navigation file for zone (<paramref name="column"/>, <paramref name="row"/>) from Guerrilla's terrain file of the same zone.</summary>
    public static NavigationFile Convert(byte[] terrain, int column, int row)
    {
        int at = GuerrillaTerrain.PathfindingAt(terrain);
        if (at < 0) return NavigationFile.None;
        int I(int offset) => BitConverter.ToInt32(terrain, offset);
        byte place = (byte)(column << 4 | row);
        (NavigationLayer?, byte[]?) Layer(int k, bool people)
        {
            int offset = I(at + 8 + k * 16), size = I(at + 12 + k * 16), tree = I(at + 16 + k * 16), treeSize = I(at + 20 + k * 16);
            if (size <= 0) return (null, null);
            return (ReadLayer(terrain, offset, place, people), terrain[tree..(tree + treeSize)]);
        }
        var (people, peopleTree) = Layer(0, true);
        var (vehicles, vehiclesTree) = Layer(1, false);
        return new NavigationFile(0, people, peopleTree, vehicles, vehiclesTree);
    }

    /// <summary>One of Guerrilla's layers (bb_pfg_layer, offsets from its start) as Armageddon's.</summary>
    static NavigationLayer ReadLayer(byte[] d, int at, byte place, bool people)
    {
        uint U(int offset) => BitConverter.ToUInt32(d, at + offset);
        Vector3 V(int offset) => new(BitConverter.ToSingle(d, offset), BitConverter.ToSingle(d, offset + 4), BitConverter.ToSingle(d, offset + 8));
        if (U(0) != NavigationLayer.Signature) throw new InvalidDataException("no navigation layer (FRPF) where Guerrilla's pathfinding says");
        int vertexCount = BitConverter.ToUInt16(d, at + 8), vertices = (int)U(0xC), cellCount = BitConverter.ToUInt16(d, at + 0x10),
            cells = (int)U(0x14), heap = (int)U(0x18), edgeCount = BitConverter.ToUInt16(d, at + 0x1C), edges = (int)U(0x20);
        if ((heap - cells) % GuerrillaCellSize != 0 || heap - cells < cellCount * GuerrillaCellSize)
            throw new InvalidDataException("Guerrilla's navigation cells aren't where its layer says");
        int slots = (heap - cells) / GuerrillaCellSize;
        var cellList = Enumerable.Range(0, cellCount).Select(i => NavigationCell.Read(d, at + cells + i * GuerrillaCellSize, cells, GuerrillaCellSize)).ToList();
        byte[] islands = people ? NavigationLayer.SwitchedOnIslands(cellList) : NavigationLayer.Islands(cellList);
        var (words, pools) = NavigationLayer.PlainPools(cellCount == 0 ? 0 : islands.Max() + 1, place, people);
        return new NavigationLayer(NavigationLayer.ArmageddonVersion,
            [.. Enumerable.Range(0, vertexCount).Select(i => V(at + vertices + i * 12))],
            [.. cellList.Select((c, i) => c with { Island = islands[i], Place = place, Extra0 = 0xFF, Extra1 = 0 })],
            slots - cellCount,
            [.. Enumerable.Range(0, edgeCount).Select(i => NavigationEdge.Read(d, at + edges + i * 8))],
            V(at + 0x24), V(at + 0x30), words, pools);
    }
}
