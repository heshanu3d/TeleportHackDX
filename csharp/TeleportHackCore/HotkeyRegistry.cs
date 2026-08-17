using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;

namespace TeleportHackCore;

/// <summary>
/// Win32 RegisterHotKey registry matching the native hotkey.txt parser and
/// modifier syntax (^ Ctrl, ! Alt, + Shift). UI-specific WndProc hooks feed
/// WM_HOTKEY ids back through TryResolve().
/// </summary>
public sealed class HotkeyRegistry : IDisposable
{
    public const int WmHotkey = 0x0312;
    private const uint ModAlt = 0x0001;
    private const uint ModControl = 0x0002;
    private const uint ModShift = 0x0004;
    private const uint ModNoRepeat = 0x4000;

    [DllImport("user32.dll", SetLastError = true)]
    private static extern bool RegisterHotKey(IntPtr hWnd, int id, uint modifiers, uint key);

    [DllImport("user32.dll", SetLastError = true)]
    private static extern bool UnregisterHotKey(IntPtr hWnd, int id);

    private readonly Dictionary<int, FavouritePoint> _entries = new();
    private IntPtr _window;
    private int _nextId = 0x5400;

    public int Apply(IntPtr window, string path, IReadOnlyList<FavouritePoint> favourites,
                     Action<string> warning)
    {
        Clear();
        _window = window;
        if (window == IntPtr.Zero || !File.Exists(path)) return 0;

        string[] cleaned = File.ReadLines(path)
            .Select(line => line.Trim())
            .Where(line => line.Length > 0 && line[0] != '#')
            .ToArray();
        int bound = 0;
        for (int i = 0; i + 1 < cleaned.Length; i += 2)
        {
            string combo = cleaned[i];
            string pointName = cleaned[i + 1];
            FavouritePoint? point = null;
            foreach (FavouritePoint candidate in favourites)
            {
                if (candidate.Description != pointName) continue;
                point = candidate;
                break;
            }
            if (point is null)
            {
                warning($"hotkey {combo}: point '{pointName}' not found");
                continue;
            }
            if (!TryParseCombo(combo, out uint modifiers, out uint key))
            {
                warning($"hotkey {combo}: unrecognised key");
                continue;
            }
            int id = _nextId++;
            if (!RegisterHotKey(window, id, modifiers, key))
            {
                warning($"hotkey {combo}: RegisterHotKey failed ({Marshal.GetLastWin32Error()})");
                continue;
            }
            _entries[id] = point.Value;
            ++bound;
        }
        return bound;
    }

    public bool TryResolve(int id, out FavouritePoint point)
        => _entries.TryGetValue(id, out point);

    public void Clear()
    {
        if (_window != IntPtr.Zero)
            foreach (int id in _entries.Keys) UnregisterHotKey(_window, id);
        _entries.Clear();
        _window = IntPtr.Zero;
        _nextId = 0x5400;
    }

    public void Dispose()
    {
        Clear();
        GC.SuppressFinalize(this);
    }

    private static bool TryParseCombo(string combo, out uint modifiers, out uint key)
    {
        modifiers = ModNoRepeat;
        int i = 0;
        while (i < combo.Length)
        {
            if (combo[i] == '+') modifiers |= ModShift;
            else if (combo[i] == '^') modifiers |= ModControl;
            else if (combo[i] == '!') modifiers |= ModAlt;
            else break;
            ++i;
        }
        string token = combo[i..];
        if (token.Length != 1)
        {
            key = 0;
            return false;
        }
        char c = char.ToUpperInvariant(token[0]);
        if (!char.IsAsciiLetterOrDigit(c))
        {
            key = 0;
            return false;
        }
        key = c;
        return true;
    }
}
