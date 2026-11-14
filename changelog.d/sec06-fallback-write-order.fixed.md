- **Fallback cache writes the value before its forwarding marker.** A crash
  between the two writes can no longer leave a marker pointing at
  non-existent data.
