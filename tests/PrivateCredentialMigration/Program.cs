using PulseWeaver.Setup;

var result = LegacyCredentialMigration.VerifySyntheticMigration();
if (result == 0)
    Console.WriteLine("Private credential migration tests passed.");
else
    Console.Error.WriteLine($"Private credential migration tests failed with code {result}.");
return result;
