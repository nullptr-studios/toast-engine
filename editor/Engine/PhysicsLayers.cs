//
// PhysicsLayers.cs by Xein
// 12 Oct 2026
//

namespace editor.Engine;

public static class PhysicsLayers {
	public const int Count = 32;
	public const int MaxAbbreviationLength = 3;

	public static string NameKey(int index) => $"physics.layers.{index}.name";
	public static string AbbreviationKey(int index) => $"physics.layers.{index}.abbr";

	public static string Name(int index) {
		if (index == 0) return "Default";
		var name = ToastSettings.GetString(NameKey(index));
		return string.IsNullOrEmpty(name) ? $"Layer {index}" : name;
	}

	public static string Abbreviation(int index) {
		if (index == 0) return "0";
		var abbreviation = ToastSettings.GetString(AbbreviationKey(index));
		return string.IsNullOrEmpty(abbreviation) ? index.ToString() : abbreviation;
	}
}
