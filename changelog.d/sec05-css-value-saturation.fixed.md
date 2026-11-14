- **Out-of-range CSS numbers saturate instead of invoking UB.** `ValueToRGB`
  and dimension extraction clamp NaN and out-of-`int`-range doubles (e.g.
  `rgb(99999999999,0,0)`) to the representable range.
