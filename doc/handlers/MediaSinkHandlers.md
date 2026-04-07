# MediaSinkHandlers

**Channels:** MEDIA_SINK_VIDEO, MEDIA_SINK_MEDIA_AUDIO, MEDIA_SINK_GUIDANCE_AUDIO, MEDIA_SINK_SYSTEM_AUDIO, MEDIA_SINK_TELEPHONY_AUDIO

5 handlers that receive media frames from the phone and push raw data to the frontend via OpenAutoTransport.

| Handler | Channel | Content |
|---|---|---|
| `MediaSinkVideoHandler` | MEDIA_SINK_VIDEO | H.264/VP9 video frames |
| `MediaSinkAudioHandler` | MEDIA_SINK_MEDIA_AUDIO | Media audio |
| `GuidanceAudioHandler` | MEDIA_SINK_GUIDANCE_AUDIO | Navigation voice prompts |
| `SystemAudioHandler` | MEDIA_SINK_SYSTEM_AUDIO | System sounds and alerts |
| `TelephonyAudioHandler` | MEDIA_SINK_TELEPHONY_AUDIO | Phone call voice audio |

Each receives an `OpenAutoTransport` pointer at construction. They decode incoming AA media frames and push raw video/audio data out via `transport->send()` with the appropriate `MsgType`.
