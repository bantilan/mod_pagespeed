- **Native fetches fail when the body mismatches its length.** Over-long
  bodies are capped and flagged (the connection is not reused) and short
  bodies at close are failures instead of being served as complete; HEAD
  responses finish with their headers.
