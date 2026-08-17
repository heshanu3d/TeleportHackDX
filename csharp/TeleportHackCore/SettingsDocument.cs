using System;
using System.Globalization;
using System.IO;
using System.Text;
using System.Text.Encodings.Web;
using System.Text.Json;
using System.Text.Json.Nodes;

namespace TeleportHackCore;

/// <summary>
/// Lossless editor for the shared flat settings.json. Unknown properties
/// (especially native ImGui layout snapshots) remain untouched when a C# UI
/// updates only its own window state or shared path/value fields.
/// </summary>
public sealed class SettingsDocument
{
    private readonly JsonObject _root;

    private SettingsDocument(string path, JsonObject root)
    {
        Path = path;
        _root = root;
    }

    public string Path { get; }

    public static SettingsDocument LoadFromApplicationDirectory()
    {
        string path = System.IO.Path.Combine(AppContext.BaseDirectory, "settings.json");
        try
        {
            if (File.Exists(path) && JsonNode.Parse(File.ReadAllText(path, Encoding.UTF8))
                    is JsonObject root)
                return new SettingsDocument(path, root);
        }
        catch
        {
            // Match native SettingsRepository: malformed/missing settings
            // fall back to defaults instead of preventing the UI from opening.
        }
        return new SettingsDocument(path, new JsonObject());
    }

    public string GetString(string key, string fallback = "")
    {
        try { return _root[key]?.GetValue<string>() ?? fallback; }
        catch { return fallback; }
    }

    public int GetInt(string key, int fallback)
    {
        try { return _root[key]?.GetValue<int>() ?? fallback; }
        catch { return fallback; }
    }

    public double GetDouble(string key, double fallback)
    {
        try { return _root[key]?.GetValue<double>() ?? fallback; }
        catch { return fallback; }
    }

    public bool GetBool(string key, bool fallback)
    {
        try { return _root[key]?.GetValue<bool>() ?? fallback; }
        catch { return fallback; }
    }

    public void Set(string key, string value) => _root[key] = value;
    public void Set(string key, int value) => _root[key] = value;
    public void Set(string key, double value) => _root[key] = value;
    public void Set(string key, bool value) => _root[key] = value;

    public string ResolvePath(string configured, string fallbackFileName)
    {
        string value = string.IsNullOrWhiteSpace(configured) ? fallbackFileName : configured;
        if (TryResolvePath(value, out string resolved)) return resolved;
        return System.IO.Path.Combine(AppContext.BaseDirectory, fallbackFileName);
    }

    public bool TryResolvePath(string configured, out string resolved)
    {
        try
        {
            resolved = System.IO.Path.IsPathRooted(configured)
                ? System.IO.Path.GetFullPath(configured)
                : System.IO.Path.GetFullPath(
                    System.IO.Path.Combine(AppContext.BaseDirectory, configured));
            return true;
        }
        catch
        {
            resolved = string.Empty;
            return false;
        }
    }

    public void Save()
    {
        string? directory = System.IO.Path.GetDirectoryName(Path);
        if (!string.IsNullOrEmpty(directory)) Directory.CreateDirectory(directory);
        string temporary = Path + ".tmp";
        // Native SettingsRepository stores/reads UTF-8 directly and its
        // intentionally-small parser doesn't decode JSON \uXXXX escapes.
        // Keep non-ASCII text (Chinese categories/paths) as literal UTF-8.
        string json = _root.ToJsonString(new JsonSerializerOptions
        {
            WriteIndented = true,
            Encoder = JavaScriptEncoder.UnsafeRelaxedJsonEscaping,
        }) + Environment.NewLine;
        File.WriteAllText(temporary, json, new UTF8Encoding(false));
        File.Move(temporary, Path, true);
    }
}
