namespace editor.LuaEditor.Types;

public readonly record struct SourceLocation(string Path, int Line);

public enum DiagnosticSeverity { Info, Warning, Error }

public sealed record Diagnostic(DiagnosticSeverity Severity, string Message, SourceLocation Location);

public sealed record MemberDecl(string Name, LuaType Type, string Doc, string? Unit, bool IsMethod,
	EquatableArray<GenericParam> Generics, EquatableArray<FunctionType> Overloads, SourceLocation Location);

public sealed record ClassDecl(string Name, EquatableArray<GenericParam> Generics, EquatableArray<LuaType> Bases,
	EquatableArray<MemberDecl> Members, string Doc, SourceLocation Location,
	EquatableArray<OperatorDecl> Operators = default);

public sealed record OperatorDecl(string Op, LuaType? Operand, LuaType Result);

public sealed record GlobalDecl(string Name, LuaType Type, string Doc, SourceLocation Location,
	EquatableArray<GenericParam> Generics = default, EquatableArray<FunctionType> Overloads = default);

public sealed record AliasDecl(string Name, LuaType Type, string Doc, SourceLocation Location);

public sealed record EnumDecl(string Name, string TableName, EquatableArray<(string Name, object? Value)> Members, string Doc, SourceLocation Location);

public sealed record DeclarationFile(string Path, EquatableArray<ClassDecl> Classes, EquatableArray<GlobalDecl> Globals,
	EquatableArray<AliasDecl> Aliases, EquatableArray<EnumDecl> Enums, EquatableArray<Diagnostic> Diagnostics);
