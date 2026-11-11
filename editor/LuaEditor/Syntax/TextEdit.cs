namespace editor.LuaEditor.Syntax;

public readonly record struct TextEdit(int Start, int Length, string Text) {
	public int End => Start + Length;

	public static TextEdit Insert(int at, string text) => new(at, 0, text);
}
