using System;
using System.Collections.Generic;
using System.Threading.Tasks;

namespace editor.Assets.Importers;

/// <summary>Destination dir + virtual path for the .meta</summary>
public record ImportContext {
	public required string DestDir { get; init; }
	public required string SourceVirtualPath { get; init; }
}

/// <summary>Interface for file format importers</summary>
public interface IAssetImporter {
	IReadOnlyList<string> SupportedExtensions { get; }
	Task<IReadOnlyList<string>> Import(string realSourcePath, ImportContext ctx, Action<string> log);
}
