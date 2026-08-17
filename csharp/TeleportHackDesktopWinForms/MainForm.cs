// MVP+ WinForms UI for TeleportHackDX. Deliberately layout-in-code (no
// .Designer.cs) so the whole surface is reviewable in one file. See
// ../../csharp/TeleportHackCore for the P/Invoke wrapper this drives.
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.IO;
using System.Linq;
using System.Windows.Forms;
using TeleportHackCore;

namespace TeleportHackDesktopWinForms;

internal sealed class MainForm : Form
{
    private readonly TeleportSession _session = new();

    // Movement-speed hack has to be re-written every frame because the game
    // keeps stomping our value; mirror the ImGui build's 1 Hz tick.
    private readonly System.Windows.Forms.Timer _moveSpeedTimer = new() { Interval = 1000 };
    private bool _moveSpeedEnabled;

    // Cached favourites so search / category-filter don't re-marshal the
    // whole list every keystroke.
    private IReadOnlyList<FavouritePoint> _allFavourites = Array.Empty<FavouritePoint>();
    // Path of the currently loaded favlist so 保存 writes back to the same
    // file without prompting.
    private string? _favlistPath;

    // --- controls (fields so timer / event handlers can reach them
    // without designer machinery) ---
    private ComboBox _processCombo = null!;
    private Button _refreshButton = null!;
    private Button _attachButton = null!;
    private Label _attachStatus = null!;
    private CheckBox _alwaysOnTopCheck = null!;
    private ComboBox _versionCombo = null!;
    private Button _loadFavouritesButton = null!;
    private ComboBox _categoryCombo = null!;
    private TextBox _searchBox = null!;
    private ListView _favouritesView = null!;
    private TextBox _descriptionBox = null!;
    private Button _insertBeforeButton = null!;
    private Button _insertAfterButton = null!;
    private Button _editButton = null!;
    private Button _deleteButton = null!;
    private Button _appendButton = null!;
    private Button _saveButton = null!;
    private Button _reloadButton = null!;
    private Button _teleportButton = null!;
    private Button _antiJumpButton = null!;
    private Button _autoLootButton = null!;
    private Button _patchLootButton = null!;
    private Button _luaUnlockButton = null!;
    private NumericUpDown _globalSpeedInput = null!;
    private Button _globalSpeedApply = null!;
    private NumericUpDown _moveSpeedInput = null!;
    private Button _moveSpeedToggle = null!;
    private TextBox _logBox = null!;

    public MainForm()
    {
        Text = "TeleportHack Desktop (WinForms / Attach)";
        MinimumSize = new Size(820, 820);
        StartPosition = FormStartPosition.CenterScreen;

        BuildLayout();

        _moveSpeedTimer.Tick += MoveSpeedTimer_Tick;
        FormClosing += MainForm_FormClosing;

        try
        {
            _versionCombo.Items.AddRange(TeleportSession.Versions().Cast<object>().ToArray());
            _versionCombo.SelectedItem = _session.Version;
        }
        catch (Exception ex) { Log("[error] enumerating versions: " + ex.Message); }
        RefreshProcesses();
        TryLoadDefaultFavourites();
    }

    // -------------------------------------------------------------------------
    // Layout
    // -------------------------------------------------------------------------

    private void BuildLayout()
    {
        var root = new TableLayoutPanel
        {
            Dock = DockStyle.Fill,
            ColumnCount = 1,
            RowCount = 10,
            Padding = new Padding(8),
        };
        root.RowStyles.Add(new RowStyle(SizeType.AutoSize));      // process
        root.RowStyles.Add(new RowStyle(SizeType.AutoSize));      // version + top
        root.RowStyles.Add(new RowStyle(SizeType.AutoSize));      // load / category / search
        root.RowStyles.Add(new RowStyle(SizeType.Percent, 60));   // favourites
        root.RowStyles.Add(new RowStyle(SizeType.AutoSize));      // description input
        root.RowStyles.Add(new RowStyle(SizeType.AutoSize));      // CRUD row
        root.RowStyles.Add(new RowStyle(SizeType.AutoSize));      // teleport
        root.RowStyles.Add(new RowStyle(SizeType.AutoSize));      // toggles
        root.RowStyles.Add(new RowStyle(SizeType.AutoSize));      // speed
        root.RowStyles.Add(new RowStyle(SizeType.Percent, 40));   // log
        root.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));

        root.Controls.Add(BuildProcessRow(), 0, 0);
        root.Controls.Add(BuildVersionRow(), 0, 1);
        root.Controls.Add(BuildLoadSearchRow(), 0, 2);
        root.Controls.Add(BuildFavouritesView(), 0, 3);
        root.Controls.Add(BuildDescriptionRow(), 0, 4);
        root.Controls.Add(BuildCrudRow(), 0, 5);
        root.Controls.Add(BuildTeleportRow(), 0, 6);
        root.Controls.Add(BuildTogglesRow(), 0, 7);
        root.Controls.Add(BuildSpeedRow(), 0, 8);
        root.Controls.Add(BuildLog(), 0, 9);
        Controls.Add(root);
    }

    private Control BuildProcessRow()
    {
        var panel = new FlowLayoutPanel { Dock = DockStyle.Fill, AutoSize = true };
        panel.Controls.Add(new Label { Text = "WoW 进程:", AutoSize = true, Margin = new Padding(0, 6, 6, 0) });
        _processCombo = new ComboBox { DropDownStyle = ComboBoxStyle.DropDownList, Width = 280 };
        panel.Controls.Add(_processCombo);
        _refreshButton = new Button { Text = "Refresh", AutoSize = true };
        _refreshButton.Click += (_, _) => RefreshProcesses();
        panel.Controls.Add(_refreshButton);
        _attachButton = new Button { Text = "Attach", AutoSize = true };
        _attachButton.Click += (_, _) => AttachSelected();
        panel.Controls.Add(_attachButton);
        _attachStatus = new Label { Text = "not attached", AutoSize = true, ForeColor = Color.DarkRed, Margin = new Padding(6, 8, 0, 0) };
        panel.Controls.Add(_attachStatus);
        return panel;
    }

    private Control BuildVersionRow()
    {
        var panel = new FlowLayoutPanel { Dock = DockStyle.Fill, AutoSize = true };
        panel.Controls.Add(new Label { Text = "client:", AutoSize = true, Margin = new Padding(0, 8, 6, 0) });
        _versionCombo = new ComboBox { DropDownStyle = ComboBoxStyle.DropDownList, Width = 140 };
        _versionCombo.SelectedIndexChanged += VersionCombo_SelectedIndexChanged;
        panel.Controls.Add(_versionCombo);
        _alwaysOnTopCheck = new CheckBox { Text = "Always on top", AutoSize = true, Margin = new Padding(20, 8, 0, 0) };
        _alwaysOnTopCheck.CheckedChanged += (_, _) => TopMost = _alwaysOnTopCheck.Checked;
        panel.Controls.Add(_alwaysOnTopCheck);
        return panel;
    }

    private Control BuildLoadSearchRow()
    {
        var panel = new FlowLayoutPanel { Dock = DockStyle.Fill, AutoSize = true };
        _loadFavouritesButton = new Button { Text = "Load favlist.fav...", AutoSize = true };
        _loadFavouritesButton.Click += (_, _) => LoadFavouritesInteractive();
        panel.Controls.Add(_loadFavouritesButton);
        panel.Controls.Add(new Label { Text = "分类:", AutoSize = true, Margin = new Padding(10, 6, 4, 0) });
        _categoryCombo = new ComboBox { DropDownStyle = ComboBoxStyle.DropDownList, Width = 160 };
        _categoryCombo.SelectedIndexChanged += (_, _) => RefreshFavouritesView();
        panel.Controls.Add(_categoryCombo);
        panel.Controls.Add(new Label { Text = "搜索:", AutoSize = true, Margin = new Padding(10, 6, 4, 0) });
        _searchBox = new TextBox { Width = 220 };
        _searchBox.TextChanged += (_, _) => RefreshFavouritesView();
        panel.Controls.Add(_searchBox);
        return panel;
    }

    private Control BuildFavouritesView()
    {
        _favouritesView = new ListView
        {
            Dock = DockStyle.Fill,
            View = View.Details,
            FullRowSelect = true,
            HideSelection = false,
            MultiSelect = false,
            GridLines = true,
        };
        _favouritesView.Columns.Add("描述", 340);
        _favouritesView.Columns.Add("分类", 100);
        _favouritesView.Columns.Add("x", 70, HorizontalAlignment.Right);
        _favouritesView.Columns.Add("y", 70, HorizontalAlignment.Right);
        _favouritesView.Columns.Add("z", 70, HorizontalAlignment.Right);
        _favouritesView.Columns.Add("r", 60, HorizontalAlignment.Right);
        _favouritesView.DoubleClick += (_, _) => TeleportSelected();
        _favouritesView.SelectedIndexChanged += FavouritesView_SelectedIndexChanged;
        return _favouritesView;
    }

    private Control BuildDescriptionRow()
    {
        var panel = new FlowLayoutPanel { Dock = DockStyle.Fill, AutoSize = true };
        panel.Controls.Add(new Label { Text = "描述:", AutoSize = true, Margin = new Padding(0, 8, 4, 0) });
        _descriptionBox = new TextBox { Width = 600 };
        panel.Controls.Add(_descriptionBox);
        return panel;
    }

    private Control BuildCrudRow()
    {
        var panel = new FlowLayoutPanel { Dock = DockStyle.Fill, AutoSize = true };
        _insertBeforeButton = MakeButton("插前", (_, _) => InsertBeforeSelected());
        _insertAfterButton = MakeButton("插后", (_, _) => InsertAfterSelected());
        _editButton = MakeButton("编辑", (_, _) => EditSelected());
        _deleteButton = MakeButton("删除", (_, _) => DeleteSelected());
        _appendButton = MakeButton("追加", (_, _) => AppendToCategory());
        _saveButton = MakeButton("保存", (_, _) => SaveFavourites());
        _reloadButton = MakeButton("重载", (_, _) => ReloadFavourites());
        panel.Controls.Add(_insertBeforeButton);
        panel.Controls.Add(_insertAfterButton);
        panel.Controls.Add(_editButton);
        panel.Controls.Add(_deleteButton);
        panel.Controls.Add(_appendButton);
        panel.Controls.Add(_saveButton);
        panel.Controls.Add(_reloadButton);
        return panel;
    }

    private static Button MakeButton(string label, EventHandler handler)
    {
        var button = new Button { Text = label, AutoSize = true, Margin = new Padding(0, 0, 6, 0) };
        button.Click += handler;
        return button;
    }

    private Control BuildTeleportRow()
    {
        _teleportButton = new Button
        {
            Text = "Teleport",
            Dock = DockStyle.Fill,
            Height = 40,
            BackColor = Color.FromArgb(30, 80, 160),
            ForeColor = Color.White,
            FlatStyle = FlatStyle.Flat,
        };
        _teleportButton.Click += (_, _) => TeleportSelected();
        return _teleportButton;
    }

    private Control BuildTogglesRow()
    {
        var panel = new FlowLayoutPanel { Dock = DockStyle.Fill, AutoSize = true };
        _antiJumpButton = MakeButton("AntiJump", (_, _) => LogToggle("AntiJump", _session.ToggleAntiJump));
        _autoLootButton = MakeButton("AutoLoot", (_, _) => LogToggle("AutoLoot", _session.ToggleAutoloot));
        _patchLootButton = MakeButton("PatchLoot", (_, _) => LogToggle("PatchLoot", _session.TogglePatchLoot));
        _luaUnlockButton = MakeButton("LuaUnlock", (_, _) => LogToggle("LuaUnlock", _session.ToggleLuaUnlock));
        panel.Controls.Add(_antiJumpButton);
        panel.Controls.Add(_autoLootButton);
        panel.Controls.Add(_patchLootButton);
        panel.Controls.Add(_luaUnlockButton);
        return panel;
    }

    private Control BuildSpeedRow()
    {
        var panel = new FlowLayoutPanel { Dock = DockStyle.Fill, AutoSize = true };
        panel.Controls.Add(new Label { Text = "Global speed:", AutoSize = true, Margin = new Padding(0, 6, 4, 0) });
        _globalSpeedInput = new NumericUpDown { Minimum = 0.1M, Maximum = 1000M, DecimalPlaces = 2, Value = 7M, Increment = 1M, Width = 80 };
        panel.Controls.Add(_globalSpeedInput);
        _globalSpeedApply = new Button { Text = "Apply", AutoSize = true };
        _globalSpeedApply.Click += (_, _) => ApplyGlobalSpeed();
        panel.Controls.Add(_globalSpeedApply);
        panel.Controls.Add(new Label { Text = "  Move speed:", AutoSize = true, Margin = new Padding(20, 6, 4, 0) });
        _moveSpeedInput = new NumericUpDown { Minimum = 0.1M, Maximum = 1000M, DecimalPlaces = 2, Value = 7M, Increment = 1M, Width = 80 };
        panel.Controls.Add(_moveSpeedInput);
        _moveSpeedToggle = new Button { Text = "Enable", AutoSize = true };
        _moveSpeedToggle.Click += (_, _) => ToggleMovementSpeed();
        panel.Controls.Add(_moveSpeedToggle);
        return panel;
    }

    private Control BuildLog()
    {
        _logBox = new TextBox
        {
            Dock = DockStyle.Fill,
            Multiline = true,
            ReadOnly = true,
            ScrollBars = ScrollBars.Vertical,
            Font = new Font(FontFamily.GenericMonospace, 9),
            WordWrap = false,
        };
        return _logBox;
    }

    // -------------------------------------------------------------------------
    // Handlers
    // -------------------------------------------------------------------------

    private void MainForm_FormClosing(object? sender, FormClosingEventArgs e)
    {
        if (_moveSpeedEnabled)
        {
            try { _session.ResetMovementSpeed(); }
            catch { /* best-effort during teardown */ }
        }
        _moveSpeedTimer.Stop();
        _session.Dispose();
    }

    private void VersionCombo_SelectedIndexChanged(object? sender, EventArgs e)
    {
        if (_versionCombo.SelectedItem is not string picked) return;
        try
        {
            if (_moveSpeedEnabled) DisableMovementSpeed();
            _session.Version = picked;
            Log($"client profile: {picked}");
        }
        catch (Exception ex) { Log("[error] set version: " + ex.Message); }
    }

    private void FavouritesView_SelectedIndexChanged(object? sender, EventArgs e)
    {
        // Match the ImGui behaviour: clicking a row copies its description
        // into the editable buffer so 编辑/删除/插前/插后 have a natural default.
        if (SelectedFavourite() is FavouritePoint point) _descriptionBox.Text = point.Description;
    }

    private void RefreshProcesses()
    {
        _processCombo.Items.Clear();
        try
        {
            foreach (var row in TeleportSession.ListProcesses("WoW.exe"))
                _processCombo.Items.Add(new ProcessRowDisplay(row));
            if (_processCombo.Items.Count > 0) _processCombo.SelectedIndex = 0;
        }
        catch (Exception ex) { Log("[error] list processes: " + ex.Message); }
    }

    private void AttachSelected()
    {
        if (_processCombo.SelectedItem is not ProcessRowDisplay row)
        {
            Log("[warn] pick a WoW process first");
            return;
        }
        try
        {
            if (_moveSpeedEnabled) DisableMovementSpeed();
            _session.Attach(row.Row.Pid);
            _attachStatus.Text = $"attached (PID {row.Row.Pid})";
            _attachStatus.ForeColor = Color.DarkGreen;
            Log($"attached to PID {row.Row.Pid}");
        }
        catch (Exception ex)
        {
            _attachStatus.Text = "not attached";
            _attachStatus.ForeColor = Color.DarkRed;
            Log("[error] attach: " + ex.Message);
        }
    }

    private void TryLoadDefaultFavourites()
    {
        var path = Path.Combine(AppContext.BaseDirectory, "favlist.fav");
        if (!File.Exists(path)) return;
        LoadFavouritesFromPath(path);
    }

    private void LoadFavouritesInteractive()
    {
        using var dialog = new OpenFileDialog
        {
            Filter = "Favlist (*.fav)|*.fav|All files (*.*)|*.*",
            InitialDirectory = AppContext.BaseDirectory,
        };
        if (dialog.ShowDialog() != DialogResult.OK) return;
        LoadFavouritesFromPath(dialog.FileName);
    }

    private void LoadFavouritesFromPath(string path)
    {
        try
        {
            _session.LoadFavourites(path);
            _favlistPath = path;
            RefreshCategoryCombo();
            _allFavourites = _session.AllFavourites();
            RefreshFavouritesView();
            Log($"loaded {_allFavourites.Count} favourites from {path}");
        }
        catch (Exception ex) { Log("[error] load favourites: " + ex.Message); }
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
        _categoryCombo.Items.Clear();
        _categoryCombo.Items.Add(TeleportSession.AllCategory);
        try
        {
            foreach (var category in _session.Categories()) _categoryCombo.Items.Add(category);
        }
        catch (Exception ex) { Log("[error] enumerate categories: " + ex.Message); }
        int preferredIndex = preferred is null ? -1 : _categoryCombo.Items.IndexOf(preferred);
        _categoryCombo.SelectedIndex = preferredIndex >= 0 ? preferredIndex : 0;
    }

    private void RefreshFavouritesView()
    {
        _favouritesView.BeginUpdate();
        _favouritesView.Items.Clear();
        var needle = _searchBox.Text?.Trim() ?? string.Empty;
        var category = _categoryCombo.SelectedItem as string ?? TeleportSession.AllCategory;
        foreach (var point in _allFavourites)
        {
            if (!MatchesCategory(point, category)) continue;
            if (needle.Length > 0 &&
                point.Description.IndexOf(needle, StringComparison.OrdinalIgnoreCase) < 0)
                continue;
            var item = new ListViewItem(point.Description) { Tag = point };
            item.SubItems.Add(point.Category);
            item.SubItems.Add(point.X.ToString("0.###"));
            item.SubItems.Add(point.Y.ToString("0.###"));
            item.SubItems.Add(point.Z.ToString("0.###"));
            item.SubItems.Add(point.Orientation.HasValue ? point.Orientation.Value.ToString("0.###") : "-");
            _favouritesView.Items.Add(item);
        }
        _favouritesView.EndUpdate();
    }

    private static bool MatchesCategory(FavouritePoint point, string category)
        => category == TeleportSession.AllCategory || point.Category == category;

    private FavouritePoint? SelectedFavourite()
    {
        if (_favouritesView.SelectedItems.Count == 0) return null;
        return _favouritesView.SelectedItems[0].Tag as FavouritePoint?;
    }

    // Returns the selected row's zero-based position within its category (or
    // within the full list if "所有" is picked). Needed because the C ABI's
    // CRUD helpers take (category, local_index) not (global_index).
    private int? SelectedLocalIndex()
    {
        var point = SelectedFavourite();
        if (point is null) return null;
        var category = _categoryCombo.SelectedItem as string ?? TeleportSession.AllCategory;
        int local = 0;
        foreach (var candidate in _allFavourites)
        {
            if (!MatchesCategory(candidate, category)) continue;
            if (candidate.Index == point.Value.Index) return local;
            ++local;
        }
        return null;
    }

    private void TeleportSelected()
    {
        if (SelectedFavourite() is not FavouritePoint point)
        {
            Log("[warn] pick a favourite first");
            return;
        }
        try
        {
            _session.Teleport(point.X, point.Y, point.Z, point.Orientation);
            Log(point.Orientation.HasValue
                ? $"teleported to '{point.Description}' (r={point.Orientation.Value:0.###})"
                : $"teleported to '{point.Description}'");
        }
        catch (Exception ex) { Log("[error] teleport: " + ex.Message); }
    }

    // -------------------------------------------------------------------------
    // CRUD helpers
    // -------------------------------------------------------------------------

    private bool RequireDescription(out string description)
    {
        description = _descriptionBox.Text?.Trim() ?? string.Empty;
        if (description.Length == 0)
        {
            Log("[warn] type a description first");
            return false;
        }
        return true;
    }

    private bool RequireSelectedIndex(out int localIndex)
    {
        var index = SelectedLocalIndex();
        if (index is null)
        {
            Log("[warn] pick a row first");
            localIndex = -1;
            return false;
        }
        localIndex = index.Value;
        return true;
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

    private string CurrentCategoryForCrud()
        => _categoryCombo.SelectedItem as string ?? TeleportSession.AllCategory;

    private void InsertBeforeSelected()
    {
        if (!RequireSelectedIndex(out var index)) return;
        if (!RequireDescription(out var description)) return;
        if (!RequirePosition(out var x, out var y, out var z, out var r)) return;
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
        if (!RequireSelectedIndex(out var index)) return;
        if (!RequireDescription(out var description)) return;
        if (!RequirePosition(out var x, out var y, out var z, out var r)) return;
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
        if (!RequireSelectedIndex(out var index)) return;
        if (!RequireDescription(out var description)) return;
        if (!RequirePosition(out var x, out var y, out var z, out var r)) return;
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
        if (!RequireSelectedIndex(out var index)) return;
        try
        {
            _session.DeleteInCategory(CurrentCategoryForCrud(), index);
            AfterFavlistMutation($"deleted row {index}");
        }
        catch (Exception ex) { Log("[error] 删除: " + ex.Message); }
    }

    private void AppendToCategory()
    {
        // Append == insert after the last row of the current category. If the
        // category is empty (or "所有" over an empty list) we fall back to
        // insert-at-zero which the underlying service accepts as "at the end".
        var category = CurrentCategoryForCrud();
        int local = 0;
        foreach (var point in _allFavourites)
            if (MatchesCategory(point, category)) ++local;
        if (!RequireDescription(out var description)) return;
        if (!RequirePosition(out var x, out var y, out var z, out var r)) return;
        try
        {
            if (local == 0)
                _session.InsertInCategory(category, 0, description, x, y, z, r);
            else
                _session.AppendInCategory(category, local - 1, description, x, y, z, r);
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
        // Refresh the cached list + view after every write so subsequent
        // insert/edit/delete calls operate on up-to-date indices.
        string previousCategory = preferredCategory ?? CurrentCategoryForCrud();
        _allFavourites = _session.AllFavourites();
        RefreshCategoryCombo(previousCategory);
        RefreshFavouritesView();
        Log(message);
    }

    // -------------------------------------------------------------------------
    // Toggle + speed
    // -------------------------------------------------------------------------

    private void LogToggle(string name, Func<bool> action)
    {
        try { Log($"{name}: {(action() ? "ON" : "OFF")}"); }
        catch (Exception ex) { Log($"[error] {name}: {ex.Message}"); }
    }

    private void ApplyGlobalSpeed()
    {
        try
        {
            double v = (double)_globalSpeedInput.Value;
            _session.SetGlobalSpeed(v);
            Log($"global speed = {v}");
        }
        catch (Exception ex) { Log("[error] global speed: " + ex.Message); }
    }

    private void ToggleMovementSpeed()
    {
        if (_moveSpeedEnabled) DisableMovementSpeed();
        else EnableMovementSpeed();
    }

    private void EnableMovementSpeed()
    {
        try
        {
            double v = (double)_moveSpeedInput.Value;
            _session.SetMovementSpeed(v);
            _moveSpeedEnabled = true;
            _moveSpeedToggle.Text = "Disable";
            _moveSpeedTimer.Start();
            Log($"movement + swim speed enabled at {v}");
        }
        catch (Exception ex) { Log("[error] enable movement speed: " + ex.Message); }
    }

    private void DisableMovementSpeed()
    {
        _moveSpeedTimer.Stop();
        _moveSpeedEnabled = false;
        _moveSpeedToggle.Text = "Enable";
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
        try { _session.SetMovementSpeed((double)_moveSpeedInput.Value); }
        catch (Exception ex)
        {
            _moveSpeedEnabled = false;
            _moveSpeedTimer.Stop();
            _moveSpeedToggle.Text = "Enable";
            Log("[error] movement speed disabled: " + ex.Message);
        }
    }

    // -------------------------------------------------------------------------
    // Log helper
    // -------------------------------------------------------------------------

    private void Log(string message)
    {
        var line = $"[{DateTime.Now:HH:mm:ss}] {message}{Environment.NewLine}";
        _logBox.AppendText(line);
    }

    private sealed record ProcessRowDisplay(ProcessRow Row)
    {
        public override string ToString() => $"{Row.Pid}  -  {Row.ExeName}";
    }
}
