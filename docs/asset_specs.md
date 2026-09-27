# Asset types specs

| Type                 | Extension     | Icon              | ChipText | ChipColor | CanBeCreated | Category | HasThumbnail | CanBeEdited | EditorTool    |
|----------------------|---------------|-------------------|----------|-----------|--------------|----------|--------------|-------------|---------------|
| Node                 | .tnode        | Box               | NODE     | Red       | false        |          | true         | true        | NodeEditor    |
| Project Settings     | .toast        | Settings          | SETTINGS | DeepPink  | false        | Project  | false        | true        | GenericEditor |
| Material             | .tmat         | Eclipse           | MAT      | Green     | true         | Visual   | false        | true        | GenericEditor |
| Material Instance    | .tmi          | Eclipse           | INST     | Green     | true         | Visual   | false        | true        | GenericEditor |
| Shader               | .slang        | WandSparkles      | SHADER   | Green     | true         | Visual   | false        | false       |               |
| Mesh                 | .tmesh        | Shapes            | MESH     | Blue      | false        | Visual   | false        | false       |               |
| Texture              | .ktx2         | Image             | TEX      | Orange    | false        | Visual   | true         | false       |               |
| Voxel Model          | .tvox         | Boxes             | VOX      | Magenta   | false        | Visual   | true         | false       |               |
| Voxel Palette        | .tpal         | Brush             | V PAL    | Magenta   | true         | Visual   | false        | true        | PaletteEditor |
| Animation            | .tanim        | Film              | ANIM     | Purple    | false        | Visual   | false        | false       |               |
| Physics Material     | .tpm          | Atom              | PHYS     | Orange    | true         | Physics  | false        | true        | GenericEditor |
| Destruction Material | .tdm          | Hammer            | DSTY     | Orange    | true         | Physics  | false        | true        | GenericEditor |
| Input Action         | .taction      | Zap               | ACTION   | Yellow    | true         | Input    | false        | true        | GenericEditor |
| Input Layout         | .tlayout      | Gamepad2          | LAYOUT   | Yellow    | true         | Input    | false        | true        | GenericEditor |
| Haptic Effect        | .thaptic      | Vibrate           | HAPTIC   | Yellow    | true         | Input    | false        | true        | HapticsEditor |
| Script               | .lua          | CodeXml           | LUA      | Magenta   | true         | Logic    | false        | false       |               |
| Data                 | .toml         | Database          | DATA     | Cyan      | true         | Data     | false        | true        | GenericEditor |
| Schema               | .schema.json  | Settings          | SCHEMA   | Cyan      | true         | Data     | false        | true        | SchemaEditor  |
| Curve                | .tcurve       | Spline            | CURVE    | Cyan      | true         | Data     | false        | true        | CurveEditor   |
| Audio Bank           | .bank         | AudioWaveform     | BANK     | Beige     | false        | Audio    | false        | false       |               |
| Audio Strings        | .strings.bank | BookHeadphones    | BSTR     | Beige     | false        | Audio    | false        | false       |               |
| Bus                  | .tbus         | SlidersVertical   | BUS      | Beige     | false        | Audio    | false        | false       |               |
| VCA                  | .tvca         | SlidersHorizontal | VCA      | Beige     | false        | Audio    | false        | false       |               |
| Port                 | .taport       | Speaker           | PORT     | Beige     | false        | Audio    | false        | false       |               |
| Snapshot             | .tasnap       | Activity          | SNAP     | Beige     | false        | Audio    | false        | false       |               |
| Audio Event          | .tae          | Volume2           | EVENT    | Beige     | false        | Audio    | false        | false       |               |
| UI Element           | .rml          | AppWindow         | UI       | Blue      | true         | UI       | false        | false       |               |
| UI Style             | .rcss         | Brush             | STYLE    | Blue      | true         | UI       | false        | false       |               |
| UI Image             | .tga          | FileImage         | IMG      | Blue      | false        | UI       | true         | false       |               |
| Font                 | .ttf          | Type              | FONT     | Blue      | false        | UI       | false        | false       |               |
| Localization         | .tloc         | Languages         | LOC      | Cyan      | true         | UI       | false        | true        | TableEditor   |
| Image Localization   | .tiloc        | Images            | ILOC     | Cyan      | true         | UI       | false        | true        | TableEditor   |
| Color Scheme         | .tcolor       | Palette           | COLOR    | Cyan      | true         | UI       | false        | true        | GenericEditor |
