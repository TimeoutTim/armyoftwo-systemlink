// SPDX-License-Identifier: GPL-3.0-only
//! Replace a mapped PE in a prepared plaintext/uncompressed XEX, recomputing
//! container hashes through the pinned acceleration library.
use std::{env, fs, io::Write};
use xex2::Xex2;
use xex2::writer::{TargetCompression, TargetEncryption, TargetMachine};

fn main() {
    let args: Vec<String> = env::args().collect();
    assert_eq!(args.len(), 4, "usage: ao2-xex-rebuild prepared.xex mapped.pe output.xex");
    let mut source = fs::read(&args[1]).expect("read source XEX");
    let pe = fs::read(&args[2]).expect("read patched mapped PE");
    // Native Join uses verified zero padding in the BINKBSS read-only page.
    // A PE executable flag alone does not change XEX loader page protections:
    // promote precisely that descriptor before rebuilding/signing its hashes.
    let parsed = Xex2::parse(&source).expect("parse source XEX");
    let flags = u32::from_le_bytes(pe[0x294..0x298].try_into().unwrap());
    let mut promoted = None;
    if flags == 0x60000040 {
        assert_eq!(&pe[0x270..0x278], b"BINKBSS\0");
        assert_eq!(u32::from_le_bytes(pe[0x278..0x27c].try_into().unwrap()), 0xf000);
        assert_eq!(u32::from_le_bytes(pe[0x27c..0x280].try_into().unwrap()), 0x201000);
        let original = parsed.extract_basefile(&source).expect("extract baseline");
        assert_eq!(u32::from_le_bytes(original[0x294..0x298].try_into().unwrap()), 0x40000040);
        assert!(original[0x204000..0x210000].iter().all(|b| *b == 0));
        assert_eq!(parsed.security_info.image_size as usize, pe.len());
        let base = parsed.header.security_offset as usize + 0x184;
        let mut page = 0;
        for i in 0..parsed.security_info.page_descriptor_count as usize {
            let offset = base + 24*i;
            let info = u32::from_be_bytes(source[offset..offset+4].try_into().unwrap());
            let count = info >> 4;
            if page == 0x20 {
                assert_eq!(count, 1, "payload must occupy one 64 KiB descriptor");
                assert_eq!(info & 15, 3, "only promote a read-only page");
                source[offset..offset+4].copy_from_slice(&((count << 4) | 1).to_be_bytes());
                promoted = Some(i);
            }
            page += count;
        }
        assert_eq!(page as usize * 0x10000, pe.len(), "unexpected XEX page geometry");
        assert!(promoted.is_some(), "missing payload page descriptor");
    }
    // The pinned library's in-place replace_pe path regenerates page hashes
    // without refreshing header_hash when there are no optional-header edits.
    // Its full assembler hashes the finished descriptor table and signs that
    // result correctly. Compressed, retail-encrypted delivery also matches the
    // format that successfully cold-launched on hardware; the normalized
    // plaintext/uncompressed baseline was rejected there. This changes the
    // container representation, not the requested mapped PE or restrictions.
    let mut output = Vec::new();
    Xex2::parse(&source).expect("parse source XEX")
        .rebuild(&source).replace_pe(pe.clone())
        .target_compression(TargetCompression::Normal)
        .target_encryption(TargetEncryption::Encrypted)
        .target_machine(TargetMachine::Retail)
        .write_to(&mut output).expect("assemble compressed retail-encrypted XEX");
    let verified = Xex2::parse(&output).expect("parse rebuilt XEX");
    let format = verified.header.file_format_info().expect("read rebuilt file format");
    assert_eq!(format.compression_type, xex2::header::CompressionType::Normal);
    assert_eq!(format.encryption_type, xex2::header::EncryptionType::Normal);
    assert_eq!(
        xex2::hashes::compute_header_hash(&output, &verified.header, &verified.security_info),
        verified.security_info.image_info.header_hash,
        "rebuilt XEX security header hash does not cover its final descriptor table"
    );
    if let Some(i) = promoted {
        let parsed = Xex2::parse(&output).expect("parse rebuilt XEX");
        let offset = parsed.header.security_offset as usize + 0x184 + 24*i;
        assert_eq!(u32::from_be_bytes(output[offset..offset+4].try_into().unwrap()), 0x11);
    }
    let check = Xex2::parse(&output).expect("parse rebuilt XEX")
        .extract_basefile(&output).expect("extract rebuilt PE");
    assert_eq!(check, pe, "rebuilt executable does not contain requested image");
    xex2::page_descriptors::verify_chain(
        &check, &verified.header, &verified.security_info, &output
    ).expect("rebuilt XEX page hash chain does not match its decoded image");
    let mut file = fs::OpenOptions::new().write(true).create_new(true)
        .open(&args[3]).expect("create new output (must not exist)");
    file.write_all(&output).expect("write output");
    println!("Verified {} byte rebuilt XEX", output.len());
}
