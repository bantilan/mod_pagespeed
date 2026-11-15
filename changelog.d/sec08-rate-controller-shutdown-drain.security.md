- **RateController shutdown drains iteratively.** Dropping a deep per-host
  queue on shutdown no longer recurses once per queued fetch, which could
  overflow the worker-thread stack and crash under load.
