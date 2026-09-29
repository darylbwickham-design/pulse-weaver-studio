using System.Diagnostics;
using System.Text.Json;
using Google.Protobuf;
using Grpc.Core;
using Youtube.Api.V3;

// One client/channel per chat session, as in Google's reference implementation.
internal static class ChatSession
{
    internal static int ReconnectDelay(bool saveQuota, int emptyCalls)
        => emptyCalls == 0 || !saveQuota ? 1000 : Math.Min(30000, 5000 << Math.Min(emptyCalls - 1, 3));

    internal static async Task Run(
        V3DataLiveChatMessageService.V3DataLiveChatMessageServiceClient client,
        LiveChatMessageListRequest request, Metadata headers, bool saveQuota,
        Action<string> emit, CancellationToken stop,
        Func<int, CancellationToken, Task>? delay = null, int maximumCalls = int.MaxValue)
    {
        delay ??= (ms, token) => Task.Delay(ms, token);
        int emptyCalls = 0;
        for (int attempt = 1; attempt <= maximumCalls; ++attempt)
        {
            stop.ThrowIfCancellationRequested();
            emit(JsonSerializer.Serialize(new { _pulse = "rpcStarted", attempt }));
            var timer = Stopwatch.StartNew();
            int batches = 0, messages = 0;
            using (var call = Program.OpenStream(client, request, headers, stop))
            {
                await foreach (var response in call.ResponseStream.ReadAllAsync(stop))
                {
                    ++batches;
                    messages += response.Items.Count;
                    emit(JsonFormatter.Default.Format(response));
                    if (!string.IsNullOrEmpty(response.NextPageToken)) request.PageToken = response.NextPageToken;
                    if (!string.IsNullOrEmpty(response.OfflineAt) || response.Items.Any(item =>
                        item.Snippet?.Type == LiveChatMessageSnippet.Types.TypeWrapper.Types.Type.ChatEndedEvent))
                        return;
                }
                // Read the actual final status, never infer it from process exit.
                var status = call.GetStatus();
                if (status.StatusCode != StatusCode.OK) throw new RpcException(status);
            }
            emptyCalls = messages > 0 ? 0 : Math.Min(emptyCalls + 1, 4);
            var waitMs = ReconnectDelay(saveQuota, emptyCalls);
            emit(JsonSerializer.Serialize(new { _pulse = "rpcCompleted", attempt,
                durationMs = timer.ElapsedMilliseconds, batches, messages, grpcStatus = 0, delayMs = waitMs }));
            if (attempt < maximumCalls) await delay(waitMs, stop);
        }
    }
}
