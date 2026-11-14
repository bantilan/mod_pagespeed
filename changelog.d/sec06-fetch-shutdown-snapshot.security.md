- **Fetcher shutdown no longer iterates a mutating pool.** Active and pending
  fetches are snapshotted before cancellation, and shutdown completion races
  are closed with a mutex-guarded protocol, fixing shutdown use-after-free
  shapes in the native fetcher.
