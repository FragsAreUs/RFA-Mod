using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Numerics;
using System.Text;
using System.Text.Json;
using System.Text.Json.Nodes;
using System.Threading;
using System.Threading.Tasks;
using RfaEditor;
using RfaFormats;
using RfaFormats.Tables;
using RfaFormats.Zones;
using SchemaType = RfaFormats.Zones.SchemaType;

namespace RfaEditorMcp;

/// <summary>
/// MCP Server for the RFA Editor - exposes editor functionality over JSON-RPC 2.0 via stdio.
/// RfaEditorMcp.exe (src\editor\RfaEditorMcp\) starts it: it reads this library and the editor's from
/// bin\editor\ into memory and calls Run, so those files are never held open while the server runs.
/// </summary>
public sealed class Program
{
    static EditorContext? _context;
    static GameLink? _gameLink;
    static Workspace? _workspace;
    static GameAssets? _assets;
    static bool _liveEditingEnabled = false;

    public static async Task<int> Run(string[] args)
    {
        // Try to find mod/game folders, but don't fail if not found - tools can set up workspace
        // --game <folder> and --mod <folder> (or RFA_GAME_DIR / RFA_MOD_DIR) override what's found from where the exe is.
        string? modFolder = Arg(args, "--mod") ?? Environment.GetEnvironmentVariable("RFA_MOD_DIR") ?? Workspace.FindModFolder();
        // The MCP servers run only when the player has turned them on: [Mcp] Enabled=1 in the mod
        // folder's rfa_mods.ini (off by default).
        if (!McpEnabled(modFolder))
        {
            Console.Error.WriteLine("RFA-Mod's MCP servers are off. To use them, set Enabled=1 under [Mcp] in rfa_mods.ini.");
            return 1;
        }
        string? gameFolder = Arg(args, "--game") ?? Environment.GetEnvironmentVariable("RFA_GAME_DIR")
                             ?? (modFolder != null ? Workspace.FindGameFolder(modFolder) : null);
        if (gameFolder != null && !Directory.Exists(gameFolder))
        {
            Console.Error.WriteLine($"Game folder {gameFolder} doesn't exist");
            gameFolder = null;
        }

        if (gameFolder != null && modFolder != null)
        {
            _workspace = new Workspace(gameFolder, modFolder);
            _assets = new GameAssets(_workspace);
            _gameLink = new GameLink(modFolder);

            _context = new EditorContext
            {
                Workspace = _workspace,
                Assets = _assets,
                Log = msg => Console.Error.WriteLine($"[LOG] {msg}"),
                OverridesChanged = () => { },
                OpenTable = _ => { },
                OpenLevel = _ => { }
            };

            Console.Error.WriteLine($"RFA Editor MCP Server started");
            Console.Error.WriteLine($"Game folder: {gameFolder}");
            Console.Error.WriteLine($"Mod folder: {modFolder}");
        }
        else
        {
            Console.Error.WriteLine($"RFA Editor MCP Server started (no workspace loaded - use create_workspace tool)");
            Console.Error.WriteLine($"Mod folder: {modFolder ?? "not found"}");
            Console.Error.WriteLine($"Game folder: {gameFolder ?? "not found"}");
        }

        using var reader = new JsonRpcReader(Console.OpenStandardInput());
        JsonRpcWriter? writer = null;

        var cts = new CancellationTokenSource();
        Console.CancelKeyPress += (_, e) => { e.Cancel = true; cts.Cancel(); };

        while (!cts.Token.IsCancellationRequested)
        {
            JsonRpc.Request? request;
            try
            {
                request = await reader.ReadRequestAsync(cts.Token);
            }
            catch (OperationCanceledException)
            {
                break;
            }
            catch (Exception ex)
            {
                Console.Error.WriteLine($"Read error: {ex.Message}");
                continue;
            }

            if (request == null) break; // EOF

            // A notification (no id: "notifications/initialized", "notifications/cancelled", ...) gets no answer.
            if (request.Id == null)
            {
                Console.Error.WriteLine($"[DEBUG] Notification: '{request.Method}'");
                continue;
            }

            // Initialize writer with detected format after first successful read
            if (writer == null)
            {
                writer = new JsonRpcWriter(Console.OpenStandardOutput(), reader.IsLengthPrefixed);
            }

            JsonRpc.Response response = await HandleRequestAsync(request, cts.Token);
            try
            {
                await writer.WriteAsync(response, cts.Token);
            }
            catch (Exception ex)
            {
                Console.Error.WriteLine($"Write error: {ex.Message}");
            }
        }

        _gameLink?.Dispose();
        return 0;
    }

    static string? Arg(string[] args, string name)
    {
        int i = Array.FindIndex(args, a => string.Equals(a, name, StringComparison.OrdinalIgnoreCase));
        return i >= 0 && i + 1 < args.Length ? args[i + 1].TrimEnd('\\') : null;
    }

    /// <summary>True if the mod folder's rfa_mods.ini says Enabled=1 under [Mcp]; false if it doesn't, or can't be read.</summary>
    static bool McpEnabled(string? modFolder)
    {
        if (modFolder == null) return false;
        string ini = Path.Combine(modFolder, "rfa_mods.ini");
        if (!File.Exists(ini)) return false;
        bool inSection = false;
        foreach (string raw in File.ReadLines(ini))
        {
            string line = raw.Trim();
            if (line.StartsWith('[')) { inSection = string.Equals(line, "[Mcp]", StringComparison.OrdinalIgnoreCase); continue; }
            if (!inSection || line.StartsWith(';')) continue;
            int eq = line.IndexOf('=');
            if (eq > 0 && string.Equals(line[..eq].Trim(), "Enabled", StringComparison.OrdinalIgnoreCase))
                return line[(eq + 1)..].Trim() == "1";
        }
        return false;
    }

    static async Task<JsonRpc.Response> HandleRequestAsync(JsonRpc.Request request, CancellationToken ct)
    {
        Console.Error.WriteLine($"[DEBUG] Received method: '{request.Method}' (id: {request.Id})");
        try
        {
            return request.Method switch
            {
                "initialize" => HandleInitialize(request),
                "tools/list" => HandleToolsList(request),
                "tools/call" => await HandleToolCallAsync(request, ct),
                "ping" => JsonRpc.Success(request.Id, new JsonObject { ["ping"] = "pong" }),
                _ => JsonRpc.Failure(request.Id, JsonRpc.MethodNotFound, $"Method not found: {request.Method}")
            };
        }
        catch (Exception ex)
        {
            Console.Error.WriteLine($"[DEBUG] Exception in handler: {ex.Message}");
            return JsonRpc.Failure(request.Id, JsonRpc.InternalError, ex.Message, JsonValue.Create(ex.StackTrace));
        }
    }

    static JsonRpc.Response HandleInitialize(JsonRpc.Request request)
    {
        var result = new JsonObject
        {
            ["protocolVersion"] = "2024-11-05",
            ["capabilities"] = new JsonObject
            {
                ["tools"] = new JsonObject()
            },
            ["serverInfo"] = new JsonObject
            {
                ["name"] = "RfaEditorMcp",
                ["version"] = "1.0.0"
            }
        };
        return JsonRpc.Success(request.Id, result);
    }

    static JsonRpc.Response HandleToolsList(JsonRpc.Request request)
    {
        var toolsArray = new JsonArray();
        foreach (var tool in McpTools.All)
        {
            toolsArray.Add(new JsonObject
            {
                ["name"] = tool.Name,
                ["description"] = tool.Description,
                ["inputSchema"] = tool.InputSchema
            });
        }
        return JsonRpc.Success(request.Id, new JsonObject { ["tools"] = toolsArray });
    }

    static async Task<JsonRpc.Response> HandleToolCallAsync(JsonRpc.Request request, CancellationToken ct)
    {
        if (request.Params is not JsonObject paramsObj || !paramsObj.TryGetPropertyValue("name", out var nameNode))
        {
            return JsonRpc.Failure(request.Id, JsonRpc.InvalidParams, "Missing tool name");
        }

        string toolName = nameNode!.GetValue<string>();
        JsonNode? arguments = paramsObj.TryGetPropertyValue("arguments", out var argsNode) ? argsNode : null;

        bool needsWorkspace = toolName is not ("create_workspace" or "start_game" or "dock_game" or "undock_game");
        if (needsWorkspace && _workspace == null)
            return ToolResult(request.Id, "No workspace: the game folder wasn't found. Call create_workspace with game_path and mods_path, " +
                                          "or start the server with --game <game folder> (and --mod <mod folder>).", true);
        JsonNode? result;
        try
        {
        result = toolName switch
        {
            // Workspace tools
            "create_workspace" => await CreateWorkspaceAsync(arguments),
            "get_workspace_info" => await GetWorkspaceInfoAsync(),
            "list_archives" => await ListArchivesAsync(),
            "list_files_in_archive" => await ListFilesInArchiveAsync(arguments),
            "read_file" => await ReadFileAsync(arguments),
            "write_file" => await WriteFileAsync(arguments),
            "delete_override" => await DeleteOverrideAsync(arguments),

            // Table tools
            "open_table" => await OpenTableAsync(arguments),
            "get_table_rows" => await GetTableRowsAsync(arguments),
            "update_table_row" => await UpdateTableRowAsync(arguments),
            "add_table_row" => await AddTableRowAsync(arguments),
            "delete_table_row" => await DeleteTableRowAsync(arguments),

            // Level/Zone tools
            "open_level" => await OpenLevelAsync(arguments),
            "list_zone_objects" => await ListZoneObjectsAsync(arguments),
            "get_zone_object" => await GetZoneObjectAsync(arguments),
            "create_zone_object" => await CreateZoneObjectAsync(arguments),
            "update_zone_object" => await UpdateZoneObjectAsync(arguments),
            "delete_zone_object" => await DeleteZoneObjectAsync(arguments),
            "save_zone" => await SaveZoneAsync(),

            // Game linking tools
            "get_game_state" => await GetGameStateAsync(),
            "send_console_command" => await SendConsoleCommandAsync(arguments),
            "send_console_commands" => await SendConsoleCommandsAsync(arguments),
            "move_game_object" => await MoveGameObjectAsync(arguments),
            "create_game_object" => await CreateGameObjectAsync(arguments),
            "remove_game_object" => await RemoveGameObjectAsync(arguments),
            "enable_live_editing" => await EnableLiveEditingAsync(arguments),
            "save_and_reload_level" => await SaveAndReloadLevelAsync(),

            // Asset/Chunk tools
            "list_chunks" => await ListChunksAsync(),
            "get_chunk_info" => await GetChunkInfoAsync(arguments),
            "get_asset_list" => await GetAssetListAsync(arguments),

            // Game launching
            "start_game" => await StartGameAsync(),
            "dock_game" => await DockGameAsync(),
            "undock_game" => await UndockGameAsync(),

            _ => throw new InvalidOperationException($"Unknown tool: {toolName}")
        };
        }
        catch (Exception ex) when (ex is not OperationCanceledException)
        {
            Console.Error.WriteLine($"[DEBUG] {toolName} failed: {ex}");
            return ToolResult(request.Id, $"{toolName} failed: {ex.GetType().Name}: {ex.Message}", true);
        }

        return ToolResult(request.Id, result?.ToJsonString(new JsonSerializerOptions { WriteIndented = true }) ?? "null", false);
    }

    /// <summary>A tools/call answer as MCP wants it: the text in "content", and isError for a failure.</summary>
    static JsonRpc.Response ToolResult(JsonNode? id, string text, bool isError) =>
        JsonRpc.Success(id, new JsonObject
        {
            ["content"] = new JsonArray(new JsonObject { ["type"] = "text", ["text"] = text }),
            ["isError"] = isError,
        });

    // ===================== Workspace Tools =====================

    static Task<JsonNode> GetWorkspaceInfoAsync()
    {
        var result = new JsonObject
        {
            ["game_folder"] = _workspace!.GameFolder,
            ["mod_folder"] = _workspace.ModFolder,
            ["overrides_folder"] = _workspace.OverridesFolder,
            ["archives_count"] = _workspace.Archives().Count
        };
        return Task.FromResult<JsonNode>(result);
    }

    static Task<JsonNode> ListArchivesAsync()
    {
        var archives = _workspace!.Archives();
        var array = new JsonArray();
        foreach (var archive in archives)
        {
            array.Add(new JsonObject
            {
                ["path"] = archive,
                ["full_path"] = _workspace.ArchivePath(archive),
                ["is_new"] = _workspace.IsNewArchive(archive),
                ["is_loose"] = _workspace.IsLooseFolder(archive)
            });
        }
        return Task.FromResult<JsonNode>(new JsonObject { ["archives"] = array });
    }

    static Task<JsonNode> ListFilesInArchiveAsync(JsonNode? args)
    {
        string archive = args?["archive"]?.GetValue<string>() ?? throw new ArgumentException("archive required");
        var pack = _workspace!.Archive(archive);
        var array = new JsonArray();
        foreach (var entry in pack.Entries)
        {
            array.Add(new JsonObject
            {
                ["name"] = entry.Name,
                ["extension"] = entry.Extension,
                ["size"] = entry.Size,
                ["compressed_size"] = entry.CompressedSize,
                ["offset"] = entry.Offset
            });
        }
        return Task.FromResult<JsonNode>(new JsonObject { ["files"] = array, ["count"] = array.Count });
    }

    static Task<JsonNode> ReadFileAsync(JsonNode? args)
    {
        string archive = args?["archive"]?.GetValue<string>() ?? throw new ArgumentException("archive required");
        string fileName = args?["file_name"]?.GetValue<string>() ?? throw new ArgumentException("file_name required");
        byte[] data = _workspace!.Read(archive, fileName);
        return Task.FromResult<JsonNode>(new JsonObject
        {
            ["data_base64"] = Convert.ToBase64String(data),
            ["size"] = data.Length
        });
    }

    static Task<JsonNode> WriteFileAsync(JsonNode? args)
    {
        string archive = args?["archive"]?.GetValue<string>() ?? throw new ArgumentException("archive required");
        string fileName = args?["file_name"]?.GetValue<string>() ?? throw new ArgumentException("file_name required");
        string dataBase64 = args?["data_base64"]?.GetValue<string>() ?? throw new ArgumentException("data_base64 required");
        byte[] data = Convert.FromBase64String(dataBase64);
        string path = _workspace!.WriteOverride(archive, fileName, data);
        return Task.FromResult<JsonNode>(new JsonObject { ["path"] = path, ["size"] = data.Length });
    }

    static Task<JsonNode> DeleteOverrideAsync(JsonNode? args)
    {
        string archive = args?["archive"]?.GetValue<string>() ?? throw new ArgumentException("archive required");
        string fileName = args?["file_name"]?.GetValue<string>() ?? throw new ArgumentException("file_name required");
        _workspace!.DeleteOverride(archive, fileName);
        return Task.FromResult<JsonNode>(new JsonObject { ["success"] = true });
    }

    static Task<JsonNode> CreateWorkspaceAsync(JsonNode? args)
    {
        string gamePath = args?["game_path"]?.GetValue<string>() ?? throw new ArgumentException("game_path required");
        string modsPath = args?["mods_path"]?.GetValue<string>() ?? throw new ArgumentException("mods_path required");

        if (!Directory.Exists(gamePath))
            throw new DirectoryNotFoundException($"Game folder not found: {gamePath}");
        if (!Directory.Exists(modsPath))
            throw new DirectoryNotFoundException($"Mods folder not found: {modsPath}");

        // Initialize workspace, assets, game link, and context
        _workspace = new Workspace(gamePath, modsPath);
        _assets = new GameAssets(_workspace);
        _gameLink = new GameLink(modsPath);

        _context = new EditorContext
        {
            Workspace = _workspace,
            Assets = _assets,
            Log = msg => Console.Error.WriteLine($"[LOG] {msg}"),
            OverridesChanged = () => { },
            OpenTable = _ => { },
            OpenLevel = _ => { }
        };

        Console.Error.WriteLine($"RFA Editor MCP Server: Workspace created");
        Console.Error.WriteLine($"Game folder: {gamePath}");
        Console.Error.WriteLine($"Mod folder: {modsPath}");

        var result = new JsonObject
        {
            ["success"] = true,
            ["game_folder"] = _workspace.GameFolder,
            ["mod_folder"] = _workspace.ModFolder,
            ["overrides_folder"] = _workspace.OverridesFolder,
            ["archives_count"] = _workspace.Archives().Count
        };
        return Task.FromResult<JsonNode>(result);
    }

    // ===================== Table Tools =====================

    static Task<JsonNode> OpenTableAsync(JsonNode? args)
    {
        string archive = args?["archive"]?.GetValue<string>() ?? throw new ArgumentException("archive required");
        string tableName = args?["table_name"]?.GetValue<string>() ?? throw new ArgumentException("table_name required");
        byte[] data = _workspace!.Read(archive, tableName);
        var table = XtblFile.FromBytes(data);
        var result = new JsonObject
        {
            ["archive"] = archive,
            ["table_name"] = tableName,
            ["description"] = table.Description?.Name ?? "unknown",
            ["field_count"] = table.Description?.Children.Count ?? 0,
            ["entry_count"] = table.Entries.Count(),
            ["has_templates"] = table.Templates.Any()
        };
        return Task.FromResult<JsonNode>(result);
    }

    static Task<JsonNode> GetTableRowsAsync(JsonNode? args)
    {
        string archive = args?["archive"]?.GetValue<string>() ?? throw new ArgumentException("archive required");
        string tableName = args?["table_name"]?.GetValue<string>() ?? throw new ArgumentException("table_name required");
        int limit = args?["limit"]?.GetValue<int>() ?? 100;
        int offset = args?["offset"]?.GetValue<int>() ?? 0;

        byte[] data = _workspace!.Read(archive, tableName);
        var table = XtblFile.FromBytes(data);

        var array = new JsonArray();
        int index = 0;
        foreach (var entry in table.Entries.Skip(offset).Take(limit))
        {
            var row = new JsonObject { ["index"] = index + offset };
            if (table.Description != null)
            {
                foreach (var field in entry.Elements)
                {
                    row[field.Name] = FieldToJson(field);
                }
            }
            else
            {
                // No description - just dump raw values
                row["raw"] = FieldToJson(entry);
            }
            array.Add(row);
            index++;
        }

        return Task.FromResult<JsonNode>(new JsonObject
        {
            ["rows"] = array,
            ["total"] = table.Entries.Count(),
            ["limit"] = limit,
            ["offset"] = offset
        });
    }

    static Task<JsonNode> UpdateTableRowAsync(JsonNode? args)
    {
        string archive = args?["archive"]?.GetValue<string>() ?? throw new ArgumentException("archive required");
        string tableName = args?["table_name"]?.GetValue<string>() ?? throw new ArgumentException("table_name required");
        int rowIndex = args?["row_index"]?.GetValue<int>() ?? throw new ArgumentException("row_index required");
        JsonObject rowData = args?["row_data"]?.AsObject() ?? throw new ArgumentException("row_data required");

        byte[] data = _workspace!.Read(archive, tableName);
        var table = XtblFile.FromBytes(data);

        if (rowIndex < 0 || rowIndex >= table.Entries.Count())
            throw new ArgumentException($"Row index {rowIndex} out of range (0-{table.Entries.Count() - 1})");

        var entry = table.Entries.ElementAt(rowIndex);
        if (table.Description != null)
        {
            foreach (var (key, value) in rowData)
            {
                var field = entry.Elements.FirstOrDefault(f => f.Name.Equals(key, StringComparison.OrdinalIgnoreCase));
                if (field != null)
                {
                    SetFieldFromJson(field, value);
                }
            }
        }

        _workspace.WriteOverride(archive, tableName, table.ToBytes());
        return Task.FromResult<JsonNode>(new JsonObject { ["success"] = true, ["row_index"] = rowIndex });
    }

    static Task<JsonNode> AddTableRowAsync(JsonNode? args)
    {
        string archive = args?["archive"]?.GetValue<string>() ?? throw new ArgumentException("archive required");
        string tableName = args?["table_name"]?.GetValue<string>() ?? throw new ArgumentException("table_name required");
        JsonObject rowData = args?["row_data"]?.AsObject() ?? throw new ArgumentException("row_data required");

        byte[] data = _workspace!.Read(archive, tableName);
        var table = XtblFile.FromBytes(data);

        string kind = table.Description?.Name ?? (table.Entries.Any() ? table.Entries.First().Name : "entry");
        string name = table.UnusedName("new_" + kind.ToLowerInvariant());
        var newEntry = table.NewEntry(name, []);

        if (table.Description != null)
        {
            foreach (var (key, value) in rowData)
            {
                var field = newEntry.Elements.FirstOrDefault(f => f.Name.Equals(key, StringComparison.OrdinalIgnoreCase));
                if (field != null)
                {
                    SetFieldFromJson(field, value);
                }
            }
        }

        _workspace.WriteOverride(archive, tableName, table.ToBytes());
        return Task.FromResult<JsonNode>(new JsonObject { ["success"] = true, ["name"] = name });
    }

    static Task<JsonNode> DeleteTableRowAsync(JsonNode? args)
    {
        string archive = args?["archive"]?.GetValue<string>() ?? throw new ArgumentException("archive required");
        string tableName = args?["table_name"]?.GetValue<string>() ?? throw new ArgumentException("table_name required");
        int rowIndex = args?["row_index"]?.GetValue<int>() ?? throw new ArgumentException("row_index required");

        byte[] data = _workspace!.Read(archive, tableName);
        var table = XtblFile.FromBytes(data);

        if (rowIndex < 0 || rowIndex >= table.Entries.Count())
            throw new ArgumentException($"Row index {rowIndex} out of range");

        var entry = table.Entries.ElementAt(rowIndex);
        table.RemoveEntry(entry);
        _workspace.WriteOverride(archive, tableName, table.ToBytes());
        return Task.FromResult<JsonNode>(new JsonObject { ["success"] = true });
    }

    // ===================== Level/Zone Tools =====================

    sealed class ZoneDocument : IDisposable
    {
        public string Archive { get; }
        public string Container { get; }
        public string ZoneName { get; }
        public ZoneFile Zone { get; }
        public Packfile Pack { get; }

        public ZoneDocument(Workspace workspace, string archive, string container, string zoneName)
        {
            Archive = archive;
            Container = container;
            ZoneName = zoneName;
            Pack = workspace.Container(archive, container);
            if (Pack.Find(zoneName) is not { } entry)
                throw new FileNotFoundException($"{zoneName} not found in {container} in {archive}");
            Zone = ZoneFile.Read(Pack.Read(entry));
        }

        public void Save()
        {
            byte[] data = Zone.ToBytes();
            _workspace!.WriteOverride(Archive, ZoneName, data);
        }

        public void Dispose() { }
    }

    static ZoneDocument? _currentZone;

    static Task<JsonNode> OpenLevelAsync(JsonNode? args)
    {
        string archive = args?["archive"]?.GetValue<string>() ?? throw new ArgumentException("archive required");
        string container = args?["container"]?.GetValue<string>() ?? throw new ArgumentException("container required");
        string zoneName = args?["zone_name"]?.GetValue<string>() ?? throw new ArgumentException("zone_name required");

        _currentZone?.Dispose();
        _currentZone = new ZoneDocument(_workspace!, archive, container, zoneName);

        var result = new JsonObject
        {
            ["archive"] = archive,
            ["container"] = container,
            ["zone_name"] = zoneName,
            ["object_count"] = _currentZone.Zone.Objects.Count
        };
        return Task.FromResult<JsonNode>(result);
    }

    static Task<JsonNode> ListZoneObjectsAsync(JsonNode? args)
    {
        if (_currentZone == null) throw new InvalidOperationException("No zone open. Call open_level first.");
        string? filterClass = args?["filter_class"]?.GetValue<string>();

        var array = new JsonArray();
        foreach (var obj in _currentZone.Zone.Objects)
        {
            string className = _assets!.ZoneNames.Class(obj.ClassHash);
            if (filterClass != null && !className.Equals(filterClass, StringComparison.OrdinalIgnoreCase))
                continue;

            var placement = obj.Placement;
            array.Add(new JsonObject
            {
                ["handle"] = $"0x{obj.Handle:X8}",
                ["class_hash"] = $"0x{obj.ClassHash:X8}",
                ["class_name"] = className,
                ["position"] = new JsonObject
                {
                    ["x"] = placement.Position.X,
                    ["y"] = placement.Position.Y,
                    ["z"] = placement.Position.Z
                }
            });
        }
        return Task.FromResult<JsonNode>(new JsonObject { ["objects"] = array, ["count"] = array.Count });
    }

    static Task<JsonNode> GetZoneObjectAsync(JsonNode? args)
    {
        if (_currentZone == null) throw new InvalidOperationException("No zone open. Call open_level first.");
        string handleStr = args?["handle"]?.GetValue<string>() ?? throw new ArgumentException("handle required");
        uint handle = uint.Parse(handleStr.StartsWith("0x") ? handleStr[2..] : handleStr, System.Globalization.NumberStyles.HexNumber);

        var obj = _currentZone.Zone.Objects.FirstOrDefault(o => o.Handle == handle);
        if (obj == null) throw new ArgumentException($"Object 0x{handle:X8} not found");

        var placement = obj.Placement;
        var result = new JsonObject
        {
            ["handle"] = $"0x{obj.Handle:X8}",
            ["class_hash"] = $"0x{obj.ClassHash:X8}",
            ["class_name"] = _assets!.ZoneNames.Class(obj.ClassHash),
            ["position"] = new JsonObject { ["x"] = placement.Position.X, ["y"] = placement.Position.Y, ["z"] = placement.Position.Z },
            ["rotation"] = MatrixToJson(placement.Rotation),
            ["properties"] = new JsonObject()
        };

        foreach (var prop in obj.Properties)
        {
            result["properties"]![_assets!.ZoneNames.Property(prop.NameHash)] = PropertyToJson(prop);
        }

        return Task.FromResult<JsonNode>(result);
    }

    static Task<JsonNode> CreateZoneObjectAsync(JsonNode? args)
    {
        if (_currentZone == null) throw new InvalidOperationException("No zone open. Call open_level first.");

        string className = args?["class_name"]?.GetValue<string>() ?? throw new ArgumentException("class_name required");
        JsonObject position = args?["position"]?.AsObject() ?? throw new ArgumentException("position required");
        JsonObject rotation = args?["rotation"]?.AsObject() ?? throw new ArgumentException("rotation required");
        JsonObject? properties = args?["properties"]?.AsObject();

        uint classHash = NameHashes.ClassHash(className);
        if (classHash == 0) throw new ArgumentException($"Unknown class: {className}");

        var schema = _assets.ZoneSchema;
        var classDef = schema?.Classes.FirstOrDefault(c => c.Hash == classHash);
        if (classDef == null) throw new ArgumentException($"Class {className} not in zone schema");

        Vector3 pos = new Vector3(position["x"]!.GetValue<float>(), position["y"]!.GetValue<float>(), position["z"]!.GetValue<float>());

        // Use ZoneFile.AddNew which is public and handles handle generation
        // Convert schema properties to NewProperty list
        var newProps = new List<NewProperty>();

        // Add oP (position + rotation) property
        var placement = new byte[48];
        float[] numbers = [pos.X, pos.Y, pos.Z, 1, 0, 0, 0, 1, 0, 0, 0, 1];
        for (int i = 0; i < numbers.Length; i++) BitConverter.TryWriteBytes(placement.AsSpan(i * 4), numbers[i]);
        newProps.Add(new NewProperty(ZoneProperty.ValueType, NameHashes.PropertyHash("oP"), placement));

        // Add properties if provided, else use schema defaults
        if (properties != null)
        {
            foreach (var (key, value) in properties)
            {
                var propDef = classDef.Properties.FirstOrDefault(p => p.Name.Equals(key, StringComparison.OrdinalIgnoreCase));
                if (propDef != null && propDef.Exported)
                {
                    var (type, valueBytes) = CreatePropertyValueFromSchema(propDef, value);
                    newProps.Add(new NewProperty(type, propDef.Hash, valueBytes));
                }
            }
        }
        else
        {
            foreach (var propDef in classDef.Properties.Where(p => p.Exported))
            {
                byte[]? defaultBytes = propDef.DefaultValue(null);
                if (defaultBytes != null)
                {
                    // DefaultValue returns already-encoded bytes, just use the property's type
                    int type = (int)propDef.Type;
                    newProps.Add(new NewProperty(type, propDef.Hash, defaultBytes));
                }
            }
        }

        // AddNew handles handle generation and bounds
        var bounds = classDef.NewObjectBounds(pos);
        var obj = _currentZone.Zone.AddNew(classHash, newProps, bounds.Min, bounds.Max, _currentZone.Zone.Objects.Select(o => o.Handle));

        return Task.FromResult<JsonNode>(new JsonObject { ["success"] = true, ["handle"] = $"0x{obj.Handle:X8}" });
    }

    static Task<JsonNode> UpdateZoneObjectAsync(JsonNode? args)
    {
        if (_currentZone == null) throw new InvalidOperationException("No zone open. Call open_level first.");
        string handleStr = args?["handle"]?.GetValue<string>() ?? throw new ArgumentException("handle required");
        uint handle = uint.Parse(handleStr.StartsWith("0x") ? handleStr[2..] : handleStr, System.Globalization.NumberStyles.HexNumber);

        var obj = _currentZone.Zone.Objects.FirstOrDefault(o => o.Handle == handle);
        if (obj == null) throw new ArgumentException($"Object 0x{handle:X8} not found");

        if (args?["position"] is JsonObject pos)
        {
            obj.MoveTo(new Vector3(pos["x"]!.GetValue<float>(), pos["y"]!.GetValue<float>(), pos["z"]!.GetValue<float>()));
        }
        if (args?["rotation"] is JsonObject rot)
        {
            obj.TurnTo(JsonToMatrix(rot));
        }
        if (args?["properties"] is JsonObject props)
        {
            var schema = _assets!.ZoneSchema;
            var classDef = schema?.Classes.FirstOrDefault(c => c.Hash == obj.ClassHash);
            if (classDef != null)
            {
                foreach (var (key, value) in props)
                {
                    var propDef = classDef.Properties.FirstOrDefault(p => p.Name.Equals(key, StringComparison.OrdinalIgnoreCase));
                    if (propDef != null)
                    {
                        var existing = obj.Properties.FirstOrDefault(p => p.NameHash == propDef.Hash);
                        if (existing != null)
                        {
                            obj.RemoveProperty(propDef.Hash);
                        }
                        var (type, valueBytes) = CreatePropertyValueFromSchema(propDef, value);
                        obj.AddProperty(type, propDef.Hash, valueBytes);
                    }
                }
            }
        }

        return Task.FromResult<JsonNode>(new JsonObject { ["success"] = true });
    }

    static Task<JsonNode> DeleteZoneObjectAsync(JsonNode? args)
    {
        if (_currentZone == null) throw new InvalidOperationException("No zone open. Call open_level first.");
        string handleStr = args?["handle"]?.GetValue<string>() ?? throw new ArgumentException("handle required");
        uint handle = uint.Parse(handleStr.StartsWith("0x") ? handleStr[2..] : handleStr, System.Globalization.NumberStyles.HexNumber);

        var obj = _currentZone.Zone.Objects.FirstOrDefault(o => o.Handle == handle);
        if (obj == null) throw new ArgumentException($"Object 0x{handle:X8} not found");

        _currentZone.Zone.Remove(obj);
        return Task.FromResult<JsonNode>(new JsonObject { ["success"] = true });
    }

    static Task<JsonNode> SaveZoneAsync()
    {
        if (_currentZone == null) throw new InvalidOperationException("No zone open. Call open_level first.");
        _currentZone.Save();
        return Task.FromResult<JsonNode>(new JsonObject { ["success"] = true });
    }

    // ===================== Game Link Tools =====================

    static Task<JsonNode> GetGameStateAsync()
    {
        var view = _gameLink!.Read();
        if (view == null || !GameLink.IsRecent(view))
        {
            return Task.FromResult<JsonNode>(new JsonObject { ["connected"] = false });
        }

        var result = new JsonObject
        {
            ["connected"] = true,
            ["frame"] = view.Frame,
            ["written"] = view.Written.ToString("o"),
            ["camera"] = new JsonObject
            {
                ["x"] = view.Camera.X,
                ["y"] = view.Camera.Y,
                ["z"] = view.Camera.Z
            },
            ["forward"] = new JsonObject
            {
                ["x"] = view.Forward.X,
                ["y"] = view.Forward.Y,
                ["z"] = view.Forward.Z
            }
        };

        if (view.Picked.HasValue)
            result["picked_handle"] = $"0x{view.Picked.Value:X8}";
        if (view.VerticalFov.HasValue)
            result["vertical_fov"] = view.VerticalFov.Value;

        return Task.FromResult<JsonNode>(result);
    }

    static Task<JsonNode> SendConsoleCommandAsync(JsonNode? args)
    {
        string command = args?["command"]?.GetValue<string>() ?? throw new ArgumentException("command required");
        _gameLink!.Send([command]);
        return Task.FromResult<JsonNode>(new JsonObject { ["success"] = true });
    }

    static Task<JsonNode> SendConsoleCommandsAsync(JsonNode? args)
    {
        JsonArray? commandsArray = args?["commands"]?.AsArray() ?? throw new ArgumentException("commands array required");
        var commands = commandsArray.Select(c => c!.GetValue<string>()).ToList();
        _gameLink!.Send(commands);
        return Task.FromResult<JsonNode>(new JsonObject { ["success"] = true, ["count"] = commands.Count });
    }

    static Task<JsonNode> MoveGameObjectAsync(JsonNode? args)
    {
        string handleStr = args?["handle"]?.GetValue<string>() ?? throw new ArgumentException("handle required");
        uint handle = uint.Parse(handleStr.StartsWith("0x") ? handleStr[2..] : handleStr, System.Globalization.NumberStyles.HexNumber);

        JsonObject position = args?["position"]?.AsObject() ?? throw new ArgumentException("position required");
        JsonObject rotation = args?["rotation"]?.AsObject() ?? throw new ArgumentException("rotation required");

        Vector3 pos = new Vector3(position["x"]!.GetValue<float>(), position["y"]!.GetValue<float>(), position["z"]!.GetValue<float>());
        Matrix4x4 rot = JsonToMatrix(rotation);

        string cmd = GameLink.MoveCommand(handle, pos, rot);
        _gameLink!.Send([cmd]);
        return Task.FromResult<JsonNode>(new JsonObject { ["success"] = true, ["command"] = cmd });
    }

    static Task<JsonNode> CreateGameObjectAsync(JsonNode? args)
    {
        string handleStr = args?["handle"]?.GetValue<string>() ?? throw new ArgumentException("handle required");
        uint handle = uint.Parse(handleStr.StartsWith("0x") ? handleStr[2..] : handleStr, System.Globalization.NumberStyles.HexNumber);
        string blockDataBase64 = args?["block_data_base64"]?.GetValue<string>() ?? throw new ArgumentException("block_data_base64 required");
        byte[] blockData = Convert.FromBase64String(blockDataBase64);

        string cmd = _gameLink!.CreateCommand(handle, blockData);
        _gameLink.Send([cmd]);
        return Task.FromResult<JsonNode>(new JsonObject { ["success"] = true, ["command"] = cmd });
    }

    static Task<JsonNode> RemoveGameObjectAsync(JsonNode? args)
    {
        string handleStr = args?["handle"]?.GetValue<string>() ?? throw new ArgumentException("handle required");
        uint handle = uint.Parse(handleStr.StartsWith("0x") ? handleStr[2..] : handleStr, System.Globalization.NumberStyles.HexNumber);

        string cmd = GameLink.RemoveCommand(handle);
        _gameLink!.Send([cmd]);
        return Task.FromResult<JsonNode>(new JsonObject { ["success"] = true, ["command"] = cmd });
    }

    static Task<JsonNode> EnableLiveEditingAsync(JsonNode? args)
    {
        bool enabled = args?["enabled"]?.GetValue<bool>() ?? throw new ArgumentException("enabled required");
        _liveEditingEnabled = enabled;
        return Task.FromResult<JsonNode>(new JsonObject { ["success"] = true, ["live_editing"] = enabled });
    }

    static Task<JsonNode> SaveAndReloadLevelAsync()
    {
        if (_currentZone == null) throw new InvalidOperationException("No zone open. Call open_level first.");
        _currentZone.Save();
        _gameLink!.Send(["reload_zone"]);
        return Task.FromResult<JsonNode>(new JsonObject { ["success"] = true });
    }

    // ===================== Asset/Chunk Tools =====================

    static Task<JsonNode> ListChunksAsync()
    {
        var chunks = _assets!.ChunkNames();
        var array = new JsonArray();
        foreach (var chunk in chunks)
        {
            array.Add(chunk);
        }
        return Task.FromResult<JsonNode>(new JsonObject { ["chunks"] = array, ["count"] = array.Count });
    }

    static Task<JsonNode> GetChunkInfoAsync(JsonNode? args)
    {
        string chunkName = args?["chunk_name"]?.GetValue<string>() ?? throw new ArgumentException("chunk_name required");
        var catalog = _assets!.ChunkCatalog();
        var entry = catalog.FirstOrDefault(c => c.Name.Equals(chunkName, StringComparison.OrdinalIgnoreCase));
        if (entry == null) throw new ArgumentException($"Chunk {chunkName} not found");

        var result = new JsonObject
        {
            ["name"] = entry.Name,
            ["type"] = entry.Type,
            ["category"] = entry.Category,
            ["setting"] = entry.Setting,
            ["stitch"] = entry.Stitch,
            ["packs"] = new JsonArray()
        };

        foreach (var (pack, container) in entry.Places)
        {
            var packObj = new JsonObject { ["pack"] = pack };
            if (container != null) packObj["container"] = container;
            result["packs"]!.AsArray().Add(packObj);
        }

        return Task.FromResult<JsonNode>(result);
    }

    static Task<JsonNode> GetAssetListAsync(JsonNode? args)
    {
        string chunkName = args?["chunk_name"]?.GetValue<string>() ?? throw new ArgumentException("chunk_name required");
        var package = _assets!.PackageOf(chunkName, out string? why);
        if (package == null) throw new ArgumentException($"Could not get asset list for {chunkName}: {why}");

        var primitivesArray = new JsonArray();
        foreach (var prim in package.Primitives)
        {
            primitivesArray.Add(new JsonObject
            {
                ["name"] = prim.Name,
                ["type"] = prim.Type,
                ["allocator"] = prim.Allocator,
                ["flags"] = prim.Flags,
                ["split_index"] = prim.SplitIndex,
                ["header_size"] = prim.HeaderSize,
                ["data_size"] = prim.DataSize
            });
        }

        var groupObj = new JsonObject
        {
            ["name"] = package.Group.Name,
            ["type"] = package.Group.Type,
            ["flags"] = package.Group.Flags
        };

        return Task.FromResult<JsonNode>(new JsonObject
        {
            ["chunk"] = package.Chunk,
            ["primitives"] = primitivesArray,
            ["group"] = groupObj
        });
    }

    // ===================== Game Launching =====================

    static Task<JsonNode> StartGameAsync()
    {
        string launcher = Path.Combine(_workspace!.ModFolder, "RFAModLauncher.exe");
        if (!File.Exists(launcher))
            throw new FileNotFoundException($"Launcher not found at {launcher}. Run build.cmd first.");

        System.Diagnostics.Process.Start(new System.Diagnostics.ProcessStartInfo(launcher)
        {
            UseShellExecute = true,
            WorkingDirectory = _workspace.ModFolder
        });

        return Task.FromResult<JsonNode>(new JsonObject { ["success"] = true, ["message"] = "Game started" });
    }

    static Task<JsonNode> DockGameAsync()
    {
        return Task.FromResult<JsonNode>(new JsonObject
        {
            ["success"] = false,
            ["message"] = "Docking requires the editor UI. Use the Game panel in the editor to dock the game."
        });
    }

    static Task<JsonNode> UndockGameAsync()
    {
        return Task.FromResult<JsonNode>(new JsonObject
        {
            ["success"] = false,
            ["message"] = "Undocking requires the editor UI. Use the Game panel in the editor to undock the game."
        });
    }

    // ===================== Helpers =====================

    static JsonNode FieldToJson(XtblElement field)
    {
        // Try to get value directly; if it has children, return as object
        if (field.HasElements)
        {
            var obj = new JsonObject();
            foreach (var child in field.Elements)
            {
                obj[child.Name] = FieldToJson(child);
            }
            return obj;
        }
        return field.Value;
    }

    static void SetFieldFromJson(XtblElement field, JsonNode value)
    {
        try
        {
            if (field.HasElements)
            {
                if (value is JsonObject obj)
                {
                    foreach (var child in field.Elements)
                    {
                        if (obj.TryGetPropertyValue(child.Name, out var childValue))
                        {
                            SetFieldFromJson(child, childValue!);
                        }
                    }
                }
            }
            else
            {
                field.Value = value.ToString();
            }
        }
        catch { /* ignore conversion errors */ }
    }

    static JsonNode PropertyToJson(ZoneProperty prop)
    {
        return prop.Type switch
        {
            ZoneProperty.TextType => prop.Text,
            ZoneProperty.ValueType => new JsonArray(prop.Floats().Select(f => JsonValue.Create(f)).ToArray()),
            ZoneProperty.ListType => new JsonArray(prop.Floats().Select(f => JsonValue.Create(f)).ToArray()),
            _ => prop.Text
        };
    }

    static (int Type, byte[] Value) CreatePropertyValue(FieldDescription def, JsonNode value)
    {
        int type = def.Type switch
        {
            FieldType.Int => ZoneProperty.ValueType,
            FieldType.Float => ZoneProperty.ValueType,
            FieldType.String => ZoneProperty.TextType,
            FieldType.Selection => ZoneProperty.TextType,
            FieldType.Flags => ZoneProperty.TextType,
            FieldType.Reference => ZoneProperty.TextType,
            FieldType.Filename => ZoneProperty.TextType,
            FieldType.Vector => ZoneProperty.ValueType,
            FieldType.Color => ZoneProperty.ValueType,
            FieldType.Element => ZoneProperty.ValueType,
            FieldType.ComboElement => ZoneProperty.ValueType,
            FieldType.List => ZoneProperty.ListType,
            FieldType.Grid => ZoneProperty.ListType,
            _ => ZoneProperty.TextType
        };

        byte[] valueBytes;
        if (def.Type == FieldType.List || def.Type == FieldType.Grid)
        {
            var arr = new List<byte>();
            if (value is JsonArray jArr)
            {
                foreach (var item in jArr)
                {
                    if (def.Item != null)
                    {
                        var itemVal = CreatePropertyValue(def.Item, item);
                        arr.AddRange(itemVal.Value);
                    }
                }
            }
            valueBytes = arr.ToArray();
        }
        else if (def.Type == FieldType.Vector)
        {
            var v = new Vector3(
                value["x"]!.GetValue<float>(),
                value["y"]!.GetValue<float>(),
                value["z"]!.GetValue<float>());
            valueBytes = new byte[12];
            BitConverter.TryWriteBytes(valueBytes.AsSpan(0), v.X);
            BitConverter.TryWriteBytes(valueBytes.AsSpan(4), v.Y);
            BitConverter.TryWriteBytes(valueBytes.AsSpan(8), v.Z);
        }
        else if (def.Type == FieldType.Color)
        {
            var c = System.Drawing.Color.FromArgb(
                value["a"]!.GetValue<byte>(),
                value["r"]!.GetValue<byte>(),
                value["g"]!.GetValue<byte>(),
                value["b"]!.GetValue<byte>());
            valueBytes = new byte[16];
            BitConverter.TryWriteBytes(valueBytes.AsSpan(0), c.R / 255f);
            BitConverter.TryWriteBytes(valueBytes.AsSpan(4), c.G / 255f);
            BitConverter.TryWriteBytes(valueBytes.AsSpan(8), c.B / 255f);
            BitConverter.TryWriteBytes(valueBytes.AsSpan(12), c.A / 255f);
        }
        else if (def.Type == FieldType.Int)
        {
            valueBytes = BitConverter.GetBytes(value.GetValue<int>());
        }
        else if (def.Type == FieldType.Float)
        {
            valueBytes = BitConverter.GetBytes(value.GetValue<float>());
        }
        else
        {
            string text = value.GetValue<string>();
            valueBytes = Encoding.Latin1.GetBytes(text + "\0");
        }

        return (type, valueBytes);
    }

    static (int Type, byte[] Value) CreatePropertyValueFromSchema(SchemaProperty prop, JsonNode value)
    {
        int type = prop.ZoneType;
        byte[] valueBytes;

        switch (prop.Type)
        {
            case SchemaType.List:
            {
                var arr = new List<byte>();
                if (value is JsonArray jArr)
                {
                    foreach (var item in jArr)
                    {
                        // For list items, we expect them to be arrays of floats matching the struct fields
                        if (item is JsonArray itemArr)
                        {
                            foreach (var field in prop.Fields)
                            {
                                if (field.ZoneSize > 0 && itemArr.Count > 0)
                                {
                                    float f = itemArr[0].GetValue<float>();
                                    byte[] fieldBytes = BitConverter.GetBytes(f);
                                    arr.AddRange(fieldBytes);
                                }
                                else if (field.Type == SchemaType.Text)
                                {
                                    string text = itemArr[0].GetValue<string>();
                                    byte[] textBytes = Encoding.Latin1.GetBytes(text + "\0");
                                    arr.AddRange(textBytes);
                                }
                                if (itemArr.Count > 1)
                                {
                                    // This is a simplified approach - in reality each field would be a separate element
                                }
                            }
                        }
                    }
                }
                valueBytes = arr.ToArray();
                break;
            }
            case SchemaType.Vector:
            {
                var v = new Vector3(
                    value["x"]!.GetValue<float>(),
                    value["y"]!.GetValue<float>(),
                    value["z"]!.GetValue<float>());
                valueBytes = new byte[12];
                BitConverter.TryWriteBytes(valueBytes.AsSpan(0), v.X);
                BitConverter.TryWriteBytes(valueBytes.AsSpan(4), v.Y);
                BitConverter.TryWriteBytes(valueBytes.AsSpan(8), v.Z);
                break;
            }
            case SchemaType.Color:
            {
                var c = System.Drawing.Color.FromArgb(
                    value["a"]!.GetValue<byte>(),
                    value["r"]!.GetValue<byte>(),
                    value["g"]!.GetValue<byte>(),
                    value["b"]!.GetValue<byte>());
                valueBytes = new byte[16];
                BitConverter.TryWriteBytes(valueBytes.AsSpan(0), c.R / 255f);
                BitConverter.TryWriteBytes(valueBytes.AsSpan(4), c.G / 255f);
                BitConverter.TryWriteBytes(valueBytes.AsSpan(8), c.B / 255f);
                BitConverter.TryWriteBytes(valueBytes.AsSpan(12), c.A / 255f);
                break;
            }
            case SchemaType.Int:
            {
                valueBytes = BitConverter.GetBytes(value.GetValue<int>());
                break;
            }
            case SchemaType.Float:
            {
                valueBytes = BitConverter.GetBytes(value.GetValue<float>());
                break;
            }
            case SchemaType.Bool:
            {
                valueBytes = [value.GetValue<bool>() ? (byte)1 : (byte)0];
                break;
            }
            case SchemaType.Handle:
            {
                uint handle = value.GetValue<uint>();
                valueBytes = BitConverter.GetBytes(handle);
                break;
            }
            case SchemaType.Box:
            {
                // Box: 6 floats (min x,y,z, max x,y,z)
                var min = new Vector3(
                    value["min_x"]!.GetValue<float>(),
                    value["min_y"]!.GetValue<float>(),
                    value["min_z"]!.GetValue<float>());
                var max = new Vector3(
                    value["max_x"]!.GetValue<float>(),
                    value["max_y"]!.GetValue<float>(),
                    value["max_z"]!.GetValue<float>());
                valueBytes = new byte[24];
                BitConverter.TryWriteBytes(valueBytes.AsSpan(0), min.X);
                BitConverter.TryWriteBytes(valueBytes.AsSpan(4), min.Y);
                BitConverter.TryWriteBytes(valueBytes.AsSpan(8), min.Z);
                BitConverter.TryWriteBytes(valueBytes.AsSpan(12), max.X);
                BitConverter.TryWriteBytes(valueBytes.AsSpan(16), max.Y);
                BitConverter.TryWriteBytes(valueBytes.AsSpan(20), max.Z);
                break;
            }
            case SchemaType.Matrix:
            {
                // Matrix: 9 floats (3x3 rotation matrix)
                valueBytes = new byte[36];
                for (int i = 0; i < 9; i++)
                {
                    string key = $"m{i / 3 + 1}{i % 3 + 1}";
                    float f = value[key]!.GetValue<float>();
                    BitConverter.TryWriteBytes(valueBytes.AsSpan(i * 4), f);
                }
                break;
            }
            default:
            {
                // Text, Enum, Flags - stored as Latin-1 string with NUL terminator
                string text = value.GetValue<string>();
                valueBytes = Encoding.Latin1.GetBytes(text + "\0");
                break;
            }
        }

        return (type, valueBytes);
    }

    static JsonObject MatrixToJson(Matrix4x4 m)
    {
        return new JsonObject
        {
            ["m11"] = m.M11, ["m12"] = m.M12, ["m13"] = m.M13,
            ["m21"] = m.M21, ["m22"] = m.M22, ["m23"] = m.M23,
            ["m31"] = m.M31, ["m32"] = m.M32, ["m33"] = m.M33
        };
    }

    static Matrix4x4 JsonToMatrix(JsonObject obj)
    {
        return new Matrix4x4(
            obj["m11"]!.GetValue<float>(), obj["m12"]!.GetValue<float>(), obj["m13"]!.GetValue<float>(), 0,
            obj["m21"]!.GetValue<float>(), obj["m22"]!.GetValue<float>(), obj["m23"]!.GetValue<float>(), 0,
            obj["m31"]!.GetValue<float>(), obj["m32"]!.GetValue<float>(), obj["m33"]!.GetValue<float>(), 0,
            0, 0, 0, 1);
    }
}