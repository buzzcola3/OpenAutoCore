# SensorHandler

**Channel:** SENSOR

Receives JSON sensor data from OpenAutoTransport via `onSensorEvent()` and encodes it as protobuf `SensorBatch` messages. Each sensor type is enabled by listing it in the `sensor_source_service.sensors` array of the service discovery config.

## Sensor Types

| JSON key | Proto enum | Proto Message | Description |
|---|---|---|---|
| `location` | `SENSOR_LOCATION` | LocationData | GPS position |
| `compass` | `SENSOR_COMPASS` | CompassData | Heading / orientation |
| `speed` | `SENSOR_SPEED` | SpeedData | Vehicle speed |
| `rpm` | `SENSOR_RPM` | RpmData | Engine RPM |
| `odometer` | `SENSOR_ODOMETER` | OdometerData | Distance travelled |
| `fuel` | `SENSOR_FUEL` | FuelData | Fuel level and range |
| `parking_brake` | `SENSOR_PARKING_BRAKE` | ParkingBrakeData | Parking brake state |
| `gear` | `SENSOR_GEAR` | GearData | Current gear |
| `night_mode` | `SENSOR_NIGHT_MODE` | NightModeData | Day / night mode |
| `environment` | `SENSOR_ENVIRONMENT_DATA` | EnvironmentData | Temperature, pressure, rain |
| `hvac` | `SENSOR_HVAC_DATA` | HvacData | Climate control |
| `driving_status` | `SENSOR_DRIVING_STATUS_DATA` | DrivingStatusData | Driving restriction level |
| `dead_reckoning` | `SENSOR_DEAD_RECKONING_DATA` | DeadReckoningData | Steering angle, wheel speeds |
| `passenger` | `SENSOR_PASSENGER_DATA` | PassengerData | Passenger presence |
| `door` | `SENSOR_DOOR_DATA` | DoorData | Door / hood / trunk state |
| `light` | `SENSOR_LIGHT_DATA` | LightData | Headlights, turn signals, hazards |
| `tire_pressure` | `SENSOR_TIRE_PRESSURE_DATA` | TirePressureData | Per-wheel tire pressure |
| `accelerometer` | `SENSOR_ACCELEROMETER_DATA` | AccelerometerData | 3-axis acceleration |
| `gyroscope` | `SENSOR_GYROSCOPE_DATA` | GyroscopeData | 3-axis rotation speed |
| `gps_satellite` | `SENSOR_GPS_SATELLITE_DATA` | GpsSatelliteData | Satellite constellation info |
| `toll_card` | `SENSOR_TOLL_CARD` | TollCardData | Toll transponder presence |

## JSON Payload Format

The transport delivers a JSON object with one or more sensor keys. All numeric values use real-world units — the handler converts to the protocol's fixed-point encoding internally.

```json
{
  "location": {
    "latitude": 48.1486,
    "longitude": 17.1077,
    "accuracy_m": 5.0,
    "altitude_m": 134.0,
    "speed_mps": 13.9,
    "bearing_deg": 90.0
  },
  "compass": {
    "bearing_deg": 180.0,
    "pitch_deg": 0.5,
    "roll_deg": -0.2
  },
  "speed": {
    "speed_mps": 13.9,
    "cruise_engaged": true,
    "cruise_set_speed_mps": 16.7
  },
  "rpm": { "rpm": 2500 },
  "odometer": { "kms": 45321.7, "trip_kms": 123.4 },
  "fuel": { "level": 65, "range": 420, "low_fuel_warning": false },
  "parking_brake": { "engaged": false },
  "gear": { "gear": "D" },
  "night_mode": { "enabled": false },
  "environment": { "temperature_c": 22.5, "pressure_kpa": 101.3, "rain": 0 },
  "hvac": { "target_temperature_c": 22.0, "current_temperature_c": 23.1 },
  "driving_status": { "status": "unrestricted" },
  "dead_reckoning": {
    "steering_angle_deg": 5.2,
    "wheel_speeds_mps": [13.9, 13.9, 14.0, 14.0]
  },
  "passenger": { "present": true },
  "door": { "hood_open": false, "trunk_open": false, "doors": [false, false, false, false] },
  "light": { "headlight": "on", "turn_indicator": "none", "hazard_lights": false },
  "tire_pressure": { "pressures_kpa": [220.0, 220.0, 215.0, 215.0] },
  "accelerometer": { "x": 0.1, "y": 0.0, "z": 9.81 },
  "gyroscope": { "x": 0.0, "y": 0.0, "z": 0.01 },
  "gps_satellite": {
    "in_use": 8,
    "in_view": 12,
    "satellites": [
      { "prn": 1, "snr": 30.0, "used_in_fix": true, "azimuth_deg": 45.0, "elevation_deg": 60.0 }
    ]
  },
  "toll_card": { "present": false }
}
```

## Gear Values

`"N"` / `"neutral"`, `"1"`–`"10"`, `"D"` / `"drive"`, `"P"` / `"park"`, `"R"` / `"reverse"`

## Driving Status Values

`"unrestricted"`, `"no_video"`, `"no_keyboard_input"`, `"no_voice_input"`, `"no_config"`, `"limit_message_len"`

## Light Values

- **headlight**: `"off"`, `"on"`, `"high"`
- **turn_indicator**: `"none"`, `"left"`, `"right"`
