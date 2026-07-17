# Asset types specs

| Type             | Extension     | Icon              | ChipText | ChipColor | CanBeCreated | Category | HasThumbnail | CanBeEdited | EditorTool    |
|------------------|---------------|-------------------|----------|-----------|--------------|----------|--------------|-------------|---------------|
| Node             | .tnode        | Box               | NODE     | Red       | false        |          | false*       | true        | NodeEditor    |
| Material         | .tmat         | Eclipse           | MAT      | Green     | true         | Visual   | false*       | true        | GenericEditor |
| MaterialInstance | .tmi          | Eclipse           | INST     | Green     | true         | Visual   | false*       | true        | GenericEditor |
| Shader           | .slang        | WandSparkles      | SHADER   | Green     | true         | Visual   | false        | false       |               |
| Mesh             | .tmesh        | Shapes            | MESH     | Blue      | false        |          | false*       | false       |               |
| Texture          | .ktx2         | Image             | TEX      | Orange    | false        |          | true         | false       |               |
| InputAction      | .taction      | Zap               | ACTION   | Yellow    | true         | Input    | false        | true        | GenericEditor |
| InputLayout      | .tlayout      | Gamepad2          | LAYOUT   | Yellow    | true         | Input    | false        | true        | GenericEditor |
| Haptics          | .thaptic      | Vibrate           | HAPTIC   | Yellow    | true         | Input    | false        | true        | HapticsEditor |
| Script           | .lua          | CodeXml           | LUA      | Magenta   | true         | Logic    | false        | false       |               | (Not implemented)
| Data             | .toml         | Database          | DATA     | Cyan      | true         | Data     | false        | true        | GenericEditor |
| Schema           | .schema.json  | Settings          | SCHEMA   | Cyan      | true         | Data     | false        | true        | SchemaEditor  |
| Curve            | .tcurve       | Spline            | CURVE    | Cyan      | true         | Data     | false        | true        | CurveEditor   |
| Audio Bank       | .bank         | AudioWaveform     | BANK     | Beige     | false        |          | false        | false       |               |
| Audio String     | .strings.bank | BookHeadphones    | BSTR     | Beige     | false        |          | false        | false       |               |
| Bus              | .tbus         | SlidersVertical   | BUS      | Beige     | false        |          | false        | false       |               |
| VCA              | .tvca         | SlidersHorizontal | VCA      | Beige     | false        |          | false        | false       |               |
| Port             | .taport       | Speaker           | PORT     | Beige     | false        |          | false        | false       |               |
| Audio Snapshot   | .tasnap       | Activity          | SNAP     | Beige     | false        |          | false        | false       |               |
| Audio Event      | .tae          | Volume2           | EVENT    | Beige     | false        |          | false        | false       |               |
