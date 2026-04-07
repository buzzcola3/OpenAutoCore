# InputSourceHandler

**Channel:** INPUT_SOURCE

Receives touch and key events from the frontend via `onTouchEvent()` and encodes them as protobuf `InputReport` messages for the phone.

## Touch Pipeline

1. Frontend sends normalized floats (0.0–1.0) for X/Y, plus pointer ID and action, as a 16-byte binary payload
2. Handler reads touchscreen dimensions and video margins from the service discovery JSON config (`UserServiceDiscoveryResponse.json`, falling back to `ServiceDiscoveryResponse.default.json`)
3. Normalized coords are scaled to pixel coords, offset by half the margin to account for video letterboxing
4. Packed into a `TouchEvent` protobuf and sent to the phone

## Touch Coordinate Formula

```
screenPixel = round(normalized × (touchDim − 1))
videoPixel  = screenPixel − margin / 2
clamped to [0, touchDim − margin − 1]
```
