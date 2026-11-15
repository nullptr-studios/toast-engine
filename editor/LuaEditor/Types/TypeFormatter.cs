using System;
using System.Globalization;
using System.Linq;
using System.Text;

namespace editor.LuaEditor.Types;

public static class TypeFormatter {
	public static string Format(LuaType type) {
		var sb = new StringBuilder();
		Write(sb, type);
		return sb.ToString();
	}

	public static string Display(LuaType type) {
		if (type is not UnionType u || !u.Members.Any(m => m is PrimitiveType { Kind: PrimitiveKind.Nil })) return Format(type);
		var rest = u.Members.Where(m => m is not PrimitiveType { Kind: PrimitiveKind.Nil }).ToList();
		if (rest.Count == 0) return "nil";
		var text = string.Join("|", rest.Select(Format));
		return (rest.Count > 1 || rest[0] is FunctionType ? "(" + text + ")" : text) + "?";
	}

	private static void Write(StringBuilder sb, LuaType type) {

		switch (type) {
			case PrimitiveType p:
				sb.Append(p.Kind.ToString().ToLowerInvariant());
				break;
			case LiteralType { Value: string s }:
				sb.Append('"').Append(s.Replace("\\", "\\\\").Replace("\"", "\\\"")).Append('"');
				break;
			case LiteralType { Value: bool b }:
				sb.Append(b ? "true" : "false");
				break;
			case LiteralType l:
				sb.Append(Convert.ToString(l.Value, CultureInfo.InvariantCulture));
				break;
			case ArrayType a:
				WriteWrapped(sb, a.Element, a.Element is UnionType or FunctionType);
				sb.Append("[]");
				break;
			case TableType t:
				sb.Append("table<");
				Write(sb, t.Key);
				sb.Append(", ");
				Write(sb, t.Value);
				sb.Append('>');
				break;
			case UnionType u:
				for (var i = 0; i < u.Members.Count; i++) {
					if (i > 0) sb.Append('|');
					// parentheses keep union members out of the return type
					WriteWrapped(sb, u.Members[i], u.Members[i] is FunctionType && i < u.Members.Count - 1);
				}
				break;
			case NamedType n:
				sb.Append(n.Name);
				if (n.Args.Count > 0) {
					sb.Append('<');
					for (var i = 0; i < n.Args.Count; i++) {
						if (i > 0) sb.Append(", ");
						Write(sb, n.Args[i]);
					}
					sb.Append('>');
				}
				break;
			case CaptureType c:
				sb.Append('`').Append(c.Name).Append('`');
				break;
			case FunctionType f:
				WriteFunction(sb, f);
				break;
			default:
				throw new ArgumentOutOfRangeException(nameof(type), type.GetType().Name, "unknown LuaType");
		}
	}

	private static void WriteWrapped(StringBuilder sb, LuaType type, bool wrap) {
		if (wrap) sb.Append('(');
		Write(sb, type);
		if (wrap) sb.Append(')');
	}

	private static void WriteFunction(StringBuilder sb, FunctionType f) {
		sb.Append("fun(");
		var parts = f.Params.Select(p => {
			if (p.Name.Length == 0) return Format(p.Type);
			return $"{p.Name}{(p.Optional ? "?" : "")}: {Format(p.Type)}";
		}).ToList();
		if (f.VarargParam is { } vararg) parts.Add(vararg == LuaType.Any ? "..." : $"...: {Format(vararg)}");
		sb.Append(string.Join(", ", parts)).Append(')');

		var returns = f.Returns.Select(r => WrapReturn(r)).ToList();
		if (f.VarargReturn is { } vret) returns.Add(vret == LuaType.Any ? "..." : $"{WrapReturn(vret)} ...");
		if (returns.Count > 0) sb.Append(": ").Append(string.Join(", ", returns));
	}

	// parentheses keep later returns out of the nested signature
	private static string WrapReturn(LuaType type) => type is FunctionType ? $"({Format(type)})" : Format(type);
}
