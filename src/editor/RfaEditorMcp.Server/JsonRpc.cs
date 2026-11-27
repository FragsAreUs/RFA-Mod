using System;
using System.Buffers;
using System.Collections.Generic;
using System.IO;
using System.IO.Pipelines;
using System.Text;
using System.Text.Json;
using System.Text.Json.Nodes;
using System.Text.Json.Serialization;
using System.Threading;
using System.Threading.Tasks;

namespace RfaEditorMcp;

/// <summary>
/// JSON-RPC 2.0 message types for MCP communication over stdio.
/// </summary>
public static class JsonRpc
{
    public const string Version = "2.0";

    public sealed record Request(
        [property: JsonPropertyName("jsonrpc")] string JsonRpc,
        [property: JsonPropertyName("id")] JsonNode? Id,
        [property: JsonPropertyName("method")] string Method,
        [property: JsonPropertyName("params")] JsonNode? Params
    );

    public sealed record Response(
        [property: JsonPropertyName("jsonrpc")] string JsonRpc = Version,
        [property: JsonPropertyName("id")] JsonNode? Id = null,
        [property: JsonPropertyName("result")] JsonNode? Result = null,
        [property: JsonPropertyName("error")] Error? Error = null
    );

    public sealed record Error(
        [property: JsonPropertyName("code")] int Code,
        [property: JsonPropertyName("message")] string Message,
        [property: JsonPropertyName("data")] JsonNode? Data = null
    );

    public sealed record Notification(
        [property: JsonPropertyName("jsonrpc")] string JsonRpc = Version,
        [property: JsonPropertyName("method")] string Method = "",
        [property: JsonPropertyName("params")] JsonNode? Params = null
    );

    // Standard JSON-RPC error codes
    public const int ParseError = -32700;
    public const int InvalidRequest = -32600;
    public const int MethodNotFound = -32601;
    public const int InvalidParams = -32602;
    public const int InternalError = -32603;

    private static readonly JsonSerializerOptions Options = new()
    {
        PropertyNameCaseInsensitive = true,
        WriteIndented = false,
        DefaultIgnoreCondition = JsonIgnoreCondition.WhenWritingNull
    };

    public static string Serialize(object obj) => JsonSerializer.Serialize(obj, Options);

    public static T? Deserialize<T>(string json) => JsonSerializer.Deserialize<T>(json, Options);

    public static Response Success(JsonNode? id, JsonNode? result) => new() { Id = id, Result = result };

    public static Response Failure(JsonNode? id, int code, string message, JsonNode? data = null)
        => new() { Id = id, Error = new Error(code, message, data) };

    public static Notification Notify(string method, JsonNode? params_) => new() { Method = method, Params = params_ };
}

/// <summary>
/// Reads JSON-RPC messages from stdin, supporting both:
/// 1. Newline-delimited JSON (MCP standard) - each message on its own line
/// 2. Length-prefixed JSON (4-byte little-endian length + JSON bytes)
/// Auto-detects format from the first message.
/// </summary>
public sealed class JsonRpcReader : IDisposable
{
    private Stream _input;
    private readonly byte[] _lengthBuffer = new byte[4];
    private bool _detectedLengthPrefixed = false;
    private bool _detectionDone = false;

    /// <summary>
    /// Gets the detected format (true = length-prefixed, false = newline-delimited).
    /// Valid after the first ReadRequestAsync call.
    /// </summary>
    public bool IsLengthPrefixed => _detectedLengthPrefixed;

    public JsonRpcReader(Stream input) => _input = input;

    public async Task<JsonRpc.Request?> ReadRequestAsync(CancellationToken ct = default)
    {
        while (!ct.IsCancellationRequested)
        {
            // Peek at first byte to detect format if not done yet
            if (!_detectionDone)
            {
                int firstByte = _input.ReadByte();
                // Skip a UTF-8 BOM and blank lines before the first message.
                while (firstByte is 0xEF or 0xBB or 0xBF or 0x20 or 0x09 or 0x0D or 0x0A)
                    firstByte = _input.ReadByte();
                if (firstByte == -1) return null; // EOF

                // Put the byte back by using a buffer
                // If first byte is '{' (0x7B), it's newline-delimited JSON
                // Otherwise, assume length-prefixed (first 4 bytes = length)
                if (firstByte == 0x7B) // '{'
                {
                    _detectedLengthPrefixed = false;
                    // We need to re-read this byte, so create a wrapper stream
                    _input = new PeekableStream(_input, (byte)firstByte);
                }
                else
                {
                    _detectedLengthPrefixed = true;
                    // First byte is part of length prefix, put it back
                    _input = new PeekableStream(_input, (byte)firstByte);
                }
                _detectionDone = true;
            }

            string? json;
            if (_detectedLengthPrefixed)
            {
                json = await ReadLengthPrefixedAsync(ct);
            }
            else
            {
                json = await ReadNewlineDelimitedAsync(ct);
            }

            if (json == null) return null; // EOF
            if (string.IsNullOrWhiteSpace(json)) continue; // Skip empty

            try
            {
                return JsonRpc.Deserialize<JsonRpc.Request>(json);
            }
            catch (JsonException ex)
            {
                Console.Error.WriteLine($"[JsonRpcReader] Parse error: {ex.Message} - JSON: {json[..Math.Min(200, json.Length)]}");
                // If parsing fails, maybe we detected wrong format - try the other
                // Skip the bad line and keep reading.
            }
        }
        return null;
    }

    // One reader for the whole session: a new StreamReader per line would read ahead past the line and throw
    // away the next messages when the client sends several at once (MCP clients do: "initialized" with "tools/list").
    private StreamReader? _lineReader;

    private async Task<string?> ReadNewlineDelimitedAsync(CancellationToken ct)
    {
        _lineReader ??= new StreamReader(_input, new UTF8Encoding(false), detectEncodingFromByteOrderMarks: false, bufferSize: 65536, leaveOpen: true);
        return await _lineReader.ReadLineAsync(ct);
    }

    private async Task<string?> ReadLengthPrefixedAsync(CancellationToken ct)
    {
        // Read 4-byte length prefix (little-endian)
        int read = 0;
        while (read < 4 && !ct.IsCancellationRequested)
        {
            int r = await _input.ReadAsync(_lengthBuffer.AsMemory(read, 4 - read), ct);
            if (r == 0) return null; // EOF
            read += r;
        }
        if (read < 4) return null;

        int length = BitConverter.ToInt32(_lengthBuffer, 0);
        if (length <= 0 || length > 10_000_000) // Sanity check: max 10MB
        {
            Console.Error.WriteLine($"[JsonRpcReader] Invalid message length: {length}");
            return null;
        }

        byte[] buffer = new byte[length];
        read = 0;
        while (read < length && !ct.IsCancellationRequested)
        {
            int r = await _input.ReadAsync(buffer.AsMemory(read, length - read), ct);
            if (r == 0) return null; // EOF
            read += r;
        }
        if (read < length) return null;

        return Encoding.UTF8.GetString(buffer);
    }

    public void Dispose() => _input.Dispose();
}

/// <summary>
/// Helper stream that prepends a single byte to the underlying stream.
/// </summary>
sealed class PeekableStream : Stream
{
    private readonly Stream _inner;
    private readonly byte _firstByte;
    private bool _firstByteRead = false;

    public PeekableStream(Stream inner, byte firstByte)
    {
        _inner = inner;
        _firstByte = firstByte;
    }

    public override bool CanRead => _inner.CanRead;
    public override bool CanSeek => false;
    public override bool CanWrite => _inner.CanWrite;
    public override long Length => throw new NotSupportedException();
    public override long Position { get => throw new NotSupportedException(); set => throw new NotSupportedException(); }

    public override int ReadByte()
    {
        if (!_firstByteRead)
        {
            _firstByteRead = true;
            return _firstByte;
        }
        return _inner.ReadByte();
    }

    public override int Read(byte[] buffer, int offset, int count)
    {
        if (!_firstByteRead)
        {
            _firstByteRead = true;
            buffer[offset] = _firstByte;
            return 1 + _inner.Read(buffer, offset + 1, count - 1);
        }
        return _inner.Read(buffer, offset, count);
    }

    public override async ValueTask<int> ReadAsync(Memory<byte> buffer, CancellationToken ct = default)
    {
        if (!_firstByteRead)
        {
            _firstByteRead = true;
            buffer.Span[0] = _firstByte;
            int rest = await _inner.ReadAsync(buffer[1..], ct);
            return 1 + rest;
        }
        return await _inner.ReadAsync(buffer, ct);
    }

    public override void Write(byte[] buffer, int offset, int count) => _inner.Write(buffer, offset, count);
    public override ValueTask WriteAsync(ReadOnlyMemory<byte> buffer, CancellationToken ct = default) => _inner.WriteAsync(buffer, ct);
    public override void Flush() => _inner.Flush();
    public override Task FlushAsync(CancellationToken ct) => _inner.FlushAsync(ct);

    // Required abstract members for Stream base class
    public override long Seek(long offset, SeekOrigin origin) => throw new NotSupportedException();
    public override void SetLength(long value) => throw new NotSupportedException();

    protected override void Dispose(bool disposing)
    {
        if (disposing) _inner.Dispose();
        base.Dispose(disposing);
    }
}

/// <summary>
/// Writes JSON-RPC messages to stdout, supporting both:
/// 1. Newline-delimited JSON (MCP standard) - each message on its own line
/// 2. Length-prefixed JSON (4-byte little-endian length + JSON bytes)
/// Matches the format detected by JsonRpcReader.
/// </summary>
public sealed class JsonRpcWriter : IDisposable
{
    private readonly Stream _output;
    private readonly bool _lengthPrefixed;

    public JsonRpcWriter(Stream output, bool lengthPrefixed = false)
    {
        _output = output;
        _lengthPrefixed = lengthPrefixed;
    }

    public async Task WriteAsync(JsonRpc.Response response, CancellationToken ct = default)
    {
        string json = JsonRpc.Serialize(response);
        if (_lengthPrefixed)
        {
            byte[] jsonBytes = Encoding.UTF8.GetBytes(json);
            byte[] lengthBytes = BitConverter.GetBytes(jsonBytes.Length);
            await _output.WriteAsync(lengthBytes, 0, 4, ct);
            await _output.WriteAsync(jsonBytes, 0, jsonBytes.Length, ct);
        }
        else
        {
            byte[] jsonBytes = Encoding.UTF8.GetBytes(json + "\n");
            await _output.WriteAsync(jsonBytes, 0, jsonBytes.Length, ct);
        }
        await _output.FlushAsync(ct);
    }

    public async Task WriteAsync(JsonRpc.Notification notification, CancellationToken ct = default)
    {
        string json = JsonRpc.Serialize(notification);
        if (_lengthPrefixed)
        {
            byte[] jsonBytes = Encoding.UTF8.GetBytes(json);
            byte[] lengthBytes = BitConverter.GetBytes(jsonBytes.Length);
            await _output.WriteAsync(lengthBytes, 0, 4, ct);
            await _output.WriteAsync(jsonBytes, 0, jsonBytes.Length, ct);
        }
        else
        {
            byte[] jsonBytes = Encoding.UTF8.GetBytes(json + "\n");
            await _output.WriteAsync(jsonBytes, 0, jsonBytes.Length, ct);
        }
        await _output.FlushAsync(ct);
    }

    public void Dispose() => _output.Dispose();
}