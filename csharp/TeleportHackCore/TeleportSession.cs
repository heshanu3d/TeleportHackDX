// Managed wrapper around a th_session* handle. This is the surface every C#
// UI (WinForms, WPF, or future ports) should use -- it hides the P/Invoke
// details, propagates the C ABI's last_error strings as thrown exceptions,
// and translates raw byte buffers to strongly typed values.
using System;
using System.Collections.Generic;
using System.Text;
using static TeleportHackCore.NativeMethods;

namespace TeleportHackCore;

public sealed class TeleportHackException : Exception
{
    public TeleportHackException(string operation, string? nativeDetail)
        : base(BuildMessage(operation, nativeDetail))
    {
        Operation = operation;
        NativeDetail = nativeDetail;
    }

    public string Operation { get; }
    public string? NativeDetail { get; }

    private static string BuildMessage(string operation, string? nativeDetail)
        => string.IsNullOrEmpty(nativeDetail)
            ? $"{operation} failed"
            : $"{operation} failed: {nativeDetail}";
}

public readonly record struct FavouritePoint(int Index, string Description, string Category,
                                              double X, double Y, double Z, double? Orientation);

public readonly record struct ProcessRow(uint Pid, string ExeName);

/// <summary>
/// One TeleportHackCApi session. Not thread-safe -- pin all calls to a
/// single UI thread (or serialize via a lock). Dispose to release the
/// underlying native handle; the finalizer catches accidental leaks in
/// crashy shutdowns but explicit disposal is strongly preferred.
/// </summary>
public sealed class TeleportSession : IDisposable
{
    private IntPtr _handle;

    public TeleportSession()
    {
        _handle = th_session_create();
        if (_handle == IntPtr.Zero)
            throw new TeleportHackException("th_session_create",
                "native allocation returned NULL");
    }

    public bool IsDisposed => _handle == IntPtr.Zero;

    public void Dispose()
    {
        if (_handle == IntPtr.Zero) return;
        th_session_destroy(_handle);
        _handle = IntPtr.Zero;
        GC.SuppressFinalize(this);
    }

    ~TeleportSession() { Dispose(); }

    private void RequireHandle()
    {
        if (_handle == IntPtr.Zero)
            throw new ObjectDisposedException(nameof(TeleportSession));
    }

    private string LastError()
        => ReadUtf8String(_handle, th_session_last_error);

    private void CheckResult(int result, string operation)
    {
        if (result != 0) throw new TeleportHackException(operation, LastError());
    }

    // ---- versions --------------------------------------------------------

    public static IReadOnlyList<string> Versions()
    {
        int count = th_version_count();
        var list = new List<string>(count);
        for (int i = 0; i < count; ++i)
        {
            int needed = th_version_name(i, null, 0);
            if (needed <= 0) continue;
            var buffer = new byte[needed + 1];
            th_version_name(i, buffer, buffer.Length);
            list.Add(Encoding.UTF8.GetString(buffer, 0, needed));
        }
        return list;
    }

    // ---- processes -------------------------------------------------------

    public static IReadOnlyList<ProcessRow> ListProcesses(string executable)
    {
        var exeBytes = ToUtf8(executable);
        // Two-call: probe count with a null buffer first, then materialise.
        int count = th_process_list(exeBytes, null, 0);
        if (count <= 0) return Array.Empty<ProcessRow>();
        var native = new NativeMethods.ProcessEntryNative[count];
        int wrote = th_process_list(exeBytes, native, count);
        var rows = new List<ProcessRow>(wrote);
        for (int i = 0; i < wrote; ++i)
            rows.Add(new ProcessRow(native[i].Pid, DecodeExeName(native[i])));
        return rows;
    }

    // ---- attach ----------------------------------------------------------

    public void Attach(uint pid)
    {
        RequireHandle();
        CheckResult(th_session_attach(_handle, pid), "attach");
    }

    public void Detach()
    {
        RequireHandle();
        CheckResult(th_session_detach(_handle), "detach");
    }

    public bool IsAttached
    {
        get
        {
            RequireHandle();
            return th_session_is_attached(_handle) == 1;
        }
    }

    public uint AttachedPid
    {
        get
        {
            RequireHandle();
            return th_session_pid(_handle);
        }
    }

    // ---- version selection ----------------------------------------------

    public string Version
    {
        get
        {
            RequireHandle();
            return ReadUtf8String(_handle, th_session_get_version);
        }
        set
        {
            RequireHandle();
            CheckResult(th_session_set_version(_handle, ToUtf8(value)),
                "set version");
        }
    }

    // ---- position / teleport --------------------------------------------

    public (double X, double Y, double Z, double? Orientation) ReadPosition()
    {
        RequireHandle();
        int rc = th_session_read_position(_handle, out double x, out double y, out double z,
                                            out double r, out int hasR);
        if (rc != 0) throw new TeleportHackException("read position", LastError());
        double? orientation = hasR != 0 ? r : null;
        return (x, y, z, orientation);
    }

    public void Teleport(double x, double y, double z, double? orientation = null)
    {
        RequireHandle();
        int hasR = orientation.HasValue ? 1 : 0;
        double r = orientation ?? 0.0;
        CheckResult(th_session_teleport(_handle, x, y, z, hasR, r), "teleport");
    }

    // ---- speed -----------------------------------------------------------

    public void SetGlobalSpeed(double value)
    {
        RequireHandle();
        CheckResult(th_session_set_global_speed(_handle, value), "set global speed");
    }

    public void SetMovementSpeed(double value)
    {
        RequireHandle();
        CheckResult(th_session_set_movement_speed(_handle, value), "set movement speed");
    }

    public void ResetMovementSpeed()
    {
        RequireHandle();
        CheckResult(th_session_reset_movement_speed(_handle), "reset movement speed");
    }

    // ---- feature toggles -------------------------------------------------

    public bool ToggleAntiJump()
    {
        RequireHandle();
        CheckResult(th_session_toggle_anti_jump(_handle, out int now), "toggle anti-jump");
        return now == 1;
    }

    public bool ToggleAutoloot()
    {
        RequireHandle();
        CheckResult(th_session_toggle_autoloot(_handle, out int now), "toggle autoloot");
        return now == 1;
    }

    public bool TogglePatchLoot()
    {
        RequireHandle();
        CheckResult(th_session_toggle_patch_loot(_handle, out int now), "toggle patch-loot");
        return now == 1;
    }

    public bool ToggleLuaUnlock()
    {
        RequireHandle();
        CheckResult(th_session_toggle_lua_unlock(_handle, out int now), "toggle lua-unlock");
        return now == 1;
    }

    // ---- favourites ------------------------------------------------------

    public void LoadFavourites(string path)
    {
        RequireHandle();
        CheckResult(th_favourites_load(_handle, ToUtf8(path)), "load favourites");
    }

    public int FavouritesCount
    {
        get
        {
            RequireHandle();
            return th_favourites_count(_handle);
        }
    }

    public FavouritePoint GetFavourite(int index)
    {
        RequireHandle();
        var description = ReadUtf8String(_handle, index, th_favourites_get_description);
        var category = ReadUtf8String(_handle, index, th_favourites_get_category);
        int rc = th_favourites_get_position(_handle, index, out double x, out double y, out double z,
                                             out double r, out int hasR);
        if (rc != 0)
            throw new TeleportHackException($"favourite [{index}] position", LastError());
        double? orientation = hasR != 0 ? r : null;
        return new FavouritePoint(index, description, category, x, y, z, orientation);
    }

    /// <summary>
    /// Enumerates every currently loaded favourite. Materialised (not
    /// lazy) so the caller can bind it directly to a DataGrid / ListView.
    /// </summary>
    public IReadOnlyList<FavouritePoint> AllFavourites()
    {
        int count = FavouritesCount;
        if (count <= 0) return Array.Empty<FavouritePoint>();
        var list = new List<FavouritePoint>(count);
        for (int i = 0; i < count; ++i) list.Add(GetFavourite(i));
        return list;
    }
}
