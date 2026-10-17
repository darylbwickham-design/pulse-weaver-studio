using System;
using System.Collections.Generic;
using System.IO;
using System.Net;
using System.Net.Sockets;
using System.Text;
using System.Threading.Tasks;
using Newtonsoft.Json.Linq;

// Streamer.bot API stand-in. The unchanged example is compiled directly.
public static class CPH
{
    public static readonly Dictionary<string, object> Globals = new Dictionary<string, object>();
    public static readonly Dictionary<string, object> Args = new Dictionary<string, object>();
    public static readonly List<string> Logs = new List<string>();
    public static T GetGlobalVar<T>(string name, bool persisted)
    {
        object value;
        return Globals.TryGetValue(name, out value) ? (T)value : default(T);
    }
    public static bool TryGetArg<T>(string name, out T value)
    {
        object argument;
        bool found = Args.TryGetValue(name, out argument);
        value = found ? (T)argument : default(T);
        return found;
    }
    public static void SetArgument(string name, object value) { Args[name] = value; }
    public static void LogError(string message) { Logs.Add(message); }
}

internal static class Program
{
    private static int checks;
    private static void Check(bool condition, string message)
    {
        if (!condition) throw new Exception(message);
        ++checks;
    }

    private static async Task Exchange(int status, bool expected)
    {
        var listener = new TcpListener(IPAddress.Loopback, 0);
        listener.Start();
        CPH.Globals["pulseweaver_port"] = ((IPEndPoint)listener.LocalEndpoint).Port;
        var server = Task.Run(async () =>
        {
            using (TcpClient client = await listener.AcceptTcpClientAsync())
            using (NetworkStream stream = client.GetStream())
            {
                stream.ReadTimeout = 5000;
                var bytes = new List<byte>();
                while (true)
                {
                    int next = stream.ReadByte();
                    if (next < 0 || bytes.Count > 65536) throw new Exception("Incomplete fixture request");
                    bytes.Add((byte)next);
                    int count = bytes.Count;
                    if (count >= 4 && bytes[count - 4] == 13 && bytes[count - 3] == 10 && bytes[count - 2] == 13 && bytes[count - 1] == 10) break;
                }
                string headers = Encoding.ASCII.GetString(bytes.ToArray());
                Check(headers.StartsWith("POST /api/v1/control HTTP/1.1"), "adapter targets only the basic control endpoint");
                Check(headers.Contains("Authorization: Bearer fixture-private-token"), "private token sent as bearer header");
                Check(headers.Contains("X-Pulse-Weaver-Client: streamerbot"), "client label sent");
                int length = 0;
                foreach (string line in headers.Split(new[] { "\r\n" }, StringSplitOptions.None))
                    if (line.StartsWith("Content-Length:", StringComparison.OrdinalIgnoreCase)) length = Int32.Parse(line.Substring(15).Trim());
                byte[] body = new byte[length];
                int offset = 0;
                while (offset < length)
                {
                    int read = await stream.ReadAsync(body, offset, length - offset);
                    if (read == 0) throw new Exception("Missing fixture body");
                    offset += read;
                }
                JObject request = JObject.Parse(Encoding.UTF8.GetString(body));
                Check(request.Value<string>("command") == "stage.next", "configured JSON command preserved");
                Check(!String.IsNullOrWhiteSpace(request.Value<string>("requestId")), "fresh request identity supplied");
                string json = new JObject { ["ok"] = expected, ["message"] = expected ? "Applied" : "Native guard rejected", ["state"] = new JObject { ["activeStage"] = "BRB" } }.ToString();
                byte[] payload = Encoding.UTF8.GetBytes(json);
                string redirect = status == 302 ? "Location: /redirected\r\n" : "";
                byte[] response = Encoding.ASCII.GetBytes("HTTP/1.1 " + status + " Fixture\r\n" + redirect + "Content-Type: application/json\r\nContent-Length: " + payload.Length + "\r\nConnection: close\r\n\r\n");
                await stream.WriteAsync(response, 0, response.Length);
                await stream.WriteAsync(payload, 0, payload.Length);
            }
        });
        try
        {
            bool result = new CPHInline().Execute();
            if (result != expected && server.IsFaulted)
                throw new Exception("Fixture server: " + server.Exception.GetBaseException().Message);
            Check(result == expected, "adapter return agrees with HTTP/native result: " + String.Join("; ", CPH.Logs));
            await server.WaitAsync(TimeSpan.FromSeconds(10));
            Check((bool)CPH.Args["pulseweaver_ok"] == expected, "following actions receive actual success");
            Check((int)CPH.Args["pulseweaver_status"] == status, "following actions receive HTTP status");
            Check(JObject.Parse((string)CPH.Args["pulseweaver_state"]).Value<string>("activeStage") == "BRB", "observed state exposed");
        }
        finally { listener.Stop(); }
    }

    private static async Task Main()
    {
        CPH.Globals["pulseweaver_token"] = "fixture-private-token";
        CPH.Globals["pulseweaver_host"] = "127.0.0.1";
        CPH.Args["pulseweaver_request"] = "{\"command\":\"stage.next\"}";
        await Exchange(200, true);
        await Exchange(409, false);
        await Exchange(302, false);
        CPH.Globals["pulseweaver_host"] = "example.invalid";
        Check(!new CPHInline().Execute(), "remote host rejected before a request");
        CPH.Globals["pulseweaver_host"] = "127.0.0.1";
        CPH.Args["pulseweaver_request"] = "not-json";
        Check(!new CPHInline().Execute(), "malformed command JSON rejected");
        CPH.Globals.Remove("pulseweaver_token");
        Check(!new CPHInline().Execute(), "missing private credential rejected");
        Check(!(bool)CPH.Args["pulseweaver_ok"] && (int)CPH.Args["pulseweaver_status"] == 0, "failure clears prior success outputs");
        foreach (string line in CPH.Logs)
            Check(!line.Contains("fixture-private-token"), "logs never expose the token");
        Console.WriteLine("PASS: " + checks + " compiled Streamer.bot adapter checks against a local fixture; no real app, platform or output actions.");
    }
}
