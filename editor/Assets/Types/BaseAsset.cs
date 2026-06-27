using Lucide.Avalonia;

namespace editor.Assets.Types;

public interface BaseAsset {
	static abstract string Type { get; } // This name will be used for the database separation
	static abstract string Extension { get; } // File extension
	static abstract string DisplayName { get; } // Display name on the UI
	static abstract string ChipText { get; } // Text that appears on the chip decoration
	static abstract string ChipColor { get; } // Color that appears on the chip decoration (name of a static resource)
	static abstract LucideIconKind Icon { get; } // Icon used to represent the asset

	static abstract bool CanBeCreated { get; } // True if the user can create it through the AssetBrowser
	static abstract string Category { get; } // Category the asset will appear on the Asset Browser dropdown

	static abstract bool HasThumbnail { get; } // If true, use that image and fall back to the Icon
	void GenerateThumbnail();

	static abstract bool CanBeImported { get; } // True if it can be imported through the Artwork Importer window

	static abstract bool CanBeEdited { get; } // True if you can double click to open
	static abstract string EditorTool { get; } // Name of the editor tool that needs to open when you double click
	static abstract string SchemaPath { get; } // Path to the Json Schema of the file if it has one

	string Uid { get; } // Asset UID
}
