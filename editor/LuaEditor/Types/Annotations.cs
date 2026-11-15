namespace editor.LuaEditor.Types;

public sealed record GenericParam(string Name, LuaType? Constraint);

public abstract record Tag;

public sealed record ClassTag(string Name, EquatableArray<GenericParam> Generics, EquatableArray<LuaType> Bases) : Tag;

public sealed record FieldTag(string Name, LuaType Type, bool Optional, string Doc) : Tag;

public sealed record TypeTag(LuaType Type) : Tag;

public sealed record ParamTag(string Name, LuaType Type, bool Optional, string Doc) : Tag;

public sealed record ReturnTag(LuaType Type, string? Name, bool Vararg, string Doc) : Tag;

public sealed record GenericTag(EquatableArray<GenericParam> Params) : Tag;

public sealed record OverloadTag(FunctionType Signature) : Tag;

public sealed record AliasTag(string Name, LuaType Type) : Tag;

public sealed record EnumTag(string Name) : Tag;

public sealed record CastTag(string Variable, LuaType Type) : Tag;

public sealed record UnitTag(string Unit) : Tag;

public sealed record UnknownTag(string Name, string Text) : Tag;

public sealed record AnnotationBlock(string Doc, EquatableArray<Tag> Tags, EquatableArray<string> Errors);

public sealed record OperatorTag(string Op, LuaType? Operand, LuaType Result) : Tag;
