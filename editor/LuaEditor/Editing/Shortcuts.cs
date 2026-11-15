using System.Collections.Generic;
using System.Linq;

namespace editor.LuaEditor.Editing;

public enum CanvasAction {
	Branch, Sequence, ForEach, CommentBox, GroupAsFunction, Straighten,
	AlignTop, AlignLeft, AlignBottom, AlignRight, Duplicate, Delete, Search, MakeTable, Tidy,
	Add, Subtract, Multiply, Divide, Greater, Less, Equal, Not,
	Number, Vec2, Vec3, Quat,
	Copy, Paste, Cut, SelectAll, Undo, Redo, Save, Find, Home
}

public sealed record KeyBinding(string Key, bool Ctrl, bool Shift, bool Alt, CanvasAction Action) {
	public string Gesture => (Ctrl ? "Ctrl+" : "") + (Shift ? "Shift+" : "") + (Alt ? "Alt+" : "") + Key;
}

public static class Shortcuts {
	public static IReadOnlyList<KeyBinding> All { get; } = [
		new("B", false, false, false, CanvasAction.Branch),
		new("S", false, false, false, CanvasAction.Sequence),
		new("F", false, false, false, CanvasAction.ForEach),
		new("C", false, false, false, CanvasAction.CommentBox),
		new("G", false, false, false, CanvasAction.GroupAsFunction),
		new("Q", false, false, false, CanvasAction.Straighten),
		new("W", false, true, false, CanvasAction.AlignTop),
		new("A", false, true, false, CanvasAction.AlignLeft),
		new("S", false, true, false, CanvasAction.AlignBottom),
		new("D", false, true, false, CanvasAction.AlignRight),
		new("W", true, false, false, CanvasAction.Duplicate),
		new("D", true, false, false, CanvasAction.Duplicate),
		new("Delete", false, false, false, CanvasAction.Delete),
		new("X", false, false, false, CanvasAction.Delete),
		new("N", false, false, false, CanvasAction.Search),
		new("M", false, false, false, CanvasAction.MakeTable),
		new("T", false, false, false, CanvasAction.Tidy),
		new("OemPlus", false, true, false, CanvasAction.Add),
		new("Add", false, false, false, CanvasAction.Add),
		new("OemMinus", false, false, false, CanvasAction.Subtract),
		new("Subtract", false, false, false, CanvasAction.Subtract),
		new("D8", false, true, false, CanvasAction.Multiply),
		new("Multiply", false, false, false, CanvasAction.Multiply),
		new("OemQuestion", false, false, false, CanvasAction.Divide),
		new("Divide", false, false, false, CanvasAction.Divide),
		new("OemPeriod", false, true, false, CanvasAction.Greater),
		new("OemComma", false, true, false, CanvasAction.Less),
		new("OemPlus", false, false, false, CanvasAction.Equal),
		new("D1", false, true, false, CanvasAction.Not),
		new("D1", false, false, false, CanvasAction.Number),
		new("D2", false, false, false, CanvasAction.Vec2),
		new("D3", false, false, false, CanvasAction.Vec3),
		new("D4", false, false, false, CanvasAction.Quat),
		new("C", true, false, false, CanvasAction.Copy),
		new("V", true, false, false, CanvasAction.Paste),
		new("X", true, false, false, CanvasAction.Cut),
		new("A", true, false, false, CanvasAction.SelectAll),
		new("Z", true, false, false, CanvasAction.Undo),
		new("Y", true, false, false, CanvasAction.Redo),
		new("Z", true, true, false, CanvasAction.Redo),
		new("S", true, false, false, CanvasAction.Save),
		new("F", true, false, false, CanvasAction.Find),
		new("Home", false, false, false, CanvasAction.Home)
	];

	public static CanvasAction? Find(string key, bool ctrl, bool shift, bool alt)
		=> All.FirstOrDefault(b => b.Key == key && b.Ctrl == ctrl && b.Shift == shift && b.Alt == alt)?.Action;
}
