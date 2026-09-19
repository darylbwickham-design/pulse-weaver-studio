using System.ComponentModel;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Text;

namespace PulseWeaver.Setup;

internal enum LegacyCredentialMigrationResult
{
    NoCredentialsNeeded,
    NoKnownLegacyRuntime,
    Migrated,
}

internal readonly record struct LegacyCredentialSlice(long Offset, int Length);

internal sealed record LegacyCredentialProfile(
    string FrontendSha256,
    string CoreSha256,
    LegacyCredentialSlice YouTubeClientId,
    LegacyCredentialSlice YouTubeClientSecret,
    LegacyCredentialSlice KickClientId,
    LegacyCredentialSlice KickClientSecret,
    LegacyCredentialSlice TwitchClientId);

internal static class LegacyCredentialMigration
{
    const uint CryptProtectUiForbidden = 0x1;
    const string ProtectedPrefix = "dpapi:";

    static readonly LegacyCredentialProfile PrivateV50ToV52 = new(
        "1985A04612B5DE1296426C6F4FF4AFBDDF28115EA36BD2B5B2D567956B8AF782",
        "CE763C5F4BA82F7B695B9A4931FDC3307C94079ECBC9F3AF61470BA4E9A7D565",
        new(0x61DB40, 72),
        new(0x61DB98, 35),
        new(0xB21C0, 26),
        new(0xB21E0, 64),
        new(0xB5E40, 30));

    static readonly (string Section, string Key, bool Secret)[] RequiredValues =
    {
        ("twitch", "client_id", false),
        ("kick", "client_id", false),
        ("kick", "client_secret", true),
        ("youtube", "client_id", false),
        ("youtube", "client_secret", true),
    };

    internal static LegacyCredentialMigrationResult MigrateBeforeUpgrade(string installDirectory,
                                                                           bool legacyRegistrationExpected)
    {
        return MigrateBeforeUpgrade(installDirectory, PrivateV50ToV52, legacyRegistrationExpected);
    }

    internal static LegacyCredentialMigrationResult MigrateBeforeUpgrade(string installDirectory,
                                                                           LegacyCredentialProfile profile,
                                                                           bool legacyRegistrationExpected)
    {
        var credentialPath = CredentialPath(installDirectory);
        var lines = ReadLines(credentialPath);
        var existing = ReadRequiredValues(lines);
        var missing = RequiredValues.Where(value => string.IsNullOrWhiteSpace(existing[Name(value.Section, value.Key)]))
                                    .ToArray();

        if (missing.Length == 0)
        {
            VerifyStoredSecrets(existing);
            return LegacyCredentialMigrationResult.NoCredentialsNeeded;
        }

        var frontendPath = Path.Combine(installDirectory, "bin", "64bit", "PulseWeaverCore.exe");
        var corePath = Path.Combine(installDirectory, "obs-plugins", "64bit", "pulse-weaver-core.dll");
        if (!File.Exists(frontendPath) && !File.Exists(corePath))
        {
            if (legacyRegistrationExpected)
                throw MigrationFailure("Setup could not find the installed private runtime.");
            return LegacyCredentialMigrationResult.NoKnownLegacyRuntime;
        }

        if (!File.Exists(frontendPath) || !File.Exists(corePath) ||
            !HasExpectedHash(frontendPath, profile.FrontendSha256) ||
            !HasExpectedHash(corePath, profile.CoreSha256))
        {
            if (legacyRegistrationExpected)
                throw MigrationFailure("Setup could not identify the installed private runtime.");
            return LegacyCredentialMigrationResult.NoKnownLegacyRuntime;
        }

        using var extracted = Extract(frontendPath, corePath, profile);
        var replacements = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase)
        {
            [Name("twitch", "client_id")] = AsAscii(extracted.TwitchClientId),
            [Name("kick", "client_id")] = AsAscii(extracted.KickClientId),
            [Name("kick", "client_secret")] = Protect(extracted.KickClientSecret),
            [Name("youtube", "client_id")] = AsAscii(extracted.YouTubeClientId),
            [Name("youtube", "client_secret")] = Protect(extracted.YouTubeClientSecret),
        };

        foreach (var value in missing)
            SetValue(lines, value.Section, value.Key, replacements[Name(value.Section, value.Key)]);

        byte[]? original = File.Exists(credentialPath) ? File.ReadAllBytes(credentialPath) : null;
        try
        {
            WriteLinesAtomically(credentialPath, lines);
            var saved = ReadRequiredValues(ReadLines(credentialPath));
            foreach (var value in RequiredValues)
            {
                var name = Name(value.Section, value.Key);
                if (string.IsNullOrWhiteSpace(saved[name]))
                    throw MigrationFailure("Setup could not verify a saved app registration.");
                if (!string.IsNullOrWhiteSpace(existing[name]) &&
                    !string.Equals(saved[name], existing[name], StringComparison.Ordinal))
                    throw MigrationFailure("Setup could not preserve an existing app registration.");
            }

            VerifyStoredSecrets(saved);
            VerifyMigratedValue(saved, existing, "twitch", "client_id", extracted.TwitchClientId, false);
            VerifyMigratedValue(saved, existing, "kick", "client_id", extracted.KickClientId, false);
            VerifyMigratedValue(saved, existing, "kick", "client_secret", extracted.KickClientSecret, true);
            VerifyMigratedValue(saved, existing, "youtube", "client_id", extracted.YouTubeClientId, false);
            VerifyMigratedValue(saved, existing, "youtube", "client_secret", extracted.YouTubeClientSecret, true);
        }
        catch
        {
            try
            {
                if (original is null)
                    File.Delete(credentialPath);
                else
                    WriteBytesAtomically(credentialPath, original);
            }
            catch
            {
                // The install still aborts before its existing runtime is removed.
            }
            throw;
        }

        return LegacyCredentialMigrationResult.Migrated;
    }

    internal static int VerifySyntheticMigration()
    {
        var roots = new List<string>();
        try
        {
            const int youtubeIdOffset = 128;
            const int youtubeSecretOffset = 256;
            const int kickIdOffset = 128;
            const int kickSecretOffset = 192;
            const int twitchIdOffset = 320;
            const string youtubePrefix = "1234567890-";
            const string youtubeSuffix = ".apps.googleusercontent.com";
            var values = new Dictionary<string, string>(StringComparer.Ordinal)
            {
                [Name("youtube", "client_id")] = youtubePrefix +
                    new string('y', 72 - youtubePrefix.Length - youtubeSuffix.Length) + youtubeSuffix,
                [Name("youtube", "client_secret")] = "GOCSPX-" + new string('s', 28),
                [Name("kick", "client_id")] = new string('K', 25) + "7",
                [Name("kick", "client_secret")] = new string('k', 63) + "7",
                [Name("twitch", "client_id")] = new string('t', 29) + "7",
            };

            var root = NewTestRoot(roots);
            var frontendPath = Path.Combine(root, "bin", "64bit", "PulseWeaverCore.exe");
            var corePath = Path.Combine(root, "obs-plugins", "64bit", "pulse-weaver-core.dll");
            var frontend = new byte[512];
            var core = new byte[512];
            PutTestValue(frontend, youtubeIdOffset, values[Name("youtube", "client_id")]);
            PutTestValue(frontend, youtubeSecretOffset, values[Name("youtube", "client_secret")]);
            PutTestValue(core, kickIdOffset, values[Name("kick", "client_id")]);
            PutTestValue(core, kickSecretOffset, values[Name("kick", "client_secret")]);
            PutTestValue(core, twitchIdOffset, values[Name("twitch", "client_id")]);
            Directory.CreateDirectory(Path.GetDirectoryName(frontendPath)!);
            Directory.CreateDirectory(Path.GetDirectoryName(corePath)!);
            File.WriteAllBytes(frontendPath, frontend);
            File.WriteAllBytes(corePath, core);
            var profile = new LegacyCredentialProfile(
                Convert.ToHexString(SHA256.HashData(frontend)), Convert.ToHexString(SHA256.HashData(core)),
                new(youtubeIdOffset, 72), new(youtubeSecretOffset, 35), new(kickIdOffset, 26),
                new(kickSecretOffset, 64), new(twitchIdOffset, 30));

            var credentialPath = CredentialPath(root);
            Directory.CreateDirectory(Path.GetDirectoryName(credentialPath)!);
            File.WriteAllText(credentialPath,
                "[twitch]\nclient_id=keep-this-existing-value\n\n[custom]\nkeep=value\n", Encoding.UTF8);
            RequireTest(MigrateBeforeUpgrade(root, profile, true) == LegacyCredentialMigrationResult.Migrated);
            var savedText = File.ReadAllText(credentialPath, Encoding.UTF8);
            var savedValues = ReadRequiredValues(ReadLines(credentialPath));
            RequireTest(RequiredValues.All(value => !string.IsNullOrWhiteSpace(savedValues[Name(value.Section, value.Key)])));
            RequireTest(savedValues[Name("twitch", "client_id")] == "keep-this-existing-value");
            RequireTest(savedText.Contains("[twitch]", StringComparison.Ordinal) &&
                        savedText.Contains("[kick]", StringComparison.Ordinal) &&
                        savedText.Contains("[youtube]", StringComparison.Ordinal) &&
                        savedText.Contains("[custom]", StringComparison.Ordinal) &&
                        savedText.Contains("keep=value", StringComparison.Ordinal));
            RequireTest(savedText.Split("client_secret=dpapi:", StringSplitOptions.None).Length - 1 == 2);
            RequireTest(!savedText.Contains(values[Name("kick", "client_secret")], StringComparison.Ordinal) &&
                        !savedText.Contains(values[Name("youtube", "client_secret")], StringComparison.Ordinal));
            RequireTest(MigrateBeforeUpgrade(root, profile, true) == LegacyCredentialMigrationResult.NoCredentialsNeeded);

            var badRoot = NewTestRoot(roots);
            var badFrontendPath = Path.Combine(badRoot, "bin", "64bit", "PulseWeaverCore.exe");
            var badCorePath = Path.Combine(badRoot, "obs-plugins", "64bit", "pulse-weaver-core.dll");
            Directory.CreateDirectory(Path.GetDirectoryName(badFrontendPath)!);
            Directory.CreateDirectory(Path.GetDirectoryName(badCorePath)!);
            var damagedFrontend = frontend.ToArray();
            damagedFrontend[0] = 1;
            File.WriteAllBytes(badFrontendPath, damagedFrontend);
            File.WriteAllBytes(badCorePath, core);
            var badCredentialPath = CredentialPath(badRoot);
            Directory.CreateDirectory(Path.GetDirectoryName(badCredentialPath)!);
            var original = Encoding.UTF8.GetBytes("[custom]\nkeep=unchanged\n");
            File.WriteAllBytes(badCredentialPath, original);
            var rejected = false;
            try { MigrateBeforeUpgrade(badRoot, profile, true); }
            catch (InvalidOperationException) { rejected = true; }
            RequireTest(rejected && File.ReadAllBytes(badCredentialPath).SequenceEqual(original));

            var emptyRoot = Path.Combine(Path.GetTempPath(), "PulseWeaver-empty-migration-test-" + Guid.NewGuid().ToString("N"));
            roots.Add(emptyRoot);
            RequireTest(MigrateBeforeUpgrade(emptyRoot, profile, false) == LegacyCredentialMigrationResult.NoKnownLegacyRuntime);
            RequireTest(!Directory.Exists(emptyRoot));
            return 0;
        }
        catch { return 61; }
        finally
        {
            foreach (var root in roots)
                try { Directory.Delete(root, true); } catch { }
        }
    }

    static string NewTestRoot(ICollection<string> roots)
    {
        var root = Path.Combine(Path.GetTempPath(), "PulseWeaver-migration-test-" + Guid.NewGuid().ToString("N"));
        roots.Add(root);
        return root;
    }

    static void PutTestValue(byte[] target, int offset, string value) =>
        Encoding.ASCII.GetBytes(value).CopyTo(target, offset);

    static void RequireTest(bool condition)
    {
        if (!condition) throw new InvalidOperationException("Synthetic migration verification failed.");
    }

    static InvalidOperationException MigrationFailure(string detail)
    {
        return new InvalidOperationException(detail +
            " The update stopped before replacing Pulse Weaver, so the current installation remains available.");
    }

    static string CredentialPath(string installDirectory) =>
        Path.Combine(installDirectory, "config", "pulseweaver", "app-credentials.ini");

    static bool HasExpectedHash(string path, string expected)
    {
        using var stream = File.OpenRead(path);
        return string.Equals(Convert.ToHexString(SHA256.HashData(stream)), expected, StringComparison.OrdinalIgnoreCase);
    }

    static LegacyCredentialSet Extract(string frontendPath, string corePath, LegacyCredentialProfile profile)
    {
        var result = new LegacyCredentialSet(
            ReadSlice(frontendPath, profile.YouTubeClientId),
            ReadSlice(frontendPath, profile.YouTubeClientSecret),
            ReadSlice(corePath, profile.KickClientId),
            ReadSlice(corePath, profile.KickClientSecret),
            ReadSlice(corePath, profile.TwitchClientId));
        try
        {
            var youtubeId = AsAscii(result.YouTubeClientId);
            var kickId = AsAscii(result.KickClientId);
            var twitchId = AsAscii(result.TwitchClientId);
            if (youtubeId.Length != 72 || !youtubeId.EndsWith(".apps.googleusercontent.com", StringComparison.Ordinal) ||
                youtubeId.IndexOf('-') <= 0 || !youtubeId[..youtubeId.IndexOf('-')].All(char.IsDigit) ||
                result.YouTubeClientSecret.Length != 35 || !StartsWithAscii(result.YouTubeClientSecret, "GOCSPX-") ||
                !result.YouTubeClientSecret.Skip(7).All(IsPortableCredentialByte) ||
                kickId.Length != 26 || !kickId.All(character => char.IsAsciiLetterUpper(character) || char.IsAsciiDigit(character)) ||
                result.KickClientSecret.Length != 64 ||
                !result.KickClientSecret.All(character =>
                    (character >= (byte)'a' && character <= (byte)'z') ||
                    (character >= (byte)'0' && character <= (byte)'9')) ||
                twitchId.Length != 30 || !twitchId.All(character => char.IsAsciiLetterLower(character) || char.IsAsciiDigit(character)))
                throw MigrationFailure("The installed private app registrations did not pass validation.");
            return result;
        }
        catch
        {
            result.Dispose();
            throw;
        }
    }

    static bool StartsWithAscii(byte[] value, string prefix) =>
        value.AsSpan().StartsWith(Encoding.ASCII.GetBytes(prefix));

    static bool IsPortableCredentialByte(byte character) =>
        (character >= (byte)'A' && character <= (byte)'Z') ||
        (character >= (byte)'a' && character <= (byte)'z') ||
        (character >= (byte)'0' && character <= (byte)'9') || character is (byte)'_' or (byte)'-';

    static byte[] ReadSlice(string path, LegacyCredentialSlice slice)
    {
        using var stream = File.OpenRead(path);
        if (slice.Offset < 0 || slice.Length <= 0 || slice.Offset > stream.Length - slice.Length)
            throw MigrationFailure("The installed private runtime is incomplete.");
        stream.Position = slice.Offset;
        var value = new byte[slice.Length];
        stream.ReadExactly(value);
        return value;
    }

    static string AsAscii(byte[] value)
    {
        if (value.Any(character => character is < 0x20 or > 0x7E))
            throw MigrationFailure("An installed app registration had an invalid format.");
        return Encoding.ASCII.GetString(value);
    }

    static string Protect(byte[] plaintext)
    {
        if (plaintext.Length == 0) throw MigrationFailure("An installed app secret was empty.");
        var inputHandle = GCHandle.Alloc(plaintext, GCHandleType.Pinned);
        try
        {
            var input = new DataBlob { Size = plaintext.Length, Data = inputHandle.AddrOfPinnedObject() };
            if (!CryptProtectData(ref input, "Pulse Weaver app credential", IntPtr.Zero, IntPtr.Zero, IntPtr.Zero,
                                  CryptProtectUiForbidden, out var output))
                throw new Win32Exception(Marshal.GetLastWin32Error(), "Windows could not protect a private app secret.");
            try
            {
                var encrypted = new byte[output.Size];
                Marshal.Copy(output.Data, encrypted, 0, encrypted.Length);
                return ProtectedPrefix + Convert.ToBase64String(encrypted);
            }
            finally
            {
                LocalFree(output.Data);
            }
        }
        finally
        {
            inputHandle.Free();
        }
    }

    static byte[] Reveal(string stored)
    {
        if (!stored.StartsWith(ProtectedPrefix, StringComparison.Ordinal))
            throw MigrationFailure("A saved private app secret is not protected by Windows.");
        byte[] encrypted;
        try { encrypted = Convert.FromBase64String(stored[ProtectedPrefix.Length..]); }
        catch (FormatException) { throw MigrationFailure("A saved private app secret is damaged."); }
        if (encrypted.Length == 0) throw MigrationFailure("A saved private app secret is empty.");

        var inputHandle = GCHandle.Alloc(encrypted, GCHandleType.Pinned);
        try
        {
            var input = new DataBlob { Size = encrypted.Length, Data = inputHandle.AddrOfPinnedObject() };
            if (!CryptUnprotectData(ref input, IntPtr.Zero, IntPtr.Zero, IntPtr.Zero, IntPtr.Zero,
                                    CryptProtectUiForbidden, out var output))
                throw new Win32Exception(Marshal.GetLastWin32Error(), "Windows could not verify a saved private app secret.");
            try
            {
                var plaintext = new byte[output.Size];
                Marshal.Copy(output.Data, plaintext, 0, plaintext.Length);
                return plaintext;
            }
            finally
            {
                for (var index = 0; index < output.Size; ++index) Marshal.WriteByte(output.Data, index, 0);
                LocalFree(output.Data);
            }
        }
        finally
        {
            inputHandle.Free();
            CryptographicOperations.ZeroMemory(encrypted);
        }
    }

    static void VerifyStoredSecrets(IReadOnlyDictionary<string, string> values)
    {
        foreach (var value in RequiredValues.Where(value => value.Secret))
        {
            var stored = values[Name(value.Section, value.Key)];
            using var revealed = new SensitiveBytes(Reveal(stored));
            if (revealed.Value.Length == 0) throw MigrationFailure("A saved private app secret is empty.");
        }
    }

    static void VerifyMigratedValue(IReadOnlyDictionary<string, string> saved,
                                    IReadOnlyDictionary<string, string> previous,
                                    string section, string key, byte[] expected, bool secret)
    {
        var name = Name(section, key);
        if (!string.IsNullOrWhiteSpace(previous[name])) return;
        if (!secret)
        {
            if (!string.Equals(saved[name], AsAscii(expected), StringComparison.Ordinal))
                throw MigrationFailure("Setup could not verify a migrated app registration.");
            return;
        }

        using var revealed = new SensitiveBytes(Reveal(saved[name]));
        if (!CryptographicOperations.FixedTimeEquals(revealed.Value, expected))
            throw MigrationFailure("Setup could not verify a migrated private app secret.");
    }

    static List<string> ReadLines(string path) =>
        File.Exists(path) ? File.ReadAllLines(path, Encoding.UTF8).ToList() : new List<string>();

    static Dictionary<string, string> ReadRequiredValues(IReadOnlyList<string> lines)
    {
        var values = RequiredValues.ToDictionary(value => Name(value.Section, value.Key), _ => string.Empty,
                                                  StringComparer.OrdinalIgnoreCase);
        string? section = null;
        foreach (var raw in lines)
        {
            var line = raw.Trim();
            if (line.StartsWith('[') && line.EndsWith(']'))
            {
                section = line[1..^1].Trim();
                continue;
            }
            if (section is null || line.Length == 0 || line[0] is ';' or '#') continue;
            var equals = line.IndexOf('=');
            if (equals <= 0) continue;
            var name = Name(section, line[..equals].Trim());
            if (values.ContainsKey(name)) values[name] = DecodeIniValue(line[(equals + 1)..].Trim());
        }
        return values;
    }

    static string DecodeIniValue(string value)
    {
        if (value.Length >= 2 && value[0] == '"' && value[^1] == '"') return value[1..^1];
        return value;
    }

    static void SetValue(List<string> lines, string section, string key, string value)
    {
        var sectionStart = -1;
        var sectionEnd = lines.Count;
        for (var index = 0; index < lines.Count; ++index)
        {
            var line = lines[index].Trim();
            if (!line.StartsWith('[') || !line.EndsWith(']')) continue;
            if (sectionStart >= 0) { sectionEnd = index; break; }
            if (string.Equals(line[1..^1].Trim(), section, StringComparison.OrdinalIgnoreCase))
                sectionStart = index;
        }

        if (sectionStart < 0)
        {
            if (lines.Count > 0 && lines[^1].Length != 0) lines.Add(string.Empty);
            lines.Add($"[{section}]");
            lines.Add($"{key}={value}");
            return;
        }

        for (var index = sectionStart + 1; index < sectionEnd; ++index)
        {
            var line = lines[index].Trim();
            if (line.Length == 0 || line[0] is ';' or '#') continue;
            var equals = line.IndexOf('=');
            if (equals > 0 && string.Equals(line[..equals].Trim(), key, StringComparison.OrdinalIgnoreCase))
            {
                lines[index] = $"{key}={value}";
                return;
            }
        }
        lines.Insert(sectionEnd, $"{key}={value}");
    }

    static string Name(string section, string key) => section + "/" + key;

    static void WriteLinesAtomically(string path, IReadOnlyList<string> lines)
    {
        var content = Encoding.UTF8.GetBytes(string.Join(Environment.NewLine, lines) + Environment.NewLine);
        WriteBytesAtomically(path, content);
    }

    static void WriteBytesAtomically(string path, byte[] content)
    {
        var directory = Path.GetDirectoryName(path)!;
        Directory.CreateDirectory(directory);
        var temporary = Path.Combine(directory, "." + Path.GetFileName(path) + "." + Guid.NewGuid().ToString("N") + ".tmp");
        try
        {
            using (var stream = new FileStream(temporary, FileMode.CreateNew, FileAccess.Write, FileShare.None,
                                               4096, FileOptions.WriteThrough))
            {
                stream.Write(content);
                stream.Flush(true);
            }
            File.Move(temporary, path, true);
        }
        finally
        {
            try { File.Delete(temporary); } catch { }
        }
    }

    [StructLayout(LayoutKind.Sequential)]
    struct DataBlob
    {
        internal int Size;
        internal IntPtr Data;
    }

    [DllImport("crypt32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    static extern bool CryptProtectData(ref DataBlob dataIn, string description, IntPtr optionalEntropy,
                                        IntPtr reserved, IntPtr prompt, uint flags, out DataBlob dataOut);

    [DllImport("crypt32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    static extern bool CryptUnprotectData(ref DataBlob dataIn, IntPtr description, IntPtr optionalEntropy,
                                          IntPtr reserved, IntPtr prompt, uint flags, out DataBlob dataOut);

    [DllImport("kernel32.dll")]
    static extern IntPtr LocalFree(IntPtr memory);

    sealed class SensitiveBytes(byte[] value) : IDisposable
    {
        internal byte[] Value { get; } = value;
        public void Dispose() => CryptographicOperations.ZeroMemory(Value);
    }

    sealed class LegacyCredentialSet(byte[] youtubeClientId, byte[] youtubeClientSecret,
                                     byte[] kickClientId, byte[] kickClientSecret, byte[] twitchClientId) : IDisposable
    {
        internal byte[] YouTubeClientId { get; } = youtubeClientId;
        internal byte[] YouTubeClientSecret { get; } = youtubeClientSecret;
        internal byte[] KickClientId { get; } = kickClientId;
        internal byte[] KickClientSecret { get; } = kickClientSecret;
        internal byte[] TwitchClientId { get; } = twitchClientId;

        public void Dispose()
        {
            CryptographicOperations.ZeroMemory(YouTubeClientId);
            CryptographicOperations.ZeroMemory(YouTubeClientSecret);
            CryptographicOperations.ZeroMemory(KickClientId);
            CryptographicOperations.ZeroMemory(KickClientSecret);
            CryptographicOperations.ZeroMemory(TwitchClientId);
        }
    }
}
