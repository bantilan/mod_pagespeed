- **Spooled request bodies are bounded and allocation-checked.** Bodies the
  module loads for its own handlers stop at 128 KiB instead of buffering
  without bound, and a failed pool allocation is reported rather than
  written through.
