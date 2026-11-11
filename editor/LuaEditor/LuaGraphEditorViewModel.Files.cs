using System;
using System.Collections.Generic;
using System.IO;
using System.Threading.Tasks;
using Avalonia.Threading;
using editor.Assets;
using editor.Assets.Types;
using editor.Components.Modals;
using editor.LuaEditor.Editing;

namespace editor.LuaEditor;

public sealed partial class LuaGraphEditorViewModel {
	private GraphDocument? m_document;
	private readonly List<FileSystemWatcher> m_watchers = [];
	private readonly DispatcherTimer m_debounce = new() { Interval = TimeSpan.FromMilliseconds(350) };
	private bool m_typesChanged;
	private bool m_asking;

	public bool IsDirty => Session?.IsDirty == true;

	public string CurrentPath { get; private set; } = "";

	public string CurrentUid { get; private set; } = "";

	public void OpenFile(string uid, string virtualPath, BaseAsset definition, string? contentSourceRealPath = null) {
		StopWatching();
		CurrentUid = uid;
		CurrentPath = virtualPath;
		Title = Path.GetFileName(virtualPath);
		var stdlib = Path.Combine(AppContext.BaseDirectory, "Resources", "lua", "stdlib");
		m_document = GraphDocument.Open(ProjectContext.Resolve(virtualPath), ProjectContext.ProjectPath, stdlib);
		if (contentSourceRealPath is not null && File.Exists(contentSourceRealPath) && m_document.LoadRecovered(File.ReadAllText(contentSourceRealPath))) Status = "Recovered unsaved changes";
		ApplyDocument();
		var real = ProjectContext.Resolve(virtualPath);
		m_lockedByOther = editor.Git.GitService.Current is { IsAvailable: true } git && git.GetLock(real) is { OwnedByMe: false };
		if (File.Exists(real) && File.GetAttributes(real).HasFlag(FileAttributes.ReadOnly)) IsReadOnly = true;
		OnPropertyChanged(nameof(CanEdit));
		StartWatching();
	}

	public bool IsAutosaveDirty => IsDirty;

	public string? AutosaveFileName => CurrentUid.Length > 0 ? CurrentUid + ".lua" : null;

	public async Task WriteAutosaveAsync(string virtualPath) {
		var text = await Dispatcher.UIThread.InvokeAsync(() => Session?.SaveText());
		if (text is null) return;
		var path = ProjectContext.Resolve(virtualPath);
		await Task.Run(() => File.WriteAllText(path, text));
	}

	private void ApplyDocument() {
		if (m_document?.Session is { } session) Open(session);
		else ShowError(m_document?.Error is { } error ? $"{error}. Fix it in a text editor, the graph reloads when the file is saved." : "The script could not be read.");
		OnPropertyChanged(nameof(IsDirty));
	}

	public async Task<bool> ConfirmCloseCurrentAsync() {
		if (!IsDirty) {
			Workspace.AutosaveService.Delete(CurrentUid, ".lua");
			return true;
		}
		if (App.MainWindow is not { } owner) return true;
		var result = await new MessageModal(new ModalConfig("Unsaved Changes", $"Save changes to '{Title}'?", ModalButtons.OkNoCancel, OkLabel: "Save")).ShowDialog<bool?>(owner);
		if (result is null) return false;
		if (result is true) {
			await Save();
			return !IsDirty;
		}
		Workspace.AutosaveService.Delete(CurrentUid, ".lua");
		return true;
	}

	private void StartWatching() {
		if (m_document is null) return;
		m_debounce.Tick -= OnDebounce;
		m_debounce.Tick += OnDebounce;
		Watch(Path.GetDirectoryName(m_document.LuaPath)!, Path.GetFileName(m_document.LuaPath), types: false);
		foreach (var folder in m_document.LibraryFolders()) Watch(folder, "*.d.lua", types: true);
	}

	private void Watch(string folder, string filter, bool types) {
		try {
			var watcher = new FileSystemWatcher(folder, filter) { NotifyFilter = NotifyFilters.LastWrite | NotifyFilters.Size | NotifyFilters.FileName, EnableRaisingEvents = true };
			FileSystemEventHandler handler = (_, _) => Dispatcher.UIThread.Post(() => {
				m_typesChanged |= types;
				m_debounce.Stop();
				m_debounce.Start();
			});
			watcher.Changed += handler;
			watcher.Created += handler;
			watcher.Renamed += (sender, e) => handler(sender, e);
			m_watchers.Add(watcher);
		} catch (Exception e) when (e is ArgumentException or IOException) {
			Status = "could not watch " + folder;
		}
	}

	private void StopWatching() {
		m_debounce.Stop();
		foreach (var w in m_watchers) w.Dispose();
		m_watchers.Clear();
	}

	private async void OnDebounce(object? sender, EventArgs e) {
		m_debounce.Stop();
		if (m_document is null || m_asking) return;
		if (m_typesChanged) {
			m_typesChanged = false;
			m_document.ReloadTypes();
			ApplyDocument();
		}
		switch (m_document.CheckDisk()) {
			case ExternalChange.Reloaded:
				ApplyDocument();
				Status = "Reloaded: the file changed outside the editor";
				break;
			case ExternalChange.Broken:
				ApplyDocument();
				break;
			case ExternalChange.Conflict:
				m_asking = true;
				try {
					var take = App.MainWindow is { } owner
						&& await new MessageModal(new ModalConfig("File Changed", $"'{Title}' was changed outside the editor and you have unsaved edits.", ModalButtons.OkNoCancel, OkLabel: "Use file on disk")).ShowDialog<bool?>(owner) is true;
					if (take) {
						m_document.AcceptDiskVersion();
						ApplyDocument();
					} else {
						m_document.KeepMine();
					}
				} finally {
					m_asking = false;
				}
				break;
		}
	}
}
