#![allow(dead_code)]

use std::io::{self, Read, Seek, SeekFrom, Write};
use std::path::{Path, PathBuf};

/**
 * @file pack.rs
 * @author Xein
 * @date 7 Jul 2026
 *
 * Shared pack format definitions for the TOAST asset packer toolchain
 *
 * On-disk format (little-endian, no padding):
 *
 * [ Header — 20 bytes ]
 *   magic[6]            "PACK\0\0"
 *   version u16         2
 *   file_count u32
 *   file_table_offset u64
 *
 * [ File data blocks: one raw or LZ4-compressed blob per file ]
 *
 * [ File table at file_table_offset ]
 *   file_count u32
 *   For each entry (sorted by hash, then rel path):
 *     hash u64
 *     path_len u32
 *     path[path_len] (UTF-8, forward slashes)
 *     offset u64        — byte offset of this file's data block
 *     orig_size u64     — uncompressed length
 *     stored_size u64   — bytes written (compressed or raw)
 *     flags u8          — bit 0: LZ4-compressed
 */

pub const MAGIC: [u8; 6] = *b"PACK\0\0";
pub const VERSION: u16 = 2;
pub const HEADER_SIZE: u64 = 20;
pub const FLAG_COMPRESSED: u8 = 1;

#[derive(Debug, Clone)]
pub struct Header {
    pub file_count: u32,
    pub file_table_offset: u64,
}

impl Header {
    pub fn write<W: Write>(&self, w: &mut W) -> io::Result<()> {
        w.write_all(&MAGIC)?;
        w.write_all(&VERSION.to_le_bytes())?;
        w.write_all(&self.file_count.to_le_bytes())?;
        w.write_all(&self.file_table_offset.to_le_bytes())?;
        Ok(())
    }

    pub fn read<R: Read>(r: &mut R) -> io::Result<Self> {
        let mut magic = [0u8; 6];
        r.read_exact(&mut magic)?;
        if magic != MAGIC {
            return Err(io::Error::new(
                io::ErrorKind::InvalidData,
                format!(
                    "bad magic: expected {:?}, got {:?}",
                    std::str::from_utf8(&MAGIC).unwrap_or("???"),
                    magic
                ),
            ));
        }

        let mut ver = [0u8; 2];
        r.read_exact(&mut ver)?;
        let version = u16::from_le_bytes(ver);
        if version != VERSION {
            return Err(io::Error::new(
                io::ErrorKind::InvalidData,
                format!("unsupported version {version}, expected {VERSION}"),
            ));
        }

        let mut fc = [0u8; 4];
        r.read_exact(&mut fc)?;
        let file_count = u32::from_le_bytes(fc);

        let mut fto = [0u8; 8];
        r.read_exact(&mut fto)?;
        let file_table_offset = u64::from_le_bytes(fto);

        Ok(Self { file_count, file_table_offset })
    }
}

#[derive(Debug, Clone)]
pub struct Entry {
    pub hash: u64,
    pub rel: String,
    pub offset: u64,
    pub orig_size: u64,
    pub stored_size: u64,
    pub flags: u8,
}

impl Entry {
    pub fn is_compressed(&self) -> bool {
        self.flags & FLAG_COMPRESSED != 0
    }

    pub fn write_to_table<W: Write>(&self, w: &mut W) -> io::Result<()> {
        w.write_all(&self.hash.to_le_bytes())?;
        let path_bytes = self.rel.as_bytes();
        w.write_all(&(path_bytes.len() as u32).to_le_bytes())?;
        w.write_all(path_bytes)?;
        w.write_all(&self.offset.to_le_bytes())?;
        w.write_all(&self.orig_size.to_le_bytes())?;
        w.write_all(&self.stored_size.to_le_bytes())?;
        w.write_all(&[self.flags])?;
        Ok(())
    }

    pub fn read_from_table<R: Read>(r: &mut R) -> io::Result<Self> {
        let hash = read_u64(r)?;

        let path_len = read_u32(r)?;
        if path_len > 4096 {
            return Err(io::Error::new(
                io::ErrorKind::InvalidData,
                format!("path_len {path_len} is unreasonably large"),
            ));
        }
        let mut path_buf = vec![0u8; path_len as usize];
        r.read_exact(&mut path_buf)?;
        let rel = String::from_utf8(path_buf).map_err(|e| {
            io::Error::new(io::ErrorKind::InvalidData, format!("non-UTF-8 path: {e}"))
        })?;

        let offset = read_u64(r)?;
        let orig_size = read_u64(r)?;
        let stored_size = read_u64(r)?;
        let mut flags_buf = [0u8; 1];
        r.read_exact(&mut flags_buf)?;
        let flags = flags_buf[0];

        Ok(Self { hash, rel, offset, orig_size, stored_size, flags })
    }
}

pub fn write_table<W: Write>(w: &mut W, entries: &[Entry]) -> io::Result<()> {
    w.write_all(&(entries.len() as u32).to_le_bytes())?;
    for e in entries {
        e.write_to_table(w)?;
    }
    Ok(())
}

pub fn read_table<R: Read>(r: &mut R, expected_count: u32) -> io::Result<Vec<Entry>> {
    let count = read_u32(r)?;
    if count != expected_count {
        return Err(io::Error::new(
            io::ErrorKind::InvalidData,
            format!("table file_count {count} doesn't match header {expected_count}"),
        ));
    }
    let mut entries = Vec::with_capacity(count as usize);
    for _ in 0..count {
        entries.push(Entry::read_from_table(r)?);
    }
    Ok(entries)
}

pub fn fnv1a_hash64(bytes: &[u8]) -> u64 {
    const BASIS: u64 = 14695981039346656037;
    const PRIME: u64 = 1099511628211;
    let mut hash = BASIS;
    for &b in bytes {
        hash ^= b as u64;
        hash = hash.wrapping_mul(PRIME);
    }
    hash
}

pub fn canonical_rel(path: &Path, base: &Path) -> io::Result<String> {
    let rel = path.strip_prefix(base).map_err(|_| {
        io::Error::new(
            io::ErrorKind::InvalidInput,
            format!("'{}' is not under base '{}'", path.display(), base.display()),
        )
    })?;
    // ensure forward slash on all platforms
    let s = rel
        .components()
        .map(|c| c.as_os_str().to_string_lossy().into_owned())
        .collect::<Vec<_>>()
        .join("/");
    Ok(s)
}

/// Collect all regular files on the folder
pub fn collect_files(dir: &Path) -> io::Result<Vec<PathBuf>> {
    let mut files = Vec::new();
    collect_recursive(dir, &mut files)?;
    files.sort();
    Ok(files)
}

fn collect_recursive(dir: &Path, out: &mut Vec<PathBuf>) -> io::Result<()> {
    for entry in std::fs::read_dir(dir)? {
        let entry = entry?;
        let path = entry.path();
        let ft = entry.file_type()?;
        if ft.is_dir() {
            collect_recursive(&path, out)?;
        } else if ft.is_file() {
            out.push(path);
        }
    }
    Ok(())
}

/// Compress data with LZ4 default
/// Returns None if size would be bigger
pub fn try_compress(data: &[u8]) -> Option<Vec<u8>> {
    if data.is_empty() {
        return None;
    }
    let compressed =
        lz4::block::compress(data, Some(lz4::block::CompressionMode::HIGHCOMPRESSION(12)), false).ok()?;
    if compressed.len() + 8 < data.len() {
        Some(compressed)
    } else {
        None
    }
}

/// Decompress an LZ4 block of `stored_size` bytes back to `orig_size` bytes
pub fn decompress(data: &[u8], orig_size: u64) -> io::Result<Vec<u8>> {
    lz4::block::decompress(data, Some(orig_size as i32)).map_err(|e| {
        io::Error::new(io::ErrorKind::InvalidData, format!("LZ4 decompress failed: {e}"))
    })
}

fn read_u32<R: Read>(r: &mut R) -> io::Result<u32> {
    let mut buf = [0u8; 4];
    r.read_exact(&mut buf)?;
    Ok(u32::from_le_bytes(buf))
}

fn read_u64<R: Read>(r: &mut R) -> io::Result<u64> {
    let mut buf = [0u8; 8];
    r.read_exact(&mut buf)?;
    Ok(u64::from_le_bytes(buf))
}

/// Read `size` bytes from `r` at `offset`.
pub fn read_blob_at<R: Read + Seek>(r: &mut R, offset: u64, size: u64) -> io::Result<Vec<u8>> {
    r.seek(SeekFrom::Start(offset))?;
    let mut buf = vec![0u8; size as usize];
    r.read_exact(&mut buf)?;
    Ok(buf)
}
