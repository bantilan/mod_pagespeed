- **No request-path wait for remote config refresh with the native
  fetcher.** The refresh only progresses on the event loop that the wait
  would block, so blocking caches apply the cached copy without waiting
  (external caches keep the configured wait), with a startup warning.
