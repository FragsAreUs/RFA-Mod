using System.Numerics;
using RfaFormats.Meshes;

namespace RfaFormats.Guerrilla;

/// <summary>
/// A zone's far-away ground: the low-detail meshes at the end of its terrain file, which the game
/// draws where the detailed sub-zones aren't loaded, made from the terrain file's own height field.
///
/// The terrain file ends with nine version-9 mesh blocks, one a sub-zone, 0xB0 bytes each (one
/// submesh, one render block); their buffers are in the .gterrain_pc, block after block: the block's
/// checksum, its 16-bit indices (a triangle strip) from the next multiple of 16, its vertices from the
/// next, then the block's last 4 bytes. A vertex (format 25, 8 bytes): x and z as (metres from the
/// zone's corner × 128) − 32,704, the height × 64, then the normal's x and z as bytes ((n + 1) ×
/// 127.5, the z byte with its top bit flipped), as ir_terrain_height_mesh_lowlod_t reads them.
///
/// Right before the nine blocks are nine 48-byte records, one a block, which place them: 3 zeros, the
/// block's box again (from the zone's corner), then the zone's corner in the world (x, 0, z) at +0x24
/// (world_08_02's 0, 0, 1275). A template's records left as they were put every zone's far-away
/// ground on the template's own square, stacked.
///
/// Each new mesh is a regular grid over its sub-zone's third of the zone both ways (edges shared with
/// the next, so no cracks), 40 cells a side, normals from the height field. It lies under the
/// detailed ground, as the game's own do (world_08_02's are 0.5 m under it on flat ground, never
/// above, and up to 22 m under on steep ground): the game draws a sub-zone's far mesh near the
/// camera too whenever that sub-zone's detailed ground hasn't marked it as covered, which a
/// sub-zone on its zone's lowest-z edge never does for its own zone (the game finds a sub-zone's
/// zone from its origin, and the edge belongs to the next zone's row). So each vertex starts
/// <see cref="Sink"/> under the ground, and wherever a cell's two triangles (as its strip makes them)
/// pass less than that over a height field sample inside the cell, the cell's corners go down by
/// the difference: peaks keep their height, hollows the mesh would bridge go under.
/// </summary>
public static class FarGround
{
    const int Cells = 40;

    /// <summary>How far under the lowest ground around it a far vertex lies, in metres (the game's own: 0.5).</summary>
    const float Sink = 0.5f;

    /// <summary>
    /// The terrain file with new far-away meshes from its height field, placed at the zone's corner
    /// (x, z in the world), and the matching .gterrain_pc. <paramref name="templateData"/> is the
    /// .gterrain_pc that went with the terrain file (for each block's last 4 bytes).
    /// </summary>
    public static (byte[] Terrain, byte[] Data) Build(byte[] terrainFile, byte[] templateData, Vector2 corner, float zoneWidth)
    {
        byte[] terrain = (byte[])terrainFile.Clone();
        var field = TerrainHeightField.Find(terrain) ?? throw new InvalidDataException("the terrain file has no height field");
        float[] heights = field.Heights(terrain);
        float Height(float x, float z)
        {
            float fx = Math.Clamp(x / field.Step, 0, field.XSamples - 1.001f), fz = Math.Clamp(z / field.Step, 0, field.ZSamples - 1.001f);
            int x0 = (int)fx, z0 = (int)fz;
            float tx = fx - x0, tz = fz - z0;
            float H(int xi, int zi) => heights[zi * field.XSamples + xi];
            return (H(x0, z0) * (1 - tx) + H(x0 + 1, z0) * tx) * (1 - tz) + (H(x0, z0 + 1) * (1 - tx) + H(x0 + 1, z0 + 1) * tx) * tz;
        }
        // A grid of far heights (i across x, j across z, from (x0, z0), cells `step` wide), each Sink
        // under the ground, then each cell's corners lowered until neither of its triangles passes
        // less than Sink over a sample inside it. Lowering a corner only lowers the cells around it, so
        // one pass leaves every cell under the ground.
        float[,] FarHeights(float x0, float z0, float step)
        {
            var far = new float[Cells + 1, Cells + 1];
            for (int j = 0; j <= Cells; j++)
                for (int i = 0; i <= Cells; i++) far[i, j] = Height(x0 + i * step, z0 + j * step) - Sink;
            for (int j = 0; j < Cells; j++)
                for (int i = 0; i < Cells; i++)
                {
                    float cx = x0 + i * step, cz = z0 + j * step, over = 0;
                    for (int zi = Math.Max(0, (int)MathF.Ceiling(cz / field.Step)); zi * field.Step <= cz + step && zi < field.ZSamples; zi++)
                        for (int xi = Math.Max(0, (int)MathF.Ceiling(cx / field.Step)); xi * field.Step <= cx + step && xi < field.XSamples; xi++)
                        {
                            float u = (xi * field.Step - cx) / step, v = (zi * field.Step - cz) / step;
                            // The strip's triangles: (i, j), (i, j + 1), (i + 1, j) and (i, j + 1), (i + 1, j), (i + 1, j + 1).
                            float surface = u + v <= 1
                                ? far[i, j] + u * (far[i + 1, j] - far[i, j]) + v * (far[i, j + 1] - far[i, j])
                                : far[i + 1, j + 1] + (1 - u) * (far[i, j + 1] - far[i + 1, j + 1]) + (1 - v) * (far[i + 1, j] - far[i + 1, j + 1]);
                            over = MathF.Max(over, surface - (heights[zi * field.XSamples + xi] - Sink));
                        }
                    if (over <= 0) continue;
                    far[i, j] -= over; far[i + 1, j] -= over; far[i, j + 1] -= over; far[i + 1, j + 1] -= over;
                }
            return far;
        }

        // The nine blocks: the version-9 mesh blocks of 0xB0 bytes with one submesh, format 25.
        var blocks = new List<int>();
        for (int at = 0; at + 0x60 <= terrain.Length; at += 4)
        {
            if (BitConverter.ToUInt32(terrain, at) != 9) continue;
            uint size = BitConverter.ToUInt32(terrain, at + 8);
            if (size != 0xB0 || at + size > terrain.Length || BitConverter.ToUInt32(terrain, at + 0xAC) != BitConverter.ToUInt32(terrain, at + 4)) continue;
            if (terrain[at + 0x21] == 25 && BitConverter.ToUInt32(terrain, at + 0x14) == 1) blocks.Add(at);
        }
        if (blocks.Count != 9) throw new InvalidDataException($"{blocks.Count} far-away mesh blocks in the terrain file, not 9");
        int Record(int n) => blocks[0] - 9 * 48 + n * 48;
        for (int n = 0; n < 9; n++)
            for (int k = 0; k < 6; k++)
                if (BitConverter.ToSingle(terrain, Record(n) + 0x0C + k * 4) != BitConverter.ToSingle(terrain, blocks[n] + 0x70 + k * 4))
                    throw new InvalidDataException($"far-away mesh block {n}'s placing record isn't before the blocks");

        var data = new MemoryStream();
        var w = new BinaryWriter(data);
        int oldStart = 0;
        float third = zoneWidth / 3;
        for (int n = 0; n < 9; n++)
        {
            int b = blocks[n];
            int oldSize = (int)BitConverter.ToUInt32(terrain, b + 0x0C);
            byte[] tail = templateData.AsSpan(oldStart + oldSize - 4, 4).ToArray();
            oldStart += oldSize;

            float x0 = n % 3 * third, z0 = n / 3 * third, step = third / Cells;
            float[,] far = FarHeights(x0, z0, step);
            var vertices = new List<(Vector3 P, Vector3 N)>();
            for (int j = 0; j <= Cells; j++)
                for (int i = 0; i <= Cells; i++)
                {
                    float x = x0 + i * step, z = z0 + j * step;
                    var normal = Vector3.Normalize(new Vector3(Height(x - 1, z) - Height(x + 1, z), 2, Height(x, z - 1) - Height(x, z + 1)));
                    vertices.Add((new Vector3(x, far[i, j], z), normal));
                }
            int V(int i, int j) => j * (Cells + 1) + i;
            // A strip per row of cells, (i, j) and (i, j + 1) in turn: as the game reads strips (a
            // triangle at an even place a, b, c; at an odd place b, a, c) each cell's two triangles,
            // facing up. Rows are joined by repeating the row's last index and the next row's first,
            // which keeps the next row on an even place.
            var strip = new List<int>();
            for (int j = 0; j < Cells; j++)
            {
                if (j > 0) { strip.Add(strip[^1]); strip.Add(V(0, j)); }
                for (int i = 0; i <= Cells; i++) { strip.Add(V(i, j)); strip.Add(V(i, j + 1)); }
            }

            int start = (int)data.Length;
            w.Write(BitConverter.ToUInt32(terrain, b + 4));  // the block's checksum
            while (data.Length % 16 != 0) w.Write((byte)0);
            int indexOffset = (int)data.Length - start;
            foreach (int i in strip) w.Write((ushort)i);
            while (data.Length % 16 != 0) w.Write((byte)0);
            int vertexOffset = (int)data.Length - start;
            foreach (var (p, nrm) in vertices)
            {
                w.Write((short)Math.Clamp(MathF.Round(p.X * 128) - 32704, short.MinValue, short.MaxValue));
                w.Write((short)Math.Clamp(MathF.Round(p.Y * 64), short.MinValue, short.MaxValue));
                w.Write((short)Math.Clamp(MathF.Round(p.Z * 128) - 32704, short.MinValue, short.MaxValue));
                w.Write((byte)Math.Clamp(MathF.Round((nrm.X + 1) * 127.5f), 0, 255));
                w.Write((byte)((byte)Math.Clamp(MathF.Round((nrm.Z + 1) * 127.5f), 0, 255) ^ 0x80));
            }
            w.Write(tail);
            int gSize = (int)data.Length - start;

            Vector3 min = new(vertices.Min(v => v.P.X), vertices.Min(v => v.P.Y), vertices.Min(v => v.P.Z));
            Vector3 max = new(vertices.Max(v => v.P.X), vertices.Max(v => v.P.Y), vertices.Max(v => v.P.Z));
            BitConverter.TryWriteBytes(terrain.AsSpan(b + 0x0C), gSize);
            BitConverter.TryWriteBytes(terrain.AsSpan(b + 0x1C), vertices.Count);
            BitConverter.TryWriteBytes(terrain.AsSpan(b + 0x3C), vertexOffset);
            BitConverter.TryWriteBytes(terrain.AsSpan(b + 0x40), strip.Count);
            BitConverter.TryWriteBytes(terrain.AsSpan(b + 0x44), indexOffset);
            float[] box = [min.X, min.Y, min.Z, max.X, max.Y, max.Z];
            for (int k = 0; k < 6; k++)
            {
                BitConverter.TryWriteBytes(terrain.AsSpan(b + 0x70 + k * 4), box[k]);
                BitConverter.TryWriteBytes(terrain.AsSpan(Record(n) + 0x0C + k * 4), box[k]);
            }
            BitConverter.TryWriteBytes(terrain.AsSpan(Record(n) + 0x24), corner.X);
            BitConverter.TryWriteBytes(terrain.AsSpan(Record(n) + 0x28), 0f);
            BitConverter.TryWriteBytes(terrain.AsSpan(Record(n) + 0x2C), corner.Y);
            int renderBlock = b + 0x60 + 48;  // its material word kept; first index, count, lowest 0, highest the last
            BitConverter.TryWriteBytes(terrain.AsSpan(renderBlock + 4), 0);
            BitConverter.TryWriteBytes(terrain.AsSpan(renderBlock + 8), strip.Count);
            BitConverter.TryWriteBytes(terrain.AsSpan(renderBlock + 12), 0);
            BitConverter.TryWriteBytes(terrain.AsSpan(renderBlock + 16), strip.Count - 1);
        }
        return (terrain, data.ToArray());
    }
}
