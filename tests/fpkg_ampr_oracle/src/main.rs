use ampr_pkg::kraken::{decode_block, Desc, Scratch};
use std::env;
use std::fs;

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
    println!("pinned AMPR readers accepted indexes and decoded the codec-2 pack chunk");
}
