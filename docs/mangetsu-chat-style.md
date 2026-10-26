# Mangetsu Chat Style Editor

The phone/chat button immediately after Mangetsu Gradient in the subtitle edit
toolbar opens the editor for the active dialogue line. It is also available as
the `edit/chat/style` command.

Select a callout beside the mock phone to change the corresponding color,
alpha, border or text-outline width. Both message sides remain visible. Alpha
uses ASS transparency: 0 is opaque and 255 is transparent. Header colors are
opaque RGB; the renderer has no separate header alpha tags. Widths use ASS
script coordinates; the mock phone illustrates the appearance rather than
reproducing the renderer's geometry. The Read mark callout edits the final
receipt state and delay in milliseconds; the preview does not animate that delay.

Apply updates the active line in one undoable action. OK applies and closes.
Cancel discards unapplied changes; the mock preview never modifies subtitles.
Opening and accepting an unchanged dialog does not author defaults. There is
no automatic chat-mode insertion, and message contents and unrelated formatting
remain unchanged. Ordinary inline color tags and tags nested inside transforms
are preserved. Editing a side writes its complete twelve-field appearance tuple.

New / Save As saves a named appearance in Aegisub's user configuration. Select
a preset to load it, then Apply to write its actual tags into the line. Save
updates the selected preset; Rename and Delete manage it. Names cannot silently
replace a different preset. Presets contain only appearance and receipt settings,
never contact names, message bodies, chat mode, event timing or animation.
Saved presets survive restarting Aegisub, and applied lines are self-contained.

Swap Left / Right exchanges all bubble appearance fields. Reset selected restores
the selected surface, while Reset All restores the dialogue style's defaults and
automatic header colors.

The serializer follows [Mangetsu's native-chat API](https://github.com/amanosatosi/libassmod/blob/206d81cfd302d453cd80e53404e01816edd8f260/docs/native-chat.md):
`\msgleft` / `\msgright` take text color/alpha, name color/alpha, bubble
color/alpha, border color/alpha/size, then outline color/alpha/size. Colors are
ASS BGR hexadecimal; read delay uses compact `\readtimeN` syntax.
