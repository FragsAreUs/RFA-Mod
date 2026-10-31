using System.Globalization;
using System.IO;
using System.IO.MemoryMappedFiles;
using System.Numerics;

namespace RfaEditor;

/// <summary>
/// The link between the level editor and the running game (started through RFAModLauncher, with
/// the console mod; see src\mods\console\console.cpp):
///   the game shows the editor what it shows, every frame, in a small block of shared memory
///     named "Local\RFAModEditorLink": the object picked there (the console's debug target: Q in
///     slew mode, or target_by_handle), where its camera is, which way it looks and its field of
///     view, and when it last wrote them (see <see cref="Read"/> for the layout);
///   the editor sends the game console commands in bin\link\to_game.txt, which the game checks
///     ten times a second, runs (as if typed into its console) and deletes;
///   the editor asks where objects are now in bin\link\where_wanted.txt (handles), and the game
///     answers in bin\link\where_now.txt (see <see cref="AskWhere"/>).
/// Positions are the game's world coordinates, the same as the zone files'.
/// </summary>
sealed class GameLink : IDisposable
{
    const string SharedName = @"Local\RFAModEditorLink";
    const uint SharedMagic = 0x4C414652;  // "RFAL"
    const int SharedSize = 80;

    readonly string folder;
    MemoryMappedFile? shared;
    MemoryMappedViewAccessor? view;
    DateTime lastTriedToOpen;

    public GameLink(string modFolder)
    {
        folder = Path.Combine(modFolder, "link");
        // The game only checks for commands while this folder is there.
        Directory.CreateDirectory(folder);
    }

    /// <summary>
    /// What the game shows now, or null if it isn't running with the console mod (or hasn't been
    /// since the editor started). The shared memory, version 1 (80 bytes):
    ///   0  "RFAL"          4  version (1)       8  sequence (odd while the game writes)
    ///   12 frame count     16 when written (Windows file time, UTC)    24 picked handle
    ///   28 camera position (3 floats)   40 its rows: right, up, forward (9 floats)   76 vertical FOV
    /// The sequence is read before and after the rest; if it changed or was odd, the game was
    /// writing, and it is read again.
    /// </summary>
    public GameView? Read()
    {
        if (view == null && !Open()) return null;
        for (int attempt = 0; attempt < 10; attempt++)
        {
            uint sequence = view!.ReadUInt32(8);
            if ((sequence & 1) != 0)
            {
                Thread.SpinWait(50);
                continue;
            }
            Interlocked.MemoryBarrier();
            uint magic = view.ReadUInt32(0), version = view.ReadUInt32(4), frame = view.ReadUInt32(12), picked = view.ReadUInt32(24);
            long written = view.ReadInt64(16);
            var camera = new Vector3(view.ReadSingle(28), view.ReadSingle(32), view.ReadSingle(36));
            var forward = new Vector3(view.ReadSingle(64), view.ReadSingle(68), view.ReadSingle(72));
            float fov = view.ReadSingle(76);
            Interlocked.MemoryBarrier();
            if (view.ReadUInt32(8) != sequence) continue;
            if (magic != SharedMagic || version != 1 || written <= 0 || written > DateTime.MaxValue.ToFileTimeUtc() || forward.Length() < 0.5f) return null;
            return new GameView(picked is 0 or 0xFFFFFFFF ? null : picked, camera, Vector3.Normalize(forward),
                                fov is > 1 and < 179 ? fov : null, frame, DateTime.FromFileTimeUtc(written));
        }
        return null;
    }

    /// <summary>Opens the game's shared memory, trying at most once a second while it isn't there.</summary>
    bool Open()
    {
        if (DateTime.UtcNow - lastTriedToOpen < TimeSpan.FromSeconds(1)) return false;
        lastTriedToOpen = DateTime.UtcNow;
        try
        {
            shared = MemoryMappedFile.OpenExisting(SharedName, MemoryMappedFileRights.Read);
            view = shared.CreateViewAccessor(0, SharedSize, MemoryMappedFileAccess.Read);
            return true;
        }
        catch (Exception ex) when (ex is FileNotFoundException or IOException or UnauthorizedAccessException)
        {
            shared?.Dispose();
            shared = null;
            return false;
        }
    }

    /// <summary>Whether the game is running with the console mod: it wrote in the last few seconds.</summary>
    public static bool IsRecent(GameView? view) => view != null && DateTime.UtcNow - view.Written < TimeSpan.FromSeconds(3);

    /// <summary>Hands console commands to the game, which runs them within a tenth of a second.</summary>
    public void Send(IEnumerable<string> commands)
    {
        string path = Path.Combine(folder, "to_game.txt"), temporary = path + ".tmp";
        File.WriteAllLines(temporary, commands);
        File.Move(temporary, path, overwrite: true);
    }

    /// <summary>
    /// Asks the game where objects are now (it answers within a tenth of a second; see
    /// <see cref="ReadWhere"/>): physics moves clutter, which falls and settles when the level starts.
    /// </summary>
    public void AskWhere(IEnumerable<uint> handles)
    {
        string path = Path.Combine(folder, "where_wanted.txt"), temporary = path + ".tmp";
        File.WriteAllLines(temporary, handles.Select(h => $"0x{h:X8}"));
        File.Move(temporary, path, overwrite: true);
    }

    /// <summary>
    /// The game's answer to <see cref="AskWhere"/>, if it has come (it is then used up): for each
    /// handle asked about, its position and rotation (rows right, up, forward) in the game now, or
    /// null if the game has no such object now (not loaded, or broken up). A line of the answer is
    /// the handle (0x...), then 3 numbers for the position and 9 for the rotation, or "gone".
    /// </summary>
    public Dictionary<uint, (Vector3 Position, Matrix4x4 Rotation)?>? ReadWhere()
    {
        string path = Path.Combine(folder, "where_now.txt");
        string[] lines;
        try
        {
            if (!File.Exists(path)) return null;
            lines = File.ReadAllLines(path);
            File.Delete(path);
        }
        catch (IOException) { return null; }  // the game is just writing it: next time
        var answer = new Dictionary<uint, (Vector3, Matrix4x4)?>();
        foreach (string line in lines)
        {
            string[] parts = line.Split(' ', StringSplitOptions.RemoveEmptyEntries);
            if (parts.Length < 2 || !parts[0].StartsWith("0x") ||
                !uint.TryParse(parts[0].AsSpan(2), NumberStyles.HexNumber, CultureInfo.InvariantCulture, out uint handle)) continue;
            if (parts.Length != 13)
            {
                answer[handle] = null;
                continue;
            }
            float[] n = parts.Skip(1).Select(p => float.Parse(p, CultureInfo.InvariantCulture)).ToArray();
            answer[handle] = (new Vector3(n[0], n[1], n[2]), new Matrix4x4(n[3], n[4], n[5], 0, n[6], n[7], n[8], 0, n[9], n[10], n[11], 0, 0, 0, 0, 1));
        }
        return answer;
    }

    /// <summary>
    /// The commands that show a place in the game: the camera moved (in slew mode, the game's free
    /// camera) to <paramref name="distance"/> back from it along the way the game's camera looks,
    /// so it is in the middle of the view; and, if given, the object picked there by its handle.
    /// </summary>
    public static IEnumerable<string> ShowCommands(Vector3 place, float distance, Vector3 gameForward, uint? handle)
    {
        Vector3 camera = place - gameForward * distance;
        yield return string.Create(CultureInfo.InvariantCulture, $"set_camera_pos {camera.X:0.###} {camera.Y:0.###} {camera.Z:0.###}");
        if (handle is uint picked) yield return $"target_by_handle 0x{picked:X8}";
    }

    public void Dispose()
    {
        view?.Dispose();
        shared?.Dispose();
    }
}

/// <summary>
/// What the game shows: the picked object's handle (if any), its camera's position, the way it
/// looks and its vertical field of view, which frame that was, and when the game wrote it.
/// </summary>
sealed record GameView(uint? Picked, Vector3 Camera, Vector3 Forward, float? VerticalFov, uint Frame, DateTime Written);
