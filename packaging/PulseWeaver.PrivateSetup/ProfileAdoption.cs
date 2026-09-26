using System.Security.Cryptography;

namespace PulseWeaver.Setup;

internal static class ProfileAdoption
{
    internal static string Adopt(string sourceRoot, string installedRoot)
    {
        sourceRoot = Path.TrimEndingDirectorySeparator(Path.GetFullPath(sourceRoot));
        installedRoot = Path.TrimEndingDirectorySeparator(Path.GetFullPath(installedRoot));
        if (sourceRoot.Equals(installedRoot, StringComparison.OrdinalIgnoreCase) ||
            sourceRoot.StartsWith(installedRoot + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase) ||
            installedRoot.StartsWith(sourceRoot + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase))
            throw new IOException("The isolated profile and installed application must be separate directories.");
        var sourceConfig = Path.Combine(sourceRoot, "config");
        var destinationConfig = Path.Combine(installedRoot, "config");
        if (!File.Exists(Path.Combine(sourceRoot, "portable_mode.txt")) ||
            !File.Exists(Path.Combine(sourceRoot, "bin", "64bit", "PulseWeaverCore.exe")) ||
            !File.Exists(Path.Combine(sourceConfig, "obs-studio", "global.ini")) ||
            !File.Exists(Path.Combine(installedRoot, "bin", "64bit", "PulseWeaverCore.exe")))
            throw new IOException("The source must be a complete isolated Pulse Weaver build and the destination a complete installation.");
        Recovery.SafeTree(sourceRoot);
        Recovery.SafeTree(installedRoot);
        using var held = Recovery.Acquire(installedRoot);
        if (Recovery.Pending(installedRoot)) throw new IOException("Recover the interrupted installation before importing a profile.");
        var backup = Recovery.Backup(installedRoot);
        var stage = Path.Combine(Recovery.BackupDirectory(installedRoot), "adopt-" + Guid.NewGuid().ToString("N"));
        try {
            Recovery.CopyTree(installedRoot, stage);
            var stagedConfig = Path.Combine(stage, "config");
            if (Directory.Exists(stagedConfig)) Directory.Delete(stagedConfig, true);
            Recovery.CopyTree(sourceConfig, stagedConfig);
            VerifySameFiles(sourceConfig, stagedConfig);
            Recovery.Replace(installedRoot, stage);
            return backup;
        } finally {
            if (Directory.Exists(stage)) Directory.Delete(stage, true);
        }
    }

    static void VerifySameFiles(string expected, string actual)
    {
        var files = Directory.EnumerateFiles(expected, "*", SearchOption.AllDirectories)
            .Select(path => Path.GetRelativePath(expected, path))
            .OrderBy(path => path, StringComparer.OrdinalIgnoreCase).ToArray();
        var copied = Directory.EnumerateFiles(actual, "*", SearchOption.AllDirectories)
            .Select(path => Path.GetRelativePath(actual, path))
            .OrderBy(path => path, StringComparer.OrdinalIgnoreCase).ToArray();
        if (files.Length == 0 || !files.SequenceEqual(copied, StringComparer.OrdinalIgnoreCase))
            throw new IOException("The staged isolated profile does not match its source.");
        foreach (var relative in files) {
            using var input = File.OpenRead(Path.Combine(expected, relative));
            using var output = File.OpenRead(Path.Combine(actual, relative));
            if (!SHA256.HashData(input).SequenceEqual(SHA256.HashData(output)))
                throw new IOException("A staged profile file failed its integrity check.");
        }
    }

    internal static int VerifySyntheticAdoption()
    {
        var temp = Path.Combine(Path.GetTempPath(), "PulseWeaver-adopt-proof-" + Guid.NewGuid().ToString("N"));
        var source = Path.Combine(temp, "Source");
        var installed = Path.Combine(temp, "Installed");
        try {
            Directory.CreateDirectory(Path.Combine(source, "bin", "64bit"));
            Directory.CreateDirectory(Path.Combine(installed, "bin", "64bit"));
            Directory.CreateDirectory(Path.Combine(source, "config", "obs-studio"));
            Directory.CreateDirectory(Path.Combine(installed, "config", "obs-studio"));
            File.WriteAllText(Path.Combine(source, "portable_mode.txt"), "");
            File.WriteAllText(Path.Combine(source, "bin", "64bit", "PulseWeaverCore.exe"), "preview-binary");
            File.WriteAllText(Path.Combine(installed, "bin", "64bit", "PulseWeaverCore.exe"), "installed-binary");
            File.WriteAllText(Path.Combine(installed, "bin", "64bit", "pulseweaver-update.json"),
                "{\"schema\":1,\"channel\":\"windows-alpha\",\"tag\":\"v1.13.1-alpha.1\"}");
            File.WriteAllText(Path.Combine(source, "config", "obs-studio", "global.ini"), "preview-scenes");
            File.WriteAllText(Path.Combine(source, "config", "obs-studio", "credentials.ini"), "protected-token");
            File.WriteAllText(Path.Combine(installed, "config", "obs-studio", "global.ini"), "old-scenes");
            var backup = Adopt(source, installed);
            if (!File.Exists(backup) || File.ReadAllText(Path.Combine(installed, "config", "obs-studio", "global.ini")) != "preview-scenes" ||
                File.ReadAllText(Path.Combine(installed, "config", "obs-studio", "credentials.ini")) != "protected-token" ||
                File.ReadAllText(Path.Combine(installed, "bin", "64bit", "PulseWeaverCore.exe")) != "installed-binary" ||
                !File.ReadAllText(Path.Combine(installed, "bin", "64bit", "pulseweaver-update.json")).Contains("windows-alpha")) return 1;
            Recovery.Restore(installed, backup, (_, _) => { });
            return File.ReadAllText(Path.Combine(installed, "config", "obs-studio", "global.ini")) == "old-scenes" ? 0 : 2;
        } finally {
            if (Directory.Exists(temp)) Directory.Delete(temp, true);
            var backups = Recovery.BackupDirectory(installed);
            if (Directory.Exists(backups)) Directory.Delete(backups, true);
            var work = Recovery.WorkDirectory(installed);
            if (Directory.Exists(work)) Directory.Delete(work, true);
        }
    }
}
