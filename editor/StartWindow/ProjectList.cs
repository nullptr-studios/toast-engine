using System;
using System.Collections.ObjectModel;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Text.Json;
using System.Text.Json.Serialization;
using Tomlyn;
using Tomlyn.Model;

namespace editor.StartWindow;

public record struct ProjectListItem {
	public string Title { get; set; }
	public string Path { get; set; }
	public string Date { get; set; }
	public string Version { get; set; }
	public string ThumbnailPath { get; set; }
}

public class ProjectList {
	private static readonly JsonSerializerOptions JsonOptions = new() { WriteIndented = true };

	private static readonly string m_dir = Path.Combine(
		Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData),
		"ToastEngine"
	);

	private static readonly string m_path = Path.Combine(m_dir, "project_list.json");

	[JsonConstructor]
	private ProjectList() { }

	public ObservableCollection<ProjectListItem> Projects { get; set; } = [];

	public static ProjectList LoadList() {
		if (!Directory.Exists(m_dir)) Directory.CreateDirectory(m_dir);

		var loaded = File.Exists(m_path) ? LoadFromJson() : new ProjectList();

		// Sort by date (newest first)
		var sorted = loaded.Projects.OrderByDescending(p => {
			if (DateTime.TryParseExact(p.Date, "dd MMM yyyy HH:mm", null, DateTimeStyles.None, out var parsedDate))
				return parsedDate;
			return DateTime.MinValue;
		}).ToList();

		loaded.Projects = new ObservableCollection<ProjectListItem>(sorted);
		return loaded;
	}

	public void SaveList() {
		using var stream = File.Create(m_path);
		JsonSerializer.Serialize(stream, this, JsonOptions);
	}

	public ProjectListItem Upsert(string toastPath) {
		var item = ReadFromToastFile(toastPath);

		for (var i = 0; i < Projects.Count; i++)
			if (Projects[i].Path == toastPath) {
				Projects.RemoveAt(i);
				break;
			}

		Projects.Insert(0, item);
		return item;
	}

	private static ProjectList LoadFromJson() {
		using var stream = File.OpenRead(m_path);
		return JsonSerializer.Deserialize<ProjectList>(stream, JsonOptions) ?? new ProjectList();
	}

	private static ProjectListItem ReadFromToastFile(string toastPath) {
		var dir = Path.GetDirectoryName(toastPath) ?? "";
		var thumbnailPath = Path.Combine(dir, ".toast", "thumbnails", "project.png");
		var thumbnail = File.Exists(thumbnailPath) ? thumbnailPath : "";
		var date = DateTime.Now.ToString("dd MMM yyyy HH:mm");

		try {
			var projectContent = File.ReadAllText(toastPath);
			var projectData = TomlSerializer.Deserialize<TomlTable>(projectContent);

			return new ProjectListItem {
				Title = projectData?["name"].ToString() ?? "Untitled Project",
				Path = toastPath,
				Date = date,
				Version = ReadVersion(projectData),
				ThumbnailPath = thumbnail
			};
		} catch {
			return new ProjectListItem {
				Title = Path.GetFileNameWithoutExtension(toastPath),
				Path = toastPath,
				Date = date,
				Version = "",
				ThumbnailPath = thumbnail
			};
		}
	}

	private static string ReadVersion(TomlTable? data) {
		if (data?["version"] is TomlArray table) return $"v{string.Join(".", table)}";
		return data?["version"]?.ToString() ?? "v0.0.?";
	}
}
