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
            (StatusCode.Unavailable, "streamUnavailable") })
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
        Console.WriteLine($"PASS: {checks} streaming protocol checks");
        return 0;
    }

    private static async Task TransportTest()
    {
        using var channel = GrpcChannel.ForAddress("https://youtube.googleapis.com", new GrpcChannelOptions { HttpHandler = new FakeServer() });
        var client = new V3DataLiveChatMessageService.V3DataLiveChatMessageServiceClient(channel);
        using var call = client.StreamList(new LiveChatMessageListRequest { LiveChatId = "fake", PageToken = "resume" },
            new Metadata { { "authorization", "Bearer test-only" } });
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
