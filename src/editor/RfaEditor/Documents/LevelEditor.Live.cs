using System.Globalization;
using System.IO;
using System.Numerics;
using System.Windows;
using System.Windows.Threading;
using RfaFormats.Zones;

namespace RfaEditor;

/// <summary>
/// Live editing: while Live is on, what you do to the zone's objects is done to the running game's
/// too (see <see cref="GameLink"/>; the game has to run with the console mod, and be in this level):
///   an object moved or turned here is moved and turned there, as you drag it (the game's own "set
///     position" for the object, which its mission scripts use);
///   an object deleted here is removed there;
///   an object whose properties you change here (the picked one: a light's colour, a flag) is made
///     again there with them: removed, and a moment later made from its zone data;
///   an object added here (a duplicate, a new chunk, a delete undone) is made there from its zone
///     data, as the game makes a level's objects. The game needs its chunk loaded: a chunk no object
///     of the zone had when Live went on is loaded first (see LoadLiveChunk), then the object made.
/// About 30 times a second the zone's objects are compared with what the game was last sent, so whatever
/// changed them (the gizmo, the inspector, the controller, magnets, undo and redo) is sent alike.
/// The level geometry's pieces are moved, taken away and added in the game too (see WatchLiveGeometry).
/// What the game can't be shown this way needs the zone saved and the level loaded again (Save and
/// reload): objects whose model can't be loaded as the game runs, and the ground. With Auto
/// reload on as well, that is done for you: two seconds after the last such change the zone is
/// saved and the game loads the level again, the player put back where they stood. Without it,
/// nothing is saved by Live itself.
/// </summary>
public partial class LevelEditor
{
    readonly DispatcherTimer liveTimer = new(DispatcherPriority.Render) { Interval = TimeSpan.FromMilliseconds(30) };
    readonly Dictionary<uint, (Vector3 Position, Matrix4x4 Rotation)> liveSent = [];  // what the game was last sent (or had)
    readonly HashSet<uint> liveNotInGame = [];  // added here since, and not made there: the game has no such object
    readonly Dictionary<uint, int> liveContent = [];  // the properties (but where it is) of objects that were picked, as the game has them
    readonly Dictionary<uint, int> liveHold = [];      // removed there to be made again: not before this tick
    int liveTicks;
    DateTime? liveReloadWanted;  // when a change was last made that the game only shows after the level is loaded again
    readonly Dictionary<uint, (Vector3 Position, Matrix4x4 Rotation)> liveGeometry = [];  // the level geometry's pieces, as the game has them
    readonly Dictionary<uint, GamePiece> liveGeometryAt = [];      // the level's own pieces: where each is in the game's stitch list
    readonly HashSet<uint> liveGeometryHidden = [];                // ... taken away in the game (deleted here, or made another piece)
    bool liveGeometryNoted;
    string? liveBarriers;                                          // the invisible walls as the game has them (BarrierSignature)                                        // the above are known (the zone's geometry may be read after Live goes on)
    readonly Dictionary<uint, string> liveGeometryMade = [];       // pieces made in the game for pieces added here: as "name|flags"
    readonly Dictionary<uint, (Vector3 Position, Matrix4x4 Rotation)> liveGeometryMadeAt = [];  // ... where the game has them
    readonly HashSet<uint> liveGeometryDropped = [];               // ... taken away again (deleted here)
    readonly Dictionary<uint, int> liveGeometryAsked = [];         // ... whose answer hasn't come yet: since which tick
    readonly HashSet<uint> liveGeometryFailed = [];                // pieces the game couldn't make: shown after Save and reload
    readonly HashSet<string> liveGeometryChunksRetried = new(StringComparer.OrdinalIgnoreCase);  // chunks loaded again as the game hadn't them
    readonly HashSet<string> liveChunks = new(StringComparer.OrdinalIgnoreCase);  // the chunks the game has loaded for this zone
    readonly Dictionary<string, (string Container, int Since)> liveChunkLoads = new(StringComparer.OrdinalIgnoreCase);  // asked of the game, not answered yet
    readonly HashSet<string> liveChunksFailed = new(StringComparer.OrdinalIgnoreCase);  // the game couldn't load them: they need a reload
    static int liveChunkNumber;
    bool liveHooked;
    int liveMoves, liveRemoves, liveCreates;

    void OnLiveChanged(object sender, RoutedEventArgs e)
    {
        if (!liveHooked)
        {
            liveHooked = true;
            liveTimer.Tick += (_, _) => LiveTick();
        }
        if (LiveBox.IsChecked == true)
        {
            // From now on: the game is taken to have every object where the zone has it now.
            NoteLiveObjects();
            liveMoves = liveRemoves = liveCreates = 0;
            liveTimer.Start();
            context.Log(GameLink.IsRecent(gameLink.Read())
                ? "Live: on. Objects you move, turn, delete or add here are moved, turned, removed and made in the running game. Other changes need Save and reload."
                : "Live: on, once the game runs (start it with RFAModLauncher or the Game panel, and go to this level).");
        }
        else
        {
            liveTimer.Stop();
            context.Log($"Live: off ({liveMoves} moves, {liveRemoves} removals and {liveCreates} new objects were sent to the game).");
        }
    }

    /// <summary>The game is taken to have every object of the zone where the zone has it now, and their chunks loaded.</summary>
    void NoteLiveObjects()
    {
        liveSent.Clear();
        liveNotInGame.Clear();
        liveChunks.Clear();
        liveContent.Clear();
        liveHold.Clear();
        liveReloadWanted = null;
        liveChunkLoads.Clear();
        liveChunksFailed.Clear();
        liveGeometryNoted = false;
        NoteLiveGeometry();
        liveBarriers = null;
        foreach (ZoneObject obj in zone.Objects.Where(LiveKind))
        {
            liveSent[obj.Handle] = obj.Placement;
            if (obj.Property("chunk_name")?.Text is string chunk) liveChunks.Add(chunk);
        }
    }

    /// <summary>A piece of the level's geometry as the game has it: its list, its place in it, its chunk, flags and where it is.</summary>
    internal readonly record struct GamePiece(int List, int Index, string Name, int Flags, Vector3 Position, Matrix4x4 Rotation);

    /// <summary>The game is taken to have the level geometry as it was loaded (see geometryInGame), nothing of ours made.</summary>
    void NoteLiveGeometry()
    {
        liveGeometry.Clear();
        liveGeometryAt.Clear();
        liveGeometryHidden.Clear();
        liveGeometryMade.Clear();
        liveGeometryMadeAt.Clear();
        liveGeometryDropped.Clear();
        liveGeometryAsked.Clear();
        liveGeometryFailed.Clear();
        liveGeometryChunksRetried.Clear();
        if (geometryInGame == null) return;
        foreach (var (handle, piece) in geometryInGame)
        {
            liveGeometryAt[handle] = piece;
            liveGeometry[handle] = (piece.Position, piece.Rotation);
            liveChunks.Add(PieceChunk(piece.Name));  // (the level loaded it)
        }
        liveGeometryNoted = true;
    }

    /// <summary>
    /// Whether the game can make this object now: it needs the object's model loaded, which a chunk
    /// the level didn't have isn't until the zone is saved and the level loaded again (making it
    /// without would fail, or crash the game).
    /// </summary>
    bool LiveCanMake(ZoneObject obj) => obj.Property("chunk_name")?.Text is not string chunk || liveChunks.Contains(chunk);

    /// <summary>
    /// Has the running game load a chunk the level hasn't, so objects of it can be made live: a
    /// container holding the chunk's model and textures (as the zone's save adds them to the level's
    /// containers) and an asset list naming only that container are put in overrides\live\, and the
    /// game told to read the list and load the container at once (editor_chunk), as it loads its
    /// preload chunks. Its answer comes back a moment later (see LiveTick). False if the chunk's files
    /// can't all be found (then only a reload shows the object).
    /// </summary>
    bool LoadLiveChunk(string chunk)
    {
        if (liveChunkLoads.ContainsKey(chunk)) return true;
        if (liveChunksFailed.Contains(chunk)) return false;
        // (Objects name their chunk with its model's file name: chunk.cchk_pc.)
        // (Level geometry pieces are stitch chunks: piece.cstch_pc.)
        bool stitch = chunk.EndsWith(".cstch_pc", StringComparison.OrdinalIgnoreCase);
        string bare = stitch ? chunk[..^9] : chunk.EndsWith(".cchk_pc", StringComparison.OrdinalIgnoreCase) ? chunk[..^8] : chunk;
        string? why;
        if ((stitch ? context.Assets.StitchPackageOf(bare, out why) : context.Assets.PackageOf(bare, out why)) is not ChunkPackage package)
        {
            context.Log($"Live: the chunk {chunk} can't be loaded in the game as it runs ({why}).");
            liveChunksFailed.Add(chunk);
            return false;
        }
        try
        {
            // As the Guerrilla world lists its own (GuerrillaWorld.Pack): the chunk's files packed in a
            // container of their own (rflive<n>.str2_pc: type 17, flags 0x480, the files' sizes) and the
            // chunk's group, <chunk>.rfgchunkx (.rfgstitchx for level geometry: what the game looks for to
            // make an object of it), naming its primitives (type 14, flags 0x300), in an asset list of
            // their own. All go in overrides\live\, where the game finds them by name once the loader
            // has looked again. The game reads the list into its container records and loads the group
            // (editor_chunk_group).
            int session = Environment.ProcessId % 10000;
            string data = $"rflive{session}_{++liveChunkNumber}";
            string assetList = data + ".asm_pc";
            string container = bare + (stitch ? ".rfgstitchx" : ".rfgchunkx");
            // Only what the level hasn't loaded already: a texture file it has (a look borrowed from one of
            // its own pieces) listed again made the game's asset list reader stop (int3).
            string own = bare + (stitch ? ".cstch_pc" : ".cchk_pc");
            var files = package.Files.Where(f => f.Name.Equals(own, StringComparison.OrdinalIgnoreCase) ||
                                                 f.Name.Equals(RfaFormats.Meshes.ModelFile.DataFileName(own), StringComparison.OrdinalIgnoreCase) ||
                                                 !LevelHasFile(f.Name)).ToList();
            var primitives = package.Primitives.Where(p => files.Any(f => f.Name.Equals(p.Name, StringComparison.OrdinalIgnoreCase))).ToList();
            byte[] bytes = RfaFormats.Packfile.CreateContainer(files);
            var packed = RfaFormats.Packfile.FromBytes(data + ".str2_pc", bytes);
            RfaFormats.AsmPrimitive Copy(RfaFormats.AsmPrimitive p) => new()
            {
                Name = p.Name, Type = p.Type, Allocator = p.Allocator, Flags = p.Flags, SplitIndex = p.SplitIndex, HeaderSize = p.HeaderSize, DataSize = p.DataSize,
            };
            var ownContainer = new RfaFormats.AsmContainer
            {
                Name = data, Type = 17, Flags = 0x480, DataOffset = (uint)packed.DataStart, CompressedSize = packed.CompressedDataSize,
            };
            ownContainer.Sizes.AddRange(files.Select(f => f.Data.Length));
            ownContainer.Primitives.AddRange(primitives.Select(Copy));
            var group = new RfaFormats.AsmContainer { Name = container, Type = 14, Flags = 0x300 };
            group.Primitives.AddRange(primitives.Select(Copy));
            var list = new RfaFormats.AsmFile();
            list.Containers.Add(ownContainer);
            list.Containers.Add(group);
            string folder = Path.Combine(context.Workspace.OverridesFolder, "live");
            Directory.CreateDirectory(folder);
            File.WriteAllBytes(Path.Combine(folder, data + ".str2_pc"), bytes);
            File.WriteAllBytes(Path.Combine(folder, assetList), list.ToBytes());
            if (!gameLink.TrySend(["editor_rescan", $"editor_chunk_group {container} {assetList}"])) return true;  // the link is busy: asked again next time
            liveChunkLoads[chunk] = (container, liveTicks);
            context.Log($"Live: loading the chunk {chunk} in the game ({files.Sum(f => (long)f.Data.Length) / 1024:N0} KB).");
            return true;
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException or InvalidDataException or NotSupportedException)
        {
            context.Log($"Live: the chunk {chunk} couldn't be prepared for the game ({ex.Message}).");
            liveChunksFailed.Add(chunk);
            return false;
        }
    }

    /// <summary>Whether one of the level's containers (the zone's, its stitch list's) has a file already.</summary>
    bool LevelHasFile(string name) =>
        ChunkContainers().Concat(StitchContainers()).Distinct().Any(c => Workspace.Container(c.Archive, c.Container).Find(name) != null);

    /// <summary>Takes the game's answers about chunks it was asked to load.</summary>
    void TakeLiveChunkAnswers()
    {
        foreach (var (chunk, (container, since)) in liveChunkLoads.ToList())
        {
            string? answer = gameLink.ChunkAnswer(container);
            if (answer == null && liveTicks - since < 1100) continue;  // (33 seconds: the game gives up after 30)
            liveChunkLoads.Remove(chunk);
            if (answer == "loaded")
            {
                liveChunks.Add(chunk);
                context.Log($"Live: the chunk {chunk} is loaded in the game.");
            }
            else
            {
                liveChunksFailed.Add(chunk);
                context.Log($"Live: the game couldn't load the chunk {chunk} as it runs{(answer == null ? " (no answer)" : "")}; objects of it show after Save and reload.");
            }
        }
    }

    static readonly uint PlaceHash = NameHashes.PropertyHash("oP"), JustPosHash = NameHashes.PropertyHash("just_pos");

    /// <summary>An object's properties, but where it is and how it is turned, as one number: it changes when they do.</summary>
    static int LiveContent(ZoneObject obj)
    {
        var hash = new HashCode();
        foreach (ZoneProperty property in obj.Properties)
        {
            if (property.NameHash == PlaceHash || property.NameHash == JustPosHash) continue;
            hash.Add(property.NameHash);
            hash.Add(property.Type);
            hash.AddBytes(property.Value);
        }
        return hash.ToHashCode();
    }

    /// <summary>Objects the game has as objects of its own (level geometry pieces are parts of one mesh there).</summary>
    static bool LiveKind(ZoneObject obj) => !IsGeometry(obj) && !IsBarrier(obj);

    /// <summary>Spawn nodes the game's link puts their squad, vehicle or exo down from as soon as they are made (see rfa_qol's console.cpp).</summary>
    static readonly HashSet<uint> SpawnsLive =
        [NameHashes.ClassHash("object_squad_spawn_node"), NameHashes.ClassHash("object_vehicle_spawn_node"), NameHashes.ClassHash("object_exo_spawn_node")];

    /// <summary>Objects the game only puts down as the level loads (pickups, preplaced weapons, turrets): made live, then the level loaded again.</summary>
    /// <summary>
    /// An item whose items_3d.xtbl entry has inherit_bone_transforms: it takes its bones from what wears it (a character's
    /// clothing, the player's coat, a monolith's or the queen's body, a banner hung on a rig), so the game can't make it
    /// on its own. Other items, cloth ones too (banners, wires), are made live as any object.
    /// </summary>
    bool WornItem(ZoneObject obj) =>
        obj.ClassHash == NameHashes.ClassHash("item") &&
        obj.Property("item_type")?.Text is { Length: > 0 } type &&
        SpawnTableEntry("items_3d.xtbl", type.Trim()) is { } entry &&
        entry.ElementsNamed("Item_Flags").SelectMany(f => f.ElementsNamed("Flag"))
             .Any(f => f.Value.Trim().Equals("inherit_bone_transforms", StringComparison.OrdinalIgnoreCase));

    static readonly HashSet<uint> ShownAfterReload =
        [NameHashes.ClassHash("item"), NameHashes.ClassHash("weapon"), NameHashes.ClassHash("object_turret_spawn_node")];

    /// <summary>
    /// A change was made that the game only shows once the level is loaded again (the ground was
    /// sculpted, level geometry changed, an object needs a model the level hasn't loaded). With Auto
    /// reload on, the zone is saved and the level loaded again two seconds after the last of them.
    /// </summary>
    void LiveNeedsReload()
    {
        if (LiveBox.IsChecked == true) liveReloadWanted = DateTime.Now;
    }

    /// <summary>
    /// The level geometry's pieces, live: one the game has that was moved or turned here is moved
    /// there (editor_stitch: its drawing and its collision); one deleted here is taken away there
    /// (editor_stitch_hide), and put back if undo brings it back; one added here (a duplicate, one
    /// from the asset browser) is made there as the game makes the level's (editor_stitch_add),
    /// its chunk loaded first if the level hasn't it (as for objects). What the game can't do (a
    /// chunk it can't load, a piece of several sub-chunks, the zone's stitch list unknown) needs the
    /// level loaded again. With a level whose pieces share one combined collision, a moved piece's
    /// collision is at both places (the old in the combined one) until the level loads again.
    /// </summary>
    void WatchLiveGeometry()
    {
        TakeLiveGeometryAnswers();
        var pieces = zone.Objects.Where(IsGeometry).ToList();
        if (stitchListName == null)
        {
            // Where the game has the pieces isn't known: any change needs a reload.
            bool changed = pieces.Count != liveGeometry.Count || pieces.Any(p => !liveGeometry.TryGetValue(p.Handle, out var had) || had != p.Placement);
            if (!changed) return;
            liveGeometry.Clear();
            foreach (ZoneObject piece in pieces) liveGeometry[piece.Handle] = piece.Placement;
            LiveNeedsReload();
            return;
        }
        if (!liveGeometryNoted)
        {
            if (geometryInGame == null) return;  // the zone's geometry isn't read yet
            NoteLiveGeometry();
        }
        string cterrain = Path.GetFileNameWithoutExtension(stitchListName) + ".cterrain_pc";
        var lines = new List<string>();
        var done = new List<Action>();  // noted once the game has been sent the lines
        var here = new HashSet<uint>();
        foreach (ZoneObject piece in pieces)
        {
            uint handle = piece.Handle;
            here.Add(handle);
            var now = piece.Placement;
            string name = PieceOf(piece);
            int flags = IntOf(piece, "stitch_flags", 4);
            string content = $"{name}|{flags}";
            if (liveGeometryAt.TryGetValue(handle, out GamePiece at))
            {
                // One of the level's own.
                bool asLoaded = string.Equals(name, at.Name, StringComparison.OrdinalIgnoreCase) && flags == at.Flags;
                if (asLoaded)
                {
                    // (Made another piece and back again: the one made for it goes.)
                    if (liveGeometryMade.ContainsKey(handle) && !liveGeometryDropped.Contains(handle))
                    {
                        lines.Add($"editor_stitch_drop {handle:X8}");
                        done.Add(() => liveGeometryDropped.Add(handle));
                    }
                    bool hidden = liveGeometryHidden.Contains(handle);
                    if (!hidden && liveGeometry.TryGetValue(handle, out var had) && had == now) continue;
                    lines.Add(StitchCommand(cterrain, at.List, at.Index, at.Name, now.Position, now.Rotation));
                    done.Add(() => { liveGeometry[handle] = now; liveGeometryHidden.Remove(handle); });
                    continue;
                }
                // Made another chunk, or its flags changed, here: the game's piece is taken away, and
                // one made as the piece is now, as for a piece added here (below).
                if (!liveGeometryHidden.Contains(handle))
                {
                    lines.Add($"editor_stitch_hide {cterrain} {at.List} {at.Index} {at.Name}");
                    done.Add(() => liveGeometryHidden.Add(handle));
                }
            }
            // Added here (or made another piece): made in the game.
            if (liveGeometryFailed.Contains(handle) || liveGeometryAsked.ContainsKey(handle)) continue;
            bool made = liveGeometryMade.TryGetValue(handle, out string? madeAs) && madeAs == content;
            bool dropped = liveGeometryDropped.Contains(handle);
            if (made && !dropped && liveGeometryMadeAt.TryGetValue(handle, out var there) && there == now) continue;
            if (!made)
            {
                string chunk = PieceChunk(name);
                if (!liveChunks.Contains(chunk))
                {
                    if (LoadLiveChunk(chunk)) continue;  // made once it is loaded
                    liveGeometryFailed.Add(handle);
                    context.Log($"Live: the level geometry piece {name} can't be made in the game as it runs (its files can't be loaded); it shows after Save and reload.");
                    LiveNeedsReload();
                    continue;
                }
            }
            // (The game takes away a piece made before for the same handle, if it was another.)
            lines.Add(AddStitchCommand(cterrain, handle, name, flags, now.Position, now.Rotation));
            done.Add(() =>
            {
                liveGeometryMadeAt[handle] = now;
                liveGeometryDropped.Remove(handle);
                liveGeometryMade[handle] = content;
                if (!made) liveGeometryAsked[handle] = liveTicks;
            });
        }
        // Deleted here: taken away in the game (and put back above if undo brings them back).
        foreach (uint handle in liveGeometryAt.Keys.Where(h => !here.Contains(h) && !liveGeometryHidden.Contains(h)).ToList())
        {
            GamePiece at = liveGeometryAt[handle];
            lines.Add($"editor_stitch_hide {cterrain} {at.List} {at.Index} {at.Name}");
            done.Add(() => liveGeometryHidden.Add(handle));
        }
        foreach (uint handle in liveGeometryMade.Keys.Where(h => !here.Contains(h) && !liveGeometryDropped.Contains(h)).ToList())
        {
            lines.Add($"editor_stitch_drop {handle:X8}");
            done.Add(() => liveGeometryDropped.Add(handle));
        }
        // Added and deleted again before the game made them: forgotten.
        liveGeometryFailed.RemoveWhere(h => !here.Contains(h));
        if (lines.Count == 0) return;
        if (!gameLink.TrySend(lines)) return;  // the link is busy: next time
        foreach (Action note in done) note();
    }

    /// <summary>The game's answers about pieces it was asked to make: made, or not (then they need a reload, or their chunk loaded first).</summary>
    void TakeLiveGeometryAnswers()
    {
        foreach (var (handle, since) in liveGeometryAsked.ToList())
        {
            string? answer = gameLink.Answer($"stitch_{handle:X8}");
            if (answer == null && liveTicks - since < 100) continue;  // (3 seconds)
            liveGeometryAsked.Remove(handle);
            string name = (liveGeometryMade.GetValueOrDefault(handle) ?? "").Split('|')[0];
            if (answer is "made" or "moved")
            {
                if (liveCreates++ < 20) context.Log($"Live: level geometry piece {name} made in the game.");
                continue;
            }
            liveGeometryMade.Remove(handle);
            liveGeometryMadeAt.Remove(handle);
            string chunk = PieceChunk(name);
            if (answer == "nochunk" && liveGeometryChunksRetried.Add(chunk))
            {
                liveChunks.Remove(chunk);  // the game hasn't its chunk after all: loaded, then made (next time)
                continue;
            }
            liveGeometryFailed.Add(handle);
            context.Log($"Live: the game couldn't make the level geometry piece {name}{(answer == null ? " (no answer)" : "")}; it shows after Save and reload.");
            LiveNeedsReload();
        }
    }

    /// <summary>The stitch chunk a piece is of (piece.cstch_pc).</summary>
    static string PieceChunk(string piece) => piece.EndsWith(".cstch_pc", StringComparison.OrdinalIgnoreCase) ? piece : piece + ".cstch_pc";

    static string StitchCommand(string cterrain, int list, int index, string name, Vector3 p, Matrix4x4 r) => string.Create(CultureInfo.InvariantCulture,
        $"editor_stitch {cterrain} {list} {index} {name} {p.X:0.####} {p.Y:0.####} {p.Z:0.####} {r.M11:0.######} {r.M12:0.######} {r.M13:0.######} {r.M21:0.######} {r.M22:0.######} {r.M23:0.######} {r.M31:0.######} {r.M32:0.######} {r.M33:0.######}");

    static string AddStitchCommand(string cterrain, uint id, string name, int flags, Vector3 p, Matrix4x4 r) => string.Create(CultureInfo.InvariantCulture,
        $"editor_stitch_add {cterrain} {id:X8} {name} {flags} {p.X:0.####} {p.Y:0.####} {p.Z:0.####} {r.M11:0.######} {r.M12:0.######} {r.M13:0.######} {r.M21:0.######} {r.M22:0.######} {r.M23:0.######} {r.M31:0.######} {r.M32:0.######} {r.M33:0.######}");

    /// <summary>Saves and has the game load the level again, if Auto reload is on and it is half a second since the last change that needs it.</summary>
    bool LiveAutoReload()
    {
        if (liveReloadWanted is not DateTime wanted || AutoReloadBox.IsChecked != true) return false;
        if ((DateTime.Now - wanted).TotalSeconds < 0.5 || sculpt != null && strokeBefore != null || carrying ||
            System.Windows.Input.Mouse.LeftButton == System.Windows.Input.MouseButtonState.Pressed) return false;
        liveReloadWanted = null;
        context.Log("Live: saving and loading the level again in the game, for the changes it can't show otherwise.");
        if (!Save())
        {
            AutoReloadBox.IsChecked = false;
            context.Log("Live: the zone couldn't be saved, so Auto reload is off. Save it yourself, then turn it on again.");
            return true;
        }
        gameLink.Send(["editor_rescan", "editor_reload"]);
        NoteTerrainReloaded();
        NoteGeometryReloaded();
        NoteLiveObjects();
        return true;
    }

    void LiveTick()
    {
        if (!GameLink.IsRecent(gameLink.Read())) return;  // the game isn't running: it is told when it is
        WatchLiveGeometry();
        // The invisible walls are in the terrain file: the game has them changed once the level is loaded again.
        if (barriersLoaded)
        {
            string walls = BarrierSignature();
            if (liveBarriers != null && walls != liveBarriers) LiveNeedsReload();
            liveBarriers = walls;
        }
        TakeLiveChunkAnswers();
        WatchLiveTerrain();
        if (LiveAutoReload()) return;
        var lines = new List<string>();
        var moved = new List<ZoneObject>();
        var made = new List<ZoneObject>();
        var here = new HashSet<uint>();
        liveTicks++;
        // The picked object's properties changed (not just where it is): the game makes objects from
        // their properties, so it is removed there now and made again in a moment (it then counts as
        // one the game hasn't, below). An object is taken to match the game when it is first picked.
        if (selected is { } picked && LiveKind(picked) && zone.Objects.Contains(picked) && liveSent.ContainsKey(picked.Handle))
        {
            int content = LiveContent(picked);
            if (!liveContent.TryGetValue(picked.Handle, out int had)) liveContent[picked.Handle] = content;
            else if (had != content && LiveCanMake(picked) && !gameLink.Waiting && gameLink.TrySend([GameLink.RemoveCommand(picked.Handle)]))
            {
                liveContent[picked.Handle] = content;
                liveSent.Remove(picked.Handle);
                liveHold[picked.Handle] = liveTicks + 3;
                return;
            }
        }
        foreach (ZoneObject obj in zone.Objects)
        {
            if (!LiveKind(obj)) continue;
            here.Add(obj.Handle);
            if (liveHold.TryGetValue(obj.Handle, out int until))
            {
                if (liveTicks < until) continue;
                liveHold.Remove(obj.Handle);
            }
            if (!liveSent.TryGetValue(obj.Handle, out var sent))
            {
                // Added here since Live went on (or put back by undo): made in the game too, if it can be.
                if (liveNotInGame.Contains(obj.Handle)) continue;
                if (LiveCanMake(obj)) made.Add(obj);
                else if (obj.Property("chunk_name")?.Text is string chunk && LoadLiveChunk(chunk)) continue;  // made once it is loaded
                else if (liveNotInGame.Add(obj.Handle))
                {
                    context.Log($"Live: {Label(obj)} was added here, but the level hasn't its model loaded; the game shows it after Save and reload.");
                    LiveNeedsReload();
                }
                continue;
            }
            var now = obj.Placement;
            if (now.Position == sent.Position && now.Rotation == sent.Rotation) continue;
            lines.Add(GameLink.MoveCommand(obj.Handle, now.Position, now.Rotation));
            moved.Add(obj);
        }
        // Objects added and deleted here that the game never had: forgotten, so a new object given the
        // same handle later is made.
        liveNotInGame.RemoveWhere(handle => !here.Contains(handle) && !liveSent.ContainsKey(handle));
        List<uint> removed = liveSent.Keys.Where(handle => !here.Contains(handle)).ToList();
        foreach (uint handle in removed)
            if (!liveNotInGame.Contains(handle)) lines.Add(GameLink.RemoveCommand(handle));
        // An object attached to one of the zone's that the game hasn't yet (its chunk still loading) waits
        // for it: made before its parent, the game drops the link to it.
        var zoneHandles = zone.Objects.Select(o => o.Handle).ToHashSet();
        for (bool held = true; held && made.Count > 0;)
        {
            var making = made.Select(o => o.Handle).ToHashSet();
            held = made.RemoveAll(o => o.Parent != 0 && o.Parent != 0xFFFFFFFF && o.Parent != o.Handle && zoneHandles.Contains(o.Parent) &&
                                       !liveSent.ContainsKey(o.Parent) && !making.Contains(o.Parent)) > 0;
        }
        if (made.Count > 0)
        {
            if (gameLink.Waiting) return;  // the game hasn't taken the last lines yet: next time
            // What an object is attached to before the object itself: the game looks its parent up as it makes it.
            var newHandles = made.Select(o => o.Handle).ToHashSet();
            int Depth(ZoneObject o)
            {
                int depth = 0;
                for (ZoneObject? at = o; at != null && depth < 16 && newHandles.Contains(at.Parent); depth++)
                    at = made.FirstOrDefault(m => m.Handle == at.Parent);
                return depth;
            }
            foreach (ZoneObject obj in made.OrderBy(Depth))
            {
                // An item worn by something (see WornItem) isn't made on its own: the game couldn't finish making one,
                // left it half-made, and ended a moment later (a pure virtual call as world::do_frame deleted it).
                if (WornItem(obj)) continue;
                // A spawn node's squad, vehicle or exo held in the spawn resources in force first (editor_reserve):
                // the game spawns it once they have streamed in.
                lines.AddRange(LiveReserveLines(obj));
                lines.Add(gameLink.CreateCommand(obj.Handle, obj.ToBlock()));
            }
        }
        if (lines.Count == 0 && removed.Count == 0 && !made.Any(WornItem)) return;
        if (lines.Count > 0 && !gameLink.TrySend(lines)) return;  // the game hasn't taken the last ones yet: next time
        foreach (ZoneObject obj in moved) liveSent[obj.Handle] = obj.Placement;
        foreach (ZoneObject obj in made)
        {
            liveSent[obj.Handle] = obj.Placement;
            if (WornItem(obj))
            {
                context.Log($"Live: {Label(obj)} isn't made in the running game: it's worn by a character or an animated rig (items_3d.xtbl " +
                            "inherit_bone_transforms: clothing, the player's coat, a monolith's body, a banner on its rig), and on its own the game " +
                            "can't finish making it (it ended the game). It may not load on its own after a reload either.");
                continue;
            }
            if (liveCreates++ < 20) context.Log($"Live: {Label(obj)} made in the game" +
                                                (SpawnsLive.Contains(obj.ClassHash) ? ", and its squad, vehicle or exo spawned there." : "."));
            // Pickups and turrets are put down by the game as the level loads: they show once it is loaded again.
            if (ShownAfterReload.Contains(obj.ClassHash))
            {
                context.Log($"Live: {Label(obj)} ({names.Class(obj.ClassHash)}) is put down by the game as the level loads: it shows after Save and reload" +
                            (AutoReloadBox.IsChecked == true ? " (done for you in a moment)." : " (or turn on Auto reload)."));
                LiveNeedsReload();
            }
        }
        foreach (uint handle in removed)
        {
            liveSent.Remove(handle);
            // Removed in the game: if undo puts it back here, the game no longer has it.
            if (!liveNotInGame.Remove(handle)) liveRemoves++;
        }
        liveMoves += moved.Count;
    }

    /// <summary>Saves the zone, then has the running game load the level again, so it shows every change.</summary>
    void OnSaveAndReload(object sender, RoutedEventArgs e)
    {
        if (!Save()) return;
        if (!GameLink.IsRecent(gameLink.Read()))
        {
            context.Log("Save and reload: saved. The game isn't running with the console mod, so there was nothing to reload.");
            return;
        }
        gameLink.Send(["editor_rescan", "editor_reload"]);
        NoteTerrainReloaded();
        // The game has every object where the saved zone puts it again.
        NoteGeometryReloaded();
        if (LiveBox.IsChecked == true) NoteLiveObjects();
        context.Log("Save and reload: saved, and the game is loading the level again.");
    }
}
