/**
 * @file packer.rs
 * @author Xein
 * @date 7 Jul 2026
 *
 * Packs a folder of files into a .pak archive
 *
 * Usage: packer <folder> [output]
 *   <folder>   Source directory
 *   [output]   Output file path (default: ./<folder_name>.pak)
 */

mod pack;

use clap::Parser;
use indicatif::{ProgressBar, ProgressStyle};
use std::fs::{self, File};
use std::io::{BufWriter, Seek, SeekFrom, Write};
use std::path::PathBuf;

#[derive(Parser)]
#[command(name = "packer", about = "Pack a folder into a .pak archive")]
struct Args {
    /// Source directory
    folder: PathBuf,
    /// Output file path (default: ./<folder_name>.pak)
    output: Option<PathBuf>,
}

fn main() -> anyhow::Result<()> {
    let args = Args::parse();

    let folder = args.folder.canonicalize().map_err(|e| {
        anyhow::anyhow!("cannot access folder '{}': {e}", args.folder.display())
    })?;

    if !folder.is_dir() {
        anyhow::bail!("'{}' is not a directory", folder.display());
    }

    // derive output path
    let folder_name = folder
        .file_name()
        .ok_or_else(|| anyhow::anyhow!("folder has no file name component"))?
        .to_string_lossy()
        .into_owned();

    let out_path: PathBuf = match args.output {
        Some(p) => {
            let p = if p.is_absolute() { p } else { std::env::current_dir().unwrap().join(p) };
            // append .pak if the user didn't supply an extension
            if p.extension().is_none() {
                p.with_extension("pak")
            } else {
                p
            }
        }
        None => std::env::current_dir()
            .expect("cannot read CWD")
            .join(format!("{folder_name}.pak")),
    };
    if let Some(parent) = out_path.parent() {
        fs::create_dir_all(parent)?;
    }

    println!("Packing '{}' → '{}'", folder.display(), out_path.display());

    // collect files
    let files = pack::collect_files(&folder)?;
    if files.is_empty() {
        println!("No files found, nothing to pack.");
        return Ok(());
    }

    // progress bar
    let pb = ProgressBar::new(files.len() as u64);
    pb.set_style(
        ProgressStyle::with_template(
            "[{bar:40.cyan/blue}] {pos}/{len} {msg}",
        )
        .unwrap()
        .progress_chars("=>-"),
    );

    let out_file = File::create(&out_path)
        .map_err(|e| anyhow::anyhow!("cannot create '{}': {e}", out_path.display()))?;
    let mut w = BufWriter::new(out_file);

    // write placeholder header
    let placeholder = pack::Header { file_count: 0, file_table_offset: 0 };
    placeholder.write(&mut w)?;

    let mut entries: Vec<pack::Entry> = Vec::with_capacity(files.len());
    let mut total_orig: u64 = 0;
    let mut total_stored: u64 = 0;

    for path in &files {
        let rel = pack::canonical_rel(path, &folder)?;
        pb.set_message(rel.clone());

        let data = fs::read(path)
            .map_err(|e| anyhow::anyhow!("cannot read '{}': {e}", path.display()))?;
        let orig_size = data.len() as u64;

        let (payload, flags) = match pack::try_compress(&data) {
            Some(compressed) => (compressed, pack::FLAG_COMPRESSED),
            None => (data, 0u8),
        };
        let stored_size = payload.len() as u64;

        // record offset before writing
        let offset = w.stream_position()?;
        if !payload.is_empty() {
            w.write_all(&payload)?;
        }

        let hash = pack::fnv1a_hash64(rel.as_bytes());
        entries.push(pack::Entry { hash, rel, offset, orig_size, stored_size, flags });

        total_orig += orig_size;
        total_stored += stored_size;
        pb.inc(1);
    }
    pb.finish_and_clear();

    // sort by (hash, rel) and write file table
    entries.sort_by(|a, b| a.hash.cmp(&b.hash).then_with(|| a.rel.cmp(&b.rel)));
    let table_offset = w.stream_position()?;
    pack::write_table(&mut w, &entries)?;

    // rewrite header with final values
    w.seek(SeekFrom::Start(0))?;
    let final_hdr = pack::Header {
        file_count: entries.len() as u32,
        file_table_offset: table_offset,
    };
    final_hdr.write(&mut w)?;
    w.flush()?;

    // summary
    let compression_pct = if total_orig > 0 {
        100.0 * (total_orig - total_stored) as f64 / total_orig as f64
    } else {
        0.0
    };

    println!("\nWrote '{}' ({} files)", out_path.display(), entries.len());
    println!("  Original size:  {:.3} MiB", total_orig as f64 / 1_048_576.0);
    println!("  Stored size:    {:.3} MiB", total_stored as f64 / 1_048_576.0);
    println!("  Compression:    {:.1} %", compression_pct);

    Ok(())
}
