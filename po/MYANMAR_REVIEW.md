# Burmese translation review

The catalog is fully translated, but a Myanmar fansubber should review the
entries in context before calling the interface complete. In particular,
review these terms:

* 39 Mode: “mora”, “attack”, “assignment”, “lane” and “handoff” in the
  timing workflow. Some are retained in English where their precise meaning
  depends on the tool.
* Motion Tracking: “sample”, “tolerance”, “tracking channel”, “jitter” and
  “spike”, especially in messages about edited ASS motion and clip geometry.
* Mangetsu: “stop”, “anchor”, “placement”, “bubble” and “callout”.

Some fork UI still constructs messages outside gettext, including dynamic
39 Mode guidance and OCR backend diagnostics. Audit those producers before
describing the entire application UI as localized.

Check the result in the actual UI for wording, mnemonic collisions, wrapping,
clipping, Myanmar font fallback and combining-mark shaping. The required file
terms are စာဖိုင်, အသံဖိုင် and ဗီဒီယိုဖိုင်; subtitle text and lines are
separate concepts. Do not fill uncertain messages with guesses or English
copies just to increase the completion count.
