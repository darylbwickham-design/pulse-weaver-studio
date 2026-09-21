using Microsoft.Win32;
using System.Diagnostics;
using System.IO.Compression;
using System.Reflection;
using System.Runtime.InteropServices;

namespace PulseWeaver.Setup;

internal static class Program
{
    const string ProductName = "Pulse Weaver Streaming Studio";
    internal static readonly string Version = Assembly.GetExecutingAssembly().GetName().Version!.ToString(3);
    const string InstallManifestName = ".pulseweaver-installed-files.txt";
    internal static readonly string InstallDirectory = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "Programs", "Pulse Weaver");
    internal static readonly string AppPath = Path.Combine(InstallDirectory, "bin", "64bit", "PulseWeaverCore.exe");
    internal static readonly string UninstallerPath = Path.Combine(InstallDirectory, "Uninstall Pulse Weaver.exe");
    static bool upgradeSelfTest;

    [STAThread]
    static int Main(string[] args)
    {
        ApplicationConfiguration.Initialize();
        if (args.Length >= 2 && args[0].Equals("/upgrade-test",StringComparison.OrdinalIgnoreCase)) return VerifyUpgrade(args[1],args.Length>2?args[2]:null,args.Length>3?args[3]:null);
        if (args.Length==3 && args[0]=="/restore-test") {
            // Only allow the other installer's self-test to target its disposable tree.
            var root=Path.GetFullPath(args[1]);var parent=Directory.GetParent(root);
            if(parent is null || !parent.Name.StartsWith("PulseWeaver-upgrade-proof-",StringComparison.Ordinal) ||
               !parent.Parent!.FullName.Equals(Path.TrimEndingDirectorySeparator(Path.GetTempPath()),StringComparison.OrdinalIgnoreCase) || Path.GetFileName(root)!="Studio") return 64;
            try {using var held=Recovery.Acquire(root);Recovery.Restore(root,args[2],(_,_)=>{});return 0;}catch{return 65;}
        }
        if (args.Any(x => x.Equals("/migration-test", StringComparison.OrdinalIgnoreCase))) return LegacyCredentialMigration.VerifySyntheticMigration();
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
        using var operationLock = Recovery.Acquire(InstallDirectory);
        if (Recovery.Pending(InstallDirectory)) throw new IOException("Recover the interrupted operation before installing.");
        if (System.Version.TryParse(Recovery.InstalledVersion(InstallDirectory), out var installed) && installed > System.Version.Parse(Version))
            throw new IOException("A newer version is installed. Use RESTORE BACKUP to roll back app files and settings together; installing old binaries over newer settings is blocked.");
        InstallInto(InstallDirectory,languageCode,LegacyRegistrationExpected(),progress);
        var current = Environment.ProcessPath ?? throw new InvalidOperationException("Setup could not locate itself.");
        if (!Path.GetFullPath(current).Equals(UninstallerPath,StringComparison.OrdinalIgnoreCase)) File.Copy(current, UninstallerPath, true);
        progress(80, "Creating shortcuts…"); var startMenu = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.StartMenu), "Programs", "Pulse Weaver.lnk"); CreateShortcut(startMenu, AppPath, "Pulse Weaver"); if (desktopShortcut) CreateShortcut(Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.DesktopDirectory), "Pulse Weaver.lnk"), AppPath, "Pulse Weaver");
        progress(90, "Registering maintenance and update support…"); using (var key = Registry.CurrentUser.CreateSubKey(@"Software\Microsoft\Windows\CurrentVersion\Uninstall\PulseWeaver")) { key.SetValue("DisplayName", ProductName); key.SetValue("DisplayVersion", Version); key.SetValue("Publisher", "Pulse Weaver"); key.SetValue("InstallLocation", InstallDirectory); key.SetValue("DisplayIcon", AppPath); key.SetValue("UninstallString", '"' + UninstallerPath + '"' + " /uninstall"); key.SetValue("ModifyPath", '"' + UninstallerPath + '"'); key.SetValue("NoModify", 0, RegistryValueKind.DWord); key.SetValue("NoRepair", 0, RegistryValueKind.DWord); key.SetValue("EstimatedSize", InstalledSizeKb(InstallDirectory), RegistryValueKind.DWord); }
        progress(100, "Pulse Weaver is ready."); if (launch) Process.Start(new ProcessStartInfo(AppPath) { UseShellExecute = true, WorkingDirectory = Path.GetDirectoryName(AppPath)! });
    }

    internal static void InstallInto(string root, string languageCode, bool migrateCredentials, Action<int,string> progress)
    {
        var payloadFiles=PayloadFiles(); // Validate before changing any user state.
        var stage=Path.Combine(Recovery.BackupDirectory(root),"stage-"+Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(stage);
        try {
            if (Directory.Exists(root) && Directory.EnumerateFileSystemEntries(root).Any()) {
                progress(8,"Creating and verifying a full app + settings backup…"); Recovery.Backup(root);
                progress(25,"Staging the update without changing the installed copy…"); Recovery.CopyTree(root,stage);
            }
            LegacyCredentialMigration.MigrateBeforeUpgrade(stage,migrateCredentials);
            CleanInstalledPayload(stage,payloadFiles); ExtractPayload(stage); WriteUpdateIdentity(stage);
            payloadFiles.Add(Path.Combine("bin","64bit","pulseweaver-update.json"));
            File.WriteAllLines(Path.Combine(stage,InstallManifestName),payloadFiles.OrderBy(p=>p,StringComparer.OrdinalIgnoreCase));
            if(!File.Exists(Path.Combine(stage,"bin","64bit","PulseWeaverCore.exe"))) throw new IOException("Incomplete runtime payload.");
            if(!ConfiguredLanguage(stage).Equals(languageCode,StringComparison.OrdinalIgnoreCase)) WriteLanguage(languageCode,stage);
            if(!upgradeSelfTest && Process.GetProcessesByName("PulseWeaverCore").Length>0) throw new IOException("Pulse Weaver started during staging. Close it and retry; the installation has not been changed.");
            progress(70,"Committing the verified update with rollback protection…"); Recovery.Replace(root,stage);
        } finally { if(Directory.Exists(stage)) Directory.Delete(stage,true); }
    }

    internal static void Maintain(string operation, string? backup, Action<int,string> progress)
    {
        if(Process.GetProcessesByName("PulseWeaverCore").Length>0) throw new IOException("Close Pulse Weaver before backup or recovery.");
        using var operationLock=Recovery.Acquire(InstallDirectory);
        if(operation=="recover") { Recovery.RecoverInterrupted(InstallDirectory); progress(100,"Interrupted operation recovered. Reopen setup to continue."); return; }
        if(Recovery.Pending(InstallDirectory)) throw new IOException("Recover the interrupted operation first.");
        if(operation=="backup") { progress(10,"Backing up and verifying app files and settings…"); var saved=Recovery.Backup(InstallDirectory); progress(100,"Backup verified: "+saved); return; }
        var restoredVersion=Recovery.Restore(InstallDirectory,backup ?? throw new IOException("Choose a backup first."),progress);
        using var key=Registry.CurrentUser.OpenSubKey(@"Software\Microsoft\Windows\CurrentVersion\Uninstall\PulseWeaver",true);
        if(key is not null) key.SetValue("DisplayVersion",restoredVersion);
    }

    static void WriteUpdateIdentity(string destination)
    {
        var identity = new { schema = 1, channel = "windows-private", tag = "v" + Version };
        File.WriteAllText(Path.Combine(destination, "bin", "64bit", "pulseweaver-update.json"),
            System.Text.Json.JsonSerializer.Serialize(identity));
    }

    internal static int Uninstall()
    {
        if (Process.GetProcessesByName("PulseWeaverCore").Length > 0) { MessageBox.Show("Close Pulse Weaver before uninstalling it.", ProductName, MessageBoxButtons.OK, MessageBoxIcon.Warning); return 1; }
        if (MessageBox.Show($"Remove Pulse Weaver {Version} from this PC?\n\nThis removes this portable copy, including its local settings. Back up the config folder first if you want to keep it.", "Uninstall Pulse Weaver", MessageBoxButtons.YesNo, MessageBoxIcon.Question) != DialogResult.Yes) return 0;
        TryDelete(Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.StartMenu), "Programs", "Pulse Weaver.lnk")); TryDelete(Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.DesktopDirectory), "Pulse Weaver.lnk")); Registry.CurrentUser.DeleteSubKeyTree(@"Software\Microsoft\Windows\CurrentVersion\Uninstall\PulseWeaver", false);
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
            var savedScene = Path.Combine(root, "config", "obs-studio", "basic", "scenes", "existing.json");
            Directory.CreateDirectory(Path.GetDirectoryName(savedScene)!);
            const string sceneJson = "{\"sources\":[{\"name\":\"Existing portrait\",\"settings\":{\"items\":[{\"pos\":{\"x\":137.5,\"y\":412.5},\"rot\":23.5}]}}]}";
            File.WriteAllText(savedScene, sceneJson);
            var backup = BackupConfiguration(root);
            using (var archive = ZipFile.OpenRead(backup!))
            using (var reader = new StreamReader(archive.GetEntry("obs-studio/basic/scenes/existing.json")!.Open()))
                if (reader.ReadToEnd() != sceneJson) return 8;
            var payloadFiles = PayloadFiles();
            CleanInstalledPayload(root, payloadFiles);
            if (File.Exists(staleBinary) || !File.Exists(savedConfig)) return 4;
            ExtractPayload(root);
            if (File.ReadAllText(savedScene) != sceneJson || File.ReadAllText(savedConfig) != "keep") return 9;
            WriteUpdateIdentity(root);
            using (var identity = System.Text.Json.JsonDocument.Parse(File.ReadAllText(Path.Combine(root, "bin", "64bit", "pulseweaver-update.json"))))
                if (identity.RootElement.GetProperty("channel").GetString() != "windows-private" ||
                    identity.RootElement.GetProperty("tag").GetString() != "v" + Version) return 7;
            var app = Path.Combine(root, "bin", "64bit", "PulseWeaverCore.exe");
            using (var stream = File.OpenRead(app))
                if (stream.Length <= 1_000_000 || stream.ReadByte() != 'M' || stream.ReadByte() != 'Z') return 2;
            var manifest = Path.Combine(root, InstallManifestName);
            var manifestStale = Path.Combine(root, "bin", "manifest-obsolete-test.dll");
            File.WriteAllText(manifestStale, "old");
            File.WriteAllLines(manifest, payloadFiles.Append(Path.Combine("bin", "manifest-obsolete-test.dll"))
                .Append(Path.Combine("config", "obs-studio", "basic", "scenes", "existing.json")));
            CleanInstalledPayload(root, payloadFiles);
            if (File.Exists(manifestStale) || !File.Exists(app) || !File.Exists(savedConfig)) return 5;
            if (File.ReadAllText(savedScene) != sceneJson) return 10;
            if (InstalledSizeKb(root) <= new FileInfo(app).Length / 1024) return 6;
            return 0;
        }
        catch { return 3; }
        finally { try { Directory.Delete(root, true); } catch { } }
    }

    static int VerifyUpgrade(string previousZip,string? copiedConfig,string? recoveryExe)
    {
        var parent=Path.Combine(Path.GetTempPath(),"PulseWeaver-upgrade-proof-"+Guid.NewGuid().ToString("N"));
        var root=Path.Combine(parent,"Studio"); Directory.CreateDirectory(root);
        static Dictionary<string,string> Digests(string directory)=>Directory.EnumerateFiles(directory,"*",SearchOption.AllDirectories)
            .ToDictionary(f=>Path.GetRelativePath(directory,f),f=>Convert.ToHexString(System.Security.Cryptography.SHA256.HashData(File.ReadAllBytes(f))),StringComparer.OrdinalIgnoreCase);
        static bool Equal(Dictionary<string,string> a,Dictionary<string,string> b)=>a.Count==b.Count&&a.All(p=>b.TryGetValue(p.Key,out var hash)&&p.Value==hash);
        try {
            using(var archive=ZipFile.OpenRead(previousZip)) foreach(var entry in archive.Entries) {
                if(string.IsNullOrEmpty(entry.Name))continue;
                var file=Recovery.SafePath(root,entry.FullName);Directory.CreateDirectory(Path.GetDirectoryName(file)!);entry.ExtractToFile(file);
            }
            File.WriteAllText(Path.Combine(root,"bin","64bit","pulseweaver-update.json"),"{\"schema\":1,\"channel\":\"windows-private\",\"tag\":\"v1.12.9\"}");
            var config=Path.Combine(root,"config");
            if(copiedConfig is not null) Recovery.CopyTree(copiedConfig,config);
            else {Directory.CreateDirectory(config);File.WriteAllText(Path.Combine(config,"preserved.json"),"{\"scene\":\"existing\",\"x\":137.5,\"rotation\":23.5}");}
            var before=Digests(root);var configBefore=Digests(config);
            upgradeSelfTest=true;
            try { using(Recovery.Acquire(root)) InstallInto(root,ConfiguredLanguage(root),false,(_,_)=>{}); }
            finally { upgradeSelfTest=false; }
            if(!Equal(configBefore,Digests(config)))throw new IOException("Upgrade changed copied configuration bytes.");
            if(Recovery.InstalledVersion(root)!=Version)throw new IOException("Upgrade identity mismatch.");
            var saved=Directory.GetFiles(Recovery.BackupDirectory(root),"*.zip").Single();
            if(recoveryExe is null) {using var held=Recovery.Acquire(root);Recovery.Restore(root,saved,(_,_)=>{});}
            else {
                var start=new ProcessStartInfo(recoveryExe) {UseShellExecute=false,CreateNoWindow=true,WindowStyle=ProcessWindowStyle.Hidden};
                start.ArgumentList.Add("/restore-test");start.ArgumentList.Add(root);start.ArgumentList.Add(saved);
                using var recovery=Process.Start(start)!;recovery.WaitForExit();if(recovery.ExitCode!=0)throw new IOException("Separate recovery installer test failed: "+recovery.ExitCode);
            }
            if(!Equal(before,Digests(root)))throw new IOException("Restored runtime/configuration differs from pre-upgrade state.");
            File.WriteAllText(Path.Combine(AppContext.BaseDirectory,"upgrade-test-result.txt"),$"PASS: 1.12.9 -> {Version} -> full restore{(recoveryExe is null?"":" using separate recovery EXE")}; {configBefore.Count} config files and {before.Count} total files preserved byte-for-byte. No installed files or registry changed.");
            return 0;
        }catch(Exception ex){File.WriteAllText(Path.Combine(AppContext.BaseDirectory,"upgrade-test-result.txt"),"FAIL: "+ex);return 61;}
        finally{try{Directory.Delete(parent,true);}catch{}}
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

    static string? BackupConfiguration(string installDirectory)
    {
        var config = Path.Combine(installDirectory, "config");
        if (!Directory.Exists(config)) return null;
        var backups = Path.Combine(installDirectory, "config-backups");
        Directory.CreateDirectory(backups);
        var backup = Path.Combine(backups, $"pre-upgrade-{DateTime.UtcNow:yyyyMMdd-HHmmss}-{Guid.NewGuid():N}.zip");
        // Fail the upgrade before changing runtime/configuration if a backup cannot be completed.
        ZipFile.CreateFromDirectory(config, backup, CompressionLevel.Optimal, false);
        return backup;
    }
    static bool IsProtectedConfiguration(string root, string target)
    {
        foreach (var name in new[] { "config", "config-backups" })
        {
            var protectedRoot = Path.GetFullPath(Path.Combine(root, name));
            if (target.Equals(protectedRoot, StringComparison.OrdinalIgnoreCase) ||
                target.StartsWith(protectedRoot + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase)) return true;
        }
        return false;
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
            if (IsProtectedConfiguration(destination, target)) throw new InvalidOperationException("Installer payload must not contain user configuration.");
            if (string.IsNullOrEmpty(entry.Name)) { Directory.CreateDirectory(target); continue; }
            Directory.CreateDirectory(Path.GetDirectoryName(target)!);
            entry.ExtractToFile(target, true);
        }
    }
    static HashSet<string> PayloadFiles()
    {
        using var input = Assembly.GetExecutingAssembly().GetManifestResourceStream("PulseWeaver.Payload.zip") ?? throw new InvalidOperationException("Installer payload is incomplete.");
        using var archive = new ZipArchive(input, ZipArchiveMode.Read);
        var root = Path.GetFullPath(InstallDirectory) + Path.DirectorySeparatorChar;
        foreach (var entry in archive.Entries)
        {
            var target = Path.GetFullPath(Path.Combine(root, entry.FullName.Replace('/', Path.DirectorySeparatorChar)));
            if (!target.StartsWith(root, StringComparison.OrdinalIgnoreCase) || IsProtectedConfiguration(root, target))
                throw new InvalidOperationException("Installer payload contains an unsafe or protected configuration path.");
        }
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
                if (IsProtectedConfiguration(installDirectory, target)) continue;
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
        try
        {
            if (!File.Exists(path)) return "en-US";
            var inGeneral = false;
            foreach (var rawLine in File.ReadLines(path))
            {
                var line = rawLine.Trim();
                if (line.StartsWith('[') && line.EndsWith(']')) { inGeneral = line.Equals("[General]", StringComparison.OrdinalIgnoreCase); continue; }
                if (inGeneral && line.StartsWith("Language=", StringComparison.OrdinalIgnoreCase)) return line[9..].Trim();
            }
        }
        catch (IOException) { }
        catch (UnauthorizedAccessException) { }
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
    static bool LegacyRegistrationExpected()
    {
        try
        {
            var value = Registry.CurrentUser.OpenSubKey(@"Software\Microsoft\Windows\CurrentVersion\Uninstall\PulseWeaver")
                                      ?.GetValue("DisplayVersion")?.ToString();
            return System.Version.TryParse(value, out var installed) &&
                   installed.CompareTo(new System.Version(1, 11, 53)) < 0;
        }
        catch { return false; }
    }
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
    readonly Button backup = new() { Text = "BACK UP NOW", AutoSize = true, Height = 40 };
    readonly Button restore = new() { Text = "RESTORE BACKUP / ROLL BACK", AutoSize = true, Height = 40 };
    readonly Label recoveryHint = new() { AutoSize = true, MaximumSize = new Size(700, 0), ForeColor = Color.FromArgb(180,195,215) };
    bool busy;
    readonly bool downgrade;

    public SetupForm()
    {
		var existingInstall = File.Exists(Program.AppPath);
		downgrade = System.Version.TryParse(Recovery.InstalledVersion(Program.InstallDirectory),out var currentVersion) && currentVersion > System.Version.Parse(Program.Version);
		if (existingInstall) {
			var installedVersion = Registry.CurrentUser.OpenSubKey(@"Software\Microsoft\Windows\CurrentVersion\Uninstall\PulseWeaver")?.GetValue("DisplayVersion")?.ToString();
			var update = string.IsNullOrWhiteSpace(installedVersion) || !string.Equals(installedVersion, Program.Version, StringComparison.OrdinalIgnoreCase);
			install.Text = downgrade ? "ROLL BACK VIA BACKUP" : update ? $"UPDATE TO {Program.Version}" : "REPAIR PULSE WEAVER";
			uninstall.Visible = true;
			status.Text = update ? $"Pulse Weaver {installedVersion} is installed — update available" : $"Pulse Weaver {Program.Version} is installed — repair or uninstall";
		}
        Text = $"Pulse Weaver {Program.Version} Setup & Recovery"; ClientSize = new Size(780, 760); MinimumSize = new Size(800, 800); StartPosition = FormStartPosition.CenterScreen; BackColor = Color.FromArgb(8, 11, 20); ForeColor = Color.White; Font = new Font("Segoe UI", 10); AutoScaleMode = AutoScaleMode.Dpi; FormBorderStyle = FormBorderStyle.Sizable; MaximizeBox = true;
        var root = new TableLayoutPanel { Dock = DockStyle.Fill, Padding = new Padding(30), RowCount = 7, ColumnCount = 1, BackColor = BackColor }; root.RowStyles.Add(new RowStyle(SizeType.Absolute, 76)); root.RowStyles.Add(new RowStyle(SizeType.Absolute, 102)); root.RowStyles.Add(new RowStyle(SizeType.Absolute, 88)); root.RowStyles.Add(new RowStyle(SizeType.Absolute, 110)); root.RowStyles.Add(new RowStyle(SizeType.Absolute, 125)); root.RowStyles.Add(new RowStyle(SizeType.Absolute, 70)); root.RowStyles.Add(new RowStyle(SizeType.Percent, 100));
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
        recoveryHint.Text="Updates automatically back up app files, scenes, overlays and settings. Restore replaces app + settings together. Backups contain private account data: keep them private on this Windows account.\nSaved outside the install folder: " + Recovery.BackupDirectory(Program.InstallDirectory);
        var recoveryPanel=new FlowLayoutPanel { Dock=DockStyle.Fill, FlowDirection=FlowDirection.TopDown, WrapContents=false };
        var recoveryButtons=new FlowLayoutPanel { AutoSize=true, WrapContents=false }; recoveryButtons.Controls.Add(backup); recoveryButtons.Controls.Add(restore);
        recoveryPanel.Controls.Add(recoveryButtons); recoveryPanel.Controls.Add(recoveryHint); root.Controls.Add(recoveryPanel);
        backup.Enabled=existingInstall; backup.Click+=async (_,_)=>await MaintenanceAsync("backup"); restore.Click+=async (_,_)=>await RestoreAsync();
        status.AutoSize=false; status.Size=new Size(710,45); status.AutoEllipsis=true;
        if(Recovery.Pending(Program.InstallDirectory)) {install.Text="RECOVER INTERRUPTED SETUP";status.Text="An interrupted operation must be recovered before continuing.";}
        else if(downgrade) status.Text="A newer version is installed. Select its pre-upgrade backup to roll back safely.";
        FormClosing+=(_,e)=>{ if(busy)e.Cancel=true; };
        var progressPanel = new Panel { Dock = DockStyle.Fill }; progressPanel.Controls.Add(progress); status.Location = new Point(0, 18); progressPanel.Controls.Add(status); root.Controls.Add(progressPanel);
        var actions = new FlowLayoutPanel { Dock = DockStyle.Fill, FlowDirection = FlowDirection.RightToLeft }; install.FlatAppearance.BorderColor = Color.FromArgb(244, 114, 182); cancel.FlatAppearance.BorderColor = Color.FromArgb(62, 75, 105); uninstall.FlatAppearance.BorderColor = Color.FromArgb(248, 113, 113); install.Click += async (_, _) => await InstallAsync(); cancel.Click += (_, _) => Close(); uninstall.Click += (_, _) => { if (Program.Uninstall() == 0) Close(); }; actions.Controls.Add(install); actions.Controls.Add(uninstall); actions.Controls.Add(cancel); root.Controls.Add(actions); Controls.Add(root);
    }

    async Task InstallAsync()
    {
        if(Recovery.Pending(Program.InstallDirectory)) {await MaintenanceAsync("recover");return;}
        if(downgrade) {await RestoreAsync();return;}
        var selectedLanguage=(language.SelectedItem as LanguageOption)?.Code ?? "en-US";
        var createDesktop=desktop.Checked; var launchAfter=launch.Checked;
        await RunAsync(()=>Program.Install(createDesktop,launchAfter,selectedLanguage,Report),"Installation complete. Your recovery backups are in:\n"+Recovery.BackupDirectory(Program.InstallDirectory));
    }

    void Report(int value,string text)=>BeginInvoke(()=>{progress.Value=value;status.Text=text;});
    async Task MaintenanceAsync(string operation,string? selected=null)=>await RunAsync(()=>Program.Maintain(operation,selected,Report),
        operation=="backup" ? "Backup created and verified in:\n"+Recovery.BackupDirectory(Program.InstallDirectory) : "Recovery complete. App files and settings have been restored together. Reopen setup before another operation.");
    async Task RestoreAsync()
    {
        if(Recovery.Pending(Program.InstallDirectory)) {await MaintenanceAsync("recover");return;}
        using var picker=new OpenFileDialog {Title="Choose the pre-upgrade Pulse Weaver recovery backup",Filter="Pulse Weaver full backup (*.zip)|*.zip",InitialDirectory=Recovery.BackupDirectory(Program.InstallDirectory)};
        if(picker.ShowDialog(this)!=DialogResult.OK)return;
        try {
            // Full integrity validation occurs again on the worker before replacement.
            SetBusy(true);
            Recovery.Snapshot snapshot;
            try {snapshot=await Task.Run(()=>Recovery.Inspect(picker.FileName));} finally {SetBusy(false);}
            if(MessageBox.Show(this,$"Restore Pulse Weaver {snapshot.Version} and all its saved settings from {snapshot.CreatedUtc.ToLocalTime():g}?\n\nThis replaces the current app, scenes, overlays and settings. Changes made since this backup will no longer be active. The current state is backed up first.\n\nOnly restore backups you trust; app files will be restored too.","Confirm rollback and restore",MessageBoxButtons.YesNo,MessageBoxIcon.Warning)!=DialogResult.Yes)return;
            await MaintenanceAsync("restore",picker.FileName);
        }catch(Exception ex){MessageBox.Show(this,ex.Message,"Cannot restore backup",MessageBoxButtons.OK,MessageBoxIcon.Error);}
    }
    async Task RunAsync(Action action,string success)
    {
        SetBusy(true);
        try {await Task.Run(action);MessageBox.Show(this,success,"Pulse Weaver maintenance",MessageBoxButtons.OK,MessageBoxIcon.Information);busy=false;Close();}
        catch(Exception ex){status.Text="Operation stopped. Backups and recovery data were retained.";MessageBox.Show(this,ex.Message,"Pulse Weaver maintenance",MessageBoxButtons.OK,MessageBoxIcon.Error);}
        finally {if(!IsDisposed)SetBusy(false);}
    }
    void SetBusy(bool value) {busy=value;install.Enabled=cancel.Enabled=uninstall.Enabled=restore.Enabled=language.Enabled=desktop.Enabled=launch.Enabled=!value;backup.Enabled=!value&&File.Exists(Program.AppPath);}

    internal int LayoutCheckCode()
    {
        var actionArea = install.Parent;
        var progressArea = status.Parent;
        if (actionArea is null || progressArea is null) return 41;
        if (actionArea.ClientSize.Height < install.Height) return 42;
        if (actionArea.ClientSize.Height < cancel.Height) return 43;
        if (progressArea.ClientSize.Height < status.Bottom) return 44;
        if (language.SelectedItem is not LanguageOption || Program.AvailableLanguages().FirstOrDefault()?.Code != "en-US") return 45;
        if(recoveryHint.Parent is null || recoveryHint.Bottom>recoveryHint.Parent.ClientSize.Height) return 46;
        return 0;
    }
}
