- **Beacon nonces fail closed on short entropy reads.** `InputFileNonceGenerator`
  retries short reads and returns a zero nonce (logging an error) when the
  entropy source is exhausted, instead of surfacing partially uninitialized
  bytes as a nonce.
