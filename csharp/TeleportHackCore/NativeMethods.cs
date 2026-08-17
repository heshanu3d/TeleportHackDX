// -----------------------------------------------------------------------------
// P/Invoke bindings for TeleportHackCApi.dll (see src/capi/api.h for the C
// contract). This file is source of truth for every native call; the
// higher-level Session wrapper below never calls DllImport directly.
//
// Rules of thumb:
//   * Strings crossing the boundary are UTF-8 byte arrays -- we never marshal
//     them as [MarshalAs(UnmanagedType.LPStr)] since that uses the current
//     ANSI code page, which corrupts Chinese descriptions on non-936
//     Windows.
//   * The C ABI's "int" is int32_t and its "uint" is uint32_t, matching
//     .NET's `int` / `uint`. Handles are IntPtr.
//   * Calling convention is cdecl (matches TH_CAPI_CALL in api.h).
// -----------------------------------------------------------------------------
using System;
using System.Runtime.InteropServices;
using System.Text;

namespace TeleportHackCore;

internal static class NativeMethods
{
    private const string DllName = "TeleportHackCApi";

    // ---- versions --------------------------------------------------------

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern int th_version_count();

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern int th_version_name(int index, byte[]? buffer, int bufferSize);

    // ---- process listing -------------------------------------------------

    [StructLayout(LayoutKind.Sequential)]
    public struct ProcessEntryNative
    {
        public uint Pid;

        // Matches th_process_entry::exe_name in api.h.
        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 64)]
        public byte[] ExeNameBytes;
    }

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern int th_process_list(byte[] executable, [Out] ProcessEntryNative[]? buffer,
                                             int maxEntries);

    // ---- session lifecycle ----------------------------------------------

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern IntPtr th_session_create();

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern void th_session_destroy(IntPtr session);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern int th_session_last_error(IntPtr session, byte[]? buffer, int bufferSize);

    // ---- attach ----------------------------------------------------------

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern int th_session_attach(IntPtr session, uint pid);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern int th_session_detach(IntPtr session);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern int th_session_is_attached(IntPtr session);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern uint th_session_pid(IntPtr session);

    // ---- version selection ----------------------------------------------

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern int th_session_set_version(IntPtr session, byte[] name);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern int th_session_get_version(IntPtr session, byte[]? buffer, int bufferSize);

    // ---- position / teleport --------------------------------------------

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern int th_session_read_position(IntPtr session, out double x, out double y,
                                                       out double z, out double r,
                                                       out int hasOrientation);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern int th_session_teleport(IntPtr session, double x, double y, double z,
                                                  int hasOrientation, double r);

    // ---- speed hacks -----------------------------------------------------

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern int th_session_set_global_speed(IntPtr session, double value);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern int th_session_set_movement_speed(IntPtr session, double value);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern int th_session_reset_movement_speed(IntPtr session);

    // ---- feature toggles -------------------------------------------------

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern int th_session_toggle_anti_jump(IntPtr session, out int nowOn);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern int th_session_toggle_autoloot(IntPtr session, out int nowOn);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern int th_session_toggle_patch_loot(IntPtr session, out int nowOn);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern int th_session_toggle_lua_unlock(IntPtr session, out int nowOn);

    // ---- favourites ------------------------------------------------------

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern int th_favourites_load(IntPtr session, byte[] path);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern int th_favourites_count(IntPtr session);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern int th_favourites_get_description(IntPtr session, int index,
                                                            byte[]? buffer, int bufferSize);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern int th_favourites_get_category(IntPtr session, int index, byte[]? buffer,
                                                         int bufferSize);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern int th_favourites_get_position(IntPtr session, int index, out double x,
                                                         out double y, out double z, out double r,
                                                         out int hasOrientation);

    // ---- favourites: CRUD (added post-MVP; only-append to preserve ABI) --

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern int th_favourites_category_count(IntPtr session);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern int th_favourites_category_name(IntPtr session, int index,
                                                          byte[]? buffer, int bufferSize);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern int th_favourites_insert_in_category(IntPtr session, byte[] category,
                                                                int localIndex, byte[] description,
                                                                double x, double y, double z,
                                                                int hasR, double r);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern int th_favourites_append_in_category(IntPtr session, byte[] category,
                                                                int localIndex, byte[] description,
                                                                double x, double y, double z,
                                                                int hasR, double r);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern int th_favourites_replace_in_category(IntPtr session, byte[] category,
                                                                 int localIndex, byte[] description,
                                                                 double x, double y, double z,
                                                                 int hasR, double r);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern int th_favourites_delete_in_category(IntPtr session, byte[] category,
                                                                int localIndex);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern int th_favourites_save(IntPtr session, byte[] path);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern int th_favourites_export(IntPtr session, byte[] path);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern int th_favourites_import_replace(IntPtr session, byte[] path);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern int th_favourites_import_merge(IntPtr session, byte[] path,
                                                         out int added,
                                                         out int skippedDuplicates);

    // ---- Teleport.lua high-level operations ----------------------------

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern int th_teleport_lua_update_current(IntPtr session, byte[] luaPath,
                                                             byte[] description,
                                                             int descriptionOccurrence);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern int th_teleport_lua_insert_current_after(IntPtr session, byte[] luaPath,
                                                                  byte[] category,
                                                                  int localIndex,
                                                                  byte[] newDescription);

    // ---- helpers ---------------------------------------------------------

    /// <summary>
    /// Encodes a managed string as a NUL-terminated UTF-8 byte array, ready
    /// to hand to the C ABI. Nulls become empty strings.
    /// </summary>
    public static byte[] ToUtf8(string? value)
    {
        var text = value ?? string.Empty;
        var bytes = new byte[Encoding.UTF8.GetByteCount(text) + 1];
        Encoding.UTF8.GetBytes(text, 0, text.Length, bytes, 0);
        // Last byte is the guaranteed NUL terminator (byte[] is zero-init).
        return bytes;
    }

    /// <summary>
    /// Two-call read for C-string out functions: first probes for the
    /// required byte count, then fetches the payload. `getter(session, null,
    /// 0)` must return the number of bytes needed (excluding NUL) or -1 on
    /// error, matching the api.h contract for every buffer-out function.
    /// </summary>
    public static string ReadUtf8String(IntPtr session,
                                         Func<IntPtr, byte[]?, int, int> getter)
    {
        int needed = getter(session, null, 0);
        if (needed <= 0) return string.Empty;
        var buffer = new byte[needed + 1];
        getter(session, buffer, buffer.Length);
        return Encoding.UTF8.GetString(buffer, 0, needed);
    }

    public static string ReadUtf8String(IntPtr session, int index,
                                         Func<IntPtr, int, byte[]?, int, int> getter)
    {
        int needed = getter(session, index, null, 0);
        if (needed <= 0) return string.Empty;
        var buffer = new byte[needed + 1];
        getter(session, index, buffer, buffer.Length);
        return Encoding.UTF8.GetString(buffer, 0, needed);
    }

    public static string DecodeExeName(ProcessEntryNative entry)
    {
        // Native side always NUL-terminates within the 64-byte field.
        int length = Array.IndexOf<byte>(entry.ExeNameBytes, 0);
        if (length < 0) length = entry.ExeNameBytes.Length;
        return Encoding.UTF8.GetString(entry.ExeNameBytes, 0, length);
    }
}
