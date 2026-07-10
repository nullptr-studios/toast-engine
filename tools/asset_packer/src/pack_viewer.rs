/**
 * @file pack_viewer.rs
 * @author Xein
 * @date 7 July 2026
 *
 * Validates a .pak archive and prints its contents as a tree
 *
 * Validation checks:
 *   - magic and version in header
 *   - file_table_offset is within the file
 *   - every entry's offset + stored_size ≤ file_table_offset
 *   - path_len is sane (≤ 4096)
 *   - for each compressed entry: decompresses cleanly to exactly orig_size bytes
 *
 * Usage: pack_viewer <pack>
 */

mod pack;

use clap::Parser;
use std::collections::BTreeMap;
use std::fs::File;
use std::io::{BufReader, Seek, SeekFrom};
use std::path::PathBuf;

#[derive(Parser)]
#[command(name = "pack_viewer", about = "Validate and display the contents of a .pak file")]
struct Args {
    /// Path to the .pak file
    pack: PathBuf,
}

fn main() -> anyhow::Result<()> {
    let args = Args::parse();

    let pack_path = args.pack.canonicalize().map_err(|e| {
        anyhow::anyhow!("cannot access '{}': {e}", args.pack.display())
    })?;

    let file = File::open(&pack_path)
        .map_err(|e| anyhow::anyhow!("cannot open '{}': {e}", pack_path.display()))?;
    let file_len = file.metadata()?.len();
    let mut r = BufReader::new(file);

    println!("Pack: {}", pack_path.display());
    println!("Size: {} bytes\n", file_len);

    let header = pack::Header::read(&mut r).map_err(|e| {
        anyhow::anyhow!("Header validation failed: {e}")
    })?;
    println!("Header:");
    println!("  magic:             PACK\\0\\0");
    println!("  version:           {}", pack::VERSION);
    println!("  file_count:        {}", header.file_count);
    println!("  file_table_offset: {}", header.file_table_offset);

    // table offset within file
    if header.file_table_offset >= file_len {
        anyhow::bail!(
            "Validation FAILED: file_table_offset {} is beyond file length {}",
            header.file_table_offset, file_len
        );
    }

    r.seek(SeekFrom::Start(header.file_table_offset))?;
    let entries = pack::read_table(&mut r, header.file_count).map_err(|e| {
        anyhow::anyhow!("File-table validation failed: {e}")
    })?;

    let mut total_orig: u64 = 0;
    let mut total_stored: u64 = 0;
    let mut errors: Vec<String> = Vec::new();

    for e in &entries {
        // bounds check: data block must lie entirely before the table
        let end = e.offset.saturating_add(e.stored_size);
        if e.offset < pack::HEADER_SIZE {
            errors.push(format!(
                "'{}': offset {} is before end of header ({})",
                e.rel, e.offset, pack::HEADER_SIZE
            ));
        } else if end > header.file_table_offset {
            errors.push(format!(
                "'{}': data block [{}, {}) extends past file_table_offset {}",
                e.rel, e.offset, end, header.file_table_offset
            ));
        }

        // decompress test for compressed entries
        if e.is_compressed() && errors.is_empty() {
            match pack::read_blob_at(&mut r, e.offset, e.stored_size) {
                Ok(blob) => match pack::decompress(&blob, e.orig_size) {
                    Ok(data) => {
                        if data.len() as u64 != e.orig_size {
                            errors.push(format!(
                                "'{}': decompressed to {} bytes, expected {}",
                                e.rel,
                                data.len(),
                                e.orig_size
                            ));
                        }
                    }
                    Err(err) => {
                        errors.push(format!("'{}': decompress failed: {err}", e.rel));
                    }
                },
                Err(err) => {
                    errors.push(format!("'{}': read failed: {err}", e.rel));
                }
            }
        }

        total_orig += e.orig_size;
        total_stored += e.stored_size;
    }

    if !errors.is_empty() {
        println!("\nValidation FAILED:");
        for err in &errors {
            println!("  ✗ {err}");
        }
        std::process::exit(1);
    }

    println!("\nValidation: OK\n");

    print_tree(&entries);

    let compression_pct = if total_orig > 0 {
        100.0 * (total_orig - total_stored) as f64 / total_orig as f64
    } else {
        0.0
    };
    println!();
    println!("{} file(s)", entries.len());
    println!("Original size: {:.3} MB", total_orig as f64 / 1_048_576.0);
    println!("Stored size:   {:.3} MB", total_stored as f64 / 1_048_576.0);
    println!("Compression:   {:.1} %", compression_pct);

    Ok(())
}


/// A node in the virtual directory tree
enum TreeNode<'a> {
    Dir(BTreeMap<String, TreeNode<'a>>),
    File(&'a pack::Entry),
}

fn build_tree<'a>(entries: &'a [pack::Entry]) -> BTreeMap<String, TreeNode<'a>> {
    let mut root: BTreeMap<String, TreeNode<'a>> = BTreeMap::new();
    for entry in entries {
        let parts: Vec<&str> = entry.rel.split('/').collect();
        insert_entry(&mut root, &parts, entry);
    }
    root
}

fn insert_entry<'a>(
    node: &mut BTreeMap<String, TreeNode<'a>>,
    parts: &[&str],
    entry: &'a pack::Entry,
) {
    if parts.len() == 1 {
        node.insert(parts[0].to_owned(), TreeNode::File(entry));
    } else {
        let dir = node.entry(parts[0].to_owned()).or_insert_with(|| TreeNode::Dir(BTreeMap::new()));
        if let TreeNode::Dir(children) = dir {
            insert_entry(children, &parts[1..], entry);
        }
    }
}

fn print_tree(entries: &[pack::Entry]) {
    let tree = build_tree(entries);
    print_node(&tree, "", true);
}

fn print_node(node: &BTreeMap<String, TreeNode<'_>>, prefix: &str, is_root: bool) {
    let items: Vec<(&String, &TreeNode)> = node.iter().collect();
    let count = items.len();
    for (i, (name, child)) in items.iter().enumerate() {
        let is_last = i + 1 == count;
        let connector = if is_last { "└── " } else { "├── " };
        let child_prefix = if is_last { "    " } else { "│   " };

        match child {
            TreeNode::File(entry) => {
                let info = if entry.is_compressed() {
                    format!(
                        "{} bytes → {} stored  [{:.1}% compression]",
                        entry.orig_size,
                        entry.stored_size,
                        100.0 * (entry.orig_size - entry.stored_size) as f64
                            / entry.orig_size.max(1) as f64
                    )
                } else {
                    format!("{} bytes  [RAW]", entry.orig_size)
                };
                if is_root {
                    println!("{connector}{name}  ({info})");
                } else {
                    println!("{prefix}{connector}{name}  ({info})");
                }
            }
            TreeNode::Dir(children) => {
                if is_root {
                    println!("{connector}{name}/");
                    print_node(children, &format!("{child_prefix}"), false);
                } else {
                    println!("{prefix}{connector}{name}/");
                    print_node(children, &format!("{prefix}{child_prefix}"), false);
                }
            }
        }
    }
}
