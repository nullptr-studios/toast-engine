using System.Collections.Generic;

namespace editor.LuaEditor.Types;

public enum PrimitiveKind { Nil, Boolean, Number, Integer, String, Table, Function, Thread, Userdata, Any, Unknown }

public abstract record LuaType {
	public static readonly LuaType Nil = new PrimitiveType(PrimitiveKind.Nil);
	public static readonly LuaType Boolean = new PrimitiveType(PrimitiveKind.Boolean);
	public static readonly LuaType Number = new PrimitiveType(PrimitiveKind.Number);
	public static readonly LuaType Integer = new PrimitiveType(PrimitiveKind.Integer);
	public static readonly LuaType String = new PrimitiveType(PrimitiveKind.String);
	public static readonly LuaType Table = new PrimitiveType(PrimitiveKind.Table);
	public static readonly LuaType Function = new PrimitiveType(PrimitiveKind.Function);
	public static readonly LuaType Any = new PrimitiveType(PrimitiveKind.Any);
	public static readonly LuaType Unknown = new PrimitiveType(PrimitiveKind.Unknown);

	public static LuaType Union(params LuaType[] members) {
		var flat = new List<LuaType>();
		foreach (var member in members) Add(flat, member);
		return flat.Count == 1 ? flat[0] : new UnionType(flat);

		static void Add(List<LuaType> into, LuaType type) {
			if (type is UnionType union) {
				foreach (var inner in union.Members) Add(into, inner);
			} else if (!into.Contains(type)) {
				into.Add(type);
			}
		}
	}

	public static LuaType Optional(LuaType type) => Union(type, Nil);

	public sealed override string ToString() => TypeFormatter.Format(this);
}

public sealed record PrimitiveType(PrimitiveKind Kind) : LuaType;

public sealed record LiteralType(object Value) : LuaType;

public sealed record ArrayType(LuaType Element) : LuaType;

public sealed record TableType(LuaType Key, LuaType Value) : LuaType;

public sealed record Param(string Name, LuaType Type, bool Optional);

public sealed record FunctionType(EquatableArray<Param> Params, EquatableArray<LuaType> Returns, LuaType? VarargParam, LuaType? VarargReturn) : LuaType;

public sealed record UnionType(EquatableArray<LuaType> Members) : LuaType;

public sealed record NamedType(string Name, EquatableArray<LuaType> Args) : LuaType;

public sealed record CaptureType(string Name) : LuaType;
