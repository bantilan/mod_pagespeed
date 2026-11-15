- **Background freshen fetches no longer reach back into the finished
  request.** A freshen whose fetch lock is granted after the user-facing
  request completed carries its own cache-put settings and fallback value,
  fixing a use-after-free on freed request memory.
