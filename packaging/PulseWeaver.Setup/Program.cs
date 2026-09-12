using Microsoft.Win32;
using System.Diagnostics;
using System.IO.Compression;
using System.Reflection;
using System.Runtime.InteropServices;

namespace PulseWeaver.Setup;

internal static class Program
{
    const string ProductName = "Pulse Weaver Public Preview";
    internal const string Version = "0.01";
    const string InstallManifestName = ".pulseweaver-installed-files.txt";
    internal static readonly string InstallDirectory = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "Programs", "Pulse Weaver Public Preview");
    internal static readonly string AppPath = Path.Combine(InstallDirectory, "bin", "64bit", "PulseWeaverCore.exe");
    internal static readonly string UninstallerPath = Path.Combine(InstallDirectory, "Uninstall Pulse Weaver.exe");

    [STAThread]
    static int Main(string[] args)
    {
        ApplicationConfiguration.Initialize();
        if (args.Any(x => x.Equals("/test", StringComparison.OrdinalIgnoreCase))) return VerifyPayload();
        if (args.Any(x => x.Equals("/layout-test", StringComparison.OrdinalIgnoreCase))) return VerifyLayout();
        if (args.Any(x => x.Equals("/language-test", StringComparison.OrdinalIgnoreCase))) return VerifyLanguage();
        if (args.Any(x => x.Equals("/uninstall", StringComparison.OrdinalIgnoreCase))) return Uninstall();
        Application.Run(new SetupForm());
        return 0;
    }

    internal static void Install(bool desktopShortcut, bool launch, string languageCode, Action<int,string> progress)
    {
        if (Process.GetProcessesByName("PulseWeaverCore").Length > 0) throw new InvalidOperationException("Pulse Weaver is running. Close it, then try the installation again.");
        progress(8, "Creating the private application folder…"); Directory.CreateDirectory(InstallDirectory);
        var payloadFiles = PayloadFiles();
        progress(14, "Removing obsolete runtime files…"); CleanInstalledPayload(InstallDirectory, payloadFiles);
        progress(18, "Installing the Pulse Weaver native studio…"); ExtractPayload();
        File.WriteAllLines(Path.Combine(InstallDirectory, InstallManifestName), payloadFiles.OrderBy(path => path, StringComparer.OrdinalIgnoreCase));
        progress(70, "Verifying the isolated portable runtime…"); if (!File.Exists(AppPath)) throw new InvalidOperationException("The native application payload is incomplete.");
        progress(75, "Applying the interface language…"); WriteLanguage(languageCode);
        var current = Environment.ProcessPath ?? throw new InvalidOperationException("Setup could not locate itself."); File.Copy(current, UninstallerPath, true);
        progress(80, "Creating shortcuts…"); var startMenu = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.StartMenu), "Programs", "Pulse Weaver Public Preview.lnk"); CreateShortcut(startMenu, AppPath, "Pulse Weaver Public Preview"); if (desktopShortcut) CreateShortcut(Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.DesktopDirectory), "Pulse Weaver Public Preview.lnk"), AppPath, "Pulse Weaver Public Preview");
        progress(90, "Registering maintenance and update support…"); using (var key = Registry.CurrentUser.CreateSubKey(@"Software\Microsoft\Windows\CurrentVersion\Uninstall\PulseWeaverPublicPreview")) { key.SetValue("DisplayName", ProductName); key.SetValue("DisplayVersion", Version); key.SetValue("Publisher", "Pulse Weaver"); key.SetValue("InstallLocation", InstallDirectory); key.SetValue("DisplayIcon", AppPath); key.SetValue("UninstallString", '"' + UninstallerPath + '"' + " /uninstall"); key.SetValue("ModifyPath", '"' + UninstallerPath + '"'); key.SetValue("NoModify", 0, RegistryValueKind.DWord); key.SetValue("NoRepair", 0, RegistryValueKind.DWord); key.SetValue("EstimatedSize", InstalledSizeKb(InstallDirectory), RegistryValueKind.DWord); }
        progress(100, "Pulse Weaver is ready."); if (launch) Process.Start(new ProcessStartInfo(AppPath) { UseShellExecute = true, WorkingDirectory = Path.GetDirectoryName(AppPath)! });
    }

    internal static int Uninstall()
    {
        if (Process.GetProcessesByName("PulseWeaverCore").Length > 0) { MessageBox.Show("Close Pulse Weaver before uninstalling it.", ProductName, MessageBoxButtons.OK, MessageBoxIcon.Warning); return 1; }
        if (MessageBox.Show($"Remove Pulse Weaver {Version} from this PC?\n\nThis removes this portable copy, including its local settings. Back up the config folder first if you want to keep it.", "Uninstall Pulse Weaver", MessageBoxButtons.YesNo, MessageBoxIcon.Question) != DialogResult.Yes) return 0;
        TryDelete(Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.StartMenu), "Programs", "Pulse Weaver Public Preview.lnk")); TryDelete(Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.DesktopDirectory), "Pulse Weaver Public Preview.lnk")); Registry.CurrentUser.DeleteSubKeyTree(@"Software\Microsoft\Windows\CurrentVersion\Uninstall\PulseWeaverPublicPreview", false);
        var command = $"/c ping 127.0.0.1 -n 3 > nul & rmdir /s /q \"{InstallDirectory}\""; Process.Start(new ProcessStartInfo("cmd.exe", command) { CreateNoWindow = true, UseShellExecute = false, WindowStyle = ProcessWindowStyle.Hidden }); MessageBox.Show("Pulse Weaver was uninstalled. Its isolated local config folder was removed with the application.", ProductName, MessageBoxButtons.OK, MessageBoxIcon.Information); return 0;
    }

    static int VerifyPayload()
    {
        var root = Path.Combine(Path.GetTempPath(), "PulseWeaver-setup-test-" + Guid.NewGuid().ToString("N"));
        try
        {
            Directory.CreateDirectory(Path.Combine(root, "bin"));
            Directory.CreateDirectory(Path.Combine(root, "data"));
            Directory.CreateDirectory(Path.Combine(root, "config"));
            var staleBinary = Path.Combine(root, "bin", "obsolete-test.dll");
            var savedConfig = Path.Combine(root, "config", "preserved-test.ini");
            File.WriteAllText(staleBinary, "old");
            File.WriteAllText(savedConfig, "keep");
            var payloadFiles = PayloadFiles();
            CleanInstalledPayload(root, payloadFiles);
            if (File.Exists(staleBinary) || !File.Exists(savedConfig)) return 4;
            ExtractPayload(root);
            var app = Path.Combine(root, "bin", "64bit", "PulseWeaverCore.exe");
            using (var stream = File.OpenRead(app))
                if (stream.Length <= 1_000_000 || stream.ReadByte() != 'M' || stream.ReadByte() != 'Z') return 2;
            var manifest = Path.Combine(root, InstallManifestName);
            var manifestStale = Path.Combine(root, "bin", "manifest-obsolete-test.dll");
            File.WriteAllText(manifestStale, "old");
            File.WriteAllLines(manifest, payloadFiles.Append(Path.Combine("bin", "manifest-obsolete-test.dll")));
            CleanInstalledPayload(root, payloadFiles);
            if (File.Exists(manifestStale) || !File.Exists(app) || !File.Exists(savedConfig)) return 5;
            if (InstalledSizeKb(root) <= new FileInfo(app).Length / 1024) return 6;
            return 0;
        }
        catch { return 3; }
        finally { try { Directory.Delete(root, true); } catch { } }
    }

    static int VerifyLayout()
    {
        using var form = new SetupForm();
        form.StartPosition = FormStartPosition.Manual;
        form.Location = new Point(-10000, -10000);
        form.Opacity = 0;
        form.Show();
        Application.DoEvents();
        var result = form.LayoutCheckCode();
        form.Hide();
        return result;
    }

    static void ExtractPayload(string? destination = null)
    {
        destination ??= InstallDirectory;
        var root = Path.GetFullPath(destination) + Path.DirectorySeparatorChar;
        using var input = Assembly.GetExecutingAssembly().GetManifestResourceStream("PulseWeaver.Payload.zip") ?? throw new InvalidOperationException("Installer payload is incomplete.");
        using var archive = new ZipArchive(input, ZipArchiveMode.Read);
        foreach (var entry in archive.Entries)
        {
            var relative = entry.FullName.Replace('/', Path.DirectorySeparatorChar);
            if (string.IsNullOrWhiteSpace(relative)) continue;
            var target = Path.GetFullPath(Path.Combine(destination, relative));
            if (!target.StartsWith(root, StringComparison.OrdinalIgnoreCase)) throw new InvalidOperationException("Unsafe installer entry was rejected.");
            if (string.IsNullOrEmpty(entry.Name)) { Directory.CreateDirectory(target); continue; }
            Directory.CreateDirectory(Path.GetDirectoryName(target)!);
            entry.ExtractToFile(target, true);
        }
    }
    static HashSet<string> PayloadFiles()
    {
        using var input = Assembly.GetExecutingAssembly().GetManifestResourceStream("PulseWeaver.Payload.zip") ?? throw new InvalidOperationException("Installer payload is incomplete.");
        using var archive = new ZipArchive(input, ZipArchiveMode.Read);
        return archive.Entries.Where(entry => !string.IsNullOrEmpty(entry.Name))
            .Select(entry => entry.FullName.Replace('/', Path.DirectorySeparatorChar))
            .ToHashSet(StringComparer.OrdinalIgnoreCase);
    }
    static void CleanInstalledPayload(string installDirectory, HashSet<string> newPayloadFiles)
    {
        var root = Path.GetFullPath(installDirectory) + Path.DirectorySeparatorChar;
        var manifest = Path.Combine(installDirectory, InstallManifestName);
        if (File.Exists(manifest))
        {
            foreach (var relative in File.ReadLines(manifest).Where(path => !newPayloadFiles.Contains(path)))
            {
                var target = Path.GetFullPath(Path.Combine(installDirectory, relative));
                if (!target.StartsWith(root, StringComparison.OrdinalIgnoreCase)) throw new InvalidOperationException("Unsafe installed-file manifest entry was rejected.");
                if (File.Exists(target)) File.Delete(target);
            }
            return;
        }
        // Builds before 1.11.28 did not write a manifest. These directories are
        // wholly owned by Pulse Weaver; user profiles and scenes live in config.
        foreach (var directory in new[] { "bin", "data", "obs-plugins" })
        {
            var target = Path.Combine(installDirectory, directory);
            if (Directory.Exists(target)) Directory.Delete(target, true);
        }
    }
    static int InstalledSizeKb(string installDirectory)
    {
        try
        {
            var bytes = Directory.EnumerateFiles(installDirectory, "*", SearchOption.AllDirectories)
                .Sum(path => new FileInfo(path).Length);
            return (int)Math.Clamp((bytes + 1023L) / 1024L, 1L, int.MaxValue);
        }
        catch
        {
            return File.Exists(AppPath) ? (int)Math.Max(1, new FileInfo(AppPath).Length / 1024) : 1;
        }
    }
    internal static IReadOnlyList<LanguageOption> AvailableLanguages()
    {
        var languages = new List<LanguageOption>();
        using var input = Assembly.GetExecutingAssembly().GetManifestResourceStream("PulseWeaver.Payload.zip") ?? throw new InvalidOperationException("Installer payload is incomplete.");
        using var archive = new ZipArchive(input, ZipArchiveMode.Read);
        var localeIndex = archive.Entries.FirstOrDefault(entry => entry.FullName.Replace('\\', '/').EndsWith("data/obs-studio/locale.ini", StringComparison.OrdinalIgnoreCase));
        if (localeIndex is not null)
        {
            using var reader = new StreamReader(localeIndex.Open());
            string? code = null;
            while (reader.ReadLine() is { } line)
            {
                line = line.Trim();
                if (line.StartsWith('[') && line.EndsWith(']')) code = line[1..^1];
                else if (code is not null && line.StartsWith("Name=", StringComparison.OrdinalIgnoreCase))
                    languages.Add(new LanguageOption(code, line[5..].Trim()));
            }
        }
        if (!languages.Any(item => item.Code.Equals("en-US", StringComparison.OrdinalIgnoreCase)))
            languages.Add(new LanguageOption("en-US", "English"));
        return languages.OrderBy(item => item.Code.Equals("en-US", StringComparison.OrdinalIgnoreCase) ? 0 : item.Code.Equals("en-GB", StringComparison.OrdinalIgnoreCase) ? 1 : 2)
            .ThenBy(item => item.Name, StringComparer.CurrentCultureIgnoreCase).ToArray();
    }
    internal static string ConfiguredLanguage(string? installDirectory = null)
    {
        var path = Path.Combine(installDirectory ?? InstallDirectory, "config", "obs-studio", "global.ini");
        if (!File.Exists(path)) return "en-US";
        var inGeneral = false;
        foreach (var rawLine in File.ReadLines(path))
        {
            var line = rawLine.Trim();
            if (line.StartsWith('[') && line.EndsWith(']')) { inGeneral = line.Equals("[General]", StringComparison.OrdinalIgnoreCase); continue; }
            if (inGeneral && line.StartsWith("Language=", StringComparison.OrdinalIgnoreCase)) return line[9..].Trim();
        }
        return "en-US";
    }
    static void WriteLanguage(string languageCode, string? installDirectory = null)
    {
        var available = AvailableLanguages();
        if (!available.Any(item => item.Code.Equals(languageCode, StringComparison.OrdinalIgnoreCase))) languageCode = "en-US";
        var path = Path.Combine(installDirectory ?? InstallDirectory, "config", "obs-studio", "global.ini");
        Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        if (!WritePrivateProfileString("General", "Language", languageCode, path))
            throw new InvalidOperationException("Setup could not save the selected interface language.");
    }
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    static extern bool WritePrivateProfileString(string section, string key, string value, string filePath);
    static int VerifyLanguage()
    {
        var root = Path.Combine(Path.GetTempPath(), "PulseWeaver-language-test-" + Guid.NewGuid().ToString("N"));
        try
        {
            WriteLanguage("en-US", root);
            if (ConfiguredLanguage(root) != "en-US") return 51;
            WriteLanguage("fr-FR", root);
            return ConfiguredLanguage(root) == "fr-FR" ? 0 : 52;
        }
        catch { return 53; }
        finally { try { Directory.Delete(root, true); } catch { } }
    }
    static void TryDelete(string path) { try { if (File.Exists(path)) File.Delete(path); } catch { } }
    static void CreateShortcut(string shortcutPath, string targetPath, string description)
    {
        Directory.CreateDirectory(Path.GetDirectoryName(shortcutPath)!); var type = Type.GetTypeFromProgID("WScript.Shell") ?? throw new InvalidOperationException("Windows shortcut support is unavailable."); dynamic shell = Activator.CreateInstance(type)!; dynamic shortcut = shell.CreateShortcut(shortcutPath); shortcut.TargetPath = targetPath; shortcut.WorkingDirectory = Path.GetDirectoryName(targetPath); shortcut.Description = description; shortcut.IconLocation = targetPath + ",0"; shortcut.Save(); Marshal.FinalReleaseComObject(shortcut); Marshal.FinalReleaseComObject(shell);
    }
}

internal sealed record LanguageOption(string Code, string Name)
{
    public override string ToString() => Name;
}

internal sealed class SetupForm : Form
{
    readonly ProgressBar progress = new() { Minimum = 0, Maximum = 100, Height = 12, Dock = DockStyle.Top, Style = ProgressBarStyle.Continuous };
    readonly Label status = new() { Text = "Ready to install", ForeColor = Color.FromArgb(150, 166, 194), AutoSize = true };
    readonly Button install = new() { Text = "INSTALL PULSE WEAVER", Width = 205, Height = 44, BackColor = Color.FromArgb(126, 45, 190), ForeColor = Color.White, FlatStyle = FlatStyle.Flat };
    readonly Button cancel = new() { Text = "CANCEL", Width = 110, Height = 44, BackColor = Color.FromArgb(30, 38, 58), ForeColor = Color.White, FlatStyle = FlatStyle.Flat };
    readonly Button uninstall = new() { Text = "UNINSTALL", Width = 125, Height = 44, BackColor = Color.FromArgb(69, 25, 35), ForeColor = Color.White, FlatStyle = FlatStyle.Flat, Visible = false };
    readonly CheckBox desktop = new() { Text = "Create a desktop shortcut", Checked = true, AutoSize = true, ForeColor = Color.FromArgb(220, 226, 240) };
    readonly CheckBox launch = new() { Text = "Launch Pulse Weaver after installation", Checked = true, AutoSize = true, ForeColor = Color.FromArgb(220, 226, 240) };
    readonly ComboBox language = new() { Width = 260, DropDownStyle = ComboBoxStyle.DropDownList };

    public SetupForm()
    {
		var existingInstall = File.Exists(Program.AppPath);
		if (existingInstall) {
			var installedVersion = Registry.CurrentUser.OpenSubKey(@"Software\Microsoft\Windows\CurrentVersion\Uninstall\PulseWeaverPublicPreview")?.GetValue("DisplayVersion")?.ToString();
			var update = string.IsNullOrWhiteSpace(installedVersion) || !string.Equals(installedVersion, Program.Version, StringComparison.OrdinalIgnoreCase);
			install.Text = update ? $"UPDATE TO {Program.Version}" : "REPAIR PULSE WEAVER";
			uninstall.Visible = true;
			status.Text = update ? $"Pulse Weaver {installedVersion} is installed — update available" : $"Pulse Weaver {Program.Version} is installed — repair or uninstall";
		}
        Text = $"Pulse Weaver {Program.Version} Setup"; ClientSize = new Size(680, 600); MinimumSize = new Size(700, 640); StartPosition = FormStartPosition.CenterScreen; BackColor = Color.FromArgb(8, 11, 20); ForeColor = Color.White; Font = new Font("Segoe UI", 10); AutoScaleMode = AutoScaleMode.Dpi; FormBorderStyle = FormBorderStyle.Sizable; MaximizeBox = true;
        var root = new TableLayoutPanel { Dock = DockStyle.Fill, Padding = new Padding(30), RowCount = 6, ColumnCount = 1, BackColor = BackColor }; root.RowStyles.Add(new RowStyle(SizeType.Absolute, 76)); root.RowStyles.Add(new RowStyle(SizeType.Absolute, 102)); root.RowStyles.Add(new RowStyle(SizeType.Absolute, 88)); root.RowStyles.Add(new RowStyle(SizeType.Absolute, 110)); root.RowStyles.Add(new RowStyle(SizeType.Absolute, 50)); root.RowStyles.Add(new RowStyle(SizeType.Percent, 100));
        var brand = new Panel { Dock = DockStyle.Fill };
        var logo = Icon.ExtractAssociatedIcon(Application.ExecutablePath)?.ToBitmap();
        brand.Controls.Add(new PictureBox { Image = logo, SizeMode = PictureBoxSizeMode.Zoom, Size = new Size(56, 56), Location = new Point(0, 4) });
        brand.Controls.Add(new Label { Text = "PULSE WEAVER", Font = new Font("Segoe UI", 18, FontStyle.Bold), ForeColor = Color.White, AutoSize = true, Location = new Point(72, 6) });
        brand.Controls.Add(new Label { Text = $"STREAMING STUDIO  ·  {Program.Version} BETA", Font = new Font("Segoe UI", 8, FontStyle.Bold), ForeColor = Color.FromArgb(251, 146, 60), AutoSize = true, Location = new Point(74, 40) });
        root.Controls.Add(brand);
        var heading = new Panel { Dock = DockStyle.Fill }; heading.Controls.Add(new Label { Text = "Everything your show needs. Together.", Font = new Font("Segoe UI", 23, FontStyle.Bold), ForeColor = Color.White, AutoSize = true, Location = new Point(0, 4) }); heading.Controls.Add(new Label { Text = "Install the performer-first Lights, Camera and Action studio for this Windows account.", Font = new Font("Segoe UI", 10), ForeColor = Color.FromArgb(160, 175, 200), AutoSize = true, Location = new Point(2, 50) }); root.Controls.Add(heading);
        var destination = new Panel { Dock = DockStyle.Fill, BackColor = Color.FromArgb(17, 23, 39), Padding = new Padding(14) }; destination.Controls.Add(new Label { Text = "INSTALL LOCATION", Font = new Font("Segoe UI", 8, FontStyle.Bold), ForeColor = Color.FromArgb(34, 211, 238), AutoSize = true, Location = new Point(14, 12) }); destination.Controls.Add(new Label { Text = Program.InstallDirectory, ForeColor = Color.FromArgb(215, 224, 240), AutoEllipsis = true, Size = new Size(555, 25), Location = new Point(14, 38) }); root.Controls.Add(destination);
        foreach (var item in Program.AvailableLanguages()) language.Items.Add(item);
        var configuredLanguage = Program.ConfiguredLanguage();
        language.SelectedItem = language.Items.Cast<LanguageOption>().FirstOrDefault(item => item.Code.Equals(configuredLanguage, StringComparison.OrdinalIgnoreCase)) ?? language.Items.Cast<LanguageOption>().First(item => item.Code.Equals("en-US", StringComparison.OrdinalIgnoreCase));
        var languageRow = new FlowLayoutPanel { AutoSize = true, FlowDirection = FlowDirection.LeftToRight, WrapContents = false };
        languageRow.Controls.Add(new Label { Text = "Interface language", AutoSize = true, ForeColor = Color.FromArgb(220, 226, 240), Margin = new Padding(0, 5, 12, 0) }); languageRow.Controls.Add(language);
        var options = new FlowLayoutPanel { Dock = DockStyle.Fill, FlowDirection = FlowDirection.TopDown, Padding = new Padding(0, 8, 0, 0) }; options.Controls.Add(languageRow); options.Controls.Add(desktop); options.Controls.Add(launch); root.Controls.Add(options);
        var progressPanel = new Panel { Dock = DockStyle.Fill }; progressPanel.Controls.Add(progress); status.Location = new Point(0, 18); progressPanel.Controls.Add(status); root.Controls.Add(progressPanel);
        var actions = new FlowLayoutPanel { Dock = DockStyle.Fill, FlowDirection = FlowDirection.RightToLeft }; install.FlatAppearance.BorderColor = Color.FromArgb(244, 114, 182); cancel.FlatAppearance.BorderColor = Color.FromArgb(62, 75, 105); uninstall.FlatAppearance.BorderColor = Color.FromArgb(248, 113, 113); install.Click += async (_, _) => await InstallAsync(); cancel.Click += (_, _) => Close(); uninstall.Click += (_, _) => { if (Program.Uninstall() == 0) Close(); }; actions.Controls.Add(install); actions.Controls.Add(uninstall); actions.Controls.Add(cancel); root.Controls.Add(actions); Controls.Add(root);
    }

    async Task InstallAsync()
    {
        install.Enabled = cancel.Enabled = false; try { var selectedLanguage = (language.SelectedItem as LanguageOption)?.Code ?? "en-US"; await Task.Run(() => Program.Install(desktop.Checked, launch.Checked, selectedLanguage, (value, text) => BeginInvoke(() => { progress.Value = value; status.Text = text; }))); MessageBox.Show(this, $"Pulse Weaver {Program.Version} is installed and ready.", "Installation complete", MessageBoxButtons.OK, MessageBoxIcon.Information); Close(); } catch (Exception ex) { status.Text = "Installation failed"; MessageBox.Show(this, ex.Message, "Pulse Weaver setup", MessageBoxButtons.OK, MessageBoxIcon.Error); install.Enabled = cancel.Enabled = true; }
    }

    internal int LayoutCheckCode()
    {
        var actionArea = install.Parent;
        var progressArea = status.Parent;
        if (actionArea is null || progressArea is null) return 41;
        if (actionArea.ClientSize.Height < install.Height) return 42;
        if (actionArea.ClientSize.Height < cancel.Height) return 43;
        if (progressArea.ClientSize.Height < status.Bottom) return 44;
        if (language.SelectedItem is not LanguageOption || Program.AvailableLanguages().FirstOrDefault()?.Code != "en-US") return 45;
        return 0;
    }
}

