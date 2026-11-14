use ampr_pkg::kraken::{decode_block, Desc, Scratch};
use std::env;
use std::fs;
use std::path::Path;

#[path = "../../../kraken-mount-reference/src/ampr_index.rs"]
#[allow(dead_code)]
mod kraken_ampr_index;

fn main() {
    let args: Vec<String> = env::args().collect();
    assert_eq!(args.len(), 4, "usage: oracle INDEX MANIFEST PACKAGE");
    let index_bytes = fs::read(&args[1]).unwrap();
    let index = ampr_format::index::Index::open(index_bytes.as_slice())
        .expect("pinned AMPRIDX3 reader accepts C output");
    assert_eq!(index.len(), 1);
    let record = index.record(1).unwrap();
    let mut path = vec![0u8; record.path_len as usize];
    assert_eq!(index.path(&record, &mut path).unwrap(), b"/app0/eboot.bin");
    assert_eq!(record.size, 0x40000);

    let manifest_bytes = fs::read(&args[2]).unwrap();
    let manifest = ampr_pkg::open_pkg(manifest_bytes.as_slice())
        .expect("pinned AMPRPAK4 pkg reader accepts C output");
    assert_eq!(manifest.file_count(), 1);
    assert_eq!(manifest.chunk_count(), 1);
    let file = manifest.file(0).unwrap();
    let mut manifest_path = vec![0u8; file.path_len as usize];
    assert_eq!(manifest.path(&file, &mut manifest_path).unwrap(), b"/app0/eboot.bin");
    assert_eq!(file.logical_size, 0x40000);
    let chunk = manifest.chunk_record(0).unwrap();
    assert_eq!(chunk.codec, 2);
    let (offset, pack_id, descriptor) = ampr_pkg::pkg_chunk(chunk);
    assert_eq!(offset, 0x20000);
    assert_eq!(pack_id, 0);
    assert_eq!(descriptor, 0x20000);

    let package = fs::read(&args[3]).unwrap();
    let reference_dir = Path::new(&args[2]).parent().unwrap().join("reference");
    fs::create_dir_all(&reference_dir).unwrap();
    let reference_package = reference_dir.join("kraken.pkg");
    fs::copy(&args[3], &reference_package).unwrap();
    let reference_file = kraken_ampr_index::IndexFile {
        path: "eboot.bin".to_string(),
        size: 0x40000,
        chunks: Some(vec![kraken_ampr_index::Chunk {
            pkg_off: 0x20000,
            stored_len: 0x40000,
            raw_len: 0x40000,
            kraken: Some(kraken_ampr_index::Kraken {
                even_len: 0x20000,
                mode: 0,
            }),
        }]),
    };
    kraken_ampr_index::write(&reference_dir, &reference_package, &[reference_file])
        .expect("pinned Kraken writer generates reference indexes");
    assert_eq!(
        fs::read(&args[1]).unwrap(),
        fs::read(reference_dir.join("ampr_emu.index")).unwrap(),
        "AMPRIDX3 output matches pinned Kraken writer byte for byte"
    );
    assert_eq!(
        fs::read(&args[2]).unwrap(),
        fs::read(reference_dir.join("ampr_assets.index")).unwrap(),
        "AMPRPAK4 output matches pinned Kraken writer byte for byte"
    );
    let start = usize::try_from(offset).unwrap();
    let end = start.checked_add(chunk.stored_size as usize).unwrap();
    let stored = package.get(start..end).expect("chunk remains inside pack 0");
    let mut decoded = vec![0u8; 0x40000];
    let mut scratch = Scratch::new_boxed();
    assert_eq!(
        decode_block(
            stored,
            Desc { even_len: 0x20000, mode: 0 },
            &mut decoded,
            &mut scratch,
        )
        .unwrap(),
        decoded.len()
    );
    for (i, byte) in decoded.iter().enumerate() {
        assert_eq!(*byte, ((i * 29 + i / 113) & 0xff) as u8, "byte {i}");
    }
    println!("C indexes match the pinned Kraken writer byte for byte; pinned AMPR readers decoded the codec-2 pack chunk");
}
