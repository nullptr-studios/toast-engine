using System;
using System.Collections.Generic;
using System.Linq;

namespace editor.LuaEditor.Types;

public static class AnnotationParser {
	public static AnnotationBlock Parse(IReadOnlyList<string> lines) {
		var doc = new List<string>();
		var tags = new List<Tag>();
		var errors = new List<string>();

		for (var i = 0; i < lines.Count; i++) {
			var body = Body(lines[i]);
			if (body.StartsWith('|')) continue;
			if (!body.StartsWith('@')) {
				doc.Add(body.Trim());
				continue;
			}

			var nameEnd = 1;
			while (nameEnd < body.Length && !char.IsWhiteSpace(body[nameEnd])) nameEnd++;
			var name = body[1..nameEnd];
			var rest = JoinContinuations(body[nameEnd..].Trim(), lines, ref i);

			try {
				Tag? tag = name switch {
					"meta" => null,
					"class" => ParseClass(rest),
					"field" => ParseField(rest),
					"type" => new TypeTag(ParseType(rest, out _)),
					"param" => ParseParam(rest),
					"return" => ParseReturn(rest),
					"generic" => new GenericTag(ParseGenerics(rest)),
					"overload" => ParseType(rest, out _) is FunctionType fn
						? new OverloadTag(fn)
						: throw new TypeSyntaxException("@overload needs a fun(...) type", 0),
					"alias" => ParseAlias(rest),
					"enum" => new EnumTag(FirstWord(StripAttributes(rest))),
					"cast" => ParseCast(rest),
					"unit" => new UnitTag(rest),
					"operator" => ParseOperator(rest),
					_ => new UnknownTag(name, rest),
				};
				if (tag is not null) tags.Add(tag);
			} catch (TypeSyntaxException e) {
				errors.Add($"@{name}: {e.Message}");
			}
		}

		return new AnnotationBlock(JoinDoc(doc), tags, errors);
	}

	private static string JoinContinuations(string rest, IReadOnlyList<string> lines, ref int index) {
		while (rest.Count(c => c == '{') > rest.Count(c => c == '}') && index + 1 < lines.Count) rest += " " + Body(lines[++index]).Trim();

		var members = new List<string>();
		while (index + 1 < lines.Count && Body(lines[index + 1]).TrimStart().StartsWith('|')) {
			var member = Body(lines[++index]).TrimStart()[1..].TrimStart('>', '+', '-').Trim();
			if (TypeExprParser.TryParsePrefix(member, 0, out var type, out _, out _)) members.Add(TypeFormatter.Format(type));
		}
		if (members.Count == 0) return rest;
		var union = string.Join("|", members);
		return rest.Length == 0 ? union : $"{rest} {union}";
	}

	private static string Body(string line) {
		var trimmed = line.TrimStart();
		return trimmed.StartsWith("---") ? trimmed[3..] : trimmed.TrimStart('-');
	}

	private static string JoinDoc(List<string> lines) {
		var parts = new List<string>();
		foreach (var line in lines) {
			if (line.Length == 0 && (parts.Count == 0 || parts[^1].Length == 0)) continue;
			parts.Add(line);
		}
		while (parts.Count > 0 && parts[^1].Length == 0) parts.RemoveAt(parts.Count - 1);
		return string.Join("\n", parts);
	}

	private static LuaType ParseType(string text, out string rest) {
		if (!TypeExprParser.TryParsePrefix(text, 0, out var type, out var end, out var error))
			throw new TypeSyntaxException(error ?? "bad type", 0);
		rest = text[end..].Trim();
		return type;
	}

	private static string CleanDoc(string text) {
		text = text.Trim();
		if (text.StartsWith('#')) return text[1..].Trim();
		if (text.StartsWith('[') && text.EndsWith(']')) return text[1..^1].Trim();
		return text;
	}

	private static int NameEnd(string text, int start = 0) {
		var i = start;
		while (i < text.Length && (char.IsLetterOrDigit(text[i]) || text[i] is '_' or '.')) i++;
		return i;
	}

	private static string FirstWord(string text) => text[..NameEnd(text)];

	private static string StripAttributes(string text) {
		text = text.Trim();
		while (text.StartsWith('(')) {
			var close = text.IndexOf(')');
			if (close < 0) break;
			text = text[(close + 1)..].TrimStart();
		}
		return text;
	}

	private static ClassTag ParseClass(string text) {
		text = StripAttributes(text);
		var end = NameEnd(text);
		if (end == 0) throw new TypeSyntaxException("@class needs a name", 0);
		var name = text[..end];
		var rest = text[end..];

		var generics = new List<GenericParam>();
		if (rest.StartsWith('<')) {
			var close = rest.IndexOf('>');
			if (close < 0) throw new TypeSyntaxException("unclosed generic list", 0);
			generics.AddRange(ParseGenerics(rest[1..close]));
			rest = rest[(close + 1)..];
		}

		var bases = new List<LuaType>();
		rest = rest.TrimStart();
		if (rest.StartsWith(':')) {
			rest = rest[1..];
			while (true) {
				bases.Add(ParseType(rest, out rest));
				if (!rest.StartsWith(',')) break;
				rest = rest[1..];
			}
		}
		return new ClassTag(name, generics, bases);
	}

	private static FieldTag ParseField(string text) {
		var words = text.Split(' ', 2, StringSplitOptions.RemoveEmptyEntries);
		if (words.Length == 2 && words[0] is "public" or "private" or "protected" or "package") text = words[1];
		var end = NameEnd(text);
		if (end == 0) throw new TypeSyntaxException("@field needs a name", 0);
		var name = text[..end];
		var optional = end < text.Length && text[end] == '?';
		var type = ParseType(text[(end + (optional ? 1 : 0))..], out var rest);
		return new FieldTag(name, type, optional, CleanDoc(rest));
	}

	private static ParamTag ParseParam(string text) {
		string name;
		int end;
		if (text.StartsWith("...")) {
			name = "...";
			end = 3;
		} else {
			end = NameEnd(text);
			if (end == 0) throw new TypeSyntaxException("@param needs a name", 0);
			name = text[..end];
		}
		var optional = end < text.Length && text[end] == '?';
		var type = ParseType(text[(end + (optional ? 1 : 0))..], out var rest);
		return new ParamTag(name, type, optional, CleanDoc(rest));
	}

	private static ReturnTag ParseReturn(string text) {
		var type = ParseType(text, out var rest);
		var vararg = false;
		if (rest.StartsWith("...")) {
			vararg = true;
			rest = rest[3..].TrimStart();
		}
		string? name = null;
		var end = NameEnd(rest);
		if (end > 0 && (char.IsLetter(rest[0]) || rest[0] == '_')) {
			name = rest[..end];
			rest = rest[end..];
		}
		return new ReturnTag(type, name, vararg, CleanDoc(rest));
	}

	private static List<GenericParam> ParseGenerics(string text) {
		var result = new List<GenericParam>();
		foreach (var part in SplitTopLevel(text)) {
			var colon = part.IndexOf(':');
			var name = (colon < 0 ? part : part[..colon]).Trim();
			if (name.Length == 0) throw new TypeSyntaxException("empty generic name", 0);
			var constraint = colon < 0 ? null : TypeExprParser.Parse(part[(colon + 1)..].Trim());
			result.Add(new GenericParam(name, constraint));
		}
		return result;
	}

	private static IEnumerable<string> SplitTopLevel(string text) {
		var depth = 0;
		var start = 0;
		for (var i = 0; i < text.Length; i++) {
			switch (text[i]) {
				case '<' or '(' or '{': depth++; break;
				case '>' or ')' or '}': depth--; break;
				case ',' when depth == 0:
					yield return text[start..i];
					start = i + 1;
					break;
			}
		}
		yield return text[start..];
	}

	private static AliasTag ParseAlias(string text) {
		var end = NameEnd(text);
		if (end == 0) throw new TypeSyntaxException("@alias needs a name", 0);
		var name = text[..end];
		var rest = text[end..].Trim();
		if (rest.Length == 0) throw new TypeSyntaxException($"@alias {name} has no type", 0);
		return new AliasTag(name, ParseType(rest, out _));
	}

	private static OperatorTag ParseOperator(string text) {
		var end = NameEnd(text);
		if (end == 0) throw new TypeSyntaxException("@operator needs an operator name", 0);
		var op = text[..end];
		var rest = text[end..].TrimStart();
		LuaType? operand = null;
		if (rest.StartsWith('(')) {
			var close = rest.LastIndexOf(')');
			if (close < 0) throw new TypeSyntaxException("unclosed @operator operand", 0);
			operand = TypeExprParser.Parse(rest[1..close]);
			rest = rest[(close + 1)..].TrimStart();
		}
		// missing return annotations follow luals behavior
		if (!rest.StartsWith(':')) return new OperatorTag(op, operand, LuaType.Any);
		return new OperatorTag(op, operand, ParseType(rest[1..], out _));
	}

	private static CastTag ParseCast(string text) {
		var end = NameEnd(text);
		if (end == 0) throw new TypeSyntaxException("@cast needs a variable", 0);
		var type = text[end..].Trim().TrimStart('+');
		return new CastTag(text[..end], ParseType(type, out _));
	}
}
