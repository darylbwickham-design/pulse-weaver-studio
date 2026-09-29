# Pulse Weaver 1.14.0 experimental alpha 3

YouTube chat now stays on Google's streaming API instead of automatically switching to continuous REST polling.

- Keep healthy streaming calls open; remove the locally imposed 45-minute deadline.
- Resume with the saved page token after normal completion. Remove the unsupported rule that called a successful connection under 60 seconds a failure.
- Back off transient errors and repeated completions with no response batches. A valid response does not trigger the empty-completion guard.
- Stop on terminal chat errors. Preserve one receive subscription per distinct chat ID in Dual.
- Record safe application-level request counts, connection duration, batch counts, and numeric gRPC status. Logs omit credentials, chat IDs, cursors, and message content from these diagnostics.
- Recognize offline indications in the REST API wrapper, although automatic REST chat polling has been removed.

The tradeoff is explicit: if streaming cannot connect, chat retries with delays or reports an unavailable service rather than spending quota on automatic polling. Sending chat and requested moderation remain available when the chat session permits them.

Validation: helper protocol checks and native/helper lifecycle mocks, plus a frontend build. The previous live connection-ending cause has not been reproduced against YouTube; actual quota savings still need measurement. No user installation was changed to run these checks.

After updating, run a short YouTube session. The application log's `[YouTube requests]` entries distinguish streamList attempts from REST operations and report how each chat connection ended. Do not infer a guaranteed quota cost from connection duration alone.
