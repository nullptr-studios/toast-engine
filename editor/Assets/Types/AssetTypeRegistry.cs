using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Reflection;

namespace editor.Assets.Types;

public static class AssetTypeRegistry {
	public static IReadOnlyList<BaseAsset> All { get; }
	public static IReadOnlyList<(string Category, IReadOnlyList<BaseAsset> Types)> CreatableByCategory { get; }

	static AssetTypeRegistry() {
		All = Assembly.GetExecutingAssembly()
			.GetTypes()
			.Where(t => t.IsSubclassOf(typeof(BaseAsset)) && !t.IsAbstract)
			.Select(t => (BaseAsset)Activator.CreateInstance(t)!)
			.ToList();

		CreatableByCategory = All
			.Where(a => a.CanBeCreated)
			.GroupBy(a => a.Category)
			.Select(g => (g.Key, (IReadOnlyList<BaseAsset>)g.ToList()))
			.ToList();
	}

	public static BaseAsset? ByExtension(string ext) =>
		All.FirstOrDefault(a => a.Extension.Equals(ext, StringComparison.OrdinalIgnoreCase));

	public static BaseAsset? ByType(string type) =>
		All.FirstOrDefault(a => a.Type.Equals(type, StringComparison.OrdinalIgnoreCase));

	public static string GetExtension(string filename) {
		foreach (var def in All) {
			if (def.Extension.Count(c => c == '.') > 1 &&
			    filename.EndsWith(def.Extension, StringComparison.OrdinalIgnoreCase))
				return def.Extension;
		}
		return Path.GetExtension(filename);
	}
}
