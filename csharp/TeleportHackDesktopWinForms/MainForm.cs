// WinForms UI for TeleportHackDX. Deliberately layout-in-code (no
// .Designer.cs) so the whole surface is reviewable in one file. See
// ../../csharp/TeleportHackCore for the P/Invoke wrapper this drives.
//
// Tabs mirror TeleportHackOnVanilla's AutoIt window (and the ImGui build):
// 坐标点 / 分类导航 / 设置 / 关于, with the process row, client row and log
// always visible.
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
    private const int DefaultPidPollSeconds = 30;

    private readonly TeleportSession _session = new();
    private readonly SettingsDocument _settings = SettingsDocument.LoadFromApplicationDirectory();
    private readonly HotkeyRegistry _hotkeys = new();

    // Movement-speed hack has to be re-written every frame because the game
    // keeps stomping our value; mirror the ImGui build's 1 Hz tick.
    private readonly System.Windows.Forms.Timer _moveSpeedTimer = new() { Interval = 1000 };
    private bool _moveSpeedEnabled;
    // TeleportHackOnVanilla's "WoW PID 查询间隔": follow a restarted client.
    private readonly System.Windows.Forms.Timer _pidPollTimer = new()
    {
        Interval = DefaultPidPollSeconds * 1000,
    };
    // Refreshes the "map:" label once a second while attached.
    private readonly System.Windows.Forms.Timer _statusTimer = new() { Interval = 1000 };

    // Cached favourites so search / category-filter don't re-marshal the
    // whole list every keystroke.
    private IReadOnlyList<FavouritePoint> _allFavourites = Array.Empty<FavouritePoint>();
    // Path of the currently loaded favlist so 保存 writes back to the same
    // file without prompting.
    private string? _favlistPath;
    private string? _teleportLuaPath;
    private string? _hotkeyPath;
    private readonly Font _boldFont;
    // Set while the category combo is repopulated so its SelectedIndexChanged
    // doesn't rebuild the 1600+ row list (and lose the scroll position) twice.
    private bool _updatingCategories;

    // --- controls (fields so timer / event handlers can reach them
    // without designer machinery) ---
    private ComboBox _processCombo = null!;
    private Button _refreshButton = null!;
    private Button _attachButton = null!;
    private Label _attachStatus = null!;
    private CheckBox _alwaysOnTopCheck = null!;
    private ComboBox _versionCombo = null!;
    private Label _mapLabel = null!;
    private TabControl _tabs = null!;
    private Button _loadFavouritesButton = null!;
    private ComboBox _categoryCombo = null!;
    private TextBox _searchBox = null!;
    private ListView _favouritesView = null!;
    private ListView _recentView = null!;
    private TextBox _descriptionBox = null!;
    private Button _teleportButton = null!;
    private TableLayoutPanel _navigationGrid = null!;
    private NumericUpDown _globalSpeedInput = null!;
    private NumericUpDown _moveSpeedInput = null!;
    private Button _moveSpeedToggle = null!;
    private NumericUpDown _pidPollInput = null!;
    private TextBox _logBox = null!;

    public MainForm()
    {
        Text = "TeleportHack Desktop (WinForms / Attach)";
        MinimumSize = new Size(820, 860);
        StartPosition = FormStartPosition.CenterScreen;
        _boldFont = new Font(Font, FontStyle.Bold);

        BuildLayout();

        _moveSpeedTimer.Tick += MoveSpeedTimer_Tick;
        _pidPollTimer.Tick += (_, _) => AutoAttachSingleProcess("WoW PID check");
        _statusTimer.Tick += (_, _) => UpdateMapLabel();
        FormClosing += MainForm_FormClosing;
        Shown += (_, _) => RegisterHotkeys();

        try
        {
            _versionCombo.Items.AddRange(TeleportSession.Versions().Cast<object>().ToArray());
            string configuredVersion = _settings.GetString("default_version", _session.Version);
            _versionCombo.SelectedItem = _versionCombo.Items.Contains(configuredVersion)
                ? configuredVersion
                : _session.Version;
        }
        catch (Exception ex) { Log("[error] enumerating versions: " + ex.Message); }
        RefreshProcesses();
        ApplySettingsAndLoadData();
        AutoAttachSingleProcess("startup");
        _pidPollTimer.Start();
        _statusTimer.Start();
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
            RowCount = 4,
            Padding = new Padding(8),
        };
        root.RowStyles.Add(new RowStyle(SizeType.AutoSize));      // process
        root.RowStyles.Add(new RowStyle(SizeType.AutoSize));      // version + top
        root.RowStyles.Add(new RowStyle(SizeType.Percent, 78));   // tabs
        root.RowStyles.Add(new RowStyle(SizeType.Percent, 22));   // log
        root.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));

        root.Controls.Add(BuildProcessRow(), 0, 0);
        root.Controls.Add(BuildVersionRow(), 0, 1);

        _tabs = new TabControl { Dock = DockStyle.Fill };
        _tabs.TabPages.Add(BuildPointsTab());
        _tabs.TabPages.Add(BuildNavigationTab());
        _tabs.TabPages.Add(BuildSettingsTab());
        _tabs.TabPages.Add(BuildAboutTab());
        root.Controls.Add(_tabs, 0, 2);
        root.Controls.Add(BuildLog(), 0, 3);
        Controls.Add(root);
    }

    private Control BuildProcessRow()
    {
        var panel = new FlowLayoutPanel { Dock = DockStyle.Fill, AutoSize = true };
        panel.Controls.Add(new Label { Text = "WoW 进程:", AutoSize = true, Margin = new Padding(0, 6, 6, 0) });
        _processCombo = new ComboBox { DropDownStyle = ComboBoxStyle.DropDownList, Width = 280 };
        panel.Controls.Add(_processCombo);
        _refreshButton = new Button { Text = "Refresh", AutoSize = true };
        _refreshButton.Click += (_, _) =>
        {
            RefreshProcesses();
            AutoAttachSingleProcess("refresh");
        };
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
        _mapLabel = new Label { Text = string.Empty, AutoSize = true, Margin = new Padding(20, 8, 0, 0) };
        panel.Controls.Add(_mapLabel);
        return panel;
    }

    private TabPage BuildPointsTab()
    {
        var page = new TabPage("坐标点");
        var layout = new TableLayoutPanel
        {
            Dock = DockStyle.Fill,
            ColumnCount = 1,
            RowCount = 7,
            Padding = new Padding(4),
        };
        layout.RowStyles.Add(new RowStyle(SizeType.AutoSize));      // load / category / search
        layout.RowStyles.Add(new RowStyle(SizeType.Percent, 65));   // favourites
        layout.RowStyles.Add(new RowStyle(SizeType.AutoSize));      // recent label
        layout.RowStyles.Add(new RowStyle(SizeType.Percent, 35));   // recent
        layout.RowStyles.Add(new RowStyle(SizeType.AutoSize));      // description
        layout.RowStyles.Add(new RowStyle(SizeType.AutoSize));      // CRUD
        layout.RowStyles.Add(new RowStyle(SizeType.AutoSize));      // teleport
        layout.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
        layout.Controls.Add(BuildLoadSearchRow(), 0, 0);
        layout.Controls.Add(BuildFavouritesView(), 0, 1);
        layout.Controls.Add(new Label
        {
            Text = "最近使用的坐标（双击再次传送）",
            AutoSize = true,
            Margin = new Padding(0, 6, 0, 2),
        }, 0, 2);
        layout.Controls.Add(BuildRecentView(), 0, 3);
        layout.Controls.Add(BuildDescriptionRow(), 0, 4);
        layout.Controls.Add(BuildCrudRow(), 0, 5);
        layout.Controls.Add(BuildTeleportRow(), 0, 6);
        page.Controls.Add(layout);
        return page;
    }

    private TabPage BuildNavigationTab()
    {
        var page = new TabPage("分类导航");
        var scroller = new Panel { Dock = DockStyle.Fill, AutoScroll = true, Padding = new Padding(6) };
        _navigationGrid = new TableLayoutPanel
        {
            Dock = DockStyle.Top,
            AutoSize = true,
            AutoSizeMode = AutoSizeMode.GrowAndShrink,
            ColumnCount = 3,
        };
        for (int i = 0; i < 3; ++i)
            _navigationGrid.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100f / 3f));
        scroller.Controls.Add(_navigationGrid);
        page.Controls.Add(scroller);
        return page;
    }

    private TabPage BuildSettingsTab()
    {
        var page = new TabPage("设置");
        var stack = new FlowLayoutPanel
        {
            Dock = DockStyle.Fill,
            FlowDirection = FlowDirection.TopDown,
            WrapContents = false,
            AutoScroll = true,
            Padding = new Padding(6),
        };
        stack.Controls.Add(SectionLabel("功能开关"));
        stack.Controls.Add(BuildTogglesRow());
        stack.Controls.Add(SectionLabel("速度"));
        stack.Controls.Add(BuildSpeedRow());
        stack.Controls.Add(SectionLabel("WoW PID 查询间隔"));
        stack.Controls.Add(BuildPidPollRow());
        stack.Controls.Add(SectionLabel("导入 / 导出"));
        stack.Controls.Add(BuildImportExportRow());
        page.Controls.Add(stack);
        return page;
    }

    private TabPage BuildAboutTab()
    {
        var page = new TabPage("关于");
        var layout = new TableLayoutPanel
        {
            Dock = DockStyle.Fill,
            ColumnCount = 1,
            RowCount = 4,
            Padding = new Padding(8),
        };
        layout.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        layout.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        layout.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        layout.RowStyles.Add(new RowStyle(SizeType.Percent, 100));
        layout.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));

        string version = "?", author = "?", notes = string.Empty;
        try
        {
            version = TeleportSession.BuildVersion();
            author = TeleportSession.AppAuthor();
            notes = TeleportSession.ReleaseNotes();
        }
        catch (Exception ex) { notes = "[error] " + ex.Message; }

        layout.Controls.Add(new Label { Text = $"TeleportHack DX  {version}", AutoSize = true, Font = _boldFont }, 0, 0);
        layout.Controls.Add(new Label { Text = "作者：" + author, AutoSize = true, Margin = new Padding(3, 6, 3, 6) }, 0, 1);
        layout.Controls.Add(new Label { Text = "版本更新日志", AutoSize = true }, 0, 2);
        layout.Controls.Add(new TextBox
        {
            Dock = DockStyle.Fill,
            Multiline = true,
            ReadOnly = true,
            ScrollBars = ScrollBars.Vertical,
            Text = notes.Replace("\n", Environment.NewLine),
        }, 0, 3);
        page.Controls.Add(layout);
        return page;
    }

    private Label SectionLabel(string text)
        => new() { Text = text, AutoSize = true, Font = _boldFont, Margin = new Padding(0, 10, 0, 4) };

    private Control BuildLoadSearchRow()
    {
        var panel = new FlowLayoutPanel { Dock = DockStyle.Fill, AutoSize = true };
        _loadFavouritesButton = new Button { Text = "Load favlist.fav...", AutoSize = true };
        _loadFavouritesButton.Click += (_, _) => LoadFavouritesInteractive();
        panel.Controls.Add(_loadFavouritesButton);
        panel.Controls.Add(new Label { Text = "分类:", AutoSize = true, Margin = new Padding(10, 6, 4, 0) });
        _categoryCombo = new ComboBox { DropDownStyle = ComboBoxStyle.DropDownList, Width = 160 };
        _categoryCombo.SelectedIndexChanged += (_, _) =>
        {
            if (!_updatingCategories) RefreshFavouritesView();
        };
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
        _favouritesView.Columns.Add("描述", 320);
        _favouritesView.Columns.Add("分类", 100);
        _favouritesView.Columns.Add("x", 70, HorizontalAlignment.Right);
        _favouritesView.Columns.Add("y", 70, HorizontalAlignment.Right);
        _favouritesView.Columns.Add("z", 70, HorizontalAlignment.Right);
        _favouritesView.Columns.Add("r", 55, HorizontalAlignment.Right);
        _favouritesView.Columns.Add("map", 50, HorizontalAlignment.Right);
        _favouritesView.DoubleClick += (_, _) => TeleportSelected();
        _favouritesView.SelectedIndexChanged += FavouritesView_SelectedIndexChanged;
        return _favouritesView;
    }

    private Control BuildRecentView()
    {
        _recentView = new ListView
        {
            Dock = DockStyle.Fill,
            View = View.Details,
            FullRowSelect = true,
            HideSelection = false,
            MultiSelect = false,
            GridLines = true,
        };
        _recentView.Columns.Add("最近使用", 320);
        _recentView.Columns.Add("x", 70, HorizontalAlignment.Right);
        _recentView.Columns.Add("y", 70, HorizontalAlignment.Right);
        _recentView.Columns.Add("z", 70, HorizontalAlignment.Right);
        _recentView.Columns.Add("map", 50, HorizontalAlignment.Right);
        _recentView.DoubleClick += (_, _) => TeleportRecentSelected();
        return _recentView;
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
        panel.Controls.Add(MakeButton("插前", (_, _) => InsertBeforeSelected()));
        panel.Controls.Add(MakeButton("插后", (_, _) => InsertAfterSelected()));
        panel.Controls.Add(MakeButton("编辑", (_, _) => EditSelected()));
        panel.Controls.Add(MakeButton("删除", (_, _) => DeleteSelected()));
        panel.Controls.Add(MakeButton("读取到炉石", (_, _) => UpdateTeleportLua()));
        panel.Controls.Add(MakeButton("插后 表&炉石", (_, _) => InsertTableAndLua()));
        panel.Controls.Add(MakeButton("追加", (_, _) => AppendToCategory()));
        panel.Controls.Add(MakeButton("保存", (_, _) => SaveFavourites()));
        panel.Controls.Add(MakeButton("重载", (_, _) => ReloadFavourites()));
        return panel;
    }

    private Control BuildImportExportRow()
    {
        var panel = new FlowLayoutPanel { AutoSize = true };
        panel.Controls.Add(MakeButton("Import Merge...", (_, _) => ImportFavourites(true)));
        panel.Controls.Add(MakeButton("Import Replace...", (_, _) => ImportFavourites(false)));
        panel.Controls.Add(MakeButton("Export...", (_, _) => ExportFavourites()));
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
        var panel = new FlowLayoutPanel { AutoSize = true };
        panel.Controls.Add(MakeButton("AntiJump", (_, _) => LogToggle("AntiJump", _session.ToggleAntiJump)));
        panel.Controls.Add(MakeButton("AutoLoot", (_, _) => LogToggle("AutoLoot", _session.ToggleAutoloot)));
        panel.Controls.Add(MakeButton("PatchLoot", (_, _) => LogToggle("PatchLoot", _session.TogglePatchLoot)));
        panel.Controls.Add(MakeButton("LuaUnlock", (_, _) => LogToggle("LuaUnlock", _session.ToggleLuaUnlock)));
        return panel;
    }

    private Control BuildSpeedRow()
    {
        var panel = new FlowLayoutPanel { AutoSize = true };
        panel.Controls.Add(new Label { Text = "Global speed:", AutoSize = true, Margin = new Padding(0, 6, 4, 0) });
        _globalSpeedInput = new NumericUpDown { Minimum = 0.1M, Maximum = 1000M, DecimalPlaces = 2, Value = 7M, Increment = 1M, Width = 80 };
        panel.Controls.Add(_globalSpeedInput);
        var globalSpeedApply = new Button { Text = "Apply", AutoSize = true };
        globalSpeedApply.Click += (_, _) => ApplyGlobalSpeed();
        panel.Controls.Add(globalSpeedApply);
        panel.Controls.Add(new Label { Text = "  Move speed:", AutoSize = true, Margin = new Padding(20, 6, 4, 0) });
        _moveSpeedInput = new NumericUpDown { Minimum = 0.1M, Maximum = 1000M, DecimalPlaces = 2, Value = 7M, Increment = 1M, Width = 80 };
        panel.Controls.Add(_moveSpeedInput);
        _moveSpeedToggle = new Button { Text = "Enable", AutoSize = true };
        _moveSpeedToggle.Click += (_, _) => ToggleMovementSpeed();
        panel.Controls.Add(_moveSpeedToggle);
        return panel;
    }

    private Control BuildPidPollRow()
    {
        var panel = new FlowLayoutPanel { AutoSize = true };
        _pidPollInput = new NumericUpDown
        {
            Minimum = 1,
            Maximum = 3600,
            Value = DefaultPidPollSeconds,
            Width = 80,
        };
        panel.Controls.Add(_pidPollInput);
        panel.Controls.Add(new Label { Text = "秒", AutoSize = true, Margin = new Padding(4, 6, 8, 0) });
        panel.Controls.Add(MakeButton("应用", (_, _) => ApplyPidPollInterval()));
        panel.Controls.Add(new Label
        {
            Text = "只有一个 WoW 进程时自动连接；间隔 1-3600 秒",
            AutoSize = true,
            ForeColor = SystemColors.GrayText,
            Margin = new Padding(8, 6, 0, 0),
        });
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
        _pidPollTimer.Stop();
        _statusTimer.Stop();
        _hotkeys.Dispose();
        SaveManagedSettings();
        _session.Dispose();
    }

    protected override void Dispose(bool disposing)
    {
        base.Dispose(disposing);
        if (disposing)
        {
            _moveSpeedTimer.Dispose();
            _pidPollTimer.Dispose();
            _statusTimer.Dispose();
            _boldFont.Dispose();
        }
    }

    protected override void WndProc(ref Message m)
    {
        if (m.Msg == HotkeyRegistry.WmHotkey &&
            _hotkeys.TryResolve(m.WParam.ToInt32(), out FavouritePoint point))
        {
            TeleportPoint(point, "hotkey teleported to");
            return;
        }
        base.WndProc(ref m);
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
        UpdateMapLabel();
    }

    private void FavouritesView_SelectedIndexChanged(object? sender, EventArgs e)
    {
        // Match the ImGui behaviour: clicking a row copies its description
        // into the editable buffer so 编辑/删除/插前/插后 have a natural default.
        if (SelectedFavourite() is FavouritePoint point) _descriptionBox.Text = point.Description;
    }

    private void RefreshProcesses(uint? selectPid = null)
    {
        _processCombo.Items.Clear();
        try
        {
            foreach (var processRow in TeleportSession.ListProcesses("WoW.exe"))
                _processCombo.Items.Add(new ProcessRowDisplay(processRow));
            int index = 0;
            for (int i = 0; selectPid.HasValue && i < _processCombo.Items.Count; ++i)
                if (_processCombo.Items[i] is ProcessRowDisplay display &&
                    display.Row.Pid == selectPid.Value)
                    index = i;
            if (_processCombo.Items.Count > 0) _processCombo.SelectedIndex = index;
        }
        catch (Exception ex) { Log("[error] list processes: " + ex.Message); }
    }

    // TeleportHackOnVanilla's SelectOnlyWowPid()/CheckWowPidForReload():
    // exactly one WoW.exe running and it isn't the attached PID -> attach.
    private void AutoAttachSingleProcess(string reason)
    {
        IReadOnlyList<ProcessRow> rows;
        try { rows = TeleportSession.ListProcesses("WoW.exe"); }
        catch (Exception ex)
        {
            Log("[error] list processes: " + ex.Message);
            return;
        }
        if (rows.Count != 1) return;
        uint pid = rows[0].Pid;
        if (_session.IsAttached && _session.AttachedPid == pid) return;
        string previous = _session.IsAttached ? _session.AttachedPid.ToString() : "none";
        Log($"{reason}: single WoW process, PID {previous} -> {pid}");
        RefreshProcesses(pid);
        AttachPid(pid);
    }

    private void AttachSelected()
    {
        if (_processCombo.SelectedItem is not ProcessRowDisplay row)
        {
            Log("[warn] pick a WoW process first");
            return;
        }
        AttachPid(row.Row.Pid);
    }

    private void AttachPid(uint pid)
    {
        try
        {
            if (_moveSpeedEnabled) DisableMovementSpeed();
            _session.Attach(pid);
            _attachStatus.Text = $"attached (PID {pid})";
            _attachStatus.ForeColor = Color.DarkGreen;
            Log($"attached to PID {pid}");
        }
        catch (Exception ex)
        {
            _attachStatus.Text = "not attached";
            _attachStatus.ForeColor = Color.DarkRed;
            Log("[error] attach: " + ex.Message);
        }
        UpdateMapLabel();
    }

    private void UpdateMapLabel()
    {
        uint? map = null;
        try
        {
            if (_session.IsAttached) map = _session.TryReadMapId();
        }
        catch { /* label is informational only */ }
        _mapLabel.Text = map.HasValue ? $"map:{map.Value}" : string.Empty;
    }

    private void ApplySettingsAndLoadData()
    {
        _alwaysOnTopCheck.Checked = _settings.GetBool("winforms_always_on_top", false);
        _globalSpeedInput.Value = ClampDecimal(_settings.GetDouble("speed_value", 7.0),
                                                _globalSpeedInput);
        _moveSpeedInput.Value = ClampDecimal(_settings.GetDouble("movement_speed_value", 7.0),
                                              _moveSpeedInput);
        int pollSeconds = SanitizePidPollSeconds(
            _settings.GetInt("pid_poll_seconds", DefaultPidPollSeconds), DefaultPidPollSeconds);
        _pidPollInput.Value = pollSeconds;
        _pidPollTimer.Interval = pollSeconds * 1000;

        if (_settings.GetBool("winforms_window_saved", false))
        {
            int width = _settings.GetInt("winforms_window_width", Width);
            int height = _settings.GetInt("winforms_window_height", Height);
            int x = _settings.GetInt("winforms_window_x", Left);
            int y = _settings.GetInt("winforms_window_y", Top);
            var candidate = new Rectangle(x, y, width, height);
            if (width >= MinimumSize.Width && height >= MinimumSize.Height &&
                Screen.AllScreens.Any(screen => screen.WorkingArea.IntersectsWith(candidate)))
            {
                StartPosition = FormStartPosition.Manual;
                Bounds = candidate;
            }
        }

        string favConfigured = _settings.GetString("favlist_path", "favlist.fav");
        string favPath = _settings.ResolvePath(favConfigured, "favlist.fav");
        string preferredCategory = _settings.GetString("last_category", TeleportSession.AllCategory);
        if (File.Exists(favPath)) LoadFavouritesFromPath(favPath, preferredCategory);

        string hotkeyConfigured = _settings.GetString("hotkey_path", "hotkey.txt");
        _hotkeyPath = _settings.ResolvePath(hotkeyConfigured, "hotkey.txt");
        string luaConfigured = _settings.GetString("teleport_lua_path");
        _teleportLuaPath = !string.IsNullOrWhiteSpace(luaConfigured) &&
                           _settings.TryResolvePath(luaConfigured, out string luaPath)
            ? luaPath
            : null;
    }

    private static int SanitizePidPollSeconds(int seconds, int fallback)
        => seconds >= 1 && seconds <= 3600 ? seconds : fallback;

    private void ApplyPidPollInterval()
    {
        int seconds = SanitizePidPollSeconds((int)_pidPollInput.Value, DefaultPidPollSeconds);
        _pidPollTimer.Stop();
        _pidPollTimer.Interval = seconds * 1000;
        _pidPollTimer.Start();
        try
        {
            _settings.Set("pid_poll_seconds", seconds);
            _settings.Save();
            Log($"WoW PID poll interval: {seconds} s");
        }
        catch (Exception ex) { Log("[error] save settings: " + ex.Message); }
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

    private void LoadFavouritesFromPath(string path, string? preferredCategory = null,
                                        int restoreTopIndex = 0)
    {
        try
        {
            _session.LoadFavourites(path);
            _favlistPath = path;
            _allFavourites = _session.AllFavourites();
            RefreshCategoryCombo(preferredCategory);
            RefreshFavouritesView(restoreTopIndex);
            RebuildNavigation();
            RegisterHotkeys();
            Log($"loaded {_allFavourites.Count} favourites from {path}");
        }
        catch (Exception ex) { Log("[error] load favourites: " + ex.Message); }
    }

    private static decimal ClampDecimal(double value, NumericUpDown control)
    {
        decimal converted;
        try { converted = (decimal)value; }
        catch { converted = control.Minimum; }
        return Math.Min(control.Maximum, Math.Max(control.Minimum, converted));
    }

    private void SaveManagedSettings()
    {
        try
        {
            Rectangle bounds = WindowState == FormWindowState.Normal ? Bounds : RestoreBounds;
            _settings.Set("winforms_window_x", bounds.X);
            _settings.Set("winforms_window_y", bounds.Y);
            _settings.Set("winforms_window_width", bounds.Width);
            _settings.Set("winforms_window_height", bounds.Height);
            _settings.Set("winforms_window_saved", true);
            _settings.Set("winforms_always_on_top", _alwaysOnTopCheck.Checked);
            _settings.Set("default_version", _versionCombo.SelectedItem as string ?? "3.3.5");
            _settings.Set("last_category",
                _categoryCombo.SelectedItem as string ?? TeleportSession.AllCategory);
            _settings.Set("speed_value", (double)_globalSpeedInput.Value);
            _settings.Set("movement_speed_value", (double)_moveSpeedInput.Value);
            _settings.Set("pid_poll_seconds", (int)_pidPollInput.Value);
            if (_favlistPath is not null) _settings.Set("favlist_path", _favlistPath);
            _settings.Save();
        }
        catch (Exception ex) { Log("[error] save settings: " + ex.Message); }
    }

    private void RegisterHotkeys()
    {
        if (!IsHandleCreated || _hotkeyPath is null) return;
        try
        {
            int count = _hotkeys.Apply(Handle, _hotkeyPath, _allFavourites,
                warning => Log("[warn] " + warning));
            Log($"registered {count} hotkeys");
        }
        catch (Exception ex) { Log("[error] hotkey config: " + ex.Message); }
    }

    private void ReloadFavourites()
    {
        if (_favlistPath is null)
        {
            Log("[warn] no favlist loaded yet");
            return;
        }
        // TeleportHackOnVanilla 5df337f: reload keeps the list scroll position.
        LoadFavouritesFromPath(_favlistPath, CurrentCategoryForCrud(), FavouritesTopIndex());
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

    // Callers refresh the list view themselves afterwards.
    private void RefreshCategoryCombo(string? preferred = null)
    {
        _updatingCategories = true;
        try
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
        finally { _updatingCategories = false; }
    }

    private int FavouritesTopIndex()
        => _favouritesView.Items.Count > 0 ? _favouritesView.TopItem?.Index ?? 0 : 0;

    // `restoreTopIndex` > 0 scrolls back to the row that was at the top
    // before the refresh (reload / edit keep the user's place in the list).
    private void RefreshFavouritesView(int restoreTopIndex = 0)
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
            item.SubItems.Add(point.MapId.HasValue ? point.MapId.Value.ToString() : "-");
            _favouritesView.Items.Add(item);
        }
        _favouritesView.EndUpdate();
        if (restoreTopIndex > 0 && restoreTopIndex < _favouritesView.Items.Count)
            _favouritesView.TopItem = _favouritesView.Items[restoreTopIndex];
    }

    private void RefreshRecentView()
    {
        _recentView.BeginUpdate();
        _recentView.Items.Clear();
        try
        {
            foreach (var entry in _session.RecentTeleports())
            {
                var item = new ListViewItem(entry.Label) { Tag = entry.Index };
                item.SubItems.Add(entry.X.ToString("0.###"));
                item.SubItems.Add(entry.Y.ToString("0.###"));
                item.SubItems.Add(entry.Z.ToString("0.###"));
                item.SubItems.Add(entry.MapId.HasValue ? entry.MapId.Value.ToString() : "-");
                _recentView.Items.Add(item);
            }
        }
        catch (Exception ex) { Log("[error] recent teleports: " + ex.Message); }
        _recentView.EndUpdate();
    }

    // Same buckets/order as TeleportHackOnVanilla's RefreshCategoryGrid().
    private void RebuildNavigation()
    {
        _navigationGrid.SuspendLayout();
        foreach (Control old in _navigationGrid.Controls.Cast<Control>().ToList()) old.Dispose();
        _navigationGrid.Controls.Clear();
        _navigationGrid.RowStyles.Clear();
        _navigationGrid.RowCount = 0;
        try
        {
            var categories = _categoryCombo.Items.Cast<object>()
                .Select(item => item as string ?? string.Empty)
                .Where(name => name.Length > 0 && name != TeleportSession.AllCategory);
            foreach (var group in TeleportSession.GroupCategories(categories))
            {
                var header = new Label
                {
                    Text = $"【{group.Name}】",
                    AutoSize = true,
                    Font = _boldFont,
                    Margin = new Padding(3, 10, 3, 3),
                };
                int row = AddNavigationRow();
                _navigationGrid.Controls.Add(header, 0, row);
                _navigationGrid.SetColumnSpan(header, 3);
                int column = 0;
                foreach (string category in group.Categories)
                {
                    if (column == 0) row = AddNavigationRow();
                    var button = new Button
                    {
                        Text = category,
                        Tag = category,
                        Dock = DockStyle.Fill,
                        Height = 28,
                        Margin = new Padding(3),
                    };
                    button.Click += NavigationButton_Click;
                    _navigationGrid.Controls.Add(button, column, row);
                    column = (column + 1) % 3;
                }
            }
        }
        catch (Exception ex) { Log("[error] category navigation: " + ex.Message); }
        _navigationGrid.ResumeLayout();
    }

    private int AddNavigationRow()
    {
        _navigationGrid.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        return _navigationGrid.RowCount++;
    }

    private void NavigationButton_Click(object? sender, EventArgs e)
    {
        if (sender is not Button { Tag: string category }) return;
        int index = _categoryCombo.Items.IndexOf(category);
        if (index < 0) return;
        _categoryCombo.SelectedIndex = index;
        _tabs.SelectedIndex = 0;
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
        TeleportPoint(point, "teleported to");
    }

    // Map guard + write + recent entry all happen natively (th_session_teleport_ex).
    private void TeleportPoint(FavouritePoint point, string verb)
    {
        try
        {
            _session.TeleportTo(point);
            Log($"{verb} '{point.Description}'{DescribeExtras(point.Orientation, point.MapId)}");
            RefreshRecentView();
        }
        catch (Exception ex) { Log("[warn] " + ex.Message); }
    }

    private void TeleportRecentSelected()
    {
        if (_recentView.SelectedItems.Count == 0 ||
            _recentView.SelectedItems[0].Tag is not int index)
            return;
        string label = _recentView.SelectedItems[0].Text;
        try
        {
            _session.TeleportRecent(index);
            Log($"teleported to recent '{label}'");
            RefreshRecentView();
        }
        catch (Exception ex) { Log("[warn] " + ex.Message); }
    }

    private static string DescribeExtras(double? orientation, uint? mapId)
    {
        string text = string.Empty;
        if (orientation.HasValue) text += $" r={orientation.Value:0.###}";
        if (mapId.HasValue) text += $" map={mapId.Value}";
        return text.Length > 0 ? " (" + text.Trim() + ")" : string.Empty;
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

    private bool RequirePosition(out double x, out double y, out double z, out double? r,
                                 out uint? map)
    {
        x = y = z = 0;
        r = null;
        map = null;
        try
        {
            var position = _session.ReadPosition();
            x = position.X;
            y = position.Y;
            z = position.Z;
            r = position.Orientation;
            map = position.MapId;
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
        if (!RequirePosition(out var x, out var y, out var z, out var r, out var map)) return;
        try
        {
            _session.InsertInCategory(CurrentCategoryForCrud(), index, description, x, y, z, r, map);
            AfterFavlistMutation($"inserted '{description}' before row {index}",
                PreferredCategoryForDescription(description));
        }
        catch (Exception ex) { Log("[error] 插前: " + ex.Message); }
    }

    private void InsertAfterSelected()
    {
        if (!RequireSelectedIndex(out var index)) return;
        if (!RequireDescription(out var description)) return;
        if (!RequirePosition(out var x, out var y, out var z, out var r, out var map)) return;
        try
        {
            _session.AppendInCategory(CurrentCategoryForCrud(), index, description, x, y, z, r, map);
            AfterFavlistMutation($"inserted '{description}' after row {index}",
                PreferredCategoryForDescription(description));
        }
        catch (Exception ex) { Log("[error] 插后: " + ex.Message); }
    }

    private void EditSelected()
    {
        if (!RequireSelectedIndex(out var index)) return;
        if (!RequireDescription(out var description)) return;
        if (!RequirePosition(out var x, out var y, out var z, out var r, out var map)) return;
        try
        {
            _session.ReplaceInCategory(CurrentCategoryForCrud(), index, description, x, y, z, r, map);
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
        // Append == insert after the last row of the current category. If the
        // category is empty (or "所有" over an empty list) we fall back to
        // insert-at-zero which the underlying service accepts as "at the end".
        var category = CurrentCategoryForCrud();
        int local = 0;
        foreach (var point in _allFavourites)
            if (MatchesCategory(point, category)) ++local;
        if (!RequireDescription(out var description)) return;
        if (!RequirePosition(out var x, out var y, out var z, out var r, out var map)) return;
        try
        {
            if (local == 0)
                _session.InsertInCategory(category, 0, description, x, y, z, r, map);
            else
                _session.AppendInCategory(category, local - 1, description, x, y, z, r, map);
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
        int topIndex = previousCategory == CurrentCategoryForCrud() ? FavouritesTopIndex() : 0;
        _allFavourites = _session.AllFavourites();
        RefreshCategoryCombo(previousCategory);
        RefreshFavouritesView(topIndex);
        RebuildNavigation();
        RegisterHotkeys();
        Log(message);
    }

    private void ImportFavourites(bool merge)
    {
        using var dialog = new OpenFileDialog
        {
            Filter = "Favlist (*.fav)|*.fav|All files (*.*)|*.*",
            InitialDirectory = AppContext.BaseDirectory,
        };
        if (dialog.ShowDialog() != DialogResult.OK) return;
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
            RebuildNavigation();
            RegisterHotkeys();
        }
        catch (Exception ex) { Log("[error] import: " + ex.Message); }
    }

    private void ExportFavourites()
    {
        using var dialog = new SaveFileDialog
        {
            Filter = "Favlist (*.fav)|*.fav|All files (*.*)|*.*",
            InitialDirectory = AppContext.BaseDirectory,
            FileName = "favlist-export.fav",
            OverwritePrompt = true,
        };
        if (dialog.ShowDialog() != DialogResult.OK) return;
        try
        {
            int written = _session.ExportFavourites(dialog.FileName);
            Log($"exported {written} favourites to {dialog.FileName}");
        }
        catch (Exception ex) { Log("[error] export: " + ex.Message); }
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
