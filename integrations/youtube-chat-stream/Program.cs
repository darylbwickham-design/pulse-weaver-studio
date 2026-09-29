using System.Text.Json;
using Google.Protobuf;
using Grpc.Core;
using Grpc.Net.Client;
using Youtube.Api.V3;

// Tokens travel only over inherited stdin, never argv, environment, files or logs.
// Stdout is bounded NDJSON consumed privately by the native frontend.
internal static class Program
{
    internal static string Reason(RpcException error)
    {
        foreach (var entry in error.Trailers)
        {
            if (entry.Key != "grpc-status-details-bin") continue;
            try
            {
                var status = Google.Rpc.Status.Parser.ParseFrom(entry.ValueBytes);
                foreach (var detail in status.Details)
                    if (detail.Is(Google.Rpc.ErrorInfo.Descriptor))
                    {
                        var reason = detail.Unpack<Google.Rpc.ErrorInfo>().Reason;
                        if (reason is "QUOTA_EXCEEDED" or "DAILY_LIMIT_EXCEEDED" or "quotaExceeded" or "dailyLimitExceeded") return "quotaExceeded";
                    }
            }
            catch (InvalidProtocolBufferException) { }
        }
        return error.StatusCode switch
        {
            StatusCode.Unauthenticated => "unauthenticated",
            StatusCode.PermissionDenied => "forbidden",
            StatusCode.FailedPrecondition => "liveChatEnded",
            StatusCode.NotFound => "liveChatNotFound",
            StatusCode.ResourceExhausted => "rateLimitExceeded",
            StatusCode.Unimplemented => "streamUnsupported",
            StatusCode.InvalidArgument => "invalidArgument",
            StatusCode.DeadlineExceeded => "deadlineExceeded",
            StatusCode.Cancelled => "cancelled",
            _ => "streamUnavailable"
        };
    }

    private static void Error(string reason, int grpcStatus = -1) => Console.WriteLine(JsonSerializer.Serialize(new { error = reason, grpcStatus }));

    internal static AsyncServerStreamingCall<LiveChatMessageListResponse> OpenStream(
        V3DataLiveChatMessageService.V3DataLiveChatMessageServiceClient client,
        LiveChatMessageListRequest request, Metadata headers, CancellationToken cancellationToken)
        => client.StreamList(request, headers, cancellationToken: cancellationToken);

    public static async Task<int> Main(string[] args)
    {
        Console.InputEncoding = new System.Text.UTF8Encoding(false);
        Console.OutputEncoding = new System.Text.UTF8Encoding(false);
        if (args is ["--self-test"]) return SelfTest.Run();
        if (args.Length != 0) return 2;
        try
        {
            var line = await Console.In.ReadLineAsync();
            if (line == null || line.Length > 65536) return 2;
            using var input = JsonDocument.Parse(line);
            var root = input.RootElement;
            var token = root.GetProperty("token").GetString();
            var chat = root.GetProperty("chatId").GetString();
            if (string.IsNullOrEmpty(token) || string.IsNullOrEmpty(chat)) return 2;
            using var stop = new CancellationTokenSource();
            // EOF after parent exit closes the connection even after a frontend crash.
            _ = Task.Run(async () => { await Console.In.ReadLineAsync(); stop.Cancel(); });
            using var handler = new SocketsHttpHandler { ConnectTimeout = TimeSpan.FromSeconds(20) };
            using var channel = GrpcChannel.ForAddress("https://youtube.googleapis.com", new GrpcChannelOptions
            {
                HttpHandler = handler,
                MaxReceiveMessageSize = 4 * 1024 * 1024
            });
            var client = new V3DataLiveChatMessageService.V3DataLiveChatMessageServiceClient(channel);
            var request = new LiveChatMessageListRequest { LiveChatId = chat };
            request.Part.Add(new[] { "id", "snippet", "authorDetails" });
            if (root.TryGetProperty("pageToken", out var cursor) && !string.IsNullOrEmpty(cursor.GetString())) request.PageToken = cursor.GetString();
            var headers = new Metadata { { "authorization", "Bearer " + token } };
            if (root.TryGetProperty("persistent", out var persistent) && persistent.GetBoolean())
            {
                var saveQuota = !root.TryGetProperty("saveQuota", out var policy) || policy.GetBoolean();
                await ChatSession.Run(client, request, headers, saveQuota, Console.WriteLine, stop.Token);
                return 0;
            }
            // Keep healthy connections open; the parent refreshes expired credentials on reconnect.
            using var call = OpenStream(client, request, headers, stop.Token);
            await foreach (var response in call.ResponseStream.ReadAllAsync(stop.Token))
            {
                Console.WriteLine(JsonFormatter.Default.Format(response));
                if (!string.IsNullOrEmpty(response.OfflineAt)) return 0;
            }
            Console.WriteLine(JsonSerializer.Serialize(new { _pulse = "rpcCompleted", grpcStatus = (int)call.GetStatus().StatusCode }));
            return 0;
        }
        catch (RpcException error) { Error(Reason(error), (int)error.StatusCode); return 0; }
        catch (OperationCanceledException) { Error("cancelled"); return 0; }
        catch { Error("streamUnavailable"); return 1; }
    }
}
