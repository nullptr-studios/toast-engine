using System;
using System.Collections.Generic;
using System.Threading.Tasks;
using editor.Assets.Types;

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

	/// <summary>Primary asset type this importer produces</summary>
	BaseAsset PrimaryOutputType { get; }

	/// <summary>All asset types this importer may produce</summary>
	IReadOnlyList<BaseAsset> OutputTypes => [PrimaryOutputType];

	Task<IReadOnlyList<string>> Import(string realSourcePath, ImportContext ctx, Action<string> log);
}
