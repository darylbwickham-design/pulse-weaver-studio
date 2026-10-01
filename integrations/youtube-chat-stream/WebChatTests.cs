using System.Net;
using System.Text.Json.Nodes;

internal static class WebChatTests
{
    const string Html = """
        <script>ytcfg.set({"INNERTUBE_CONTEXT":{"client":{"clientName":"WEB","clientVersion":"test-version"}}});</script>
        <script>var ytInitialData = {"contents":{"liveChatRenderer":{"header":{"subMenuItems":[
        {"title":"Top chat","continuation":{"reloadContinuationData":{"continuation":"top"}}},
        {"title":"Live chat","continuation":{"reloadContinuationData":{"continuation":"all"}}}]},
        "continuations":[{"reloadContinuationData":{"continuation":"top"}}]}}};</script>
        """;
    const string Response = """
        {"continuationContents":{"liveChatContinuation":{"continuations":[{"timedContinuationData":{"continuation":"next","timeoutMs":6000}}],"actions":[
        {"addChatItemAction":{"item":{"liveChatTextMessageRenderer":{"id":"message1","authorName":{"simpleText":"Creator"},"authorExternalChannelId":"author1","authorBadges":[{"liveChatAuthorBadgeRenderer":{"icon":{"iconType":"OWNER"}}}],"message":{"runs":[{"text":"hello 世界 "},{"emoji":{"shortcuts":[":wave:"]}}]}}}}},
        {"addChatItemAction":{"item":{"liveChatPaidMessageRenderer":{"id":"paid","authorName":{"simpleText":"Fan"},"purchaseAmountText":{"simpleText":"£2"},"message":{"simpleText":"thanks"}}}}},
        {"markChatItemAsDeletedAction":{"targetItemId":"message1"}},
        {"markChatItemsByAuthorAsDeletedAction":{"externalChannelId":"author1"}},
        {"replaceChatItemAction":{"targetItemId":"other","replacementItem":{"liveChatPlaceholderItemRenderer":{}}}},
        {"addLiveChatTickerItemAction":{"item":{"liveChatPaidMessageRenderer":{"id":"paid"}}}}
        ]}}}
        """;
    internal static async Task<int> Run()
    {
        int checks = 0;
        void Check(bool value) { ++checks; if (!value) throw new Exception("Web chat check " + checks); }
        var page = WebChat.Bootstrap(Html);
        Check(page.Continuation == "all");
        Check(WebChat.Str(page.Context["client"]?["clientVersion"]) == "test-version");
        Check(WebChat.Bootstrap(Html.Replace("var ytInitialData =", "window[\"ytInitialData\"] =")).Continuation == "all");
        Check(WebChat.Bootstrap("ytcfg.set(a);function x(){window.x=1;}ytcfg.set({\"unrelated\":true});" + Html).Continuation == "all");
        var parsed = WebChat.Parse(JsonNode.Parse(Response)!.AsObject());
        Check(parsed.DelayMs == 6000 && parsed.Continuation == "next");
        Check(parsed.Items.Count == 5); // Ticker copy never duplicates the paid message.
        Check(WebChat.Str(parsed.Items[0]?["snippet"]?["displayMessage"]) == "hello 世界 :wave:");
        Check(parsed.Items[0]?["authorDetails"]?["isChatOwner"]?.GetValue<bool>() == true);
        Check(WebChat.Str(parsed.Items[1]?["snippet"]?["type"]) == "superChatEvent");
        Check(WebChat.Str(parsed.Items[2]?["snippet"]?["messageDeletedDetails"]?["deletedMessageId"]) == "message1");
        Check(WebChat.Str(parsed.Items[3]?["snippet"]?["userBannedDetails"]?["bannedUserDetails"]?["channelId"]) == "author1");
        Check(WebChat.Str(parsed.Items[4]?["snippet"]?["messageDeletedDetails"]?["deletedMessageId"]) == "other");
        Check(WebChat.Text(JsonNode.Parse("""{"runs":[{"emoji":{"shortcuts":[],"image":{"accessibility":{"accessibilityData":{"label":"heart"}}}}}]}""")) == "heart");
        var delays = new List<int>(); var output = new List<string>();
        using var fake = new Server();
        await WebChat.Run("abcdefghijk", "", output.Add, CancellationToken.None, fake,
            (ms, _) => { delays.Add(ms); return Task.CompletedTask; }, 2);
        Check(fake.Requests == 3 && fake.MaxInFlight == 1);
        Check(fake.Cursors.SequenceEqual(new[] { "all", "next" }));
        Check(delays.SequenceEqual(new[] {6000}));
        Check(output.Count(x => x.Contains("webRequestStarted")) == 3);
        Check(!output.Any(x => x.Contains("rpcStarted")));
        using var cancelled = new CancellationTokenSource();
        using var stopping = new Server();
        try {
            await WebChat.Run("abcdefghijk", "", _ => {}, cancelled.Token, stopping,
                (_, token) => { cancelled.Cancel(); return Task.Delay(1, token); }, 3);
            throw new Exception("Web cancellation ignored");
        } catch (OperationCanceledException) { Check(stopping.Requests == 2); }
        using var resumed = new Server();
        await WebChat.Run("abcdefghijk", "resume", _ => {}, CancellationToken.None, resumed, (_, _) => Task.CompletedTask, 1);
        Check(resumed.Cursors.Single() == "resume");
        using var invalid = new Server();
        try { await WebChat.Run("https://attacker.invalid", "", _ => {}, CancellationToken.None, invalid); }
        catch (WebChat.Failure e) { Check(e.Reason == "invalidArgument" && invalid.Requests == 0); }
        using var denied = new Server { Code = HttpStatusCode.Forbidden };
        try { await WebChat.Run("abcdefghijk", "", _ => {}, CancellationToken.None, denied); }
        catch (WebChat.Failure e) { Check(e.Reason == "webChatRestricted" && denied.Requests == 1); }
        try { WebChat.Bootstrap(Html.Replace("\"header\":", "\"isReplay\":true,\"header\":")); }
        catch (WebChat.Failure e) { Check(e.Reason == "liveChatEnded"); }
        try { WebChat.Parse(new JsonObject()); }
        catch (WebChat.Failure e) { Check(e.Reason == "webChatChanged"); }
        Check(WebChat.Parse(JsonNode.Parse(Response.Replace("\"timeoutMs\":6000", "\"timeoutMs\":0"))!.AsObject()).DelayMs == 1000);
        Console.WriteLine($"PASS: {checks} web chat checks");
        return checks;
    }
    sealed class Server : HttpMessageHandler
    {
        internal int Requests, MaxInFlight;
        int inFlight;
        internal HttpStatusCode Code = HttpStatusCode.OK;
        internal List<string> Cursors = new();
        protected override async Task<HttpResponseMessage> SendAsync(HttpRequestMessage request, CancellationToken token)
        {
            token.ThrowIfCancellationRequested(); ++Requests;
            MaxInFlight = Math.Max(MaxInFlight, ++inFlight);
            try {
                if (request.RequestUri?.Host != "www.youtube.com" || request.Headers.Authorization is not null || request.Headers.Contains("Cookie"))
                    throw new Exception("Web transport used credentials or unexpected host");
                string content = Html;
                if (request.Method == HttpMethod.Post) {
                    if (request.RequestUri.AbsolutePath != "/youtubei/v1/live_chat/get_live_chat") throw new Exception("Unexpected endpoint");
                    var body = JsonNode.Parse(await request.Content!.ReadAsStringAsync(token));
                    if (body?["token"] is not null || body?["key"] is not null) throw new Exception("Data API credentials in web request");
                    Cursors.Add(WebChat.Str(body?["continuation"])); content = Response;
                }
                return new HttpResponseMessage(Code) { Content = new StringContent(content) };
            } finally { --inFlight; }
        }
    }
}
