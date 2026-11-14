- **In-place fetch failures release the driver and answer the client.** When
  an in-place fetch fails mid-body, the held-back headers are answered with
  the failure and the rewrite driver is freed instead of leaking (and hanging
  the client).
