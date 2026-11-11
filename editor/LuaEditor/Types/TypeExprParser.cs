using System;
using System.Collections.Generic;
using System.Globalization;

namespace editor.LuaEditor.Types;

public sealed class TypeSyntaxException(string message, int position) : Exception(message) {
	public int Position { get; } = position;
}

public static class TypeExprParser {
	public static LuaType Parse(string text) {
		var cursor = new Cursor(text, 0);
		var type = cursor.ParseUnion();
		cursor.SkipWhitespace();
		if (!cursor.AtEnd) throw new TypeSyntaxException($"unexpected '{text[cursor.Pos]}' after type", cursor.Pos);
		return type;
	}

	public static bool TryParsePrefix(string text, int start, out LuaType type, out int end, out string? error) {
		try {
			var cursor = new Cursor(text, start);
			type = cursor.ParseUnion();
			end = cursor.Pos;
			error = null;
			return true;
		} catch (TypeSyntaxException e) {
			type = LuaType.Unknown;
			end = start;
			error = e.Message;
			return false;
		}
	}

	private static PrimitiveKind? PrimitiveOf(string name) => name switch {
		"nil" => PrimitiveKind.Nil,
		"boolean" => PrimitiveKind.Boolean,
		"number" => PrimitiveKind.Number,
		"integer" => PrimitiveKind.Integer,
		"string" => PrimitiveKind.String,
		"table" => PrimitiveKind.Table,
		"function" => PrimitiveKind.Function,
		"thread" => PrimitiveKind.Thread,
		"userdata" or "lightuserdata" => PrimitiveKind.Userdata,
		"any" => PrimitiveKind.Any,
		"unknown" => PrimitiveKind.Unknown,
		_ => null,
	};

	private sealed class Cursor(string text, int pos) {
		public int Pos = pos;
		public bool AtEnd => Pos >= text.Length;
		private char Peek(int offset = 0) => Pos + offset < text.Length ? text[Pos + offset] : '\0';

		public void SkipWhitespace() {
			while (!AtEnd && char.IsWhiteSpace(text[Pos])) Pos++;
		}

		private TypeSyntaxException Error(string message) => new(message, Pos);

		private void Expect(char c) {
			SkipWhitespace();
			if (Peek() != c) throw Error(AtEnd ? $"expected '{c}' but the type ended" : $"expected '{c}' but found '{Peek()}'");
			Pos++;
		}

		private bool StartsWith(string s) => string.CompareOrdinal(text, Pos, s, 0, s.Length) == 0;

		public LuaType ParseUnion() {
			var members = new List<LuaType> { ParsePostfix() };
			while (true) {
				var save = Pos;
				SkipWhitespace();
				if (Peek() != '|') {
					Pos = save;
					break;
				}
				Pos++;
				members.Add(ParsePostfix());
			}
			return members.Count == 1 ? members[0] : LuaType.Union(members.ToArray());
		}

		private LuaType ParsePostfix() {
			var type = ParsePrimary();
			while (true) {
				// spaces distinguish documentation brackets from array suffixes
				if (Peek() == '[' && Peek(1) == ']') {
					Pos += 2;
					type = new ArrayType(type);
				} else if (Peek() == '?') {
					Pos++;
					type = LuaType.Optional(type);
				} else {
					return type;
				}
			}
		}

		private LuaType ParsePrimary() {
			SkipWhitespace();
			if (AtEnd) throw Error("expected a type but the text ended");
			var c = Peek();
			switch (c) {
				case '"' or '\'':
					return new LiteralType(ReadString(c));
				case '(': {
					Pos++;
					var inner = ParseUnion();
					// one pin cannot carry several return values
					SkipWhitespace();
					while (Peek() == ',') {
						Pos++;
						ParseUnion();
						SkipWhitespace();
					}
					Expect(')');
					return inner;
				}
				case '`': {
					Pos++;
					var start = Pos;
					while (!AtEnd && Peek() != '`') Pos++;
					if (AtEnd) throw Error("unterminated `name`");
					var name = text[start..Pos];
					Pos++;
					return new CaptureType(name);
				}
				case '{':
					SkipBalanced('{', '}');
					return LuaType.Table;
			}
			if (char.IsDigit(c) || (c == '-' && char.IsDigit(Peek(1)))) return ReadNumber();
			if (!IsNameStart(c)) throw Error($"unexpected '{c}'");

			var nameStart = Pos;
			var word = ReadName();
			switch (word) {
				case "async": {
					SkipWhitespace();
					if (!StartsWith("fun")) throw Error("expected 'fun' after 'async'");
					Pos += 3;
					return ParseFunction();
				}
				case "fun":
					return ParseFunction();
				case "true":
					return new LiteralType(true);
				case "false":
					return new LiteralType(false);
				case "table" when Peek() == '<': {
					Pos++;
					var key = ParseUnion();
					Expect(',');
					var value = ParseUnion();
					Expect('>');
					return new TableType(key, value);
				}
			}
			if (PrimitiveOf(word) is { } kind) return new PrimitiveType(kind);
			if (word.EndsWith('.')) {
				Pos = nameStart;
				throw Error($"bad type name '{word}'");
			}

			var args = new List<LuaType>();
			if (Peek() == '<') {
				Pos++;
				args.Add(ParseUnion());
				while (true) {
					SkipWhitespace();
					if (Peek() == ',') {
						Pos++;
						args.Add(ParseUnion());
						continue;
					}
					Expect('>');
					break;
				}
			}
			return new NamedType(word, args);
		}

		private LuaType ParseFunction() {
			Expect('(');
			var parameters = new List<Param>();
			LuaType? varargParam = null;
			SkipWhitespace();
			if (Peek() != ')') {
				while (true) {
					SkipWhitespace();
					if (StartsWith("...")) {
						Pos += 3;
						SkipWhitespace();
						if (Peek() == ':') {
							Pos++;
							varargParam = ParseUnion();
						} else {
							varargParam = LuaType.Any;
						}
					} else {
						parameters.Add(ParseParam());
					}
					SkipWhitespace();
					if (Peek() == ',') {
						Pos++;
						continue;
					}
					break;
				}
			}
			Expect(')');

			var returns = new List<LuaType>();
			LuaType? varargReturn = null;
			var save = Pos;
			SkipWhitespace();
			if (Peek() != ':') {
				Pos = save;
				return new FunctionType(parameters, returns, varargParam, null);
			}
			Pos++;
			while (true) {
				SkipWhitespace();
				if (StartsWith("...")) {
					Pos += 3;
					varargReturn = LuaType.Any;
					break;
				}
				var type = ParseUnion();
				var afterType = Pos;
				SkipWhitespace();
				if (StartsWith("...")) {
					Pos += 3;
					varargReturn = type;
					break;
				}
				returns.Add(type);
				if (Peek() == ',') {
					var comma = Pos;
					Pos++;
					if (NamedParamAhead()) {
						Pos = comma;
						break;
					}
					continue;
				}
				Pos = afterType;
				break;
			}
			return new FunctionType(parameters, returns, varargParam, varargReturn);
		}

		private bool NamedParamAhead() {
			var save = Pos;
			SkipWhitespace();
			var found = false;
			if (StartsWith("...")) {
				Pos += 3;
				SkipWhitespace();
				found = Peek() == ':';
			} else if (IsNameStart(Peek())) {
				ReadName();
				if (Peek() == '?') Pos++;
				SkipWhitespace();
				found = Peek() == ':';
			}
			Pos = save;
			return found;
		}

		private Param ParseParam() {
			var start = Pos;
			if (IsNameStart(Peek())) {
				var name = ReadName();
				var optional = false;
				if (Peek() == '?') {
					optional = true;
					Pos++;
				}
				SkipWhitespace();
				if (Peek() == ':') {
					Pos++;
					return new Param(name, ParseUnion(), optional);
				}
			}
			Pos = start;
			return new Param("", ParseUnion(), false);
		}

		private static bool IsNameStart(char c) => char.IsLetter(c) || c == '_';

		private string ReadName() {
			var start = Pos;
			while (!AtEnd && (char.IsLetterOrDigit(Peek()) || Peek() == '_' || (Peek() == '.' && IsNameStart(Peek(1))))) Pos++;
			return text[start..Pos];
		}

		private string ReadString(char quote) {
			Pos++;
			var sb = new System.Text.StringBuilder();
			while (true) {
				if (AtEnd) throw Error("unterminated string literal");
				var c = text[Pos++];
				if (c == quote) return sb.ToString();
				if (c == '\\' && !AtEnd) c = text[Pos++];
				sb.Append(c);
			}
		}

		private LuaType ReadNumber() {
			var start = Pos;
			if (Peek() == '-') Pos++;
			while (!AtEnd && (char.IsDigit(Peek()) || Peek() == '.')) Pos++;
			var s = text[start..Pos];
			if (long.TryParse(s, NumberStyles.AllowLeadingSign, CultureInfo.InvariantCulture, out var l)) return new LiteralType(l);
			if (double.TryParse(s, NumberStyles.Float, CultureInfo.InvariantCulture, out var d)) return new LiteralType(d);
			throw Error($"bad number '{s}'");
		}

		private void SkipBalanced(char open, char close) {
			var depth = 0;
			do {
				if (AtEnd) throw Error($"unbalanced '{open}'");
				var c = text[Pos++];
				if (c == open) depth++;
				else if (c == close) depth--;
			} while (depth > 0);
		}
	}
}
