- **Event-channel shutdown is race-safe.** The channel pointer is atomic with
  a null-tolerant protocol, late completions are dropped after teardown, the
  connection object is kept alive instead of freed under late writers, pipe
  writes refuse after close, and the read side closes through nginx.
