using Google.Protobuf;
using Google.Protobuf.WellKnownTypes;
using Grpc.Core;
using Youtube.Api.V3;
using Grpc.Net.Client;
using System.Net;
using System.Net.Http.Headers;

internal static class SelfTest
{
    public static int Run()
    {
        int checks = 0;
        void Check(bool condition) { ++checks; if (!condition) throw new Exception("Regression failure " + checks); }
        foreach (var pair in new[] {
            (StatusCode.Unauthenticated, "unauthenticated"), (StatusCode.PermissionDenied, "forbidden"),
            (StatusCode.ResourceExhausted, "rateLimitExceeded"), (StatusCode.Unimplemented, "streamUnsupported"),
            (StatusCode.NotFound, "liveChatNotFound"), (StatusCode.FailedPrecondition, "liveChatEnded"),
            (StatusCode.Unavailable, "streamUnavailable"),
            (StatusCode.DeadlineExceeded, "deadlineExceeded"), (StatusCode.Cancelled, "cancelled") })
            Check(Program.Reason(new RpcException(new Status(pair.Item1, "secret must not escape"))) == pair.Item2);
        var status = new Google.Rpc.Status { Code = 8 };
        status.Details.Add(Any.Pack(new Google.Rpc.ErrorInfo { Reason = "QUOTA_EXCEEDED" }));
        Check(Program.Reason(new RpcException(new Status(StatusCode.ResourceExhausted, "private"),
            new Metadata { { "grpc-status-details-bin", status.ToByteArray() } })) == "quotaExceeded");
        var request = new LiveChatMessageListRequest { LiveChatId = "chat", PageToken = "resume" };
        request.Part.Add(new[] { "id", "snippet", "authorDetails" });
        var decoded = LiveChatMessageListRequest.Parser.ParseFrom(request.ToByteArray());
        Check(decoded.PageToken == "resume" && decoded.Part.Count == 3);
        var response = new LiveChatMessageListResponse { NextPageToken = "next" };
        Check(JsonFormatter.Default.Format(response).Contains("nextPageToken"));
        var message = JsonParser.Default.Parse<LiveChatMessageListResponse>("""
            {"nextPageToken":"resume","items":[{"id":"id","snippet":{"type":"TEXT_MESSAGE_EVENT","displayMessage":"Hello 世界 🎉"},"authorDetails":{"displayName":"Creator","isChatModerator":true}}]}
            """);
        var messageJson = JsonFormatter.Default.Format(LiveChatMessageListResponse.Parser.ParseFrom(message.ToByteArray()));
        Check(messageJson.Contains("TEXT_MESSAGE_EVENT") && messageJson.Contains("世界") && messageJson.Contains("isChatModerator"));
        TransportTest().GetAwaiter().GetResult();
        ++checks;
        SessionTest().GetAwaiter().GetResult();
        checks += 8;
        WebChatTests.Run().GetAwaiter().GetResult();
        Console.WriteLine($"PASS: {checks} streaming protocol checks");
        return 0;
    }

    private static async Task SessionTest()
    {
        using var server = new SessionServer();
        using var channel = GrpcChannel.ForAddress("https://youtube.googleapis.com", new GrpcChannelOptions { HttpHandler = server });
        var client = new V3DataLiveChatMessageService.V3DataLiveChatMessageServiceClient(channel);
        var waits = new List<int>();
        var output = new List<string>();
        var headers = new Metadata { { "authorization", "Bearer test-only" } };
        await ChatSession.Run(client, new LiveChatMessageListRequest { LiveChatId = "fake", PageToken = "resume" },
            headers, true, output.Add, CancellationToken.None,
            (ms, _) => { waits.Add(ms); return Task.CompletedTask; }, 6);
        if (server.Calls != 6) throw new Exception("Duplicate RPCs");
        if (!waits.SequenceEqual(new[] { 5000, 10000, 20000, 1000, 5000 })) throw new Exception("Quiet response backoff or message reset failed");
        if (!server.Cursors.SequenceEqual(new[] { "resume", "cursor1", "cursor2", "cursor3", "cursor4", "cursor5" })) throw new Exception("Cursor resumption failed");
        if (output.Count(line => line.Contains("rpcStarted")) != 6 || output.Count(line => line.Contains("rpcCompleted")) != 6) throw new Exception("RPC diagnostics failed");
        if (ChatSession.ReconnectDelay(true, 999) != 30000 || ChatSession.ReconnectDelay(false, 999) != 1000) throw new Exception("Delay bounds failed");

        using var cancelled = new CancellationTokenSource();
        int before = server.Calls;
        try {
            await ChatSession.Run(client, new LiveChatMessageListRequest { LiveChatId = "fake" }, headers, true, _ => {}, cancelled.Token,
                (ms, token) => { cancelled.Cancel(); return Task.Delay(ms, token); }, 3);
            throw new Exception("Cancellation ignored");
        } catch (OperationCanceledException) { }
        if (server.Calls != before + 1) throw new Exception("RPC started after cancellation during delay");

        server.Fail = true;
        before = server.Calls;
        try {
            await ChatSession.Run(client, new LiveChatMessageListRequest { LiveChatId = "fake" }, headers, true, _ => {}, CancellationToken.None,
                (_, _) => Task.CompletedTask, 3);
            throw new Exception("Terminal error ignored");
        } catch (RpcException error) when (error.StatusCode == StatusCode.ResourceExhausted) { }
        if (server.Calls != before + 1) throw new Exception("Helper retried a rejected RPC");

        server.Fail = false; server.Offline = true;
        before = server.Calls;
        await ChatSession.Run(client, new LiveChatMessageListRequest { LiveChatId = "fake" }, headers, true, _ => {}, CancellationToken.None,
            (_, _) => Task.CompletedTask, 3);
        if (server.Calls != before + 1) throw new Exception("Offline chat reconnected");
    }

    private sealed class SessionServer : HttpMessageHandler
    {
        internal int Calls;
        internal bool Fail, Offline;
        internal readonly List<string> Cursors = new();
        protected override async Task<HttpResponseMessage> SendAsync(HttpRequestMessage request, CancellationToken cancellationToken)
        {
            if (request.Headers.Contains("grpc-timeout")) throw new Exception("Unexpected RPC deadline");
            ++Calls;
            var bytes = await request.Content!.ReadAsByteArrayAsync(cancellationToken);
            Cursors.Add(LiveChatMessageListRequest.Parser.ParseFrom(bytes[5..]).PageToken);
            var batch = new LiveChatMessageListResponse { NextPageToken = "cursor" + Calls };
            if (Calls == 4) batch.Items.Add(new LiveChatMessage { Id = "message" });
            if (Offline) batch.OfflineAt = "2026-09-29T22:00:00Z";
            var payload = batch.ToByteArray();
            var frame = new byte[payload.Length + 5];
            System.Buffers.Binary.BinaryPrimitives.WriteInt32BigEndian(frame.AsSpan(1, 4), payload.Length);
            payload.CopyTo(frame, 5);
            var response = new HttpResponseMessage(HttpStatusCode.OK) { Version = new Version(2, 0),
                Content = new ByteArrayContent(Fail ? Array.Empty<byte>() : frame) };
            response.Content.Headers.ContentType = new MediaTypeHeaderValue("application/grpc");
            response.TrailingHeaders.Add("grpc-status", Fail ? "8" : "0");
            return response;
        }
    }

    private static async Task TransportTest()
    {
        using var channel = GrpcChannel.ForAddress("https://youtube.googleapis.com", new GrpcChannelOptions { HttpHandler = new FakeServer() });
        var client = new V3DataLiveChatMessageService.V3DataLiveChatMessageServiceClient(channel);
        using var call = Program.OpenStream(client, new LiveChatMessageListRequest { LiveChatId = "fake", PageToken = "resume" },
            new Metadata { { "authorization", "Bearer test-only" } }, CancellationToken.None);
        var cursors = new List<string>();
        await foreach (var item in call.ResponseStream.ReadAllAsync()) cursors.Add(item.NextPageToken);
        if (!cursors.SequenceEqual(new[] { "one", "two" })) throw new Exception("gRPC response framing failed");
    }

    private sealed class FakeServer : HttpMessageHandler
    {
        protected override async Task<HttpResponseMessage> SendAsync(HttpRequestMessage request, CancellationToken cancellationToken)
        {
            if (request.RequestUri?.AbsolutePath != "/youtube.api.v3.V3DataLiveChatMessageService/StreamList" ||
                request.Headers.Authorization?.ToString() != "Bearer test-only") throw new Exception("gRPC route or authentication failed");
            if (request.Headers.Contains("grpc-timeout")) throw new Exception("Healthy streams must not have a forced deadline");
            var requestBytes = await request.Content!.ReadAsByteArrayAsync(cancellationToken);
            var decoded = LiveChatMessageListRequest.Parser.ParseFrom(requestBytes[5..]);
            if (decoded.PageToken != "resume") throw new Exception("Resume cursor missing");
            using var frames = new MemoryStream();
            foreach (var cursor in new[] { "one", "two" })
            {
                var bytes = new LiveChatMessageListResponse { NextPageToken = cursor }.ToByteArray();
                frames.WriteByte(0);
                var size = new byte[4]; System.Buffers.Binary.BinaryPrimitives.WriteInt32BigEndian(size, bytes.Length);
                frames.Write(size); frames.Write(bytes);
            }
            var response = new HttpResponseMessage(HttpStatusCode.OK) { Version = new Version(2, 0), Content = new ByteArrayContent(frames.ToArray()) };
            response.Content.Headers.ContentType = new MediaTypeHeaderValue("application/grpc");
            response.TrailingHeaders.Add("grpc-status", "0");
            return response;
        }
    }
}
