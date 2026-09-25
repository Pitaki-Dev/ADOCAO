# Level input for the `Render Video` workflow

Drop the level to render here:

| File | Required | Notes |
|---|---|---|
| `level.adofai` | yes | The level. Replaced wholesale when `level_url` is passed to the workflow. |
| `music.ogg` | no | The song. Any container ffmpeg can read (ogg/mp3/wav/flac/m4a); the extension does not matter. |

Then run **Actions → Render Video → Run workflow**.

You can also skip these files entirely and pass `level_url` / `music_url`
instead — useful for keeping copyrighted audio out of git.
