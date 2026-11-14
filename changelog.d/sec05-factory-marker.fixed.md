- **Harden nginx configuration-cycle handling.** The process-global factory
  marker is reset explicitly when a new cycle's main configuration is created
  (a stale marker no longer aborts the worker), and init-module errors are
  reported through the main configuration's message handler, which exists in
  the master process.
