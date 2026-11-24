# Burmese translation review

The catalog needs a full contextual review by a Myanmar fansubber; translation
counts and format checks do not verify natural wording. Project-owner choices:

* Subtitle file: စာဖိုင်; audio file: အသံဖိုင်; video file: ဗီဒီယိုဖိုင်.
  Subtitle lines and text are different concepts.
* ASS Actor/Name field: နာမည်. Motion Tracking: လှုပ်ရှားမှုပုံတူကူးခြင်း;
  Track Motion: လှုပ်ရှားမှု ပုံတူကူးရန်.
* Undo: လုပ်ထားတာနောက်ဆုတ်; Redo: လုပ်ထားတာရှေ့တိုး. Toolbar
  labels use နောက်ဆုတ် and ရှေ့တိုး. These describe edit history; do not
  apply them to navigation, reset, revert or removing comments.
* Subtitle Save As: နာမည်ပြောင်းပြီးစာဖိုင်သိမ်းရန်; subtitle Export:
  ပုံစံပြောင်းပြီးသိမ်းရန်. Other save and export operations need context.
* Recent lists use အရင်ကသုံးတဲ့; media reload means ပြန်တင်ရန်, not play.
  Short ongoing status messages can use နေတုန်း when context is clear.

In particular, review these terms:

* 39 Mode: “mora”, “attack”, “assignment”, “lane” and “handoff” in the
  timing workflow. Some are retained in English where their precise meaning
  depends on the tool.
* Motion Tracking: “sample”, “tolerance”, “tracking channel”, “jitter” and
  “spike”, especially in messages about edited ASS motion and clip geometry.
* Mangetsu: “stop”, “anchor”, “placement”, “bubble” and “callout”.

Some fork UI still constructs messages outside gettext. In particular,
`CompactResultReason` and its dynamic match reasons in 39 Mode, the inspector
text, and OCR runtime debug details need further review. The catalog's other
entries have not all received the requested contextual language review. Audit
those areas before describing the entire application UI as localized.

Check the result in the actual UI for wording, mnemonic collisions, wrapping,
clipping, Myanmar font fallback and combining-mark shaping. The required file
terms are စာဖိုင်, အသံဖိုင် and ဗီဒီယိုဖိုင်; subtitle text and lines are
separate concepts. Do not fill uncertain messages with guesses or English
copies just to increase the completion count.
