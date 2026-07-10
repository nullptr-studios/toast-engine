/**
 * @file unpacker.rs
 * @author Xein
 * @date 7 Jul 2026
 *
 * Extracts a .pak archive back to the filesystem
 *
 * Usage: unpacker <pack> [output_dir]
 *   <pack>        Path to the .pak file
 *   [output_dir]  Output directory (default: ./<pack_stem>/)
 */

mod pack;

use clap::Parser;
use indicatif::{ProgressBar, ProgressStyle};
use std::fs::{self, File};
use std::io::{BufReader, BufWriter, Seek, SeekFrom, Write};
use std::path::PathBuf;

#[derive(Parser)]
#[command(name = "unpacker", about = "Extract a .pak archive to a directory")]
struct Args {
    /// Path to the .pak file
    pack: PathBuf,
    /// Output directory (default: ./<pack_stem>/)
    output_dir: Option<PathBuf>,
}

fn main() -> anyhow::Result<()> {
    let args = Args::parse();

    let pack_path = args.pack.canonicalize().map_err(|e| {
        anyhow::anyhow!("cannot access '{}': {e}", args.pack.display())
    })?;

    // derive default output dir from pack stem
    let stem = pack_path
        .file_stem()
        .ok_or_else(|| anyhow::anyhow!("pack file has no stem"))?
        .to_string_lossy()
        .into_owned();

    let out_dir = args
        .output_dir
        .map(|d| if d.is_absolute() { d } else { std::env::current_dir().unwrap().join(d) })
        .unwrap_or_else(|| std::env::current_dir().expect("cannot read CWD").join(&stem));

    println!("Extracting '{}' → '{}'", pack_path.display(), out_dir.display());

    let file = File::open(&pack_path)
        .map_err(|e| anyhow::anyhow!("cannot open '{}': {e}", pack_path.display()))?;
    let mut r = BufReader::new(file);

    // read and validate header
    let header = pack::Header::read(&mut r)?;

    // seek to table and read entries
    r.seek(SeekFrom::Start(header.file_table_offset))?;
    let entries = pack::read_table(&mut r, header.file_count)?;

    if entries.is_empty() {
        println!("Pack is empty, nothing to extract.");
        return Ok(());
    }

    fs::create_dir_all(&out_dir)?;

    // single-line progress bar
    let pb = ProgressBar::new(entries.len() as u64);
    pb.set_style(
        ProgressStyle::with_template(
            "{spinner:.green} [{bar:40.cyan/blue}] {pos}/{len} {msg}",
        )
        .unwrap()
        .progress_chars("=>-"),
    );

    for entry in &entries {
        pb.set_message(entry.rel.clone());

        // read stored blob
        let blob = pack::read_blob_at(&mut r, entry.offset, entry.stored_size)?;

        // decompress if needed
        let data = if entry.is_compressed() {
            pack::decompress(&blob, entry.orig_size)?
        } else {
            blob
        };

        // write to output path
        let dest = out_dir.join(&entry.rel);
        if let Some(parent) = dest.parent() {
            fs::create_dir_all(parent)?;
        }
        let out_file = File::create(&dest)
            .map_err(|e| anyhow::anyhow!("cannot create '{}': {e}", dest.display()))?;
        let mut bw = BufWriter::new(out_file);
        bw.write_all(&data)?;
        bw.flush()?;

        pb.inc(1);
    }
    pb.finish_and_clear();

    println!("Extracted {} file(s) to '{}'", entries.len(), out_dir.display());
    Ok(())
}
