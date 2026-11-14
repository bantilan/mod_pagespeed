- **Beacon nonces longer than issued are refused before comparison.**
  `ValidateAndExpireNonce` rejects over-long nonces up front, so a sender can
  no longer choose how much work each constant-time comparison does.
