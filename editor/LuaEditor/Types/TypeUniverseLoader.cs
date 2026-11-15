using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;

namespace editor.LuaEditor.Types;

public static class TypeUniverseLoader {
	public static string DefaultStdlibDir => Path.Combine(AppContext.BaseDirectory, "Resources", "lua", "stdlib");

	public static TypeUniverse Load(string projectRoot, string stdlibDir) {
		var diagnostics = new List<Diagnostic>();
		var rc = LuaRc.Load(projectRoot, diagnostics);
		var files = new List<DeclarationFile>();

		if (Directory.Exists(stdlibDir)) {
			ReadAll(LuaFiles(stdlibDir, false, diagnostics), files, diagnostics);
		} else {
			diagnostics.Add(new Diagnostic(DiagnosticSeverity.Warning, $"standard library definitions not found at {stdlibDir}",
				new SourceLocation(stdlibDir, 1)));
		}

		foreach (var library in rc.Libraries) {
			if (Directory.Exists(library)) ReadAll(LuaFiles(library, true, diagnostics), files, diagnostics);
			else if (File.Exists(library)) ReadAll([library], files, diagnostics);
		}

		files.Insert(0, new DeclarationFile("(loader)", [], [], [], [], diagnostics));
		return TypeUniverse.Build(files);
	}

	// skip links to avoid recursive directory loops
	private static List<string> LuaFiles(string root, bool recursive, List<Diagnostic> diagnostics) {
		var result = new List<string>();
		var pending = new Stack<string>();
		pending.Push(root);
		while (pending.Count > 0) {
			var dir = pending.Pop();
			try {
				result.AddRange(Directory.GetFiles(dir, "*.lua"));
				if (!recursive) continue;
				foreach (var sub in Directory.GetDirectories(dir)) {
					if (!File.GetAttributes(sub).HasFlag(FileAttributes.ReparsePoint)) pending.Push(sub);
				}
			} catch (Exception e) when (e is IOException or UnauthorizedAccessException) {
				diagnostics.Add(new Diagnostic(DiagnosticSeverity.Warning, $"cannot read {dir}: {e.Message}", new SourceLocation(dir, 1)));
			}
		}
		return result;
	}

	private static void ReadAll(IEnumerable<string> paths, List<DeclarationFile> into, List<Diagnostic> diagnostics) {
		foreach (var path in paths.Order(StringComparer.Ordinal)) {
			try {
				into.Add(DefinitionFileReader.Read(path, File.ReadAllText(path)));
			} catch (IOException e) {
				diagnostics.Add(new Diagnostic(DiagnosticSeverity.Warning, $"cannot read {path}: {e.Message}", new SourceLocation(path, 1)));
			} catch (UnauthorizedAccessException e) {
				diagnostics.Add(new Diagnostic(DiagnosticSeverity.Warning, $"cannot read {path}: {e.Message}", new SourceLocation(path, 1)));
			}
		}
	}
}
