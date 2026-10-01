using System.Net;
using System.Text;
using System.Text.Json;
using System.Text.Json.Nodes;
using System.Text.RegularExpressions;

// Anonymous web-chat transport. Never receives account credentials or sends Data API requests.
internal static class WebChat
{
    internal sealed class Failure(string reason) : Exception { internal string Reason => reason; }
    internal record Page(JsonObject Context, string Continuation);
    internal record Batch(JsonArray Items, string Continuation, int DelayMs);
    internal static string Text(JsonNode? node)
    {
        if (node is null) return "";
        if (node["simpleText"] is JsonValue simple) return simple.GetValue<string>();
        var text = new StringBuilder();
        if (node["runs"] is JsonArray runs)
            foreach (var run in runs)
                text.Append(Str(run?["text"]) is { Length: > 0 } value ? value :
                    (run?["emoji"]?["shortcuts"] is JsonArray { Count: > 0 } shortcuts ? Str(shortcuts[0]) : "") is { Length: > 0 } shortcut ? shortcut :
                    Str(run?["emoji"]?["image"]?["accessibility"]?["accessibilityData"]?["label"]));
        return text.ToString();
    }
    internal static string Str(JsonNode? value) => value is JsonValue v && v.TryGetValue<string>(out var s) ? s : "";
    static JsonNode? Find(JsonNode? node, string key)
    {
        if (node is JsonObject obj) {
            if (obj.TryGetPropertyValue(key, out var value)) return value;
            foreach (var child in obj) if (Find(child.Value, key) is { } found) return found;
        } else if (node is JsonArray array)
            foreach (var child in array) if (Find(child, key) is { } found) return found;
        return null;
    }
    // Parse a JSON object, never evaluate JavaScript supplied by a page.
    static JsonObject? ObjectAfter(string html, string marker, string? requiredKey = null)
    {
        int search = 0;
        while (search < html.Length) {
        int start = html.IndexOf(marker, search, StringComparison.Ordinal);
        if (start < 0) return null;
        search = start + marker.Length;
        start = search;
        while (start < html.Length && char.IsWhiteSpace(html[start])) ++start;
        if (start == html.Length || html[start] != '{') continue;
        int depth = 0; bool quoted = false, escape = false;
        for (int i = start; i < html.Length; ++i) {
            char c = html[i];
            if (quoted) {
                if (escape) escape = false;
                else if (c == '\\') escape = true;
                else if (c == '"') quoted = false;
            } else if (c == '"') quoted = true;
            else if (c == '{') ++depth;
            else if (c == '}' && --depth == 0) {
                try {
                    var value = JsonNode.Parse(html.AsSpan(start, i - start + 1).ToString()) as JsonObject;
                    if (value is not null && (requiredKey is null || value.ContainsKey(requiredKey))) return value;
                } catch (JsonException) { /* A JavaScript function or non-JSON config is not executed. */ }
                search = i + 1;
                break;
            }
        }
        }
        return null;
    }
    internal static Page Bootstrap(string html)
    {
        var initial = ObjectAfter(html, "var ytInitialData =") ?? ObjectAfter(html, "window[\"ytInitialData\"] =")
            ?? ObjectAfter(html, "ytInitialData=") ?? ObjectAfter(html, "ytInitialData =");
        var renderer = Find(initial, "liveChatRenderer");
        if (renderer is null) throw new Failure("webChatUnavailable");
        if (renderer["isReplay"]?.GetValue<bool>() == true) throw new Failure("liveChatEnded");
        var config = ObjectAfter(html, "ytcfg.set(", "INNERTUBE_CONTEXT");
        var context = config?["INNERTUBE_CONTEXT"] as JsonObject;
        if (context is null || string.IsNullOrEmpty(Str(context["client"]?["clientVersion"])))
            throw new Failure("webChatChanged");
        string token = "";
        // Use the unfiltered Live chat selection, not the default Top chat selection.
        if (Find(renderer, "subMenuItems") is JsonArray options)
            foreach (var option in options)
                if (Str(option?["title"]).Equals("Live chat", StringComparison.OrdinalIgnoreCase))
                    token = Str(option?["continuation"]?["reloadContinuationData"]?["continuation"]);
        if (token.Length == 0) token = Continuation(renderer).Token;
        if (token.Length == 0) throw new Failure("webChatUnavailable");
        return new Page((JsonObject)context.DeepClone(), token);
    }
    static (string Token, int Delay) Continuation(JsonNode? renderer)
    {
        if (renderer?["continuations"] is JsonArray continuations)
            foreach (var entry in continuations)
                foreach (var kind in new[] { "invalidationContinuationData", "timedContinuationData", "reloadContinuationData" }) {
                    var data = entry?[kind];
                    var token = Str(data?["continuation"]);
                    if (token.Length == 0) continue;
                    int delay = data?["timeoutMs"] is JsonValue n && n.TryGetValue<int>(out var ms) ? ms : 3000;
                    return (token, Math.Max(1000, delay));
                }
        return ("", 3000);
    }
    static JsonObject Event(string id, string type, string message = "", string user = "", string userId = "") => new() {
        ["id"] = id,
        ["snippet"] = new JsonObject { ["type"] = type, ["displayMessage"] = message },
        ["authorDetails"] = new JsonObject { ["displayName"] = user, ["channelId"] = userId }
    };
    static void Item(JsonNode? item, JsonArray result)
    {
        if (item is not JsonObject obj) return;
        foreach (var pair in obj) {
            if (pair.Value is not JsonObject r) continue;
            if (pair.Key is not ("liveChatTextMessageRenderer" or "liveChatPaidMessageRenderer" or
                "liveChatPaidStickerRenderer" or "liveChatMembershipItemRenderer")) continue;
            var id = Str(r["id"]);
            if (id.Length == 0) continue;
            var message = Text(r["message"]);
            var amount = Text(r["purchaseAmountText"]);
            if (message.Length == 0) message = Text(r["headerSubtext"]);
            if (message.Length == 0) message = Text(r["sticker"]?["accessibility"]?["accessibilityData"] is JsonObject a
                ? new JsonObject { ["simpleText"] = Str(a["label"]) } : null);
            if (amount.Length > 0) message = amount + (message.Length > 0 ? " · " + message : "");
            // All readable chat entries share the same unified feed; no separate browser pane.
            var e = Event(id, "textMessageEvent", message, Text(r["authorName"]), Str(r["authorExternalChannelId"]));
            e["webDisplay"] = true;
            if (pair.Key == "liveChatPaidMessageRenderer") {
                e["snippet"]!["type"] = "superChatEvent";
                e["snippet"]!["superChatDetails"] = new JsonObject { ["amountDisplayString"] = amount };
            } else if (pair.Key == "liveChatPaidStickerRenderer") e["snippet"]!["type"] = "superStickerEvent";
            else if (pair.Key == "liveChatMembershipItemRenderer") e["snippet"]!["type"] = "newSponsorEvent";
            if (r["authorBadges"] is JsonArray badges)
                foreach (var badge in badges) {
                    var b = badge?["liveChatAuthorBadgeRenderer"];
                    var icon = Str(b?["icon"]?["iconType"]);
                    var label = Str(b?["tooltip"]);
                    var author = e["authorDetails"]!;
                    if (icon == "OWNER") author["isChatOwner"] = true;
                    if (icon == "MODERATOR") author["isChatModerator"] = true;
                    if (icon == "VERIFIED") author["isVerified"] = true;
                    if (label.Contains("Member", StringComparison.OrdinalIgnoreCase)) author["isChatSponsor"] = true;
                }
            result.Add(e);
        }
    }
    internal static Batch Parse(JsonObject response)
    {
        var renderer = response["continuationContents"]?["liveChatContinuation"];
        if (renderer is null) throw new Failure("webChatChanged");
        var result = new JsonArray();
        if (renderer["actions"] is JsonArray actions)
            foreach (var action in actions) {
                if (action?["addChatItemAction"] is { } add) Item(add["item"], result);
                else if (action?["markChatItemAsDeletedAction"] is { } deleted) {
                    var id = Str(deleted["targetItemId"]);
                    var e = Event("delete:" + id, "messageDeletedEvent");
                    e["snippet"]!["messageDeletedDetails"] = new JsonObject { ["deletedMessageId"] = id };
                    if (id.Length > 0) result.Add(e);
                } else if (action?["removeChatItemAction"] is { } removed) {
                    var id = Str(removed["targetItemId"]);
                    var e = Event("delete:" + id, "messageDeletedEvent");
                    e["snippet"]!["messageDeletedDetails"] = new JsonObject { ["deletedMessageId"] = id };
                    if (id.Length > 0) result.Add(e);
                } else if (action?["markChatItemsByAuthorAsDeletedAction"] is { } banned) {
                    var id = Str(banned["externalChannelId"]);
                    // Repeated bans must also remove messages posted after an earlier timeout.
                    var e = Event("", "userBannedEvent");
                    e["snippet"]!["userBannedDetails"] = new JsonObject { ["bannedUserDetails"] = new JsonObject { ["channelId"] = id } };
                    if (id.Length > 0) result.Add(e);
                } else if (action?["replaceChatItemAction"] is { } replacement) {
                    // Deletion tombstones are delivered as replacement renderers on some web clients.
                    if (Find(replacement["replacementItem"], "liveChatPlaceholderItemRenderer") is not null) {
                        var id = Str(replacement["targetItemId"]);
                        var e = Event("delete:" + id, "messageDeletedEvent");
                        e["snippet"]!["messageDeletedDetails"] = new JsonObject { ["deletedMessageId"] = id };
                        if (id.Length > 0) result.Add(e);
                    }
                }
            }
        var continuation = Continuation(renderer);
        return new Batch(result, continuation.Token, continuation.Delay);
    }
    static async Task<string> Read(HttpClient http, HttpRequestMessage request, CancellationToken token)
    {
        using var response = await http.SendAsync(request, HttpCompletionOption.ResponseHeadersRead, token);
        if (response.StatusCode is HttpStatusCode.Forbidden or HttpStatusCode.Unauthorized)
            throw new Failure("webChatRestricted");
        if ((int)response.StatusCode == 429) throw new Failure("webChatRateLimited");
        response.EnsureSuccessStatusCode();
        using var stream = await response.Content.ReadAsStreamAsync(token);
        using var bytes = new MemoryStream();
        var buffer = new byte[16384];
        int size;
        while ((size = await stream.ReadAsync(buffer, token)) != 0) {
            if (bytes.Length + size > 4 * 1024 * 1024) throw new Failure("webChatChanged");
            bytes.Write(buffer, 0, size);
        }
        return Encoding.UTF8.GetString(bytes.ToArray());
    }
    internal static int RequestDelay(Batch batch) => Math.Min(batch.DelayMs, batch.Items.Count > 0 ? 2000 : 5000);

    // Drain one bounded response before requesting another. The cursor is committed only
    // after its final chunk, so cancellation cannot skip messages still in this buffer.
    internal static async Task<int> EmitSmoothed(Batch batch, Action<string> emit,
        Func<int, CancellationToken, Task> delay, CancellationToken stop)
    {
        int size = Math.Max(1, (batch.Items.Count + 15) / 16);
        int elapsed = 0;
        for (int offset = 0; offset < Math.Max(1, batch.Items.Count); offset += size) {
            stop.ThrowIfCancellationRequested();
            if (offset > 0) { await delay(40, stop); elapsed += 40; }
            stop.ThrowIfCancellationRequested();
            var items = new JsonArray();
            for (int i = offset; i < Math.Min(offset + size, batch.Items.Count); ++i)
                items.Add(batch.Items[i]?.DeepClone());
            var output = new JsonObject { ["items"] = items };
            if (offset + size >= batch.Items.Count) output["nextPageToken"] = batch.Continuation;
            emit(output.ToJsonString());
        }
        return elapsed;
    }
    internal static async Task Run(string videoId, string resume, Action<string> emit, CancellationToken stop,
        HttpMessageHandler? handler = null, Func<int, CancellationToken, Task>? delay = null, int maximumCalls = int.MaxValue)
    {
        if (!Regex.IsMatch(videoId, "^[A-Za-z0-9_-]{11}$")) throw new Failure("invalidArgument");
        delay ??= (ms, token) => Task.Delay(ms, token);
        using var http = new HttpClient(handler ?? new SocketsHttpHandler {
            AllowAutoRedirect = false, UseCookies = false, AutomaticDecompression = DecompressionMethods.All,
            ConnectTimeout = TimeSpan.FromSeconds(20)
        }) { Timeout = TimeSpan.FromSeconds(45) };
        http.DefaultRequestHeaders.UserAgent.ParseAdd("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/134.0.0.0 Safari/537.36");
        using var initialRequest = new HttpRequestMessage(HttpMethod.Get, "https://www.youtube.com/live_chat?is_popout=1&v=" + videoId + "&hl=en");
        emit(JsonSerializer.Serialize(new { _pulse = "webRequestStarted", attempt = 0 }));
        var page = Bootstrap(await Read(http, initialRequest, stop));
        emit(JsonSerializer.Serialize(new { _pulse = "webRequestCompleted", attempt = 0, messages = 0 }));
        var cursor = string.IsNullOrEmpty(resume) ? page.Continuation : resume;
        for (int attempt = 1; attempt <= maximumCalls; ++attempt) {
            stop.ThrowIfCancellationRequested();
            using var request = new HttpRequestMessage(HttpMethod.Post, "https://www.youtube.com/youtubei/v1/live_chat/get_live_chat?prettyPrint=false");
            request.Headers.Referrer = initialRequest.RequestUri;
            request.Headers.Add("Origin", "https://www.youtube.com");
            request.Headers.Add("X-Youtube-Client-Name", "1");
            request.Headers.Add("X-Youtube-Client-Version", Str(page.Context["client"]?["clientVersion"]));
            request.Content = new StringContent(new JsonObject { ["context"] = page.Context.DeepClone(), ["continuation"] = cursor }.ToJsonString(), Encoding.UTF8, "application/json");
            emit(JsonSerializer.Serialize(new { _pulse = "webRequestStarted", attempt }));
            var raw = await Read(http, request, stop);
            if (raw.StartsWith(")]}'", StringComparison.Ordinal)) raw = raw[(raw.IndexOf('\n') + 1)..];
            var batch = Parse(JsonNode.Parse(raw) as JsonObject ?? throw new Failure("webChatChanged"));
            int requestDelay = RequestDelay(batch);
            emit(JsonSerializer.Serialize(new { _pulse = "webRequestCompleted", attempt, messages = batch.Items.Count, delayMs = requestDelay, serverDelayMs = batch.DelayMs }));
            int drainedMs = await EmitSmoothed(batch, emit, delay, stop);
            if (batch.Continuation.Length == 0) throw new Failure("liveChatEnded");
            cursor = batch.Continuation;
            if (attempt < maximumCalls) await delay(Math.Max(0, requestDelay - drainedMs), stop);
        }
    }
}
