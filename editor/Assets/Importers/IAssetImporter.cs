using System;
using System.Collections.Generic;
using System.Threading.Tasks;
using editor.Assets.Types;
using Lucide.Avalonia;

namespace editor.Assets.Importers;

/// <summary>Destination dir + virtual path for the .meta</summary>
public record ImportContext {
	public required string DestDir { get; init; }
	public required string SourceVirtualPath { get; init; }

	// On a reimport we want the regenerated assets to keep the UIDs they had before
	public IReadOnlyList<string>? ReuseUids { get; init; }

	// nth output keeps its old UID if we have one, otherwise a new UID is generated
	public string UidFor(int outputIndex) {
		return ReuseUids is { } uids && outputIndex < uids.Count
			? uids[outputIndex]
			: UidGenerator.Generate();
	}
}

/// <summary>Interface for file format importers</summary>
public interface IAssetImporter {
	IReadOnlyList<string> SupportedExtensions { get; }

	/// <summary>Human-readable name shown in the settings card header</summary>
	string DisplayName { get; }

	/// <summary>Lucide icon shown in the settings card header</summary>
	LucideIconKind Icon { get; }

	/// <summary>Primary asset type this importer produces</summary>
	BaseAsset PrimaryOutputType { get; }

	/// <summary>All asset types this importer may produce</summary>
	IReadOnlyList<BaseAsset> OutputTypes => [PrimaryOutputType];

	bool CanHandle(string filePath);

	/// <summary>
	///    Returns all importers whose settings should be shown in the UI when this importer's
	///    files are selected
	/// </summary>
	/// Composite importers return themselves plus any sub-importers they delegate to
	IReadOnlyList<IAssetImporter> GetAllSettingsImporters() {
		return [this];
	}

	/// <summary>
	///    Returns an ordered list of setting descriptors for the auto-generated settings UI
	/// </summary>
	/// Adding a descriptor here automatically adds a row to the import window
	IReadOnlyList<ImporterSetting> GetSettings();

	Task<IReadOnlyList<string>> Import(
		string realSourcePath, ImportContext ctx, Action<string> log,
		Action<double>? progress = null);
}
