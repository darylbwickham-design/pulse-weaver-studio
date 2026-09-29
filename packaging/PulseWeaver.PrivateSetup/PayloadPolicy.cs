using System.IO.Compression;

namespace PulseWeaver.Setup;

// Validate the whole archive before staging any file. Use the same Windows path
// rules as recovery so ZIP aliases cannot overwrite configuration or each other.
internal static class PayloadPolicy
{
    internal static bool IsProtectedConfiguration(string root, string target)
    {
        foreach (var name in new[] { "config", "config-backups" }) {
            var protectedRoot = Path.GetFullPath(Path.Combine(root, name));
            if (target.Equals(protectedRoot, StringComparison.OrdinalIgnoreCase) ||
                target.StartsWith(protectedRoot + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase)) return true;
        }
        return false;
    }

    internal static string Target(string root, ZipArchiveEntry entry)
    {
        var directory = entry.FullName.EndsWith('/') || entry.FullName.EndsWith('\\');
        var relative = directory ? entry.FullName[..^1] : entry.FullName;
        var target = Recovery.SafePath(root, relative);
        if (IsProtectedConfiguration(root, target)) throw new IOException("Installer payload must not contain user configuration.");
        if ((entry.ExternalAttributes & 0x400) != 0 || ((entry.ExternalAttributes >> 16) & 0xf000) == 0xa000)
            throw new IOException("Linked installer entries are not supported.");
        if (directory && entry.Length != 0) throw new IOException("Installer directory entry contains file data.");
        return target;
    }

    internal static HashSet<string> Validate(ZipArchive archive, string root)
    {
        var names = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        var files = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        var targets = new List<string>();
        foreach (var entry in archive.Entries) {
            var target = Target(root, entry);
            var relative = Path.GetRelativePath(root, target);
            if (!names.Add(relative)) throw new IOException("Duplicate installer entry was rejected.");
            targets.Add(relative);
            if (!entry.FullName.EndsWith('/') && !entry.FullName.EndsWith('\\')) files.Add(relative);
        }
        foreach (var relative in targets) {
            for (var parent = Path.GetDirectoryName(relative); !string.IsNullOrEmpty(parent); parent = Path.GetDirectoryName(parent))
                if (files.Contains(parent)) throw new IOException("Installer file conflicts with a directory.");
        }
        return files;
    }
}
