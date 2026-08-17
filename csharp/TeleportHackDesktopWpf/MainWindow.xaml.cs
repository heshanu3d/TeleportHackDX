// MVP+ WPF UI for TeleportHackDX. Deliberately code-behind (no MVVM
// framework) so the whole surface is reviewable inline. See
// ../../csharp/TeleportHackCore for the P/Invoke wrapper this drives.
using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Interop;
using System.Windows.Threading;
using Microsoft.Win32;
using TeleportHackCore;

namespace TeleportHackDesktopWpf;

public partial class MainWindow : Window
{
    private readonly TeleportSession _session = new();
    private readonly SettingsDocument _settings = SettingsDocument.LoadFromApplicationDirectory();
    private readonly HotkeyRegistry _hotkeys = new();
    private readonly DispatcherTimer _moveSpeedTimer =
        new DispatcherTimer { Interval = TimeSpan.FromSeconds(1) };
    private bool _moveSpeedEnabled;

    private IReadOnlyList<FavouritePoint> _allFavourites = Array.Empty<FavouritePoint>();
    private readonly ObservableCollection<FavouriteRow> _rows = new();
    private string? _favlistPath;
    private string? _teleportLuaPath;
    private string? _hotkeyPath;
    private HwndSource? _windowSource;

    public MainWindow()
    {
        InitializeComponent();
        FavouritesView.ItemsSource = _rows;

        _moveSpeedTimer.Tick += MoveSpeedTimer_Tick;
        Closing += (_, _) => Shutdown();
        SourceInitialized += MainWindow_SourceInitialized;

        try
        {
            foreach (var version in TeleportSession.Versions()) VersionCombo.Items.Add(version);
            string configuredVersion = _settings.GetString("default_version", _session.Version);
            VersionCombo.SelectedItem = VersionCombo.Items.Contains(configuredVersion)
                ? configuredVersion
                : _session.Version;
        }
        catch (Exception ex) { Log("[error] enumerating versions: " + ex.Message); }

        RefreshProcesses();
        ApplySettingsAndLoadData();
    }

    // -------------------------------------------------------------------------
    // Event handlers (XAML-facing)
    // -------------------------------------------------------------------------

    private void RefreshButton_Click(object sender, RoutedEventArgs e) => RefreshProcesses();

    private void AttachButton_Click(object sender, RoutedEventArgs e)
    {
        if (ProcessCombo.SelectedItem is not ProcessRowDisplay row)
        {
            Log("[warn] pick a WoW process first");
            return;
        }
        try
        {
            if (_moveSpeedEnabled) DisableMovementSpeed();
            _session.Attach(row.Row.Pid);
            AttachStatus.Text = $"attached (PID {row.Row.Pid})";
            AttachStatus.Foreground = System.Windows.Media.Brushes.DarkGreen;
            Log($"attached to PID {row.Row.Pid}");
        }
        catch (Exception ex)
        {
            AttachStatus.Text = "not attached";
            AttachStatus.Foreground = System.Windows.Media.Brushes.DarkRed;
            Log("[error] attach: " + ex.Message);
        }
    }

    private void VersionCombo_SelectionChanged(object sender, SelectionChangedEventArgs e)
    {
        if (VersionCombo.SelectedItem is not string picked) return;
        try
        {
            if (_moveSpeedEnabled) DisableMovementSpeed();
            _session.Version = picked;
            Log($"client profile: {picked}");
        }
        catch (Exception ex) { Log("[error] set version: " + ex.Message); }
    }

    private void AlwaysOnTopCheck_CheckedChanged(object sender, RoutedEventArgs e)
    {
        Topmost = AlwaysOnTopCheck.IsChecked == true;
    }

    private void LoadFavouritesButton_Click(object sender, RoutedEventArgs e)
    {
        var dialog = new OpenFileDialog
        {
            Filter = "Favlist (*.fav)|*.fav|All files (*.*)|*.*",
            InitialDirectory = AppContext.BaseDirectory,
        };
        if (dialog.ShowDialog(this) != true) return;
        LoadFavouritesFromPath(dialog.FileName);
    }

    private void SearchBox_TextChanged(object sender, TextChangedEventArgs e) => RefreshFavouritesView();

    private void CategoryCombo_SelectionChanged(object sender, SelectionChangedEventArgs e)
        => RefreshFavouritesView();

    private void FavouritesView_SelectionChanged(object sender, SelectionChangedEventArgs e)
    {
        if (FavouritesView.SelectedItem is FavouriteRow row)
            DescriptionBox.Text = row.Point.Description;
    }

    private void FavouritesView_MouseDoubleClick(object sender, System.Windows.Input.MouseButtonEventArgs e)
        => TeleportSelected();

    private void TeleportButton_Click(object sender, RoutedEventArgs e) => TeleportSelected();

    private void InsertBeforeButton_Click(object sender, RoutedEventArgs e) => InsertBeforeSelected();
    private void InsertAfterButton_Click(object sender, RoutedEventArgs e) => InsertAfterSelected();
    private void EditButton_Click(object sender, RoutedEventArgs e) => EditSelected();
    private void DeleteButton_Click(object sender, RoutedEventArgs e) => DeleteSelected();
    private void AppendButton_Click(object sender, RoutedEventArgs e) => AppendToCategory();
    private void SaveButton_Click(object sender, RoutedEventArgs e) => SaveFavourites();
    private void ReloadButton_Click(object sender, RoutedEventArgs e) => ReloadFavourites();
    private void ReadToLuaButton_Click(object sender, RoutedEventArgs e) => UpdateTeleportLua();
    private void InsertTableLuaButton_Click(object sender, RoutedEventArgs e) => InsertTableAndLua();
    private void ImportMergeButton_Click(object sender, RoutedEventArgs e) => ImportFavourites(true);
    private void ImportReplaceButton_Click(object sender, RoutedEventArgs e) => ImportFavourites(false);
    private void ExportButton_Click(object sender, RoutedEventArgs e) => ExportFavourites();

    private void AntiJumpButton_Click(object sender, RoutedEventArgs e)
        => LogToggle("AntiJump", _session.ToggleAntiJump);
    private void AutoLootButton_Click(object sender, RoutedEventArgs e)
        => LogToggle("AutoLoot", _session.ToggleAutoloot);
    private void PatchLootButton_Click(object sender, RoutedEventArgs e)
        => LogToggle("PatchLoot", _session.TogglePatchLoot);
    private void LuaUnlockButton_Click(object sender, RoutedEventArgs e)
        => LogToggle("LuaUnlock", _session.ToggleLuaUnlock);

    private void GlobalSpeedApply_Click(object sender, RoutedEventArgs e)
    {
        if (!TryParseSpeed(GlobalSpeedInput.Text, out double value))
        {
            Log("[error] global speed must be a number");
            return;
        }
        try
        {
            _session.SetGlobalSpeed(value);
            Log($"global speed = {value.ToString(CultureInfo.InvariantCulture)}");
        }
        catch (Exception ex) { Log("[error] global speed: " + ex.Message); }
    }

    private void MoveSpeedToggle_Click(object sender, RoutedEventArgs e)
    {
        if (_moveSpeedEnabled) DisableMovementSpeed();
        else EnableMovementSpeed();
    }

    private void MainWindow_SourceInitialized(object? sender, EventArgs e)
    {
        _windowSource = HwndSource.FromHwnd(new WindowInteropHelper(this).Handle);
        _windowSource?.AddHook(WindowMessageHook);
        RegisterHotkeys();
    }

    private IntPtr WindowMessageHook(IntPtr hwnd, int msg, IntPtr wParam, IntPtr lParam,
                                      ref bool handled)
    {
        if (msg == HotkeyRegistry.WmHotkey &&
            _hotkeys.TryResolve(wParam.ToInt32(), out FavouritePoint point))
        {
            try
            {
                _session.Teleport(point.X, point.Y, point.Z, point.Orientation);
                Log($"hotkey teleported to '{point.Description}'");
            }
            catch (Exception ex) { Log("[error] hotkey teleport: " + ex.Message); }
            handled = true;
        }
        return IntPtr.Zero;
    }

    // -------------------------------------------------------------------------
    // Core actions
    // -------------------------------------------------------------------------

    private void RefreshProcesses()
    {
        ProcessCombo.Items.Clear();
        try
        {
            foreach (var row in TeleportSession.ListProcesses("WoW.exe"))
                ProcessCombo.Items.Add(new ProcessRowDisplay(row));
            if (ProcessCombo.Items.Count > 0) ProcessCombo.SelectedIndex = 0;
        }
        catch (Exception ex) { Log("[error] list processes: " + ex.Message); }
    }

    private void ApplySettingsAndLoadData()
    {
        AlwaysOnTopCheck.IsChecked = _settings.GetBool("wpf_always_on_top", false);
        GlobalSpeedInput.Text = _settings.GetDouble("speed_value", 7.0)
            .ToString(CultureInfo.InvariantCulture);
        MoveSpeedInput.Text = _settings.GetDouble("movement_speed_value", 7.0)
            .ToString(CultureInfo.InvariantCulture);

        if (_settings.GetBool("wpf_window_saved", false))
        {
            double width = _settings.GetDouble("wpf_window_width", Width);
            double height = _settings.GetDouble("wpf_window_height", Height);
            double left = _settings.GetDouble("wpf_window_x", Left);
            double top = _settings.GetDouble("wpf_window_y", Top);
            if (double.IsFinite(width) && double.IsFinite(height) &&
                double.IsFinite(left) && double.IsFinite(top) &&
                width >= MinWidth && height >= MinHeight)
            {
                var candidate = new Rect(left, top, width, height);
                var virtualDesktop = new Rect(SystemParameters.VirtualScreenLeft,
                                              SystemParameters.VirtualScreenTop,
                                              SystemParameters.VirtualScreenWidth,
                                              SystemParameters.VirtualScreenHeight);
                if (candidate.IntersectsWith(virtualDesktop))
                {
                    WindowStartupLocation = WindowStartupLocation.Manual;
                    Left = left;
                    Top = top;
                    Width = width;
                    Height = height;
                }
            }
        }

        string hotkeyConfigured = _settings.GetString("hotkey_path", "hotkey.txt");
        _hotkeyPath = _settings.ResolvePath(hotkeyConfigured, "hotkey.txt");
        string luaConfigured = _settings.GetString("teleport_lua_path");
        _teleportLuaPath = !string.IsNullOrWhiteSpace(luaConfigured) &&
                           _settings.TryResolvePath(luaConfigured, out string luaPath)
            ? luaPath
            : null;

        string favConfigured = _settings.GetString("favlist_path", "favlist.fav");
        string favPath = _settings.ResolvePath(favConfigured, "favlist.fav");
        string preferredCategory = _settings.GetString("last_category", TeleportSession.AllCategory);
        if (File.Exists(favPath)) LoadFavouritesFromPath(favPath, preferredCategory);
    }

    private void LoadFavouritesFromPath(string path, string? preferredCategory = null)
    {
        try
        {
            _session.LoadFavourites(path);
            _favlistPath = path;
            RefreshCategoryCombo(preferredCategory);
            _allFavourites = _session.AllFavourites();
            RefreshFavouritesView();
            RegisterHotkeys();
            Log($"loaded {_allFavourites.Count} favourites from {path}");
        }
        catch (Exception ex) { Log("[error] load favourites: " + ex.Message); }
    }

    private void RegisterHotkeys()
    {
        if (_windowSource is null || _hotkeyPath is null) return;
        try
        {
            int count = _hotkeys.Apply(_windowSource.Handle, _hotkeyPath, _allFavourites,
                warning => Log("[warn] " + warning));
            Log($"registered {count} hotkeys");
        }
        catch (Exception ex) { Log("[error] hotkey config: " + ex.Message); }
    }

    private void SaveManagedSettings()
    {
        try
        {
            Rect bounds = WindowState == System.Windows.WindowState.Normal
                ? new Rect(Left, Top, ActualWidth, ActualHeight)
                : RestoreBounds;
            _settings.Set("wpf_window_x", bounds.Left);
            _settings.Set("wpf_window_y", bounds.Top);
            _settings.Set("wpf_window_width", bounds.Width);
            _settings.Set("wpf_window_height", bounds.Height);
            _settings.Set("wpf_window_saved", true);
            _settings.Set("wpf_always_on_top", AlwaysOnTopCheck.IsChecked == true);
            _settings.Set("default_version", VersionCombo.SelectedItem as string ?? "3.3.5");
            _settings.Set("last_category",
                CategoryCombo.SelectedItem as string ?? TeleportSession.AllCategory);
            if (TryParseSpeed(GlobalSpeedInput.Text, out double global))
                _settings.Set("speed_value", global);
            if (TryParseSpeed(MoveSpeedInput.Text, out double movement))
                _settings.Set("movement_speed_value", movement);
            if (_favlistPath is not null) _settings.Set("favlist_path", _favlistPath);
            _settings.Save();
        }
        catch (Exception ex) { Log("[error] save settings: " + ex.Message); }
    }

    private void ReloadFavourites()
    {
        if (_favlistPath is null)
        {
            Log("[warn] no favlist loaded yet");
            return;
        }
        LoadFavouritesFromPath(_favlistPath);
    }

    private void SaveFavourites()
    {
        if (_favlistPath is null)
        {
            Log("[warn] no favlist path known -- use Load first");
            return;
        }
        try
        {
            int written = _session.SaveFavourites(_favlistPath);
            Log($"saved {written} favourites to {_favlistPath}");
        }
        catch (Exception ex) { Log("[error] save favourites: " + ex.Message); }
    }

    private void RefreshCategoryCombo(string? preferred = null)
    {
        CategoryCombo.Items.Clear();
        CategoryCombo.Items.Add(TeleportSession.AllCategory);
        try
        {
            foreach (var category in _session.Categories()) CategoryCombo.Items.Add(category);
        }
        catch (Exception ex) { Log("[error] enumerate categories: " + ex.Message); }
        int preferredIndex = preferred is null ? -1 : CategoryCombo.Items.IndexOf(preferred);
        CategoryCombo.SelectedIndex = preferredIndex >= 0 ? preferredIndex : 0;
    }

    private void RefreshFavouritesView()
    {
        _rows.Clear();
        var needle = SearchBox.Text?.Trim() ?? string.Empty;
        var category = CategoryCombo.SelectedItem as string ?? TeleportSession.AllCategory;
        foreach (var point in _allFavourites)
        {
            if (!MatchesCategory(point, category)) continue;
            if (needle.Length > 0 &&
                point.Description.IndexOf(needle, StringComparison.OrdinalIgnoreCase) < 0)
                continue;
            _rows.Add(new FavouriteRow(point));
        }
    }

    private static bool MatchesCategory(FavouritePoint point, string category)
        => category == TeleportSession.AllCategory || point.Category == category;

    private void TeleportSelected()
    {
        if (FavouritesView.SelectedItem is not FavouriteRow row)
        {
            Log("[warn] pick a favourite first");
            return;
        }
        try
        {
            var p = row.Point;
            _session.Teleport(p.X, p.Y, p.Z, p.Orientation);
            Log(p.Orientation.HasValue
                ? $"teleported to '{p.Description}' (r={p.Orientation.Value:0.###})"
                : $"teleported to '{p.Description}'");
        }
        catch (Exception ex) { Log("[error] teleport: " + ex.Message); }
    }

    // -------------------------------------------------------------------------
    // Favourites CRUD
    // -------------------------------------------------------------------------

    private FavouritePoint? SelectedFavourite()
        => FavouritesView.SelectedItem is FavouriteRow row ? row.Point : null;

    private int? SelectedLocalIndex()
    {
        var point = SelectedFavourite();
        if (point is null) return null;
        var category = CurrentCategoryForCrud();
        int local = 0;
        foreach (var candidate in _allFavourites)
        {
            if (!MatchesCategory(candidate, category)) continue;
            if (candidate.Index == point.Value.Index) return local;
            ++local;
        }
        return null;
    }

    private string CurrentCategoryForCrud()
        => CategoryCombo.SelectedItem as string ?? TeleportSession.AllCategory;

    private bool RequireSelectedIndex(out int localIndex)
    {
        var selected = SelectedLocalIndex();
        if (selected is null)
        {
            Log("[warn] pick a row first");
            localIndex = -1;
            return false;
        }
        localIndex = selected.Value;
        return true;
    }

    private bool RequireDescription(out string description)
    {
        description = DescriptionBox.Text?.Trim() ?? string.Empty;
        if (description.Length != 0) return true;
        Log("[warn] type a description first");
        return false;
    }

    private bool RequirePosition(out double x, out double y, out double z, out double? r)
    {
        x = y = z = 0;
        r = null;
        try
        {
            var position = _session.ReadPosition();
            x = position.X;
            y = position.Y;
            z = position.Z;
            r = position.Orientation;
            return true;
        }
        catch (Exception ex)
        {
            Log("[error] read current position: " + ex.Message);
            return false;
        }
    }

    private void InsertBeforeSelected()
    {
        if (!RequireSelectedIndex(out int index) ||
            !RequireDescription(out string description) ||
            !RequirePosition(out double x, out double y, out double z, out double? r)) return;
        try
        {
            _session.InsertInCategory(CurrentCategoryForCrud(), index, description, x, y, z, r);
            AfterFavlistMutation($"inserted '{description}' before row {index}",
                PreferredCategoryForDescription(description));
        }
        catch (Exception ex) { Log("[error] 插前: " + ex.Message); }
    }

    private void InsertAfterSelected()
    {
        if (!RequireSelectedIndex(out int index) ||
            !RequireDescription(out string description) ||
            !RequirePosition(out double x, out double y, out double z, out double? r)) return;
        try
        {
            _session.AppendInCategory(CurrentCategoryForCrud(), index, description, x, y, z, r);
            AfterFavlistMutation($"inserted '{description}' after row {index}",
                PreferredCategoryForDescription(description));
        }
        catch (Exception ex) { Log("[error] 插后: " + ex.Message); }
    }

    private void EditSelected()
    {
        if (!RequireSelectedIndex(out int index) ||
            !RequireDescription(out string description) ||
            !RequirePosition(out double x, out double y, out double z, out double? r)) return;
        try
        {
            _session.ReplaceInCategory(CurrentCategoryForCrud(), index, description, x, y, z, r);
            AfterFavlistMutation($"updated row {index} ('{description}')",
                PreferredCategoryForDescription(description));
        }
        catch (Exception ex) { Log("[error] 编辑: " + ex.Message); }
    }

    private void DeleteSelected()
    {
        if (!RequireSelectedIndex(out int index)) return;
        try
        {
            _session.DeleteInCategory(CurrentCategoryForCrud(), index);
            AfterFavlistMutation($"deleted row {index}");
        }
        catch (Exception ex) { Log("[error] 删除: " + ex.Message); }
    }

    private void UpdateTeleportLua()
    {
        if (_teleportLuaPath is null)
        {
            Log("[warn] Teleport.lua path not configured in settings.json");
            return;
        }
        if (SelectedFavourite() is not FavouritePoint point)
        {
            Log("[warn] pick a row first");
            return;
        }
        int occurrence = _allFavourites.Count(candidate =>
            candidate.Index < point.Index && candidate.Description == point.Description);
        try
        {
            _session.UpdateTeleportLuaFromCurrent(_teleportLuaPath, point.Description, occurrence);
            Log($"Teleport.lua: updated '{point.Description}'");
        }
        catch (Exception ex) { Log("[error] 读取到炉石: " + ex.Message); }
    }

    private void InsertTableAndLua()
    {
        if (_teleportLuaPath is null)
        {
            Log("[warn] Teleport.lua path not configured in settings.json");
            return;
        }
        if (!RequireSelectedIndex(out int index) || !RequireDescription(out string description))
            return;
        try
        {
            _session.InsertCurrentIntoTableAndLua(_teleportLuaPath, CurrentCategoryForCrud(),
                                                   index, description);
            AfterFavlistMutation($"appended table + Teleport.lua: '{description}'",
                PreferredCategoryForDescription(description));
        }
        catch (Exception ex) { Log("[error] 插后 表&炉石: " + ex.Message); }
    }

    private void AppendToCategory()
    {
        var category = CurrentCategoryForCrud();
        int localCount = _allFavourites.Count(point => MatchesCategory(point, category));
        if (!RequireDescription(out string description) ||
            !RequirePosition(out double x, out double y, out double z, out double? r)) return;
        try
        {
            if (localCount == 0)
                _session.InsertInCategory(category, 0, description, x, y, z, r);
            else
                _session.AppendInCategory(category, localCount - 1, description, x, y, z, r);
            AfterFavlistMutation($"appended '{description}' after the last row of {category}",
                PreferredCategoryForDescription(description));
        }
        catch (Exception ex) { Log("[error] 追加: " + ex.Message); }
    }

    private string PreferredCategoryForDescription(string description)
    {
        if (CurrentCategoryForCrud() == TeleportSession.AllCategory)
            return TeleportSession.AllCategory;
        int separator = description.IndexOf('-');
        return separator >= 0 ? description[..separator] : "其他";
    }

    private void AfterFavlistMutation(string message, string? preferredCategory = null)
    {
        string previousCategory = preferredCategory ?? CurrentCategoryForCrud();
        _allFavourites = _session.AllFavourites();
        RefreshCategoryCombo(previousCategory);
        RefreshFavouritesView();
        RegisterHotkeys();
        Log(message);
    }

    private void ImportFavourites(bool merge)
    {
        var dialog = new OpenFileDialog
        {
            Filter = "Favlist (*.fav)|*.fav|All files (*.*)|*.*",
            InitialDirectory = AppContext.BaseDirectory,
        };
        if (dialog.ShowDialog(this) != true) return;
        try
        {
            string previous = CurrentCategoryForCrud();
            if (merge)
            {
                var report = _session.ImportMerge(dialog.FileName);
                Log($"merged {report.Added} new, skipped {report.SkippedDuplicates} duplicates");
            }
            else
            {
                int loaded = _session.ImportReplace(dialog.FileName);
                Log($"imported {loaded} points (replaced)");
            }
            _allFavourites = _session.AllFavourites();
            RefreshCategoryCombo(previous);
            RefreshFavouritesView();
            RegisterHotkeys();
        }
        catch (Exception ex) { Log("[error] import: " + ex.Message); }
    }

    private void ExportFavourites()
    {
        var dialog = new SaveFileDialog
        {
            Filter = "Favlist (*.fav)|*.fav|All files (*.*)|*.*",
            InitialDirectory = AppContext.BaseDirectory,
            FileName = "favlist-export.fav",
            OverwritePrompt = true,
        };
        if (dialog.ShowDialog(this) != true) return;
        try
        {
            int written = _session.ExportFavourites(dialog.FileName);
            Log($"exported {written} favourites to {dialog.FileName}");
        }
        catch (Exception ex) { Log("[error] export: " + ex.Message); }
    }

    private void LogToggle(string name, Func<bool> action)
    {
        try { Log($"{name}: {(action() ? "ON" : "OFF")}"); }
        catch (Exception ex) { Log($"[error] {name}: {ex.Message}"); }
    }

    private void EnableMovementSpeed()
    {
        if (!TryParseSpeed(MoveSpeedInput.Text, out double value))
        {
            Log("[error] movement speed must be a number");
            return;
        }
        try
        {
            _session.SetMovementSpeed(value);
            _moveSpeedEnabled = true;
            MoveSpeedToggle.Content = "Disable";
            _moveSpeedTimer.Start();
            Log($"movement + swim speed enabled at {value.ToString(CultureInfo.InvariantCulture)}");
        }
        catch (Exception ex) { Log("[error] enable movement speed: " + ex.Message); }
    }

    private void DisableMovementSpeed()
    {
        _moveSpeedTimer.Stop();
        _moveSpeedEnabled = false;
        MoveSpeedToggle.Content = "Enable";
        try
        {
            _session.ResetMovementSpeed();
            Log("movement speed restored to run=7, swim=4.7");
        }
        catch (Exception ex) { Log("[error] disable movement speed: " + ex.Message); }
    }

    private void MoveSpeedTimer_Tick(object? sender, EventArgs e)
    {
        if (!_moveSpeedEnabled) return;
        if (!TryParseSpeed(MoveSpeedInput.Text, out double value)) return;
        try { _session.SetMovementSpeed(value); }
        catch (Exception ex)
        {
            _moveSpeedEnabled = false;
            _moveSpeedTimer.Stop();
            MoveSpeedToggle.Content = "Enable";
            Log("[error] movement speed disabled: " + ex.Message);
        }
    }

    private void Shutdown()
    {
        if (_moveSpeedEnabled)
        {
            try { _session.ResetMovementSpeed(); }
            catch { /* best effort on teardown */ }
        }
        _moveSpeedTimer.Stop();
        _hotkeys.Dispose();
        if (_windowSource is not null)
        {
            _windowSource.RemoveHook(WindowMessageHook);
            _windowSource = null;
        }
        SaveManagedSettings();
        _session.Dispose();
    }

    // -------------------------------------------------------------------------
    // Helpers
    // -------------------------------------------------------------------------

    private static bool TryParseSpeed(string text, out double value)
        => double.TryParse(text, NumberStyles.Float,
            CultureInfo.InvariantCulture, out value);

    private void Log(string message)
    {
        var line = $"[{DateTime.Now:HH:mm:ss}] {message}{Environment.NewLine}";
        LogBox.AppendText(line);
        LogBox.ScrollToEnd();
    }

    /// <summary>Row DTO with preformatted numeric text so we can bind
    /// directly to GridView columns.</summary>
    private sealed record FavouriteRow(FavouritePoint Point)
    {
        public string Description => Point.Description;
        public string Category => Point.Category;
        public string XText => Point.X.ToString("0.###", CultureInfo.InvariantCulture);
        public string YText => Point.Y.ToString("0.###", CultureInfo.InvariantCulture);
        public string ZText => Point.Z.ToString("0.###", CultureInfo.InvariantCulture);
        public string RText => Point.Orientation.HasValue
            ? Point.Orientation.Value.ToString("0.###", CultureInfo.InvariantCulture)
            : "-";
    }

    private sealed record ProcessRowDisplay(ProcessRow Row)
    {
        public override string ToString() => $"{Row.Pid}  -  {Row.ExeName}";
    }
}
