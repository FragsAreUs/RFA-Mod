using System.Numerics;

namespace RfaFormats.Zones;

/// <summary>
/// A lamp's animation curve (.lightmult_pc, .lightpos_pc, .lightrot_pc, in the areas' precache
/// archives): how its brightness, position or turn changes over time. A lamp names them in its
/// multi_curve, trans_curve and rot_curve properties ("la_fire_flicker_001.lightmult" is the file
/// la_fire_flicker_001.lightmult_pc); 1,221 lamps flicker like fire, 1,013 strobe, 343 are broken.
///
/// Layout: "LITE", version (4), kind (u8: 3 brightness, 1 position, 0 turn), 0 (u16), the number of
/// keys (u16), then the keys: a time in seconds and the value (a brightness: one float, a multiplier;
/// a position: three, an offset in metres; a turn: four, a rotation as x, y, z, w). The last key's
/// time is the curve's length; looping curves end on the value they start with. All 15 in the game
/// read this way (fire flicker, strobes, pulses, a broken light, lightning, a flashlight's sway).
/// </summary>
public sealed class LightCurve
{
    public int Kind { get; }
    public IReadOnlyList<(float Time, Vector4 Value)> Keys { get; }
    public float Length => Keys.Count > 0 ? Keys[^1].Time : 0;

    LightCurve(int kind, List<(float, Vector4)> keys)
    {
        Kind = kind;
        Keys = keys;
    }

    public static LightCurve Read(byte[] data)
    {
        if (data.Length < 10 || data[0] != 'L' || data[1] != 'I' || data[2] != 'T' || data[3] != 'E')
            throw new InvalidDataException("not a light curve (no \"LITE\" at the start)");
        int kind = data[5], count = BitConverter.ToUInt16(data, 8);
        if (count == 0) return new LightCurve(kind, []);
        int stride = (data.Length - 10) / count;  // 8 for brightness, 16 for a position
        if (stride < 8 || stride > 20 || stride % 4 != 0 || 10 + count * stride != data.Length)
            throw new InvalidDataException($"a light curve of {count} keys doesn't fit its {data.Length} bytes");
        var keys = new List<(float, Vector4)>(count);
        for (int i = 0; i < count; i++)
        {
            int at = 10 + i * stride;
            var value = new float[4];
            for (int v = 0; v < (stride - 4) / 4; v++) value[v] = BitConverter.ToSingle(data, at + 4 + v * 4);
            keys.Add((BitConverter.ToSingle(data, at), new Vector4(value[0], value[1], value[2], value[3])));
        }
        return new LightCurve(kind, keys);
    }

    /// <summary>The value at a time, going round and round (straight lines between keys).</summary>
    public Vector4 At(float seconds)
    {
        if (Keys.Count == 0) return Vector4.Zero;
        if (Keys.Count == 1 || Length <= 0) return Keys[0].Value;
        float t = seconds % Length;
        if (t < 0) t += Length;
        for (int i = 1; i < Keys.Count; i++)
        {
            if (Keys[i].Time < t) continue;
            (float t0, Vector4 v0) = Keys[i - 1];
            (float t1, Vector4 v1) = Keys[i];
            return t1 > t0 ? Vector4.Lerp(v0, v1, (t - t0) / (t1 - t0)) : v1;
        }
        return Keys[^1].Value;
    }
}
