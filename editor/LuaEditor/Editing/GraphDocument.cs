using System;
using System.Collections.Generic;
using System.IO;
using System.Security.Cryptography;
using System.Text;
using editor.LuaEditor.Graph;
using editor.LuaEditor.Layout;
using editor.LuaEditor.Syntax;
using editor.LuaEditor.Types;

namespace editor.LuaEditor.Editing;

public enum ExternalChange { None, Reloaded, Conflict, Broken }

public sealed class GraphDocument {
	private readonly string m_stdlibDir;
	private string m_lastHash = "";

	private GraphDocument(string luaPath, string projectRoot, string stdlibDir) {
		LuaPath = luaPath;
		ProjectRoot = projectRoot;
		m_stdlibDir = stdlibDir;
	}

	public string LuaPath { get; }
	public string MetaPath => LuaPath + ".meta";
	public string ProjectRoot { get; }

	public GraphSession? Session { get; private set; }

	public string? Error { get; private set; }

	public TypeUniverse Universe { get; private set; } = TypeUniverse.Build([]);

	public static GraphDocument Open(string luaPath, string projectRoot, string stdlibDir) {
		var doc = new GraphDocument(luaPath, projectRoot, stdlibDir);
		doc.Universe = doc.LoadUniverse();
		doc.Load(File.ReadAllText(luaPath), doc.ReadMeta());
		doc.m_lastHash = Hash(File.ReadAllText(luaPath));
		return doc;
	}

	private TypeUniverse LoadUniverse() {
		try {
			return TypeUniverseLoader.Load(ProjectRoot, m_stdlibDir);
		} catch (Exception e) when (e is IOException or UnauthorizedAccessException) {
			return TypeUniverse.Build([]);
		}
	}

	private GraphMeta? ReadMeta() {
		try {
			return File.Exists(MetaPath) ? GraphMeta.Read(File.ReadAllText(MetaPath)) : null;
		} catch (IOException) {
			return null;
		}
	}

	private bool Load(string text, GraphMeta? meta) {
		var document = LuaDocument.Parse(text);
		if (!LuaGraph.TryBuild(document, Universe, out var graph, out var errors)) {
			Session = null;
			Error = errors.Count > 0 ? $"line {errors[0].Location.Line}: {errors[0].Message}" : "the script could not be read";
			return false;
		}
		Session = GraphSession.Open(graph!, meta);
		Error = null;
		return true;
	}

	public bool Save(out string? message) {
		message = null;
		if (Session is null) {
			message = "the script has errors and cannot be saved from the graph";
			return false;
		}
		var text = Session.SaveText();
		if (text is null) {
			message = Session.Message;
			return false;
		}
		try {
			var metaText = Session.CaptureLayout().Write(File.Exists(MetaPath) ? File.ReadAllText(MetaPath) : "");
			WriteAtomic(LuaPath, text);
			if (metaText is not null) WriteAtomic(MetaPath, metaText);
			m_lastHash = Hash(text);
		} catch (Exception e) when (e is IOException or UnauthorizedAccessException) {
			message = "could not write the file: " + e.Message;
			return false;
		}
		Session.MarkSaved(text);
		return true;
	}

	private static void WriteAtomic(string path, string content) {
		var temp = path + ".tmp";
		File.WriteAllText(temp, content, new UTF8Encoding(false));
		File.Move(temp, path, overwrite: true);
	}

	public ExternalChange CheckDisk() {
		string text;
		try {
			text = File.ReadAllText(LuaPath);
		} catch (IOException) {
			return ExternalChange.None;
		}
		if (Hash(text) == m_lastHash) return ExternalChange.None;
		if (Session is { IsDirty: true }) return ExternalChange.Conflict;
		return AcceptDiskVersion() ? ExternalChange.Reloaded : ExternalChange.Broken;
	}

	public bool AcceptDiskVersion() {
		var text = File.ReadAllText(LuaPath);
		m_lastHash = Hash(text);
		var meta = Session?.CaptureLayout() ?? ReadMeta();
		return Load(text, meta);
	}

	public bool LoadRecovered(string text) {
		if (!Load(text, ReadMeta())) return false;
		Session!.MarkDirty();
		return true;
	}

	public void KeepMine() => m_lastHash = Hash(File.ReadAllText(LuaPath));

	public void ReloadTypes() {
		var text = Session?.SaveText() ?? File.ReadAllText(LuaPath);
		var meta = Session?.CaptureLayout() ?? ReadMeta();
		Universe = LoadUniverse();
		Load(text, meta);
	}

	private static string Hash(string text) => Convert.ToHexString(SHA256.HashData(Encoding.UTF8.GetBytes(text)));

	public IReadOnlyList<string> LibraryFolders() {
		var folders = new List<string>();
		foreach (var entry in LuaRc.Load(ProjectRoot, []).Libraries) {
			var full = Path.GetFullPath(Path.Combine(ProjectRoot, entry));
			if (Directory.Exists(full)) folders.Add(full);
		}
		return folders;
	}
}
