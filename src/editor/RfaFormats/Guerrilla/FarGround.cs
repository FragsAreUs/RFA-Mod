using System.Numerics;
using RfaFormats.Meshes;

namespace RfaFormats.Guerrilla;

/// <summary>
/// A zone's far-away ground: the low-detail meshes at the end of its terrain file, which the game
/// draws where the detailed sub-zones aren't loaded, made from the terrain file's own height field.
///
/// The terrain file keeps nine version-9 mesh blocks, one a sub-zone, 0xB0 bytes each (see
/// <see cref="Block"/>); their buffers are in the .gterrain_pc, block after block: the block's
/// checksum, its 16-bit indices (a triangle strip) from the next multiple of 16, its vertices from the
/// next, then the checksum again. A vertex (format 25, 8 bytes): x and z as (metres from the zone's
/// corner × 128) − 32,704, the height × 64, then the normal's x and z as bytes ((n + 1) × 127.5, the z
/// byte with its top bit flipped), as ir_terrain_height_mesh_lowlod_t reads them. The checksum ties a
/// block to its buffer (any value, as long as both carry it): ours is a hash of the buffer.
///
/// Right before the nine blocks are nine 48-byte records, one a block, which place them: 3 zeros, the
/// block's box again (from the zone's corner), then the zone's corner in the world (x, 0, z) at +0x24
/// (world_08_02's 0, 0, 1275).
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
    /// A zone's far-away meshes from its ground: the nine records and blocks for its terrain file
    /// (<see cref="TerrainFile.FarRecords"/>, <see cref="TerrainFile.FarBlocks"/>) and its .gterrain_pc.
    /// <paramref name="heights"/> are the height field's (<paramref name="samples"/> across, <paramref name="step"/>
    /// metres apart, laid out as <see cref="TerrainHeightField.Heights"/>); <paramref name="corner"/> is the
    /// zone's corner in the world (x, z).
    /// </summary>
    public static (byte[] Records, List<byte[]> Blocks, byte[] Data) Build(float[] heights, int samples, float step, Vector2 corner, float zoneWidth)
    {
        float Height(float x, float z)
        {
            float fx = Math.Clamp(x / step, 0, samples - 1.001f), fz = Math.Clamp(z / step, 0, samples - 1.001f);
            int x0 = (int)fx, z0 = (int)fz;
            float tx = fx - x0, tz = fz - z0;
            float H(int xi, int zi) => heights[zi * samples + xi];
            return (H(x0, z0) * (1 - tx) + H(x0 + 1, z0) * tx) * (1 - tz) + (H(x0, z0 + 1) * (1 - tx) + H(x0 + 1, z0 + 1) * tx) * tz;
        }
        // A grid of far heights (i across x, j across z, from (x0, z0), cells `step` wide), each Sink
        // under the ground, then each cell's corners lowered until neither of its triangles passes
        // less than Sink over a sample inside it. Lowering a corner only lowers the cells around it, so
        // one pass leaves every cell under the ground.
        float[,] FarHeights(float x0, float z0, float cell)
        {
            var far = new float[Cells + 1, Cells + 1];
            for (int j = 0; j <= Cells; j++)
                for (int i = 0; i <= Cells; i++) far[i, j] = Height(x0 + i * cell, z0 + j * cell) - Sink;
            for (int j = 0; j < Cells; j++)
                for (int i = 0; i < Cells; i++)
                {
                    float cx = x0 + i * cell, cz = z0 + j * cell, over = 0;
                    for (int zi = Math.Max(0, (int)MathF.Ceiling(cz / step)); zi * step <= cz + cell && zi < samples; zi++)
                        for (int xi = Math.Max(0, (int)MathF.Ceiling(cx / step)); xi * step <= cx + cell && xi < samples; xi++)
                        {
                            float u = (xi * step - cx) / cell, v = (zi * step - cz) / cell;
                            // The strip's triangles: (i, j), (i, j + 1), (i + 1, j) and (i, j + 1), (i + 1, j), (i + 1, j + 1).
                            float surface = u + v <= 1
                                ? far[i, j] + u * (far[i + 1, j] - far[i, j]) + v * (far[i, j + 1] - far[i, j])
                                : far[i + 1, j + 1] + (1 - u) * (far[i, j + 1] - far[i + 1, j + 1]) + (1 - v) * (far[i + 1, j] - far[i + 1, j + 1]);
                            over = MathF.Max(over, surface - (heights[zi * samples + xi] - Sink));
                        }
                    if (over <= 0) continue;
                    far[i, j] -= over; far[i + 1, j] -= over; far[i, j + 1] -= over; far[i + 1, j + 1] -= over;
                }
            return far;
        }

        var data = new MemoryStream();
        var w = new BinaryWriter(data);
        var records = new byte[9 * 48];
        var blocks = new List<byte[]>();
        float third = zoneWidth / 3;
        for (int n = 0; n < 9; n++)
        {
            float x0 = n % 3 * third, z0 = n / 3 * third, cell = third / Cells;
            float[,] far = FarHeights(x0, z0, cell);
            var vertices = new List<(Vector3 P, Vector3 N)>();
            for (int j = 0; j <= Cells; j++)
                for (int i = 0; i <= Cells; i++)
                {
                    float x = x0 + i * cell, z = z0 + j * cell;
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

            // The buffer, first with a placeholder for the checksum (a hash of the rest, written after).
            int start = (int)data.Length;
            w.Write(0u);
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
            uint checksum = 2166136261;
            foreach (byte x in data.GetBuffer().AsSpan(start + 16, (int)data.Length - start - 16)) checksum = (checksum ^ x) * 16777619;
            if (checksum == 0) checksum = 1;
            w.Write(checksum);
            data.Position = start;
            w.Write(checksum);
            data.Position = data.Length;
            int gSize = (int)data.Length - start;

            Vector3 min = new(vertices.Min(v => v.P.X), vertices.Min(v => v.P.Y), vertices.Min(v => v.P.Z));
            Vector3 max = new(vertices.Max(v => v.P.X), vertices.Max(v => v.P.Y), vertices.Max(v => v.P.Z));
            blocks.Add(Block(checksum, gSize, vertices.Count, vertexOffset, strip.Count, indexOffset, min, max));
            float[] placing = [min.X, min.Y, min.Z, max.X, max.Y, max.Z, corner.X, 0, corner.Y];
            for (int k = 0; k < placing.Length; k++) BitConverter.TryWriteBytes(records.AsSpan(n * 48 + 0x0C + k * 4), placing[k]);
        }
        return (records, blocks, data.ToArray());
    }

    /// <summary>
    /// A far mesh's version-9 mesh block (0xB0 bytes), as the game's: version 9, the checksum, the
    /// block's size, its buffer's size; 1 level, 1 submesh at 0x60, the vertex count; vertices of 8
    /// bytes in format 25 (no texture coordinates); six floats 1 1 1 0 0 0; the vertex offset, index
    /// count and index offset in the buffer; 2-byte indices as one strip (topology 0), 1 render block;
    /// the submesh (1, its pivot at the corner, its box; two run-time words); the render block (a
    /// run-time material word, from index 0, the index count, 0, the index count - 1);
    /// 8 zero bytes and the checksum again.
    /// </summary>
    public static byte[] Block(uint checksum, int bufferSize, int vertexCount, int vertexOffset, int indexCount, int indexOffset, Vector3 min, Vector3 max)
    {
        var b = new byte[0xB0];
        void I(int at, uint v) => BitConverter.TryWriteBytes(b.AsSpan(at), v);
        void F(int at, float v) => BitConverter.TryWriteBytes(b.AsSpan(at), v);
        I(0x00, 9); I(0x04, checksum); I(0x08, 0xB0); I(0x0C, (uint)bufferSize);
        I(0x10, 1); I(0x14, 1); I(0x18, 0x60); I(0x1C, (uint)vertexCount);
        b[0x20] = 8; b[0x21] = 25;
        F(0x24, 1); F(0x28, 1); F(0x2C, 1);
        I(0x3C, (uint)vertexOffset); I(0x40, (uint)indexCount); I(0x44, (uint)indexOffset);
        b[0x48] = 2; b[0x4A] = 1;
        I(0x60, 1);
        F(0x70, min.X); F(0x74, min.Y); F(0x78, min.Z); F(0x7C, max.X); F(0x80, max.Y); F(0x84, max.Z);
        I(0x98, (uint)indexCount); I(0xA0, (uint)(indexCount - 1));
        I(0xAC, checksum);
        return b;
    }
}
