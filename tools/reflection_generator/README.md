# reflection_generator

Reads the C++ headers, finds `[[ToastNode]]` classes and `Event<T>` structs and writes everything the engine and the editor need to know about them.

```bash
reflection_generator --database cache/game_reflect.json --output lib/generated --input lib/src
```

| flag | |
| --- | --- |
| `--database <file>` | json metadata the editor reads |
| `--output <dir>` | generated c++, has to exist |
| `--input <dir>` | folder searched for `.hpp` files, repeat it for more folders |
| `--include-root <dir>` | generated includes are written relative to this folder |
| `--register-fn <name>` | name of the registration function, default `registerEngineTypes` |
| `--split-typeinfo` | one `.type_info.cpp` per node instead of inline, keeps rebuilds small |
| `--attribute <name>` | attribute added to every node, repeat it for more |
| `--lua-stubs <file>` | lua types for the nodes |
| `--event-lua-stubs <file>` | lua types for the events |

## What comes out

- `<node>.generated.hpp` one per node
- `<node>.type_info.cpp` one per node, only with `--split-typeinfo`
- `reflect.generated.cpp` registers every node and event
- the json database and the two lua files

Files whose content did not change are left alone so nothing rebuilds for no reason.

## How it is laid out

The run goes top to bottom in `Pipeline.cs`: find headers, parse, validate, order, write.

| folder | what lives there |
| --- | --- |
| `Parsing` | tree-sitter in, plain records out. `Syntax.cs` is the only file that talks to tree-sitter |
| `Model` | the records the rest of the tool passes around |
| `Rules` | the decisions people tend to change, start here |
| `Analysis` | field groups and the order nodes are emitted in |
| `Output` | views, templates and writers |

Templates never compute anything. A view in `Output/CppViews.cs` or `Output/LuaOutput.cs` holds every value a template prints, and a template that asks for something that does not exist fails the build.

## Recipes

**add a lifecycle function**
Add a line to `Rules/TickFunctions.cs`. Keep it in step with `TickFunctionList` in `reflect.hpp`.

**add a field type**
Add the enum value in `Model/FieldKind.cs`, its name in the same file, then a case in `Rules/FieldKinds.cs`. Add the lua type in `Rules/LuaTypes.cs`.

**change how a c++ type shows up in lua**
`Rules/LuaTypes.cs`. There is one function for fields, one for parameters and returns, one for events.

**add a rule that rejects a header**
`Rules/Validation.cs`, throw a `GeneratorException`.

**change the generated c++**
Edit the template in `Output/Templates`. If it needs a new value add it to the view first. The `NodeInfo` initializer is shared by the header and the cpp in `node_info_fields.scriban`.

**add an output file**
Add a template and a view, render it from a writer in `Output` and call the writer from `Pipeline.cs`.

**add a command line flag**
`Options.cs`.

## Things to know

- Control lines in templates end with `~}}` so the newline after them is dropped.
- Attributes are written sorted by name and a repeated name keeps its last arguments, in the c++ and in the json.
- The json keys have to match `editor/Engine/ReflectionDatabase.cs`.
- Unknown field types fall back to `int_t`. The code compiles and the inspector widget will be wrong.
