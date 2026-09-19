using System.IO.Compression;
using System.Security.Cryptography;
using System.Text.Json;

namespace PulseWeaver.Setup;

// Shared by both installer versions. Recovery archives stay beside, never inside,
// the installation. All destructive replacement is journaled on the same volume.
internal static class Recovery
{
    internal sealed record Entry(string Path, long Size, string Sha256);
    internal sealed record Snapshot(int Schema, string Product, string Version, DateTime CreatedUtc, Entry[] Files);
    sealed record Transaction(int Schema, string Root, string[] Before, string[] After, bool Committed = false);
    const string Index = "pulseweaver-backup.json";
    internal static string BackupDirectory(string root) => Path.GetFullPath(root) + " Backups";
    internal static string WorkDirectory(string root) => Path.GetFullPath(root) + ".recovery-work";
    static bool Excluded(string name) => name.Equals("config-backups", StringComparison.OrdinalIgnoreCase) ||
        name.Equals("Uninstall Pulse Weaver.exe", StringComparison.OrdinalIgnoreCase);
    internal static string InstalledVersion(string root)
    {
        try { using var json = JsonDocument.Parse(File.ReadAllText(Path.Combine(root,"bin","64bit","pulseweaver-update.json")));
            return json.RootElement.GetProperty("tag").GetString()!.TrimStart('v'); }
        catch { return "unknown"; }
    }
    internal static void SafeTree(string root)
    {
        root = Path.GetFullPath(root);
        for (var parent = new DirectoryInfo(root); parent is not null; parent = parent.Parent)
            if (parent.Exists && (parent.Attributes & FileAttributes.ReparsePoint) != 0)
                throw new IOException("Linked folders are not supported for installation or recovery.");
        if (!Directory.Exists(root)) return;
        foreach (var item in new DirectoryInfo(root).EnumerateFileSystemInfos()) {
            if ((item.Attributes & FileAttributes.ReparsePoint) != 0) throw new IOException("Linked files/folders are not supported: " + item.Name);
            if (item is DirectoryInfo) SafeTree(item.FullName);
        }
    }
    internal static string SafePath(string root, string relative)
    {
        var parts = relative.Replace('\\','/').Split('/');
        if (parts.Length == 0 || parts.Any(p => string.IsNullOrWhiteSpace(p) || p is "." or ".." ||
            p.EndsWith('.') || p.EndsWith(' ') || p.IndexOfAny(Path.GetInvalidFileNameChars()) >= 0 ||
            System.Text.RegularExpressions.Regex.IsMatch(p, @"^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(\.|$)", System.Text.RegularExpressions.RegexOptions.IgnoreCase)))
            throw new IOException("Unsafe archive path rejected.");
        var full = Path.GetFullPath(Path.Combine(root, Path.Combine(parts)));
        if (!full.StartsWith(Path.TrimEndingDirectorySeparator(Path.GetFullPath(root)) + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase))
            throw new IOException("Archive path escapes its destination.");
        return full;
    }
    internal static FileStream Acquire(string root)
    {
        SafeTree(root);
        var backups = BackupDirectory(root);
        Directory.CreateDirectory(backups); SafeTree(backups);
        return new FileStream(Path.Combine(backups,"maintenance.lock"), FileMode.OpenOrCreate, FileAccess.ReadWrite, FileShare.None);
    }
    static string Hash(string file) { using var input = File.OpenRead(file); return Convert.ToHexString(SHA256.HashData(input)); }
    static IEnumerable<string> Top(string root) => Directory.Exists(root)
        ? Directory.EnumerateFileSystemEntries(root).Select(Path.GetFileName).Where(n => n is not null && !Excluded(n)).Cast<string>()
        : Enumerable.Empty<string>();
    internal static void CopyTree(string from, string to)
    {
        SafeTree(from); Directory.CreateDirectory(to); SafeTree(to);
        foreach (var name in Top(from)) Copy(Path.Combine(from,name), Path.Combine(to,name));
    }
    static void Copy(string from, string to)
    {
        if (Directory.Exists(from)) { Directory.CreateDirectory(to); foreach (var item in Directory.EnumerateFileSystemEntries(from)) Copy(item, Path.Combine(to,Path.GetFileName(item))); }
        else { Directory.CreateDirectory(Path.GetDirectoryName(to)!); File.Copy(from,to,true); }
    }
    internal static string Backup(string root)
    {
        SafeTree(root);
        var backupRoot = BackupDirectory(root); Directory.CreateDirectory(backupRoot); SafeTree(backupRoot);
        var path = Path.Combine(backupRoot, $"PulseWeaver-{DateTime.UtcNow:yyyyMMdd-HHmmss}-{Guid.NewGuid():N}.zip");
        var partial = path + ".partial";
        var entries = new List<Entry>();
        using (var output = new FileStream(partial,FileMode.CreateNew,FileAccess.Write,FileShare.None)) {
            using (var archive = new ZipArchive(output,ZipArchiveMode.Create,true)) {
                foreach (var top in Top(root)) {
                    var source = Path.Combine(root,top);
                    var files = Directory.Exists(source) ? Directory.EnumerateFiles(source,"*",SearchOption.AllDirectories) : new[]{source};
                    foreach (var file in files) {
                        var relative = Path.GetRelativePath(root,file).Replace('\\','/');
                        archive.CreateEntryFromFile(file,"files/"+relative,CompressionLevel.Fastest);
                        entries.Add(new(relative,new FileInfo(file).Length,Hash(file)));
                    }
                }
                using var writer = new StreamWriter(archive.CreateEntry(Index).Open());
                writer.Write(JsonSerializer.Serialize(new Snapshot(1,"PulseWeaver",InstalledVersion(root),DateTime.UtcNow,entries.ToArray())));
            }
            output.Flush(true);
        }
        // Verify every byte before allowing an installation to proceed.
        Inspect(partial);
        File.Move(partial,path);
        return path;
    }
    internal static Snapshot Inspect(string path, string? extractTo = null)
    {
        using var archive = ZipFile.OpenRead(path);
        if (archive.Entries.Count > 200000) throw new IOException("Backup contains too many entries.");
        var index = archive.GetEntry(Index) ?? throw new IOException("Not a full Pulse Weaver recovery backup. Legacy config-only ZIPs cannot restore app files.");
        if (index.Length > 32 * 1024 * 1024) throw new IOException("Backup index is too large.");
        using var reader = new StreamReader(index.Open());
        var snapshot = JsonSerializer.Deserialize<Snapshot>(reader.ReadToEnd()) ?? throw new IOException("Invalid backup index.");
        if (snapshot.Schema != 1 || snapshot.Product != "PulseWeaver" || snapshot.Files is null || snapshot.Files.Length == 0)
            throw new IOException("Unsupported or empty recovery backup.");
        if (archive.Entries.Count != snapshot.Files.Length + 1) throw new IOException("Unexpected or duplicate archive entries.");
        var names = new HashSet<string>(StringComparer.OrdinalIgnoreCase); long total = 0;
        foreach (var file in snapshot.Files) {
            var dest = SafePath(extractTo ?? Path.GetTempPath(),file.Path);
            if (Excluded(file.Path.Replace('\\','/').Split('/')[0]) || !names.Add(file.Path.Replace('\\','/')))
                throw new IOException("Duplicate or protected recovery entry.");
            var entry = archive.GetEntry("files/" + file.Path) ?? throw new IOException("Missing recovery entry.");
            if (entry.Length != file.Size || file.Size < 0 || (total = checked(total + file.Size)) > 32L*1024*1024*1024)
                throw new IOException("Invalid recovery file size.");
            using var input = entry.Open();
            if (Convert.ToHexString(SHA256.HashData(input)) != file.Sha256) throw new IOException("Recovery backup failed its integrity check.");
            if (extractTo is not null) { Directory.CreateDirectory(Path.GetDirectoryName(dest)!); entry.ExtractToFile(dest,false); }
        }
        return snapshot;
    }
    internal static bool Pending(string root) => File.Exists(Path.Combine(WorkDirectory(root),"transaction.json"));
    static void Journal(string work, Transaction transaction)
    {
        var path = Path.Combine(work,"transaction.json");
        using (var stream = new FileStream(path+".tmp",FileMode.Create,FileAccess.Write,FileShare.None)) {
            JsonSerializer.Serialize(stream,transaction); stream.Flush(true);
        }
        File.Move(path+".tmp",path,true);
    }
    static void Move(string source, string target)
    {
        if (Directory.Exists(source)) Directory.Move(source,target);
        else if (File.Exists(source)) File.Move(source,target);
    }
    internal static void RecoverInterrupted(string root)
    {
        var work=WorkDirectory(root); SafeTree(root); SafeTree(work);
        var journal=Path.Combine(work,"transaction.json");
        if (!File.Exists(journal)) return;
        var state=JsonSerializer.Deserialize<Transaction>(File.ReadAllText(journal)) ?? throw new IOException("Unreadable recovery journal.");
        if (state.Schema != 1 || !Path.GetFullPath(root).Equals(state.Root,StringComparison.OrdinalIgnoreCase)) throw new IOException("Recovery journal belongs to another installation.");
        foreach(var name in state.Before.Concat(state.After)) {
            SafePath(root,name);
            if (Path.GetFileName(name)!=name || Excluded(name)) throw new IOException("Unsafe recovery journal.");
        }
        if (!state.Committed) {
            var failed=Path.Combine(work,"failed"); Directory.CreateDirectory(failed);
            foreach(var name in state.After.Union(state.Before,StringComparer.OrdinalIgnoreCase)) {
                var old=Path.Combine(work,"previous",name); var current=Path.Combine(root,name);
                if (File.Exists(old)||Directory.Exists(old)||!state.Before.Contains(name,StringComparer.OrdinalIgnoreCase)) {
                    if (File.Exists(current)||Directory.Exists(current)) Move(current,Path.Combine(failed,Guid.NewGuid().ToString("N")));
                    Move(old,current);
                }
            }
        }
        // Keep displaced data for manual recovery; no cleanup can destroy evidence.
        var history=Path.Combine(BackupDirectory(root),"transactions"); Directory.CreateDirectory(history);
        Directory.Move(work,Path.Combine(history,DateTime.UtcNow.ToString("yyyyMMdd-HHmmss")+"-"+Guid.NewGuid().ToString("N")));
    }
    internal static void Replace(string root, string prepared, Action<int>? fault = null)
    {
        SafeTree(root); SafeTree(prepared); Directory.CreateDirectory(root);
        if(Pending(root)) throw new IOException("An interrupted operation must be recovered first.");
        var work=WorkDirectory(root);
        if(Directory.Exists(work)) throw new IOException("Recovery work directory already exists; preserve it for inspection.");
        Directory.CreateDirectory(Path.Combine(work,"previous"));
        var state=new Transaction(1,Path.GetFullPath(root),Top(root).ToArray(),Top(prepared).ToArray());
        Journal(work,state);
        try {
            int step=0;
            foreach(var name in state.Before) { Move(Path.Combine(root,name),Path.Combine(work,"previous",name)); fault?.Invoke(++step); }
            foreach(var name in state.After) { Move(Path.Combine(prepared,name),Path.Combine(root,name)); fault?.Invoke(++step); }
            Journal(work,state with {Committed=true});
        } catch { RecoverInterrupted(root); throw; }
        RecoverInterrupted(root);
    }
    internal static string Restore(string root,string backup,Action<int,string> progress)
    {
        Inspect(backup);
        progress(10,"Backing up the current app and settings before restore…");
        var safety=Top(root).Any() ? Backup(root) : "No prior files (empty installation)";
        var staging=Path.Combine(BackupDirectory(root),"restore-"+Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(staging);
        try {
            progress(35,"Verifying and staging the selected backup…"); var snapshot=Inspect(backup,staging);
            if (!File.Exists(Path.Combine(staging,"bin","64bit","PulseWeaverCore.exe"))) throw new IOException("Backup has no Pulse Weaver runtime.");
            progress(75,"Restoring the saved app version and settings together…"); Replace(root,staging);
            progress(100,"Restored "+snapshot.Version+". Previous state backed up at "+safety);
            return snapshot.Version;
        } finally { if(Directory.Exists(staging)) Directory.Delete(staging,true); }
    }
}
