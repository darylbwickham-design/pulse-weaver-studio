using System;
using System.Net;
using System.Net.Http;
using System.Net.Http.Headers;
using System.Text;
using Newtonsoft.Json;
using Newtonsoft.Json.Linq;

// Paste into one Streamer.bot Execute C# Code sub-action.
// Configure a curated pulseweaver_request JSON argument before this sub-action.
public class CPHInline
{
    private static readonly HttpClient Client = new HttpClient(new HttpClientHandler { UseProxy = false, AllowAutoRedirect = false })
    {
        Timeout = TimeSpan.FromSeconds(8)
    };

    public bool Execute()
    {
        CPH.SetArgument("pulseweaver_ok", false);
        CPH.SetArgument("pulseweaver_status", 0);
        CPH.SetArgument("pulseweaver_message", "Pulse Weaver command was not sent.");
        CPH.SetArgument("pulseweaver_state", "{}");
        try
        {
            string host = CPH.GetGlobalVar<string>("pulseweaver_host", true);
            if (String.IsNullOrWhiteSpace(host)) host = "127.0.0.1";
            IPAddress address;
            if (!String.Equals(host, "localhost", StringComparison.OrdinalIgnoreCase) &&
                (!IPAddress.TryParse(host, out address) || !IPAddress.IsLoopback(address)))
            {
                CPH.LogError("Pulse Weaver basic controls require a loopback host.");
                return false;
            }
            int port = CPH.GetGlobalVar<int?>("pulseweaver_port", true) ?? 18775;
            if (port < 1024 || port > 65535) return false;
            string token = CPH.GetGlobalVar<string>("pulseweaver_token", true);
            if (String.IsNullOrWhiteSpace(token))
            {
                CPH.LogError("Set the private pulseweaver_token global variable from this installation's Local API settings.");
                return false;
            }
            string json;
            if (!CPH.TryGetArg("pulseweaver_request", out json)) return false;
            JObject command = JObject.Parse(json);
            command["requestId"] = Guid.NewGuid().ToString("N");
            Uri endpoint = new UriBuilder("http", host, port, "/api/v1/control").Uri;
            using (var request = new HttpRequestMessage(HttpMethod.Post, endpoint))
            {
                request.Headers.Authorization = new AuthenticationHeaderValue("Bearer", token.Trim());
                request.Headers.Add("X-Pulse-Weaver-Client", "streamerbot");
                request.Content = new StringContent(command.ToString(Formatting.None), Encoding.UTF8, "application/json");
                using (HttpResponseMessage response = Client.SendAsync(request).GetAwaiter().GetResult())
                {
                    string body = response.Content.ReadAsStringAsync().GetAwaiter().GetResult();
                    JObject result = JObject.Parse(body);
                    bool ok = response.IsSuccessStatusCode && result.Value<bool?>("ok") == true;
                    CPH.SetArgument("pulseweaver_ok", ok);
                    CPH.SetArgument("pulseweaver_status", (int)response.StatusCode);
                    CPH.SetArgument("pulseweaver_message", result.Value<string>("message") ?? "Check the Pulse Weaver response.");
                    CPH.SetArgument("pulseweaver_state", (result["state"] ?? new JObject()).ToString(Formatting.None));
                    if (!ok) CPH.LogError("Pulse Weaver rejected the command (HTTP " + (int)response.StatusCode + ").");
                    return ok;
                }
            }
        }
        catch (Exception exception)
        {
            // Do not print credentials, complete request headers, or raw exception messages.
            CPH.LogError("Pulse Weaver request failed (" + exception.GetType().Name + "). Check its local connection and JSON arguments.");
            return false;
        }
    }
}
