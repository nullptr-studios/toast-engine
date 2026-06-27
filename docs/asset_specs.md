# Asset types specs

| Type             | Extension    | Icon         | ChipText | ChipColor | CanBeCreated | Category | HasThumbnail | CanBeEdited | EditorTool    |
|------------------|--------------|--------------|----------|-----------|--------------|----------|--------------|-------------|---------------|
| Node             | .tnode       | Box          | NODE     | Red       | false        |          | false*       | true        | NodeEditor    |
| Material         | .tmat        | Eclipse      | MAT      | Green     | true         | Visual   | false*       | true        | GenericEditor |
| MaterialInstance | .tmi         | Eclipse      | INST     | Green     | true         | Visual   | false*       | true        | GenericEditor | (Not implemented)
| Shader           | .slang       | WandSparkles | SHADER   | Green     | true         | Visual   | false        | false       |               |
| Mesh             | .tmesh       | Shapes       | MESH     | Blue      | false        |          | false*       | false       |               |
| Texture          | .ktx2        | Image        | TEX      | Orange    | false        |          | true         | false       |               |
| InputAction      | .taction     | Zap          | ACTION   | Yellow    | true         | Input    | false        | true        | GenericEditor | (Not implemented)
| InputLayout      | .tlayout     | Gamepad2     | LAYOUT   | Yellow    | true         | Input    | false        | true        | GenericEditor | (Not implemented)
| Haptics          | .thaptic     | Vibrate      | HAPTIC   | Yellow    | true         | Input    | false        | true        | HapticsEditor | (Not implemented)
| Script           | .lua         | CodeXml      | LUA      | Magenta   | true         | Logic    | false        | false       |               | (Not implemented)
| Data             | .toml        | Database     | DATA     | Cyan      | true         | Data     | false        | true        | GenericEditor |
| Schema           | .schema.json | Settings     | SCHEMA   | Cyan      | true         | Data     | false        | true        | SchemaEditor  | (Not implemented)
| Curve            | .tcurve      | Spline       | CURVE    | Cyan      | true         | Data     | false        | true        | CurveEditor   |