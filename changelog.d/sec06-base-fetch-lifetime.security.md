- **Base-fetch callback no longer finalizes under a live upstream.**
  Output that only went down the filter chain is not finalized while the
  origin still owns the request; requests ending under an upstream hold
  the reference its cleanup drops first.
