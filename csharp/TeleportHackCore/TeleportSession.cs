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

/// <summary>
/// One favlist.fav row. <see cref="Orientation"/> and <see cref="MapId"/> are
/// optional: missing means "keep the current facing" / "don't verify the
/// map", which is different from an explicit 0.
/// </summary>
public readonly record struct FavouritePoint(int Index, string Description, string Category,
                                              double X, double Y, double Z, double? Orientation,
                                              uint? MapId = null);

/// <summary>One "最近使用的坐标" entry (newest first).</summary>
public readonly record struct RecentEntry(int Index, string Label, double X, double Y, double Z,
                                          double? Orientation, uint? MapId);

/// <summary>One "分类导航" group with its categories in favlist order.</summary>
public sealed record CategoryGroup(string Name, IReadOnlyList<string> Categories);

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

    /// <summary>
    /// Current position. Orientation / MapId are null when the client can't
    /// report them (e.g. 1.12.x before the movement struct is loaded).
    /// </summary>
    public (double X, double Y, double Z, double? Orientation, uint? MapId) ReadPosition()
    {
        RequireHandle();
        int rc = th_session_read_position_ex(_handle, out double x, out double y, out double z,
                                             out double r, out int hasR, out uint mapId,
                                             out int hasMap);
        if (rc != 0) throw new TeleportHackException("read position", LastError());
        double? orientation = hasR != 0 ? r : null;
        uint? map = hasMap != 0 ? mapId : null;
        return (x, y, z, orientation, map);
    }

    /// <summary>Current map ID, or null while it can't be read.</summary>
    public uint? TryReadMapId()
    {
        RequireHandle();
        return th_session_read_map_id(_handle, out uint mapId) == 0 ? mapId : (uint?)null;
    }

    public void Teleport(double x, double y, double z, double? orientation = null)
    {
        RequireHandle();
        int hasR = orientation.HasValue ? 1 : 0;
        double r = orientation ?? 0.0;
        CheckResult(th_session_teleport(_handle, x, y, z, hasR, r), "teleport");
    }

    /// <summary>
    /// User-level teleport: refuses a destination on another map (the
    /// exception message explains why), writes facing when present and
    /// records the destination in <see cref="RecentTeleports"/>.
    /// </summary>
    public void TeleportTo(string label, double x, double y, double z, double? orientation,
                           uint? mapId)
    {
        RequireHandle();
        CheckResult(th_session_teleport_ex(_handle, ToUtf8(label), x, y, z,
                                           orientation.HasValue ? 1 : 0, orientation ?? 0.0,
                                           mapId.HasValue ? 1 : 0, mapId ?? 0u),
                    "teleport");
    }

    public void TeleportTo(FavouritePoint point)
        => TeleportTo(point.Description, point.X, point.Y, point.Z, point.Orientation,
                      point.MapId);

    // ---- recent teleports -----------------------------------------------

    public IReadOnlyList<RecentEntry> RecentTeleports()
    {
        RequireHandle();
        int count = th_recent_count(_handle);
        if (count <= 0) return Array.Empty<RecentEntry>();
        var list = new List<RecentEntry>(count);
        for (int i = 0; i < count; ++i)
        {
            string label = ReadUtf8String(_handle, i, th_recent_get_label);
            if (th_recent_get_position(_handle, i, out double x, out double y, out double z,
                                       out double r, out int hasR, out uint mapId,
                                       out int hasMap) != 0)
                continue;
            list.Add(new RecentEntry(i, label, x, y, z, hasR != 0 ? r : (double?)null,
                                     hasMap != 0 ? mapId : (uint?)null));
        }
        return list;
    }

    /// <summary>Teleports to recent entry <paramref name="index"/> again.</summary>
    public void TeleportRecent(int index)
    {
        RequireHandle();
        CheckResult(th_recent_teleport(_handle, index), "recent teleport");
    }

    // ---- category navigation + about (no session needed) ----------------

    /// <summary>
    /// Buckets <paramref name="categories"/> into the five "分类导航" groups
    /// (杂项, 东部王国, 卡利姆多, 燃烧的远征, 巫妖王之怒). The first group
    /// starts with <see cref="AllCategory"/>; empty groups are kept.
    /// </summary>
    public static IReadOnlyList<CategoryGroup> GroupCategories(IEnumerable<string> categories)
    {
        int count = th_category_group_count();
        var buckets = new List<List<string>>(count);
        for (int i = 0; i < count; ++i) buckets.Add(new List<string>());
        if (count > 0) buckets[0].Add(AllCategory);
        foreach (string category in categories)
        {
            if (category == AllCategory) continue;
            int group = th_category_group_of(ToUtf8(category));
            if (group < 0 || group >= count) group = 0;
            buckets[group].Add(category);
        }
        var groups = new List<CategoryGroup>(count);
        for (int i = 0; i < count; ++i)
            groups.Add(new CategoryGroup(ReadIndexedStaticUtf8String(i, th_category_group_name),
                                         buckets[i]));
        return groups;
    }

    public static string BuildVersion() => ReadStaticUtf8String(th_build_version);
    public static string AppAuthor() => ReadStaticUtf8String(th_app_author);
    public static string ReleaseNotes() => ReadStaticUtf8String(th_release_notes);

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
        uint? map = th_favourites_get_map_id(_handle, index, out uint mapId, out int hasMap) == 0 &&
                    hasMap != 0
            ? mapId
            : null;
        return new FavouritePoint(index, description, category, x, y, z, orientation, map);
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

    // ---- favourites CRUD (post-MVP additions) ---------------------------

    /// <summary>
    /// Synthetic "all points" bucket recognised by every CRUD helper below
    /// -- pass this for `category` to bypass category-local index resolution.
    /// Matches th::ALL_CATEGORY / TH_CAPI_ALL_CATEGORY in the C header.
    /// </summary>
    public const string AllCategory = "所有";

    public IReadOnlyList<string> Categories()
    {
        RequireHandle();
        int count = th_favourites_category_count(_handle);
        if (count <= 0) return Array.Empty<string>();
        var list = new List<string>(count);
        for (int i = 0; i < count; ++i)
        {
            int needed = th_favourites_category_name(_handle, i, null, 0);
            if (needed <= 0) continue;
            var buffer = new byte[needed + 1];
            th_favourites_category_name(_handle, i, buffer, buffer.Length);
            list.Add(System.Text.Encoding.UTF8.GetString(buffer, 0, needed));
        }
        return list;
    }

    public void InsertInCategory(string category, int localIndex, string description,
                                   double x, double y, double z, double? orientation = null,
                                   uint? mapId = null)
    {
        RequireHandle();
        CheckResult(
            th_favourites_insert_in_category_ex(_handle, ToUtf8(category), localIndex,
                                                 ToUtf8(description), x, y, z,
                                                 orientation.HasValue ? 1 : 0, orientation ?? 0.0,
                                                 mapId.HasValue ? 1 : 0, mapId ?? 0u),
            "insert favourite");
    }

    public void AppendInCategory(string category, int localIndex, string description,
                                   double x, double y, double z, double? orientation = null,
                                   uint? mapId = null)
    {
        RequireHandle();
        CheckResult(
            th_favourites_append_in_category_ex(_handle, ToUtf8(category), localIndex,
                                                 ToUtf8(description), x, y, z,
                                                 orientation.HasValue ? 1 : 0, orientation ?? 0.0,
                                                 mapId.HasValue ? 1 : 0, mapId ?? 0u),
            "append favourite");
    }

    public void ReplaceInCategory(string category, int localIndex, string description,
                                    double x, double y, double z, double? orientation = null,
                                    uint? mapId = null)
    {
        RequireHandle();
        CheckResult(
            th_favourites_replace_in_category_ex(_handle, ToUtf8(category), localIndex,
                                                  ToUtf8(description), x, y, z,
                                                  orientation.HasValue ? 1 : 0, orientation ?? 0.0,
                                                  mapId.HasValue ? 1 : 0, mapId ?? 0u),
            "replace favourite");
    }

    public void DeleteInCategory(string category, int localIndex)
    {
        RequireHandle();
        CheckResult(th_favourites_delete_in_category(_handle, ToUtf8(category), localIndex),
            "delete favourite");
    }

    /// <summary>
    /// Persists the current in-memory list to `path` in favlist.fav's
    /// canonical form (5 fields, plus the map field for rows that carry one).
    /// Returns the number of records written.
    /// </summary>
    public int SaveFavourites(string path)
    {
        RequireHandle();
        int written = th_favourites_save(_handle, ToUtf8(path));
        if (written < 0)
            throw new TeleportHackException("save favourites", LastError());
        return written;
    }

    public int ExportFavourites(string path)
    {
        RequireHandle();
        int written = th_favourites_export(_handle, ToUtf8(path));
        if (written < 0)
            throw new TeleportHackException("export favourites", LastError());
        return written;
    }

    public int ImportReplace(string path)
    {
        RequireHandle();
        int loaded = th_favourites_import_replace(_handle, ToUtf8(path));
        if (loaded < 0)
            throw new TeleportHackException("import replace", LastError());
        return loaded;
    }

    public (int Added, int SkippedDuplicates) ImportMerge(string path)
    {
        RequireHandle();
        CheckResult(th_favourites_import_merge(_handle, ToUtf8(path), out int added,
                                                out int skipped), "import merge");
        return (added, skipped);
    }

    public void UpdateTeleportLuaFromCurrent(string luaPath, string description,
                                               int descriptionOccurrence)
    {
        RequireHandle();
        CheckResult(th_teleport_lua_update_current(_handle, ToUtf8(luaPath),
                                                    ToUtf8(description),
                                                    descriptionOccurrence),
                    "Teleport.lua update");
    }

    public void InsertCurrentIntoTableAndLua(string luaPath, string category, int localIndex,
                                              string newDescription)
    {
        RequireHandle();
        CheckResult(th_teleport_lua_insert_current_after(_handle, ToUtf8(luaPath),
                                                          ToUtf8(category), localIndex,
                                                          ToUtf8(newDescription)),
                    "Teleport.lua + table insert");
    }
}
