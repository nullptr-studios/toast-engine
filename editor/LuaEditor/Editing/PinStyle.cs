using System.Linq;
using editor.LuaEditor.Types;

namespace editor.LuaEditor.Editing;

public enum PinShape { Circle, Diamond, Square, Arrow, Hexagon, Triangle, Stack, Pair }

public static class PinStyle {
	public const string Exec = "#fafafa";
	public const string Number = "#00ff40";
	public const string Integer = "#b6ff3d";
	public const string Array = "#00c2a8";
	public const string Text = "#ad73ff";
	public const string Boolean = "#ff1659";
	public const string Table = "#73ffea";
	public const string Function = "#ffa300";
	public const string Object = "#29adff";
	public const string Vector = "#ffec27";
	public const string Nothing = "#a0a0a0";
	public const string Anything = "#d8d8d8";

	public static string ColorOf(LuaType type) {
		type = Strip(type);
		return type switch {
			PrimitiveType { Kind: PrimitiveKind.Number } => Number,
			PrimitiveType { Kind: PrimitiveKind.Integer } => Integer,
			ArrayType => Array,
			PrimitiveType { Kind: PrimitiveKind.String } => Text,
			PrimitiveType { Kind: PrimitiveKind.Boolean } => Boolean,
			PrimitiveType { Kind: PrimitiveKind.Table } or TableType => Table,
			PrimitiveType { Kind: PrimitiveKind.Function } or FunctionType => Function,
			PrimitiveType { Kind: PrimitiveKind.Nil } => Nothing,
			PrimitiveType { Kind: PrimitiveKind.Any or PrimitiveKind.Unknown } => Anything,
			LiteralType { Value: string } => Text,
			LiteralType { Value: bool } => Boolean,
			LiteralType => Number,
			NamedType { Name: "vec2" or "vec3" or "vec4" or "quat" or "color" } => Vector,
			NamedType => Object,
			UnionType u => ColorOf(u.Members.FirstOrDefault(m => m is not PrimitiveType { Kind: PrimitiveKind.Nil }) ?? LuaType.Any),
			_ => Anything
		};
	}

	public static PinShape ShapeOf(LuaType type) => Strip(type) switch {
			ArrayType => PinShape.Stack,
			TableType => PinShape.Pair,
			PrimitiveType { Kind: PrimitiveKind.Table } => PinShape.Diamond,
		NamedType { Name: not ("vec2" or "vec3" or "vec4" or "quat" or "color") } => PinShape.Diamond,
			PrimitiveType { Kind: PrimitiveKind.Boolean } or LiteralType { Value: bool } => PinShape.Triangle,
		PrimitiveType { Kind: PrimitiveKind.Function } or FunctionType => PinShape.Square,
		_ => PinShape.Circle
	};

	public static bool IsOptional(LuaType type) => type is UnionType u && u.Members.Any(m => m is PrimitiveType { Kind: PrimitiveKind.Nil }) && u.Members.Any(m => m is not PrimitiveType { Kind: PrimitiveKind.Nil });

	private static LuaType Strip(LuaType type) => type is UnionType u && u.Members.Count(m => m is not PrimitiveType { Kind: PrimitiveKind.Nil }) == 1
		? u.Members.First(m => m is not PrimitiveType { Kind: PrimitiveKind.Nil })
		: type;
}
