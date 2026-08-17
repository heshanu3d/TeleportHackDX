// MVP WinForms UI for TeleportHackDX. Deliberately layout-in-code (no
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

    // Cached favourites so search doesn't re-marshal the whole list every
    // keystroke.
    private IReadOnlyList<FavouritePoint> _allFavourites = Array.Empty<FavouritePoint>();

    // --- controls (declared as fields so the timer / event handlers can
    // reach them without the designer machinery) ---
    private ComboBox _processCombo = null!;
    private Button _refreshButton = null!;
    private Button _attachButton = null!;
    private Label _attachStatus = null!;
    private CheckBox _alwaysOnTopCheck = null!;
    private ComboBox _versionCombo = null!;
    private Button _loadFavouritesButton = null!;
    private TextBox _searchBox = null!;
    private ListView _favouritesView = null!;
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
        MinimumSize = new Size(720, 720);
        StartPosition = FormStartPosition.CenterScreen;

        BuildLayout();

        _moveSpeedTimer.Tick += MoveSpeedTimer_Tick;
        FormClosing += MainForm_FormClosing;

        try
        {
            _versionCombo.Items.AddRange(TeleportSession.Versions().Cast<object>().ToArray());
            _versionCombo.SelectedItem = _session.Version;
        }
        catch (Exception ex)
        {
            Log("[error] enumerating versions: " + ex.Message);
        }
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
            RowCount = 8,
            Padding = new Padding(8),
            AutoSize = false,
        };
        root.RowStyles.Add(new RowStyle(SizeType.AutoSize));   // process
        root.RowStyles.Add(new RowStyle(SizeType.AutoSize));   // version + always-on-top
        root.RowStyles.Add(new RowStyle(SizeType.AutoSize));   // load / search
        root.RowStyles.Add(new RowStyle(SizeType.Percent, 60)); // table
        root.RowStyles.Add(new RowStyle(SizeType.AutoSize));   // teleport
        root.RowStyles.Add(new RowStyle(SizeType.AutoSize));   // toggles
        root.RowStyles.Add(new RowStyle(SizeType.AutoSize));   // speeds
        root.RowStyles.Add(new RowStyle(SizeType.Percent, 40)); // log
        root.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));

        root.Controls.Add(BuildProcessRow(), 0, 0);
        root.Controls.Add(BuildVersionRow(), 0, 1);
        root.Controls.Add(BuildLoadSearchRow(), 0, 2);
        root.Controls.Add(BuildFavouritesView(), 0, 3);
        root.Controls.Add(BuildTeleportRow(), 0, 4);
        root.Controls.Add(BuildTogglesRow(), 0, 5);
        root.Controls.Add(BuildSpeedRow(), 0, 6);
        root.Controls.Add(BuildLog(), 0, 7);
        Controls.Add(root);
    }

    private Control BuildProcessRow()
    {
        var panel = new FlowLayoutPanel { Dock = DockStyle.Fill, AutoSize = true };
        panel.Controls.Add(new Label { Text = "WoW 进程:", AutoSize = true, TextAlign = ContentAlignment.MiddleLeft, Margin = new Padding(0, 6, 6, 0) });
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
        panel.Controls.Add(new Label { Text = "搜索:", AutoSize = true, Margin = new Padding(10, 6, 4, 0) });
        _searchBox = new TextBox { Width = 260 };
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
        _favouritesView.Columns.Add("描述", 320);
        _favouritesView.Columns.Add("分类", 100);
        _favouritesView.Columns.Add("x", 70, HorizontalAlignment.Right);
        _favouritesView.Columns.Add("y", 70, HorizontalAlignment.Right);
        _favouritesView.Columns.Add("z", 70, HorizontalAlignment.Right);
        _favouritesView.Columns.Add("r", 60, HorizontalAlignment.Right);
        _favouritesView.DoubleClick += (_, _) => TeleportSelected();
        return _favouritesView;
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
        _antiJumpButton = MakeToggleButton("AntiJump", () => LogToggle("AntiJump", _session.ToggleAntiJump));
        _autoLootButton = MakeToggleButton("AutoLoot", () => LogToggle("AutoLoot", _session.ToggleAutoloot));
        _patchLootButton = MakeToggleButton("PatchLoot", () => LogToggle("PatchLoot", _session.TogglePatchLoot));
        _luaUnlockButton = MakeToggleButton("LuaUnlock", () => LogToggle("LuaUnlock", _session.ToggleLuaUnlock));
        panel.Controls.Add(_antiJumpButton);
        panel.Controls.Add(_autoLootButton);
        panel.Controls.Add(_patchLootButton);
        panel.Controls.Add(_luaUnlockButton);
        return panel;
    }

    private static Button MakeToggleButton(string label, Action action)
    {
        var button = new Button { Text = label, AutoSize = true, Margin = new Padding(0, 0, 6, 0) };
        button.Click += (_, _) => action();
        return button;
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
            catch { /* best effort during teardown */ }
        }
        _moveSpeedTimer.Stop();
        _session.Dispose();
    }

    private void VersionCombo_SelectedIndexChanged(object? sender, EventArgs e)
    {
        if (_versionCombo.SelectedItem is not string picked) return;
        try
        {
            if (_moveSpeedEnabled) DisableMovementSpeed(); // avoid stray writes on old profile
            _session.Version = picked;
            Log($"client profile: {picked}");
        }
        catch (Exception ex) { Log("[error] set version: " + ex.Message); }
    }

    private void RefreshProcesses()
    {
        _processCombo.Items.Clear();
        try
        {
            var rows = TeleportSession.ListProcesses("WoW.exe");
            foreach (var row in rows)
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
            _allFavourites = _session.AllFavourites();
            RefreshFavouritesView();
            Log($"loaded {_allFavourites.Count} favourites from {path}");
        }
        catch (Exception ex) { Log("[error] load favourites: " + ex.Message); }
    }

    private void RefreshFavouritesView()
    {
        _favouritesView.BeginUpdate();
        _favouritesView.Items.Clear();
        var needle = _searchBox.Text?.Trim() ?? string.Empty;
        foreach (var point in _allFavourites)
        {
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

    private void TeleportSelected()
    {
        if (_favouritesView.SelectedItems.Count == 0)
        {
            Log("[warn] pick a favourite first");
            return;
        }
        if (_favouritesView.SelectedItems[0].Tag is not FavouritePoint point) return;
        try
        {
            _session.Teleport(point.X, point.Y, point.Z, point.Orientation);
            Log(point.Orientation.HasValue
                ? $"teleported to '{point.Description}' (r={point.Orientation.Value:0.###})"
                : $"teleported to '{point.Description}'");
        }
        catch (Exception ex) { Log("[error] teleport: " + ex.Message); }
    }

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
        try
        {
            double v = (double)_moveSpeedInput.Value;
            _session.SetMovementSpeed(v);
        }
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

    // Wraps ProcessRow with a nice ToString for the combo box display.
    private sealed record ProcessRowDisplay(ProcessRow Row)
    {
        public override string ToString() => $"{Row.Pid}  -  {Row.ExeName}";
    }
}
