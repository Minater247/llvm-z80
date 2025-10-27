#!/usr/bin/env python3
"""
ELF to TRSDOS /CMD File Converter

Converts Z80 ELF files produced by clang/LLVM to TRSDOS /CMD format.
Based on the TRSDOS /CMD File Format Specification.
"""

import argparse
import struct
import sys
from pathlib import Path
from typing import List, Tuple, Optional


SHT_PROGBITS = 1
SHT_NOBITS = 8
SHF_ALLOC = 0x2

class ELFParser:
    """Simple ELF parser for Z80 files"""
    
    def __init__(self, filename: str):
        self.filename = filename
        with open(filename, 'rb') as f:
            self.data = f.read()
        
        # Parse ELF header
        if self.data[:4] != b'\x7fELF':
            raise ValueError("Not a valid ELF file")
        
        # Check if it's 32-bit little-endian
        if self.data[4] != 1 or self.data[5] != 1:
            raise ValueError("Only 32-bit little-endian ELF files supported")
        
        # Parse ELF header fields
        self.entry_point = struct.unpack('<I', self.data[24:28])[0]
        self.phoff = struct.unpack('<I', self.data[28:32])[0]  # Program header offset
        self.shoff = struct.unpack('<I', self.data[32:36])[0]  # Section header offset
        self.phentsize = struct.unpack('<H', self.data[42:44])[0]  # Program header entry size
        self.phnum = struct.unpack('<H', self.data[44:46])[0]  # Number of program headers
        self.shentsize = struct.unpack('<H', self.data[46:48])[0]  # Section header entry size
        self.shnum = struct.unpack('<H', self.data[48:50])[0]  # Number of section headers
        self.shstrndx = struct.unpack('<H', self.data[50:52])[0]  # Section header string table index
    
    def get_loadable_segments(self) -> List[Tuple[int, int, bytes]]:
        """Get all PT_LOAD segments as (vaddr, paddr, data) tuples"""
        segments = []
        
        for i in range(self.phnum):
            ph_offset = self.phoff + i * self.phentsize
            ph_data = self.data[ph_offset:ph_offset + self.phentsize]
            
            p_type = struct.unpack('<I', ph_data[0:4])[0]
            if p_type == 1:  # PT_LOAD
                p_offset = struct.unpack('<I', ph_data[4:8])[0]
                p_vaddr = struct.unpack('<I', ph_data[8:12])[0]
                p_paddr = struct.unpack('<I', ph_data[12:16])[0]
                p_filesz = struct.unpack('<I', ph_data[16:20])[0]
                p_memsz = struct.unpack('<I', ph_data[20:24])[0]
                
                # Extract the data from the file
                segment_data = self.data[p_offset:p_offset + p_filesz]
                
                # If memory size > file size, pad with zeros
                if p_memsz > p_filesz:
                    segment_data += b'\x00' * (p_memsz - p_filesz)
                
                segments.append((p_vaddr, p_paddr, segment_data))
        
        return segments
    
    def get_sections(self) -> List[Tuple[str, int, int, int, bytes]]:
        """Get all sections as (name, vaddr, size, type, data) tuples"""
        sections = []
        
        # Get string table for section names
        if self.shstrndx >= self.shnum:
            return sections
        
        strshdr_offset = self.shoff + self.shstrndx * self.shentsize
        strshdr_data = self.data[strshdr_offset:strshdr_offset + self.shentsize]
        str_offset = struct.unpack('<I', strshdr_data[16:20])[0]
        str_size = struct.unpack('<I', strshdr_data[20:24])[0]
        strtab = self.data[str_offset:str_offset + str_size]
        
        for i in range(self.shnum):
            sh_offset = self.shoff + i * self.shentsize
            sh_data = self.data[sh_offset:sh_offset + self.shentsize]
            
            sh_name_idx = struct.unpack('<I', sh_data[0:4])[0]
            sh_type = struct.unpack('<I', sh_data[4:8])[0]
            sh_flags = struct.unpack('<I', sh_data[8:12])[0]
            sh_addr = struct.unpack('<I', sh_data[12:16])[0]
            sh_offset_in_file = struct.unpack('<I', sh_data[16:20])[0]
            sh_size = struct.unpack('<I', sh_data[20:24])[0]
            
            # Extract section name
            name_end = strtab.find(b'\x00', sh_name_idx)
            if name_end == -1:
                name_end = len(strtab)
            name = strtab[sh_name_idx:name_end].decode('ascii', errors='ignore')
            
            # Only include allocated sections with data
            if sh_flags & SHF_ALLOC and sh_size > 0:
                if sh_type == SHT_NOBITS:  # e.g. .bss
                    section_data = b'\x00' * sh_size
                else:
                    section_data = self.data[sh_offset_in_file:sh_offset_in_file + sh_size]
                    if len(section_data) < sh_size:
                        section_data += b'\x00' * (sh_size - len(section_data))

                sections.append((name, sh_addr, sh_size, sh_type, section_data))
        
        return sections

class CMDWriter:
    """TRSDOS /CMD file writer"""
    
    def __init__(self):
        self.records = []
    
    def add_header(self, module_name: str):
        """Add module header record (type 0x05)"""
        name_bytes = module_name.encode('ascii')[:8].ljust(8, b' ')
        self.records.append(struct.pack('<BB', 0x05, len(name_bytes)) + name_bytes)
    
    def add_load_block(self, addr: int, data: bytes):
        """Add load block record(s) (type 0x01)"""
        # Split data into chunks of max 254 bytes (256 - 2 for address)
        offset = 0
        while offset < len(data):
            chunk_size = min(254, len(data) - offset)
            chunk_data = data[offset:offset + chunk_size]
            
            # Length field: actual length is chunk_size + 2 (for address)
            # But if it's exactly 254, 255, or 256 bytes, use special encoding
            total_len = chunk_size + 2
            if total_len == 256:
                len_byte = 0x00
            elif total_len == 255:
                len_byte = 0x01  # This would be 255 bytes payload + 2 = 257, invalid
                # Actually, let's handle this correctly
                if chunk_size == 254:
                    len_byte = 0x00  # 0x00 means 256 total, so 254 payload
                else:
                    len_byte = total_len & 0xFF
            else:
                len_byte = total_len & 0xFF
            
            record = struct.pack('<BBH', 0x01, len_byte, addr + offset) + chunk_data
            self.records.append(record)
            offset += chunk_size
    
    def add_transfer_address(self, addr: int):
        """Add transfer address record (type 0x02)"""
        self.records.append(struct.pack('<BBH', 0x02, 0x02, addr))
    
    def write_to_file(self, filename: str):
        """Write all records to a /CMD file"""
        with open(filename, 'wb') as f:
            for record in self.records:
                f.write(record)

def convert_elf_to_cmd(elf_file: str, cmd_file: str, module_name: Optional[str] = None, verbose: bool = False):
    """Convert ELF file to CMD file"""
    
    if verbose:
        print(f"Parsing ELF file: {elf_file}")
    
    # Parse ELF file
    try:
        elf = ELFParser(elf_file)
    except Exception as e:
        print(f"Error parsing ELF file: {e}", file=sys.stderr)
        return False
    
    if verbose:
        print(f"Entry point: 0x{elf.entry_point:04X}")
    
    # Create CMD writer
    cmd = CMDWriter()
    
    # Add module header if specified
    if module_name:
        cmd.add_header(module_name)
        if verbose:
            print(f"Added module header: {module_name}")
    
    # For Z80 ELF files, use sections instead of segments to avoid padding
    sections = elf.get_sections()
    if verbose:
        print(f"Found {len(sections)} loadable sections:")
    
    sections_added = False
    for name, vaddr, size, sh_type, data in sections:
        if len(data) == 0:
            continue

        if sh_type not in (SHT_PROGBITS, SHT_NOBITS):
            continue

        if verbose:
            print(f"  Section {name} at 0x{vaddr:04X}, size {len(data)} bytes")
        cmd.add_load_block(vaddr, data)
        sections_added = True
    
    # Fallback to segments if no sections were added
    if not sections_added:
        segments = elf.get_loadable_segments()
        if verbose:
            print(f"No suitable sections found, using {len(segments)} loadable segments:")
        
        for vaddr, paddr, data in segments:
            if len(data) > 0:  # Only add segments with actual data
                if verbose:
                    print(f"  Segment at 0x{vaddr:04X}, size {len(data)} bytes")
                cmd.add_load_block(vaddr, data)
    
    # Add transfer address
    cmd.add_transfer_address(elf.entry_point)
    if verbose:
        print(f"Added transfer address: 0x{elf.entry_point:04X}")
    
    # Write CMD file
    try:
        cmd.write_to_file(cmd_file)
        if verbose:
            print(f"Written CMD file: {cmd_file}")
        return True
    except Exception as e:
        print(f"Error writing CMD file: {e}", file=sys.stderr)
        return False

def main():
    parser = argparse.ArgumentParser(description="Convert Z80 ELF files to TRSDOS /CMD format")
    parser.add_argument("elf_file", help="Input ELF file")
    parser.add_argument("cmd_file", help="Output CMD file")
    parser.add_argument("-n", "--name", help="Module name for CMD header")
    parser.add_argument("-v", "--verbose", action="store_true", help="Verbose output")
    
    args = parser.parse_args()
    
    if not Path(args.elf_file).exists():
        print(f"Error: ELF file '{args.elf_file}' not found", file=sys.stderr)
        return 1
    
    success = convert_elf_to_cmd(args.elf_file, args.cmd_file, args.name, args.verbose)
    return 0 if success else 1

if __name__ == "__main__":
    sys.exit(main())
