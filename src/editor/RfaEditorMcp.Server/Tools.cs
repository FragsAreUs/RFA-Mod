using System;
using System.Collections.Generic;
using System.IO;
using System.Numerics;
using System.Text.Json.Nodes;
using System.Threading.Tasks;
using RfaEditor;
using RfaFormats;

namespace RfaEditorMcp;

/// <summary>
/// MCP tool definitions for the RFA Editor.
/// </summary>
public static class McpTools
{
    /// <summary>
    /// All tools exposed by the RFA Editor MCP server.
    /// </summary>
    public static readonly IReadOnlyList<Tool> All = new List<Tool>
    {
        // Game/Workspace tools
        new Tool("create_workspace", "Create and initialize a new workspace with game and mods paths",
            new JsonObject
            {
                ["type"] = "object",
                ["properties"] = new JsonObject
                {
                    ["game_path"] = new JsonObject { ["type"] = "string", ["description"] = "Path to Red Faction: Armageddon game installation folder" },
                    ["mods_path"] = new JsonObject { ["type"] = "string", ["description"] = "Path to mods folder (typically bin/mods or a custom mods folder)" }
                },
                ["required"] = new JsonArray { "game_path", "mods_path" }
            }),

        new Tool("get_workspace_info", "Get information about the current workspace (game folder, mod folder, archives)",
            new JsonObject { ["type"] = "object", ["properties"] = new JsonObject() }),

        new Tool("list_archives", "List all archives in the workspace (game archives, new archives in overrides, loose file folders)",
            new JsonObject { ["type"] = "object", ["properties"] = new JsonObject() }),

        new Tool("list_files_in_archive", "List files in a specific archive",
            new JsonObject
            {
                ["type"] = "object",
                ["properties"] = new JsonObject
                {
                    ["archive"] = new JsonObject { ["type"] = "string", ["description"] = "Archive path relative to game folder" }
                },
                ["required"] = new JsonArray { "archive" }
            }),

        new Tool("read_file", "Read a file from an archive (as the game sees it: override if present, else original)",
            new JsonObject
            {
                ["type"] = "object",
                ["properties"] = new JsonObject
                {
                    ["archive"] = new JsonObject { ["type"] = "string", ["description"] = "Archive path relative to game folder" },
                    ["file_name"] = new JsonObject { ["type"] = "string", ["description"] = "File name within the archive" }
                },
                ["required"] = new JsonArray { "archive", "file_name" }
            }),

        new Tool("write_file", "Write a file to an archive (creates an override in overrides\\)",
            new JsonObject
            {
                ["type"] = "object",
                ["properties"] = new JsonObject
                {
                    ["archive"] = new JsonObject { ["type"] = "string", ["description"] = "Archive path relative to game folder" },
                    ["file_name"] = new JsonObject { ["type"] = "string", ["description"] = "File name within the archive" },
                    ["data_base64"] = new JsonObject { ["type"] = "string", ["description"] = "File content as base64" }
                },
                ["required"] = new JsonArray { "archive", "file_name", "data_base64" }
            }),

        new Tool("delete_override", "Delete an override file (restore the game's original)",
            new JsonObject
            {
                ["type"] = "object",
                ["properties"] = new JsonObject
                {
                    ["archive"] = new JsonObject { ["type"] = "string", ["description"] = "Archive path relative to game folder" },
                    ["file_name"] = new JsonObject { ["type"] = "string", ["description"] = "File name within the archive" }
                },
                ["required"] = new JsonArray { "archive", "file_name" }
            }),

        // Table tools
        new Tool("open_table", "Open a table in the editor (returns table info)",
            new JsonObject
            {
                ["type"] = "object",
                ["properties"] = new JsonObject
                {
                    ["archive"] = new JsonObject { ["type"] = "string", ["description"] = "Archive path relative to game folder" },
                    ["table_name"] = new JsonObject { ["type"] = "string", ["description"] = "Table file name (e.g., weapons.xtbl)" }
                },
                ["required"] = new JsonArray { "archive", "table_name" }
            }),

        new Tool("get_table_rows", "Get rows from a table",
            new JsonObject
            {
                ["type"] = "object",
                ["properties"] = new JsonObject
                {
                    ["archive"] = new JsonObject { ["type"] = "string", ["description"] = "Archive path relative to game folder" },
                    ["table_name"] = new JsonObject { ["type"] = "string", ["description"] = "Table file name" },
                    ["limit"] = new JsonObject { ["type"] = "integer", ["description"] = "Max rows to return (default 100)" },                    ["offset"] = new JsonObject { ["type"] = "integer", ["description"] = "Row offset (default 0)" }
                },
                ["required"] = new JsonArray { "archive", "table_name" }
            }),

        new Tool("update_table_row", "Update a row in a table",
            new JsonObject
            {
                ["type"] = "object",
                ["properties"] = new JsonObject
                {
                    ["archive"] = new JsonObject { ["type"] = "string", ["description"] = "Archive path relative to game folder" },
                    ["table_name"] = new JsonObject { ["type"] = "string", ["description"] = "Table file name" },
                    ["row_index"] = new JsonObject { ["type"] = "integer", ["description"] = "Row index to update" },
                    ["row_data"] = new JsonObject { ["type"] = "object", ["description"] = "Row data as key-value pairs" }
                },
                ["required"] = new JsonArray { "archive", "table_name", "row_index", "row_data" }
            }),

        new Tool("add_table_row", "Add a new row to a table",
            new JsonObject
            {
                ["type"] = "object",
                ["properties"] = new JsonObject
                {
                    ["archive"] = new JsonObject { ["type"] = "string", ["description"] = "Archive path relative to game folder" },
                    ["table_name"] = new JsonObject { ["type"] = "string", ["description"] = "Table file name" },
                    ["row_data"] = new JsonObject { ["type"] = "object", ["description"] = "Row data as key-value pairs" }
                },
                ["required"] = new JsonArray { "archive", "table_name", "row_data" }
            }),

        new Tool("delete_table_row", "Delete a row from a table",
            new JsonObject
            {
                ["type"] = "object",
                ["properties"] = new JsonObject
                {
                    ["archive"] = new JsonObject { ["type"] = "string", ["description"] = "Archive path relative to game folder" },
                    ["table_name"] = new JsonObject { ["type"] = "string", ["description"] = "Table file name" },
                    ["row_index"] = new JsonObject { ["type"] = "integer", ["description"] = "Row index to delete" }
                },
                ["required"] = new JsonArray { "archive", "table_name", "row_index" }
            }),

        // Level/Zone tools
        new Tool("open_level", "Open a level/zone in the editor",
            new JsonObject
            {
                ["type"] = "object",
                ["properties"] = new JsonObject
                {
                    ["archive"] = new JsonObject { ["type"] = "string", ["description"] = "Archive path relative to game folder" },
                    ["container"] = new JsonObject { ["type"] = "string", ["description"] = "Container name (e.g., zone.rfgzone_pc)" },
                    ["zone_name"] = new JsonObject { ["type"] = "string", ["description"] = "Zone file name" }
                },
                ["required"] = new JsonArray { "archive", "container", "zone_name" }
            }),

        new Tool("list_zone_objects", "List all objects in the currently open zone",
            new JsonObject
            {
                ["type"] = "object",
                ["properties"] = new JsonObject
                {
                    ["filter_class"] = new JsonObject { ["type"] = "string", ["description"] = "Optional class name filter (e.g., object_light)" }
                }
            }),

        new Tool("get_zone_object", "Get details of a specific zone object by handle",
            new JsonObject
            {
                ["type"] = "object",
                ["properties"] = new JsonObject
                {
                    ["handle"] = new JsonObject { ["type"] = "string", ["description"] = "Object handle (hex, e.g., 0x12345678)" }
                },
                ["required"] = new JsonArray { "handle" }
            }),

        new Tool("create_zone_object", "Create a new object in the zone",
            new JsonObject
            {
                ["type"] = "object",
                ["properties"] = new JsonObject
                {
                    ["class_name"] = new JsonObject { ["type"] = "string", ["description"] = "Object class (e.g., object_light, object_chunk)" },
                    ["position"] = new JsonObject
                    {
                        ["type"] = "object",
                        ["properties"] = new JsonObject
                        {
                            ["x"] = new JsonObject { ["type"] = "number" },
                            ["y"] = new JsonObject { ["type"] = "number" },
                            ["z"] = new JsonObject { ["type"] = "number" }
                        },
                        ["required"] = new JsonArray { "x", "y", "z" }
                    },
                    ["rotation"] = new JsonObject
                    {
                        ["type"] = "object",
                        ["properties"] = new JsonObject
                        {
                            ["m11"] = new JsonObject { ["type"] = "number" },
                            ["m12"] = new JsonObject { ["type"] = "number" },
                            ["m13"] = new JsonObject { ["type"] = "number" },
                            ["m21"] = new JsonObject { ["type"] = "number" },
                            ["m22"] = new JsonObject { ["type"] = "number" },
                            ["m23"] = new JsonObject { ["type"] = "number" },
                            ["m31"] = new JsonObject { ["type"] = "number" },
                            ["m32"] = new JsonObject { ["type"] = "number" },
                            ["m33"] = new JsonObject { ["type"] = "number" }
                        },
                        ["required"] = new JsonArray { "m11", "m12", "m13", "m21", "m22", "m23", "m31", "m32", "m33" }
                    },
                    ["properties"] = new JsonObject { ["type"] = "object", ["description"] = "Additional object properties" }
                },
                ["required"] = new JsonArray { "class_name", "position", "rotation" }
            }),

        new Tool("update_zone_object", "Update an existing zone object",
            new JsonObject
            {
                ["type"] = "object",
                ["properties"] = new JsonObject
                {
                    ["handle"] = new JsonObject { ["type"] = "string", ["description"] = "Object handle (hex)" },
                    ["position"] = new JsonObject
                    {
                        ["type"] = "object",
                        ["properties"] = new JsonObject
                        {
                            ["x"] = new JsonObject { ["type"] = "number" },
                            ["y"] = new JsonObject { ["type"] = "number" },
                            ["z"] = new JsonObject { ["type"] = "number" }
                        }
                    },
                    ["rotation"] = new JsonObject
                    {
                        ["type"] = "object",
                        ["properties"] = new JsonObject
                        {
                            ["m11"] = new JsonObject { ["type"] = "number" },
                            ["m12"] = new JsonObject { ["type"] = "number" },
                            ["m13"] = new JsonObject { ["type"] = "number" },
                            ["m21"] = new JsonObject { ["type"] = "number" },
                            ["m22"] = new JsonObject { ["type"] = "number" },
                            ["m23"] = new JsonObject { ["type"] = "number" },
                            ["m31"] = new JsonObject { ["type"] = "number" },
                            ["m32"] = new JsonObject { ["type"] = "number" },
                            ["m33"] = new JsonObject { ["type"] = "number" }
                        }
                    },
                    ["properties"] = new JsonObject { ["type"] = "object", ["description"] = "Object properties to update" }
                },
                ["required"] = new JsonArray { "handle" }
            }),

        new Tool("delete_zone_object", "Delete a zone object",
            new JsonObject
            {
                ["type"] = "object",
                ["properties"] = new JsonObject
                {
                    ["handle"] = new JsonObject { ["type"] = "string", ["description"] = "Object handle (hex)" }
                },
                ["required"] = new JsonArray { "handle" }
            }),

        new Tool("save_zone", "Save the current zone",
            new JsonObject { ["type"] = "object", ["properties"] = new JsonObject() }),

        // Game linking tools
        new Tool("get_game_state", "Get current game state from the running game (via shared memory link)",
            new JsonObject { ["type"] = "object", ["properties"] = new JsonObject() }),

        new Tool("send_console_command", "Send a console command to the running game",
            new JsonObject
            {
                ["type"] = "object",
                ["properties"] = new JsonObject
                {
                    ["command"] = new JsonObject { ["type"] = "string", ["description"] = "Console command to execute" }
                },
                ["required"] = new JsonArray { "command" }
            }),

        new Tool("send_console_commands", "Send multiple console commands to the running game",
            new JsonObject
            {
                ["type"] = "object",
                ["properties"] = new JsonObject
                {
                    ["commands"] = new JsonObject { ["type"] = "array", ["items"] = new JsonObject { ["type"] = "string" }, ["description"] = "Console commands to execute" }
                },
                ["required"] = new JsonArray { "commands" }
            }),

        new Tool("move_game_object", "Move an object in the running game (Live editing)",
            new JsonObject
            {
                ["type"] = "object",
                ["properties"] = new JsonObject
                {
                    ["handle"] = new JsonObject { ["type"] = "string", ["description"] = "Object handle (hex)" },
                    ["position"] = new JsonObject
                    {
                        ["type"] = "object",
                        ["properties"] = new JsonObject
                        {
                            ["x"] = new JsonObject { ["type"] = "number" },
                            ["y"] = new JsonObject { ["type"] = "number" },
                            ["z"] = new JsonObject { ["type"] = "number" }
                        },
                        ["required"] = new JsonArray { "x", "y", "z" }
                    },
                    ["rotation"] = new JsonObject
                    {
                        ["type"] = "object",
                        ["properties"] = new JsonObject
                        {
                            ["m11"] = new JsonObject { ["type"] = "number" },
                            ["m12"] = new JsonObject { ["type"] = "number" },
                            ["m13"] = new JsonObject { ["type"] = "number" },
                            ["m21"] = new JsonObject { ["type"] = "number" },
                            ["m22"] = new JsonObject { ["type"] = "number" },
                            ["m23"] = new JsonObject { ["type"] = "number" },
                            ["m31"] = new JsonObject { ["type"] = "number" },
                            ["m32"] = new JsonObject { ["type"] = "number" },
                            ["m33"] = new JsonObject { ["type"] = "number" }
                        },
                        ["required"] = new JsonArray { "m11", "m12", "m13", "m21", "m22", "m23", "m31", "m32", "m33" }
                    }
                },
                ["required"] = new JsonArray { "handle", "position", "rotation" }
            }),

        new Tool("create_game_object", "Create an object in the running game (Live editing)",
            new JsonObject
            {
                ["type"] = "object",
                ["properties"] = new JsonObject
                {
                    ["handle"] = new JsonObject { ["type"] = "string", ["description"] = "Object handle (hex)" },
                    ["block_data_base64"] = new JsonObject { ["type"] = "string", ["description"] = "Object block data as base64" }
                },
                ["required"] = new JsonArray { "handle", "block_data_base64" }
            }),

        new Tool("remove_game_object", "Remove an object from the running game (Live editing)",
            new JsonObject
            {
                ["type"] = "object",
                ["properties"] = new JsonObject
                {
                    ["handle"] = new JsonObject { ["type"] = "string", ["description"] = "Object handle (hex)" }
                },
                ["required"] = new JsonArray { "handle" }
            }),

        new Tool("enable_live_editing", "Enable/disable live editing mode (sync editor changes to running game)",
            new JsonObject
            {
                ["type"] = "object",
                ["properties"] = new JsonObject
                {
                    ["enabled"] = new JsonObject { ["type"] = "boolean", ["description"] = "Whether to enable live editing" }
                },
                ["required"] = new JsonArray { "enabled" }
            }),

        new Tool("save_and_reload_level", "Save the zone and reload the level in the running game",
            new JsonObject { ["type"] = "object", ["properties"] = new JsonObject() }),

        // Asset/Chunk tools
        new Tool("list_chunks", "List all available chunks (models) in the game",
            new JsonObject { ["type"] = "object", ["properties"] = new JsonObject() }),

        new Tool("get_chunk_info", "Get information about a specific chunk (model, textures, asset list entry)",
            new JsonObject
            {
                ["type"] = "object",
                ["properties"] = new JsonObject
                {
                    ["chunk_name"] = new JsonObject { ["type"] = "string", ["description"] = "Chunk name without extension (e.g., s_edf_tech_flyer)" }
                },
                ["required"] = new JsonArray { "chunk_name" }
            }),

        new Tool("get_asset_list", "Get asset list entries for a chunk",
            new JsonObject
            {
                ["type"] = "object",
                ["properties"] = new JsonObject
                {
                    ["chunk_name"] = new JsonObject { ["type"] = "string", ["description"] = "Chunk name without extension" }
                },
                ["required"] = new JsonArray { "chunk_name" }
            }),

        // Game launching
        new Tool("start_game", "Start the game through RFAModLauncher",
            new JsonObject { ["type"] = "object", ["properties"] = new JsonObject() }),

        new Tool("dock_game", "Dock the running game window into the editor's Game panel",
            new JsonObject { ["type"] = "object", ["properties"] = new JsonObject() }),

        new Tool("undock_game", "Undock the game from the editor's Game panel",
            new JsonObject { ["type"] = "object", ["properties"] = new JsonObject() }),
    };

    public sealed record Tool(
        string Name,
        string Description,
        JsonObject InputSchema
    );
}