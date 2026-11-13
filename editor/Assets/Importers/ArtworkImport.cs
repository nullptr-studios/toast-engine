//
// ArtworkImport.cs by Xein
//

using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Threading.Tasks;
using editor.Components.Modals;

namespace editor.Assets.Importers;

public sealed record ArtworkOutput(string Uid, string Name, string? Hash, string Uri);

public static class ArtworkImport {
	public static async Task<IReadOnlyList<ArtworkOutput>?> RunAsync(
		IAssetImporter importer, string realSource, string sourceVirtual, string destDir,
		IReadOnlyList<ArtworkOutput> previous, Action<string> log, Action<double>? progress = null) {
		if (previous.Count == 0) {
			var produced = await Generate(importer, realSource, new ImportContext {
				DestDir = destDir, SourceVirtualPath = sourceVirtual
			}, log, progress);
			return produced.Select(p => Describe(p.Uid, p.Path)).ToList();
		}

		var stageRoot = Path.Combine(ProjectContext.CachePath, "import_staging", Guid.NewGuid().ToString("N"));
		// Match the live directory name so importers don't add a duplicate subfolder.
		var stageDest = Path.Combine(stageRoot, new DirectoryInfo(destDir).Name);
		Directory.CreateDirectory(stageDest);
		IReadOnlyList<(string Uid, string Path)> producedInStage = [];
		IReadOnlyList<ArtworkOutput>? applied = null;
		var preserveStage = false;
		try {
			producedInStage = await Generate(importer, realSource, new ImportContext {
				DestDir = stageDest, SourceVirtualPath = sourceVirtual, IsStaging = true
			}, log, progress);
			var plan = new ArtworkReimport(previous, producedInStage, stageDest, destDir);
			IReadOnlyDictionary<string, MissingAssetResolution> decisions = new Dictionary<string, MissingAssetResolution>();
			if (plan.Missing.Count > 0) {
				var owner = ModalService.FindActiveWindow();
				var reviewed = owner is null ? null
					: await new ReimportReviewWindow(sourceVirtual, plan.ModifiedCount, plan.Added, plan.Missing)
						.ShowDialog<IReadOnlyDictionary<string, MissingAssetResolution>?>(owner);
				if (reviewed is null) {
					log($"Skipped {sourceVirtual}");
					return null;
				}
				decisions = reviewed;
			}

			applied = plan.Apply(decisions, stageRoot, log);
			return applied;
		} catch (AggregateException) {
			// Keep original files available if failed
			preserveStage = true;
			throw;
		} finally {
			var previousUids = previous.Select(p => p.Uid).ToHashSet();
			if (applied is not null) previousUids.UnionWith(applied.Select(p => p.Uid));
			foreach (var (uid, _) in producedInStage)
				if (!previousUids.Contains(uid))
					AssetDatabase.TryDelete(ThumbnailPath(uid));

			if (!preserveStage) {
				try {
					AssetDatabase.DeleteDirectory(stageRoot);
				} catch (IOException ex) {
					log($"Could not clean up reimport staging files: {ex.Message}");
				} catch (UnauthorizedAccessException ex) {
					log($"Could not clean up reimport staging files: {ex.Message}");
				}
			}
		}
	}

	private static async Task<IReadOnlyList<(string Uid, string Path)>> Generate(
		IAssetImporter importer, string realSource, ImportContext context, Action<string> log,
		Action<double>? progress) {
		var writes = new List<(string Uid, string Path)>();
		using (MetaFile.Capture(writes))
			await importer.Import(realSource, context, log, progress);

		var produced = writes.GroupBy(w => w.Path, ArtworkReimport.PathComparer).Select(group => group.Last()).ToList();
		foreach (var output in produced)
			if (!File.Exists(output.Path))
				throw new IOException($"Import did not generate {Path.GetFileName(output.Path)}.");
		return produced;
	}

	private static ArtworkOutput Describe(string uid, string path) {
		return new ArtworkOutput(uid, Path.GetFileName(path), AssetDatabase.ComputeHash(path),
			ProjectContext.ToVirtual(path) ?? path);
	}

	internal static string ThumbnailPath(string uid) => ProjectContext.Resolve($"cache://thumbnails/{uid}.png");
}
