# Pulse Weaver 1.14.0 unstable 10

Includes the YouTube streaming-chat corrections from experimental alpha 3 with the existing Control showcase.

Healthy chat connections have no forced 45-minute deadline. Normal completion resumes using the saved cursor, without the previous under-60-second failure rule. Automatic REST chat polling is removed. Transient failures retry with increasing delays; terminal failures stop. Repeated completions with no batches also receive a bounded reconnect delay.

Safe application-level request counters and connection results make the next live run diagnosable without logging credentials, chat IDs, continuation tokens, or message content in these diagnostics.

The helper protocol and native/helper lifecycle checks run entirely against mocks. The frontend is compiled, but the real YouTube connection-ending cause and live quota savings remain to be verified. If streaming is unavailable, incoming chat may be delayed or unavailable rather than falling back to continuous REST requests.

Scenes, ports, accounts, and stream routing retain their existing behavior. This package is for users to install through their chosen update channel; no local installation was changed during development.
