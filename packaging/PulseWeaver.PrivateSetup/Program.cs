using Microsoft.Win32;
using System.Diagnostics;
using System.IO.Compression;
using System.Reflection;
using System.Runtime.InteropServices;

namespace PulseWeaver.Setup;

internal static class Program
{
#if PULSE_MOTION_PREVIEW
    internal const string ProductName = "Pulse Weaver Motion Preview";
    internal const string ProductLabel = "Pulse Weaver Motion Preview";
    const string InstallFolderName = "Pulse Weaver Motion Preview";
    const string ShortcutFileName = "Pulse Weaver Motion Preview.lnk";
    const string RegistryProductKey = "PulseWeaverMotionPreview";
    const string UpdateChannel = "windows-motion-preview";
    internal const string InstallerSubtitle = "MOTION PREVIEW";
#else
    internal const string ProductName = "Pulse Weaver Streaming Studio";
    internal const string ProductLabel = "Pulse Weaver";
    const string InstallFolderName = "Pulse Weaver";
    const string ShortcutFileName = "Pulse Weaver.lnk";
    const string RegistryProductKey = "PulseWeaver";
#if PULSE_ALPHA
    const string UpdateChannel = "windows-alpha";
    internal const string InstallerSubtitle = "STREAMING STUDIO  ·  EXPERIMENTAL ALPHA";
#elif PULSE_UNSTABLE
    const string UpdateChannel = "windows-unstable";
    internal const string InstallerSubtitle = "STREAMING STUDIO  ·  UNSTABLE SHOWCASE";
#else
    const string UpdateChannel = "windows-private";
    internal const string InstallerSubtitle = "STREAMING STUDIO  ·  RELEASE";
#endif
#endif
    internal static readonly string Version = Assembly.GetExecutingAssembly().GetName().Version!.ToString(3);
#if PULSE_ALPHA
    internal static readonly string ReleaseTag = "v" + Version + "-alpha." + Assembly.GetExecutingAssembly()
        .GetCustomAttributes<AssemblyMetadataAttribute>().Single(a => a.Key == "AlphaRevision").Value;
#elif PULSE_UNSTABLE
    internal static readonly string ReleaseTag = "v" + Version + "-unstable." + Assembly.GetExecutingAssembly()
        .GetCustomAttributes<AssemblyMetadataAttribute>().Single(a => a.Key == "AlphaRevision").Value;
#else
    internal static readonly string ReleaseTag = "v" + Version;
#endif
    const string InstallManifestName = ".pulseweaver-installed-files.txt";
    internal static readonly string InstallDirectory = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "Programs", InstallFolderName);
    internal static readonly string AppPath = Path.Combine(InstallDirectory, "bin", "64bit", "PulseWeaverCore.exe");
    internal static readonly string UninstallerPath = Path.Combine(InstallDirectory, $"Uninstall {ProductLabel}.exe");
    internal static readonly string UninstallRegistryPath = @"Software\Microsoft\Windows\CurrentVersion\Uninstall\" + RegistryProductKey;
    static bool upgradeSelfTest;

    [STAThread]
    static int Main(string[] args)
    {
        ApplicationConfiguration.Initialize();
        try { if (DetachedLaunch.Bootstrap(args)) return 0; }
        catch (Exception ex) { MessageBox.Show(ex.Message, ProductLabel + " setup", MessageBoxButtons.OK, MessageBoxIcon.Error); return 1; }
        if (args.Length >= 2 && args[0].Equals("/upgrade-test",StringComparison.OrdinalIgnoreCase)) return VerifyUpgrade(args[1],args.Length>2?args[2]:null,args.Length>3?args[3]:null);
        if (args.Length==3 && args[0]=="/restore-test") {
            // Only allow the other installer's self-test to target its disposable tree.
            var root=Path.GetFullPath(args[1]);var parent=Directory.GetParent(root);
            if(parent is null || !parent.Name.StartsWith("PulseWeaver-upgrade-proof-",StringComparison.Ordinal) ||
               !parent.Parent!.FullName.Equals(Path.TrimEndingDirectorySeparator(Path.GetTempPath()),StringComparison.OrdinalIgnoreCase) || Path.GetFileName(root)!="Studio") return 64;
            try {using var held=Recovery.Acquire(root);Recovery.Restore(root,args[2],(_,_)=>{});return 0;}catch{return 65;}
        }
        if (args.Any(x => x.Equals("/migration-test", StringComparison.OrdinalIgnoreCase))) return LegacyCredentialMigration.VerifySyntheticMigration();
        if (args.Any(x => x.Equals("/adopt-test", StringComparison.OrdinalIgnoreCase))) return ProfileAdoption.VerifySyntheticAdoption();
        if (args.Length == 2 && args[0].Equals("/adopt-profile", StringComparison.OrdinalIgnoreCase)) {
            try {
                if (Process.GetProcessesByName("PulseWeaverCore").Length > 0)
                    throw new IOException("Close every Pulse Weaver window before importing the isolated profile.");
                var backup = ProfileAdoption.Adopt(args[1], InstallDirectory);
                File.WriteAllText(Path.Combine(Recovery.BackupDirectory(InstallDirectory), "last-profile-adoption.txt"),
                    "Isolated profile copied into the installed app. The previous complete installation is backed up at " + backup);
                return 0;
            } catch (Exception ex) {
                MessageBox.Show(ex.Message, "Profile import failed", MessageBoxButtons.OK, MessageBoxIcon.Error);
                return 1;
            }
        }
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
        if (Recovery.RequiresRestore(InstallDirectory, ReleaseTag))
            throw new IOException("A newer version is installed. Use RESTORE BACKUP to roll back app files and settings together; installing old binaries over newer settings is blocked.");
        InstallInto(InstallDirectory,languageCode,LegacyRegistrationExpected(),progress);
        var current = Environment.ProcessPath ?? throw new InvalidOperationException("Setup could not locate itself.");
        if (!Path.GetFullPath(current).Equals(UninstallerPath,StringComparison.OrdinalIgnoreCase)) File.Copy(current, UninstallerPath, true);
        progress(80, "Creating shortcuts…"); var startMenu = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.StartMenu), "Programs", ShortcutFileName); CreateShortcut(startMenu, AppPath, ProductLabel); if (desktopShortcut) CreateShortcut(Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.DesktopDirectory), ShortcutFileName), AppPath, ProductLabel);
        progress(90, "Registering maintenance and update support…"); using (var key = Registry.CurrentUser.CreateSubKey(UninstallRegistryPath)) { key.SetValue("DisplayName", ProductName); key.SetValue("DisplayVersion", Version); key.SetValue("Publisher", "Pulse Weaver"); key.SetValue("InstallLocation", InstallDirectory); key.SetValue("DisplayIcon", AppPath); key.SetValue("UninstallString", '"' + UninstallerPath + '"' + " /uninstall"); key.SetValue("ModifyPath", '"' + UninstallerPath + '"'); key.SetValue("NoModify", 0, RegistryValueKind.DWord); key.SetValue("NoRepair", 0, RegistryValueKind.DWord); key.SetValue("EstimatedSize", InstalledSizeKb(InstallDirectory), RegistryValueKind.DWord); }
        progress(100, ProductLabel + " is ready."); if (launch) Process.Start(new ProcessStartInfo(AppPath) { UseShellExecute = true, WorkingDirectory = Path.GetDirectoryName(AppPath)! });
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
        using var key=Registry.CurrentUser.OpenSubKey(UninstallRegistryPath,true);
        if(key is not null) key.SetValue("DisplayVersion",restoredVersion);
    }

    static void WriteUpdateIdentity(string destination)
    {
        var identity = new { schema = 1, channel = UpdateChannel, tag = ReleaseTag };
        File.WriteAllText(Path.Combine(destination, "bin", "64bit", "pulseweaver-update.json"),
            System.Text.Json.JsonSerializer.Serialize(identity));
    }

    internal static int Uninstall()
    {
        if (Process.GetProcessesByName("PulseWeaverCore").Length > 0) { MessageBox.Show("Close Pulse Weaver before uninstalling it.", ProductName, MessageBoxButtons.OK, MessageBoxIcon.Warning); return 1; }
        if (MessageBox.Show($"Remove {ProductLabel} {Version} from this PC?\n\nThis removes this portable copy, including its local settings. Back up the config folder first if you want to keep it.", "Uninstall " + ProductLabel, MessageBoxButtons.YesNo, MessageBoxIcon.Question) != DialogResult.Yes) return 0;
        TryDelete(Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.StartMenu), "Programs", ShortcutFileName)); TryDelete(Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.DesktopDirectory), ShortcutFileName)); Registry.CurrentUser.DeleteSubKeyTree(UninstallRegistryPath, false);
        var command = $"/c ping 127.0.0.1 -n 3 > nul & rmdir /s /q \"{InstallDirectory}\""; Process.Start(new ProcessStartInfo("cmd.exe", command) { CreateNoWindow = true, UseShellExecute = false, WindowStyle = ProcessWindowStyle.Hidden }); MessageBox.Show(ProductLabel + " was uninstalled. Its isolated local config folder was removed with the application.", ProductName, MessageBoxButtons.OK, MessageBoxIcon.Information); return 0;
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
            using (var registration = System.Text.Json.JsonDocument.Parse(File.ReadAllText(Path.Combine(root, "data", "pulse-weaver", "youtube-desktop-client.json")))) {
                var document = registration.RootElement;
                var client = document.GetProperty("installed");
                var fields = client.EnumerateObject().Select(property => property.Name).ToHashSet(StringComparer.Ordinal);
                bool validFields = fields.SetEquals(new[] { "client_id", "client_secret" });
                if (document.EnumerateObject().Count() != 1 || !validFields ||
                    !(client.GetProperty("client_id").GetString()?.EndsWith(".apps.googleusercontent.com", StringComparison.Ordinal) ?? false) ||
                    string.IsNullOrWhiteSpace(client.GetProperty("client_secret").GetString())) return 11;
            }
            if (File.ReadAllText(savedScene) != sceneJson || File.ReadAllText(savedConfig) != "keep") return 9;
            WriteUpdateIdentity(root);
            using (var identity = System.Text.Json.JsonDocument.Parse(File.ReadAllText(Path.Combine(root, "bin", "64bit", "pulseweaver-update.json"))))
                if (identity.RootElement.GetProperty("channel").GetString() != UpdateChannel ||
                    identity.RootElement.GetProperty("tag").GetString() != ReleaseTag) return 7;
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
            var identityPath = Path.Combine(root,"bin","64bit","pulseweaver-update.json");
            if (!File.Exists(identityPath)) File.WriteAllText(identityPath,System.Text.Json.JsonSerializer.Serialize(new { schema=1,channel="windows-private",tag="v1.12.15" }));
            var previousVersion = Recovery.InstalledVersion(root);
            var config=Path.Combine(root,"config");
            if(copiedConfig is not null) Recovery.CopyTree(copiedConfig,config);
            else {
                Directory.CreateDirectory(config);
                File.WriteAllText(Path.Combine(config,"preserved.json"),"{\"scene\":\"existing\",\"x\":137.5,\"rotation\":23.5}");
                var api = Path.Combine(config,"obs-studio","plugin_config","pulse-weaver-core"); Directory.CreateDirectory(api);
                File.WriteAllText(Path.Combine(api,"pulse-weaver.ini"),"[api]\nport=19755\ntoken=synthetic-retained-token\n");
                var credentials = Path.Combine(config,"pulseweaver"); Directory.CreateDirectory(credentials);
                File.WriteAllText(Path.Combine(credentials,"app-credentials.ini"),"[youtube]\nrefresh_token=synthetic-opaque-credential\n");
                File.WriteAllText(Path.Combine(credentials,"updates.ini"),"[General]\nincludeAlpha=true\n");
            }
            var before=Digests(root);var configBefore=Digests(config);
            upgradeSelfTest=true;
            try { using(Recovery.Acquire(root)) InstallInto(root,ConfiguredLanguage(root),false,(_,_)=>{}); }
            finally { upgradeSelfTest=false; }
            if(!Equal(configBefore,Digests(config)))throw new IOException("Upgrade changed copied configuration bytes.");
            if(Recovery.InstalledVersion(root)!=ReleaseTag.TrimStart('v'))throw new IOException("Upgrade identity mismatch.");
            var saved=Directory.GetFiles(Recovery.BackupDirectory(root),"*.zip").Single();
            if(recoveryExe is null) {using var held=Recovery.Acquire(root);Recovery.Restore(root,saved,(_,_)=>{});}
            else {
                var start=new ProcessStartInfo(recoveryExe) {UseShellExecute=false,CreateNoWindow=true,WindowStyle=ProcessWindowStyle.Hidden};
                start.ArgumentList.Add("/restore-test");start.ArgumentList.Add(root);start.ArgumentList.Add(saved);
                using var recovery=Process.Start(start)!;recovery.WaitForExit();if(recovery.ExitCode!=0)throw new IOException("Separate recovery installer test failed: "+recovery.ExitCode);
            }
            if(!Equal(before,Digests(root)))throw new IOException("Restored runtime/configuration differs from pre-upgrade state.");
            File.WriteAllText(Path.Combine(AppContext.BaseDirectory,"upgrade-test-result.txt"),$"PASS: {previousVersion} -> {ReleaseTag} -> full restore{(recoveryExe is null?"":" using separate recovery EXE")}; {configBefore.Count} config files and {before.Count} total files preserved byte-for-byte. No installed files or registry changed.");
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
        if (result == 0) result = form.AgreementCheckCode();
        if (Environment.GetEnvironmentVariable("PULSE_SETUP_PREVIEW") is { Length: > 0 } preview) {
            using var bitmap = new Bitmap(form.Width, form.Height);
            form.DrawToBitmap(bitmap, new Rectangle(Point.Empty, form.Size));
            bitmap.Save(preview, System.Drawing.Imaging.ImageFormat.Png);
        }
        foreach (var size in new[] { form.MinimumSize, new Size(1040, 880) }) {
            form.Size = size; Application.DoEvents();
            if (result == 0) result = form.LayoutCheckCode();
        }
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
        => PayloadPolicy.IsProtectedConfiguration(root, target);
    static void ExtractPayload(string? destination = null)
    {
        destination ??= InstallDirectory;
        using var input = Assembly.GetExecutingAssembly().GetManifestResourceStream("PulseWeaver.Payload.zip") ?? throw new InvalidOperationException("Installer payload is incomplete.");
        using var archive = new ZipArchive(input, ZipArchiveMode.Read);
        PayloadPolicy.Validate(archive, destination);
        foreach (var entry in archive.Entries)
        {
            var target = PayloadPolicy.Target(destination, entry);
            if (entry.FullName.EndsWith('/') || entry.FullName.EndsWith('\\')) { Directory.CreateDirectory(target); continue; }
            Directory.CreateDirectory(Path.GetDirectoryName(target)!);
            entry.ExtractToFile(target, true);
        }
    }
    static HashSet<string> PayloadFiles()
    {
        using var input = Assembly.GetExecutingAssembly().GetManifestResourceStream("PulseWeaver.Payload.zip") ?? throw new InvalidOperationException("Installer payload is incomplete.");
        using var archive = new ZipArchive(input, ZipArchiveMode.Read);
        return PayloadPolicy.Validate(archive, InstallDirectory);
    }
    static void CleanInstalledPayload(string installDirectory, HashSet<string> newPayloadFiles)
    {
        var root = Path.GetFullPath(installDirectory) + Path.DirectorySeparatorChar;
        var manifest = Path.Combine(installDirectory, InstallManifestName);
        if (File.Exists(manifest))
        {
            foreach (var relative in File.ReadLines(manifest).Where(path => !newPayloadFiles.Contains(path)))
            {
                var target = Recovery.SafePath(installDirectory, relative);
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
            var value = Registry.CurrentUser.OpenSubKey(UninstallRegistryPath)
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

internal sealed class SetupButton : Button
{
    protected override void OnPaint(PaintEventArgs e)
    {
        base.OnPaint(e);
        if (Enabled) return;
        using var background = new SolidBrush(BackColor);
        e.Graphics.FillRectangle(background, ClientRectangle);
        TextRenderer.DrawText(e.Graphics, Text, Font, ClientRectangle, Color.FromArgb(157, 168, 190),
            TextFormatFlags.HorizontalCenter | TextFormatFlags.VerticalCenter | TextFormatFlags.EndEllipsis);
    }
}

internal sealed class SetupForm : Form
{
    readonly ProgressBar progress = new() { Minimum = 0, Maximum = 100, Height = 12, Dock = DockStyle.Top, Style = ProgressBarStyle.Continuous };
    readonly Label status = new() { Text = "Ready to install", ForeColor = Color.FromArgb(150, 166, 194), AutoSize = true };
    readonly Button install = new SetupButton() { Text = "INSTALL " + Program.ProductLabel.ToUpperInvariant(), Width = 260, Height = 44, BackColor = Color.FromArgb(126, 45, 190), ForeColor = Color.White, FlatStyle = FlatStyle.Flat };
    readonly Button cancel = new() { Text = "CANCEL", Width = 110, Height = 44, BackColor = Color.FromArgb(30, 38, 58), ForeColor = Color.White, FlatStyle = FlatStyle.Flat };
    readonly Button uninstall = new() { Text = "UNINSTALL", Width = 125, Height = 44, BackColor = Color.FromArgb(69, 25, 35), ForeColor = Color.White, FlatStyle = FlatStyle.Flat, Visible = false };
    readonly CheckBox desktop = new() { Text = "Create a desktop shortcut", Checked = true, AutoSize = true, ForeColor = Color.FromArgb(220, 226, 240) };
    readonly CheckBox launch = new() { Text = "Launch " + Program.ProductLabel + " after installation", Checked = true, AutoSize = true, ForeColor = Color.FromArgb(220, 226, 240) };
    readonly ComboBox language = new() { Width = 260, DropDownStyle = ComboBoxStyle.DropDownList, BackColor = Color.White, ForeColor = Color.FromArgb(25, 32, 45) };
    readonly CheckBox agreement = new() { Text = "I agree to the Terms of Service and acknowledge the Privacy Policy.", Checked = false, AutoSize = true };
    readonly LinkLabel policies = new() { Text = "Terms of Service    Privacy Policy", AutoSize = true, LinkColor = Color.FromArgb(103, 215, 238), ActiveLinkColor = Color.White, VisitedLinkColor = Color.FromArgb(103, 215, 238) };
    readonly Button backup = new() { Text = "BACK UP NOW", AutoSize = true, Height = 40 };
    readonly Button restore = new() { Text = "RESTORE BACKUP / ROLL BACK", AutoSize = true, Height = 40 };
    readonly Label recoveryHint = new() { AutoSize = true, MaximumSize = new Size(700, 0), ForeColor = Color.FromArgb(180,195,215) };
    bool busy;
    readonly bool downgrade;

    public SetupForm()
    {
		var existingInstall = File.Exists(Program.AppPath);
		downgrade = Recovery.RequiresRestore(Program.InstallDirectory, Program.ReleaseTag);
		if (existingInstall) {
			var installedVersion = Registry.CurrentUser.OpenSubKey(Program.UninstallRegistryPath)?.GetValue("DisplayVersion")?.ToString();
			var update = string.IsNullOrWhiteSpace(installedVersion) || !string.Equals(installedVersion, Program.Version, StringComparison.OrdinalIgnoreCase);
			install.Text = downgrade ? "ROLL BACK VIA BACKUP" : update ? $"UPDATE TO {Program.Version}" : "REPAIR " + Program.ProductLabel.ToUpperInvariant();
			uninstall.Visible = true;
			status.Text = update ? $"{Program.ProductLabel} {installedVersion} is installed — update available" : $"{Program.ProductLabel} {Program.Version} is installed — repair or uninstall";
		}
        Text = $"{Program.ProductLabel} {Program.Version} Setup";
        ClientSize = new Size(760, 720); MinimumSize = new Size(680, 620);
        StartPosition = FormStartPosition.CenterScreen; BackColor = Color.FromArgb(15, 19, 29);
        ForeColor = Color.FromArgb(230, 235, 244); Font = new Font("Segoe UI", 10); AutoScaleMode = AutoScaleMode.Dpi;
        var shell = new TableLayoutPanel { Dock = DockStyle.Fill, ColumnCount = 1, RowCount = 2 };
        shell.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
        shell.RowStyles.Add(new RowStyle(SizeType.Percent, 100)); shell.RowStyles.Add(new RowStyle(SizeType.Absolute, 130));
        var scroll = new Panel { Dock = DockStyle.Fill, AutoScroll = true };
        var root = new TableLayoutPanel { Dock = DockStyle.Top, AutoSize = true, ColumnCount = 1, RowCount = 0, Padding = new Padding(28, 24, 28, 12) };
        root.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
        void Add(Control control) {
            var row = root.RowCount++; root.RowStyles.Add(new RowStyle(SizeType.AutoSize));
            control.Margin = new Padding(0, 0, 0, 16); control.Anchor = AnchorStyles.Left | AnchorStyles.Right;
            root.Controls.Add(control, 0, row);
        }
        var brand = new Panel { Height = 68 };
        var logo = Icon.ExtractAssociatedIcon(Application.ExecutablePath)?.ToBitmap();
        brand.Controls.Add(new PictureBox { Image = logo, SizeMode = PictureBoxSizeMode.Zoom, Size = new Size(48, 48), Location = new Point(0, 4) });
        brand.Controls.Add(new Label { Text = "Pulse Weaver", Font = new Font("Segoe UI", 20, FontStyle.Bold), AutoSize = true, Location = new Point(64, 0) });
        brand.Controls.Add(new Label { Text = $"{Program.InstallerSubtitle}  ·  {Program.Version}", Font = new Font("Segoe UI", 9), ForeColor = Color.FromArgb(155, 171, 194), AutoSize = true, Location = new Point(66, 38) });
        Add(brand);
        Add(new Label { Text = existingInstall ? "Update your studio" : "Set up your studio", Font = new Font("Segoe UI", 18, FontStyle.Bold), AutoSize = true });
        var destination = new TableLayoutPanel { AutoSize = true, ColumnCount = 1, Padding = new Padding(16), BackColor = Color.FromArgb(24, 31, 44) };
        destination.Controls.Add(new Label { Text = "INSTALL LOCATION", Font = new Font("Segoe UI", 8, FontStyle.Bold), ForeColor = Color.FromArgb(103, 215, 238), AutoSize = true, Margin = new Padding(0, 0, 0, 8) });
        destination.Controls.Add(new Label { Text = Program.InstallDirectory, Dock = DockStyle.Top, AutoSize = false, Height = 26, AutoEllipsis = true, Margin = Padding.Empty }); Add(destination);
        foreach (var item in Program.AvailableLanguages()) language.Items.Add(item);
        language.DrawMode = DrawMode.OwnerDrawFixed;
        language.DrawItem += (_, e) => {
            e.DrawBackground();
            var selected = e.Index >= 0 ? language.Items[e.Index] : language.SelectedItem;
            TextRenderer.DrawText(e.Graphics, selected?.ToString() ?? "English", e.Font, e.Bounds,
                (e.State & DrawItemState.Selected) != 0 ? SystemColors.HighlightText : language.ForeColor,
                TextFormatFlags.Left | TextFormatFlags.VerticalCenter | TextFormatFlags.EndEllipsis);
            e.DrawFocusRectangle();
        };
        var configuredLanguage = Program.ConfiguredLanguage();
        language.SelectedItem = language.Items.Cast<LanguageOption>().FirstOrDefault(item => item.Code.Equals(configuredLanguage, StringComparison.OrdinalIgnoreCase)) ?? language.Items.Cast<LanguageOption>().First(item => item.Code.Equals("en-US", StringComparison.OrdinalIgnoreCase));
        var options = new FlowLayoutPanel { AutoSize = true, FlowDirection = FlowDirection.TopDown, WrapContents = false };
        var languageRow = new FlowLayoutPanel { AutoSize = true, WrapContents = false, Margin = new Padding(0, 0, 0, 10) };
        languageRow.Controls.Add(new Label { Text = "Interface language", AutoSize = true, Margin = new Padding(0, 5, 16, 0) });
        languageRow.Controls.Add(language); options.Controls.Add(languageRow); options.Controls.Add(desktop); options.Controls.Add(launch); Add(options);
        var legal = new FlowLayoutPanel { AutoSize = true, FlowDirection = FlowDirection.TopDown, WrapContents = false, Padding = new Padding(16), BackColor = Color.FromArgb(24, 31, 44) };
        agreement.Margin = new Padding(0, 0, 0, 8); legal.Controls.Add(agreement);
        policies.Links.Add(0, 16, "https://www.lumi-con.com/pulse-weaver/terms/");
        policies.Links.Add(20, 14, "https://www.lumi-con.com/pulse-weaver/privacy/");
        policies.LinkClicked += (_, e) => {
            try { Process.Start(new ProcessStartInfo((string)e.Link!.LinkData!) { UseShellExecute = true }); }
            catch (Exception ex) { MessageBox.Show(this, "Could not open the policy link. " + ex.Message, "Policy link", MessageBoxButtons.OK, MessageBoxIcon.Information); }
        };
        legal.Controls.Add(policies); Add(legal); agreement.CheckedChanged += (_, _) => RefreshInstallEnabled();
        legal.SizeChanged += (_, _) => agreement.MaximumSize = new Size(Math.Max(200, legal.ClientSize.Width - legal.Padding.Horizontal), 0);
        var recoveryPanel = new FlowLayoutPanel { AutoSize = true, FlowDirection = FlowDirection.TopDown, WrapContents = false };
        recoveryPanel.Controls.Add(new Label { Text = "Backup & recovery", Font = new Font("Segoe UI", 10, FontStyle.Bold), AutoSize = true });
        var recoveryButtons = new FlowLayoutPanel { AutoSize = true, WrapContents = true };
        backup.Text = "Back up now"; restore.Text = "Restore backup…"; recoveryButtons.Controls.Add(backup); recoveryButtons.Controls.Add(restore);
        recoveryPanel.Controls.Add(recoveryButtons); recoveryHint.Text = "Updates back up your app and settings automatically. Keep recovery backups private.";
        recoveryPanel.Controls.Add(recoveryHint); Add(recoveryPanel);
        backup.Enabled = existingInstall; backup.Click += async (_, _) => await MaintenanceAsync("backup"); restore.Click += async (_, _) => await RestoreAsync();
        scroll.Controls.Add(root); shell.Controls.Add(scroll, 0, 0);
        var footer = new TableLayoutPanel { Dock = DockStyle.Fill, ColumnCount = 1, RowCount = 3, Padding = new Padding(28, 8, 28, 14), BackColor = Color.FromArgb(20, 26, 38) };
        footer.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
        footer.RowStyles.Add(new RowStyle(SizeType.Absolute, 8)); footer.RowStyles.Add(new RowStyle(SizeType.Absolute, 32)); footer.RowStyles.Add(new RowStyle(SizeType.Percent, 100));
        progress.Height = 4; progress.Dock = DockStyle.Fill; footer.Controls.Add(progress, 0, 0);
        var progressPanel = new Panel { Dock = DockStyle.Fill }; status.AutoSize = false; status.Dock = DockStyle.Fill; status.AutoEllipsis = true;
        progressPanel.Controls.Add(status); footer.Controls.Add(progressPanel, 0, 1);
        if (Recovery.Pending(Program.InstallDirectory)) { install.Text = "Recover setup"; status.Text = "Recover the interrupted operation before continuing."; }
        else if (downgrade) status.Text = "A newer version is installed. Restore its pre-upgrade backup to roll back.";
        else if (!existingInstall) status.Text = "Review the policies and tick the agreement box to install.";
        var actions = new FlowLayoutPanel { Dock = DockStyle.Fill, FlowDirection = FlowDirection.RightToLeft, WrapContents = false };
        install.Text = existingInstall ? install.Text.Replace("UPDATE TO", "Update to").Replace("REPAIR", "Repair") : "Install Pulse Weaver";
        install.Width = 220; cancel.Text = "Cancel"; uninstall.Text = "Uninstall";
        foreach (var button in new[] { install, cancel, uninstall, backup, restore }) {
            button.FlatStyle = FlatStyle.Flat; button.FlatAppearance.BorderSize = 1; button.FlatAppearance.BorderColor = Color.FromArgb(57, 69, 89);
            button.BackColor = Color.FromArgb(29, 38, 54); button.ForeColor = ForeColor; button.Cursor = Cursors.Hand; button.Padding = new Padding(8, 0, 8, 0);
        }
        install.BackColor = Color.FromArgb(100, 65, 194); install.FlatAppearance.BorderColor = install.BackColor;
        install.Click += async (_, _) => await InstallAsync(); cancel.Click += (_, _) => Close(); uninstall.Click += (_, _) => { if (Program.Uninstall() == 0) Close(); };
        actions.Controls.Add(install); actions.Controls.Add(cancel); actions.Controls.Add(uninstall); footer.Controls.Add(actions, 0, 2);
        shell.Controls.Add(footer, 0, 1); Controls.Add(shell); FormClosing += (_, e) => { if (busy) e.Cancel = true; };
        AcceptButton = install; CancelButton = cancel; RefreshInstallEnabled();
    }

    async Task InstallAsync()
    {
        if(Recovery.Pending(Program.InstallDirectory)) {await MaintenanceAsync("recover");return;}
        if(downgrade) {await RestoreAsync();return;}
        if (!agreement.Checked) { agreement.Focus(); return; }
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
    void RefreshInstallEnabled()
    {
        install.Enabled = !busy && (agreement.Checked || downgrade || Recovery.Pending(Program.InstallDirectory));
        install.BackColor = install.Enabled ? Color.FromArgb(100, 65, 194) : Color.FromArgb(43, 49, 66);
        install.FlatAppearance.BorderColor = install.BackColor;
    }
    void SetBusy(bool value) {busy=value;cancel.Enabled=uninstall.Enabled=restore.Enabled=language.Enabled=desktop.Enabled=launch.Enabled=agreement.Enabled=policies.Enabled=!value;backup.Enabled=!value&&File.Exists(Program.AppPath);RefreshInstallEnabled();}

    internal int AgreementCheckCode()
    {
        if (agreement.Checked || policies.Links.Count != 2) return 47;
        if (!downgrade && !Recovery.Pending(Program.InstallDirectory)) {
            if (install.Enabled) return 48;
            agreement.Checked = true; if (!install.Enabled) return 49;
            SetBusy(true); if (install.Enabled || agreement.Enabled) return 50;
            SetBusy(false); if (!install.Enabled) return 54;
            agreement.Checked = false; if (install.Enabled) return 55;
        }
        return 0;
    }

    internal int LayoutCheckCode()
    {
        var actionArea = install.Parent;
        var progressArea = status.Parent;
        if (actionArea is null || progressArea is null) return 41;
        if (actionArea.ClientSize.Height < install.Height) return 42;
        if (actionArea.ClientSize.Height < cancel.Height) return 43;
        if (actionArea.Controls.Cast<Control>().Any(control => control.Visible && (control.Right > actionArea.ClientSize.Width || control.Bottom > actionArea.ClientSize.Height))) return 56;
        if (progressArea.ClientSize.Height < status.Bottom) return 44;
        if (language.SelectedItem is not LanguageOption || Program.AvailableLanguages().FirstOrDefault()?.Code != "en-US") return 45;
        if(recoveryHint.Parent is null || recoveryHint.Bottom>recoveryHint.Parent.ClientSize.Height) return 46;
        if (agreement.Parent is null || agreement.Right > agreement.Parent.ClientSize.Width - agreement.Parent.Padding.Right) return 57;
        return 0;
    }
}
