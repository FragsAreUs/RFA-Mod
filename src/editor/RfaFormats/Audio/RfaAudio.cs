using System.Runtime.InteropServices;
using System.Text;

namespace RfaFormats.Audio;

/// <summary>
/// RFA-Mod's own sound engine (src\audio\, rfa_audio.dll beside the program, with OpenAL Soft's soft_oal.dll): it
/// plays the Guerrilla sound banks GuerrillaBuilder writes, as the game will once it plays them itself, with Wwise
/// keeping Armageddon's. The editor previews banks with it; GuerrillaBuilder checks them with its
/// loopback device, without speakers. One engine a process: <see cref="Open"/> once, then load banks and post events.
/// Every call is safe from any thread. Positions are the game's (x right, y up, z forward), in metres.
/// </summary>
public static class RfaAudio
{
    const string Dll = "rfa_audio.dll";

    /// <summary>Whether the engine and OpenAL Soft are beside this program (built by build.cmd into bin\editor\).</summary>
    public static bool Installed =>
        File.Exists(Path.Combine(AppContext.BaseDirectory, Dll)) && File.Exists(Path.Combine(AppContext.BaseDirectory, "soft_oal.dll"));

    public static bool IsOpen { get; private set; }

    /// <summary>Opens OpenAL Soft and the engine (again: it closes first). Throws with why it can't.</summary>
    /// <param name="loopback">No speakers: sound is made only by <see cref="Render"/> (for checks).</param>
    /// <param name="ownThread">The engine updates itself every 10 ms (else call <see cref="Tick"/>).</param>
    public static void Open(bool loopback = false, bool ownThread = true, int sampleRate = 0, int maxSources = 0, int cacheMb = 0, bool hrtf = false)
    {
        var settings = new Settings
        {
            Loopback = loopback ? 1 : 0, SampleRate = sampleRate, MaxSources = maxSources, CacheMb = cacheMb, OwnThread = ownThread ? 1 : 0,
            Hrtf = hrtf ? 1 : 0,
        };
        var error = new byte[1024];
        if (rfa_audio_open(ref settings, error, error.Length) == 0)
            throw new InvalidOperationException(Text(error));
        IsOpen = true;
    }

    public static void Close()
    {
        if (!IsOpen) return;
        rfa_audio_close();
        IsOpen = false;
    }

    /// <summary>The device's name and rate, and OpenAL Soft's version.</summary>
    public static string DeviceInfo() => Text(1024, (b, n) => rfa_audio_device_info(b, n));

    /// <summary>Loads a bank and its media file (may be null). Returns its id; throws with why it can't.</summary>
    public static uint LoadBank(string bankPath, string? mediaPath)
    {
        var error = new byte[1024];
        uint id = rfa_audio_load_bank(bankPath, mediaPath, error, error.Length);
        return id != 0 ? id : throw new InvalidDataException(Text(error));
    }

    /// <summary>
    /// Loads one of Armageddon's own banks and its media in place from an archive that holds them uncompressed
    /// (sounds.vpp_pc, voices.vpp_pc); <paramref name="name"/> without .bnk_pc. Returns its id; throws with why it can't.
    /// </summary>
    public static uint LoadBankFromArchive(string archive, string name)
    {
        var error = new byte[1024];
        uint id = rfa_audio_load_bank_from_archive(archive, Encoding.ASCII.GetBytes(name + "\0"), error, error.Length);
        return id != 0 ? id : throw new InvalidDataException(Text(error));
    }

    public static void UnloadBank(uint bank) => rfa_audio_unload_bank(bank);

    public static uint[] BankEvents(uint bank) => Items<uint>(n => rfa_audio_bank_events(bank, null, 0), a => rfa_audio_bank_events(bank, a, a.Length));
    public static ObjectInfo[] BankObjects(uint bank) => Items<ObjectInfo>(n => rfa_audio_bank_objects(bank, null, 0), a => rfa_audio_bank_objects(bank, a, a.Length));
    public static uint[] BankParameters(uint bank) => Items<uint>(n => rfa_audio_bank_parameters(bank, null, 0), a => rfa_audio_bank_parameters(bank, a, a.Length));
    public static uint[] BankSwitchGroups(uint bank) => Items<uint>(n => rfa_audio_bank_switch_groups(bank, null, 0), a => rfa_audio_bank_switch_groups(bank, a, a.Length));
    /// <summary>
    /// Every id of the loaded banks that Wwise made from a name (events, banks, dialogue events, buses, switch groups and
    /// values, state groups and states, game parameters), with its kind: what a name can be found for.
    /// </summary>
    public static (uint Id, NamedKind Kind)[] NamedIds()
    {
        int n = rfa_audio_named_ids(null, null, 0);
        var ids = new uint[n];
        var kinds = new int[n];
        int got = n > 0 ? Math.Min(n, rfa_audio_named_ids(ids, kinds, n)) : 0;
        return [.. Enumerable.Range(0, got).Select(i => (ids[i], (NamedKind)kinds[i]))];
    }

    /// <summary>The kinds of <see cref="NamedIds"/> (rfa_audio.h's RFA_AUDIO_NAMED_*).</summary>
    public enum NamedKind { Bank = 1, Event, Dialogue, Bus, SwitchGroup, Switch, StateGroup, State, Parameter }

    public static uint[] BankSwitchValues(uint bank, uint group) =>
        Items<uint>(n => rfa_audio_bank_switch_values(bank, group, null, 0), a => rfa_audio_bank_switch_values(bank, group, a, a.Length));
    public static ActionInfo[] EventActions(uint eventId) => Items<ActionInfo>(n => rfa_audio_event_actions(eventId, null, 0), a => rfa_audio_event_actions(eventId, a, a.Length));

    /// <summary>The range of a game parameter's curves in a bank, or null if no curve uses it.</summary>
    public static (float Min, float Max)? ParameterRange(uint bank, uint parameter) =>
        rfa_audio_parameter_range(bank, parameter, out float min, out float max) != 0 ? (min, max) : null;

    /// <summary>What went wrong loading a bank, a line each (none: empty).</summary>
    public static string[] BankProblems(uint bank) =>
        Text(1 << 16, (b, n) => rfa_audio_bank_problems(bank, b, n)).Split('\n', StringSplitOptions.RemoveEmptyEntries);

    public static EventInfo GetEventInfo(uint eventId)
    {
        rfa_audio_get_event_info(eventId, out var info);
        return info;
    }

    /// <summary>An event or object as readable text: its actions, its tree of containers and sounds.</summary>
    public static string Describe(uint id) => Text(1 << 18, (b, n) => rfa_audio_describe(id, b, n));

    public static float BusVolume(uint bus) => rfa_audio_bus_volume(bus);

    public static uint Post(uint eventId, ulong gameObject) => rfa_audio_post(eventId, gameObject);
    public static uint PlayNode(uint nodeId, ulong gameObject) => rfa_audio_play_node(nodeId, gameObject);
    public static uint PlayMedium(uint bank, uint medium, bool loop, ulong gameObject) => rfa_audio_play_medium(bank, medium, loop ? 1 : 0, gameObject);

    /// <summary>Plays 16-bit samples (channels interleaved): Guerrilla's own waves, to compare with ours.</summary>
    public static uint PlaySamples(short[] samples, int channels, int rate, bool loop, int loopStart, float volumeDb, float pitchCents, bool threeD, ulong gameObject) =>
        rfa_audio_play_samples(samples, samples.Length / Math.Max(1, channels), channels, rate, loop ? 1 : 0, loopStart, volumeDb, pitchCents, threeD ? 1 : 0, gameObject);

    public static void StopPlaying(uint playingId, int fadeMs = 0) => rfa_audio_stop_playing(playingId, fadeMs);
    /// <summary>Stops what plays on a game object (0: on every one).</summary>
    public static void StopObject(ulong gameObject, int fadeMs = 0) => rfa_audio_stop_object(gameObject, fadeMs);
    public static bool IsPlaying(uint playingId) => rfa_audio_is_playing(playingId) != 0;

    /// <summary>A play ended: its playing id, event, game object and how many voices it started (0: it never made a sound).</summary>
    public delegate void Ended(uint playingId, uint eventId, ulong gameObject, int voices);

    /// <summary>Called when a play ends, on the engine's thread (or in <see cref="Tick"/> and <see cref="Render"/>).</summary>
    public static void OnEnded(Ended? handler)
    {
        ended = handler;
        endCallback = handler == null ? null : (playing, eventId, obj, voices, user) => ended?.Invoke(playing, eventId, obj, voices);
        rfa_audio_set_end_callback(endCallback, IntPtr.Zero);
    }
    static Ended? ended;
    static EndCallback? endCallback;  // kept alive while the engine holds it

    public static void SetPosition(ulong gameObject, float x, float y, float z) => rfa_audio_set_position(gameObject, [x, y, z], null);
    public static void SetListener(float x, float y, float z) => rfa_audio_set_listener([x, y, z], null, null);
    public static void SetParameter(ulong gameObject, uint parameter, float value) => rfa_audio_set_parameter(gameObject, parameter, value);
    public static void ResetParameter(ulong gameObject, uint parameter) => rfa_audio_reset_parameter(gameObject, parameter);
    public static void SetSwitch(ulong gameObject, uint group, uint value) => rfa_audio_set_switch(gameObject, group, value);
    public static void SetState(uint group, uint state) => rfa_audio_set_state(group, state);
    public static void RemoveObject(ulong gameObject) => rfa_audio_remove_object(gameObject);

    public static void Tick() => rfa_audio_tick();

    /// <summary>Loopback only: renders frames of stereo float sound (into <paramref name="into"/>, 2 floats a frame, or nowhere), moving time on.</summary>
    public static int Render(int frames, float[]? into = null) => rfa_audio_render(into, frames);

    /// <summary>The engine's time in seconds (loopback: the time rendered).</summary>
    public static double Time => rfa_audio_time();

    public static Voice[] Voices()
    {
        for (int tries = 0; ; tries++)
        {
            int n = rfa_audio_voices(null, 0);
            var voices = new Voice[n + 8];
            int got = rfa_audio_voices(voices, voices.Length);
            if (got <= voices.Length || tries > 3) return voices[..Math.Min(got, voices.Length)];
        }
    }

    public static Status GetStatus()
    {
        rfa_audio_get_status(out var status);
        return status;
    }

    /// <summary>Wwise's id of a name: FNV-1 (32 bits) of its lower-case letters (as WwiseBankWriter.Id).</summary>
    public static uint Id(string name)
    {
        uint h = 2166136261;
        foreach (byte b in Encoding.ASCII.GetBytes(name.ToLowerInvariant())) h = (h * 16777619) ^ b;
        return h;
    }

    // ---- The structures, as rfa_audio.h lays them out ----

    [StructLayout(LayoutKind.Sequential)]
    struct Settings
    {
        public int Loopback, SampleRate, MaxSources, CacheMb, OwnThread, Hrtf;
        public float VolumeThresholdDb;
    }

    /// <summary>A bank's object: its kind (2 sound, 3 action, 4 event, 5 random/sequence, 6 switch, 7 actor-mixer, 8 bus, 9 layer, 14 attenuation).</summary>
    [StructLayout(LayoutKind.Sequential)]
    public struct ObjectInfo
    {
        public uint Id, Type, Parent, Medium;
        public int Codec, Loops, Children, HasMedium;
        /// <summary>A node's bus (those with no parent have one).</summary>
        public uint Bus;
    }

    /// <summary>An event's action: its kind (4 play, 1 stop, 0x90 break), scope (0x011 the event's object, 0x010 every one), target, delay and fade.</summary>
    [StructLayout(LayoutKind.Sequential)]
    public struct ActionInfo
    {
        public uint Id, Kind, Scope, Target;
        public int DelayMs, FadeMs;
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct EventInfo
    {
        public int Found, Plays, Stops, Breaks, Others;
        /// <summary>Something it plays goes on for ever (until stopped or broken).</summary>
        public int Loops;
        public int ThreeD, Missing;
        /// <summary>Its longest medium (one pass) with the delays, in seconds; −1 if not known.</summary>
        public float LongestSeconds;
        /// <summary>Where its fade with distance ends (m); 0 if it has none.</summary>
        public float FarthestMetres;
        public uint SwitchGroup;
        /// <summary>Sounds it plays that are synthesized (Wwise's tone and synthesis plug-ins): not played yet.</summary>
        public int Synthesized;
        /// <summary>Objects it plays of kinds not read yet (music, dialogue): in a loaded bank, but not played.</summary>
        public int NotRead;
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct Voice
    {
        public uint PlayingId, EventId, SoundId, MediumId;
        public ulong GameObject;
        public uint Bus;
        /// <summary>1 PCM, 2 Wwise ADPCM, 3 samples given (Guerrilla's original).</summary>
        public int Codec;
        public int Channels, Rate, Loops, IsVirtual, Streamed, ThreeD;
        public float LevelDb, Distance, PositionSeconds, LengthSeconds;
        public int Priority;
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct Status
    {
        public int Playing, Voices, VirtualVoices, Streams, SourcesUsed, Sources;
        public long CacheBytes;
        public int Banks;
        public long VoicesRefused, VoicesEndedQuiet, VoicesStolen, Underruns;
        public long MediaFailed;  // voices that didn't start: their medium couldn't be read or decoded
        public long VoicesNoSource;  // voices that didn't start, or went on in silence: no OpenAL source was free
    }

    // ---- Helpers ----

    static string Text(byte[] bytes)
    {
        int end = System.Array.IndexOf(bytes, (byte)0);
        return Encoding.UTF8.GetString(bytes, 0, end >= 0 ? end : bytes.Length);
    }

    static string Text(int size, Func<byte[], int, int> get)
    {
        var buffer = new byte[size];
        int length = get(buffer, size);
        if (length >= size)
        {
            buffer = new byte[length + 1];
            get(buffer, buffer.Length);
        }
        return Text(buffer);
    }

    static T[] Items<T>(Func<int, int> count, Func<T[], int> fill)
    {
        var items = new T[count(0)];
        int got = items.Length > 0 ? fill(items) : 0;
        return got < items.Length ? items[..got] : items;
    }

    // ---- rfa_audio.h ----

    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    delegate void EndCallback(uint playingId, uint eventId, ulong gameObject, int voices, IntPtr user);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)] static extern int rfa_audio_open(ref Settings settings, byte[] error, int errorSize);
    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)] static extern void rfa_audio_close();
    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)] static extern int rfa_audio_device_info(byte[] text, int size);
    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Unicode)]
    static extern uint rfa_audio_load_bank(string bankPath, string? mediaPath, byte[] error, int errorSize);
    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Unicode)]
    static extern uint rfa_audio_load_bank_from_archive(string archive, byte[] bankName, byte[] error, int errorSize);
    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)] static extern void rfa_audio_unload_bank(uint bank);
    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)] static extern int rfa_audio_bank_events(uint bank, [Out] uint[]? ids, int max);
    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)] static extern int rfa_audio_bank_objects(uint bank, [Out] ObjectInfo[]? info, int max);
    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)] static extern int rfa_audio_bank_parameters(uint bank, [Out] uint[]? ids, int max);
    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)] static extern int rfa_audio_bank_switch_groups(uint bank, [Out] uint[]? ids, int max);
    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)] static extern int rfa_audio_bank_switch_values(uint bank, uint group, [Out] uint[]? ids, int max);
    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)] static extern int rfa_audio_named_ids([Out] uint[]? ids, [Out] int[]? kinds, int max);
    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)] static extern int rfa_audio_parameter_range(uint bank, uint parameter, out float min, out float max);
    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)] static extern int rfa_audio_bank_problems(uint bank, byte[] text, int size);
    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)] static extern int rfa_audio_event_actions(uint eventId, [Out] ActionInfo[]? info, int max);
    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)] static extern int rfa_audio_get_event_info(uint eventId, out EventInfo info);
    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)] static extern int rfa_audio_describe(uint id, byte[] text, int size);
    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)] static extern float rfa_audio_bus_volume(uint bus);
    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)] static extern uint rfa_audio_post(uint eventId, ulong gameObject);
    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)] static extern uint rfa_audio_play_node(uint nodeId, ulong gameObject);
    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)] static extern uint rfa_audio_play_medium(uint bank, uint medium, int loop, ulong gameObject);
    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
    static extern uint rfa_audio_play_samples(short[] samples, int frames, int channels, int rate, int loop, int loopStart, float volumeDb, float pitchCents, int threeD, ulong gameObject);
    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)] static extern void rfa_audio_stop_playing(uint playingId, int fadeMs);
    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)] static extern void rfa_audio_stop_object(ulong gameObject, int fadeMs);
    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)] static extern int rfa_audio_is_playing(uint playingId);
    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)] static extern void rfa_audio_set_end_callback(EndCallback? callback, IntPtr user);
    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)] static extern void rfa_audio_set_position(ulong gameObject, float[] position, float[]? front);
    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)] static extern void rfa_audio_set_listener(float[] position, float[]? front, float[]? up);
    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)] static extern void rfa_audio_set_parameter(ulong gameObject, uint parameter, float value);
    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)] static extern void rfa_audio_reset_parameter(ulong gameObject, uint parameter);
    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)] static extern void rfa_audio_set_switch(ulong gameObject, uint group, uint value);
    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)] static extern void rfa_audio_set_state(uint group, uint state);
    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)] static extern void rfa_audio_remove_object(ulong gameObject);
    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)] static extern void rfa_audio_tick();
    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)] static extern int rfa_audio_render([Out] float[]? output, int frames);
    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)] static extern double rfa_audio_time();
    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)] static extern int rfa_audio_voices([Out] Voice[]? voices, int max);
    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)] static extern int rfa_audio_get_status(out Status status);
}
