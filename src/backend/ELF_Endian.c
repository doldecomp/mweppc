#include "compiler/ELF_Endian.h"
#include "compiler/CompilerTools.h"

static void (*error_handler)(char*, int);

static int swap_tagged_record_data(UInt8* data, short attr, char readBeforeSwap);
static double read_double(UInt8* bytes, char readBeforeSwap);

void Convert_DWARF_Info(
    UInt8* data, UInt32 size, void (*errorHandler)(char*, int), char readBeforeSwap
) {
    int offset = 0;
    volatile struct {
        int length;
        int end;
    } entry;
    int length;
    short tag;
    unsigned short attr;
    UInt32* p32;
    UInt16* p16;
    unsigned char c;

    error_handler = errorHandler;
    while ((UInt32)offset < size) {
        entry.end = offset;
        if (readBeforeSwap) {
            p32 = (UInt32*)(data + offset);
            entry.length = *p32;
            CTool_EndianConvertInPlaceWord32Ptr(p32);
        } else {
            p32 = (UInt32*)(data + offset);
            CTool_EndianConvertInPlaceWord32Ptr(p32);
            entry.length = *p32;
        }
        if (entry.length < 8) {
            if (entry.length <= 0) {
                error_handler(__FILE__, 396);
            }
            offset += entry.length;
        } else {
            if (readBeforeSwap) {
                tag = *(UInt16*)(data + (offset + 4));
                CTool_EndianConvertInPlaceWord16Ptr((UInt16*)(data + (offset + 4)));
            } else {
                p16 = (UInt16*)(data + (offset + 4));
                CTool_EndianConvertInPlaceWord16Ptr(p16);
                tag = *p16;
            }
            offset += 6;
            if (tag == 0) {
                entry.length = entry.length - 6;
                offset += entry.length;
            } else {
                goto attributes;
            }
        }
        continue;
next_attribute:
        while (offset < entry.end) {
            if (readBeforeSwap) {
                p16 = (UInt16*)(data + offset);
                attr = *p16;
                CTool_EndianConvertInPlaceWord16Ptr(p16);
            } else {
                p16 = (UInt16*)(data + offset);
                CTool_EndianConvertInPlaceWord16Ptr(p16);
                attr = *p16;
            }
            offset += 2;
            switch (attr & 0xF) {
                case 1:
                    if (readBeforeSwap) {
                        p32 = (UInt32*)(data + offset);
                        CTool_EndianConvertInPlaceWord32Ptr(p32);
                    } else {
                        p32 = (UInt32*)(data + offset);
                        CTool_EndianConvertInPlaceWord32Ptr(p32);
                    }
                    offset += 4;
                    break;
                case 2:
                    if (readBeforeSwap) {
                        p32 = (UInt32*)(data + offset);
                        CTool_EndianConvertInPlaceWord32Ptr(p32);
                    } else {
                        p32 = (UInt32*)(data + offset);
                        CTool_EndianConvertInPlaceWord32Ptr(p32);
                    }
                    offset += 4;
                    break;
                case 3:
                    offset += swap_tagged_record_data(data + offset, attr, readBeforeSwap);
                    break;
                case 4:
                    if (readBeforeSwap) {
                        p32 = (UInt32*)(data + offset);
                        length = *p32;
                        CTool_EndianConvertInPlaceWord32Ptr(p32);
                    } else {
                        p32 = (UInt32*)(data + offset);
                        CTool_EndianConvertInPlaceWord32Ptr(p32);
                        length = *p32;
                    }
                    offset += 4;
                    if (attr == 0xF4) {
                        while (length != 0) {
                            if (readBeforeSwap) {
                                p32 = (UInt32*)(data + offset);
                                CTool_EndianConvertInPlaceWord32Ptr(p32);
                            } else {
                                p32 = (UInt32*)(data + offset);
                                CTool_EndianConvertInPlaceWord32Ptr(p32);
                            }
                            offset += 4;
                            length -= 4;
                            while (data[offset] != 0) {
                                offset++;
                                length--;
                            }
                            offset++;
                            length--;
                        }
                    }
                    break;
                case 5:
                    if (readBeforeSwap) {
                        p16 = (UInt16*)(data + offset);
                        CTool_EndianConvertInPlaceWord16Ptr(p16);
                    } else {
                        p16 = (UInt16*)(data + offset);
                        CTool_EndianConvertInPlaceWord16Ptr(p16);
                    }
                    offset += 2;
                    break;
                case 6:
                    if (readBeforeSwap) {
                        p32 = (UInt32*)(data + offset);
                        CTool_EndianConvertInPlaceWord32Ptr(p32);
                    } else {
                        p32 = (UInt32*)(data + offset);
                        CTool_EndianConvertInPlaceWord32Ptr(p32);
                    }
                    offset += 4;
                    break;
                case 7:
                    read_double(data + offset, readBeforeSwap);
                    offset += 8;
                    break;
                case 8:
                    while ((c = data[offset]) != 0) {
                        offset++;
                    }
                    offset++;
                    break;
            }
        }
    }
    return;
attributes:
    entry.end = entry.end + entry.length;
    goto next_attribute;
}

static int swap_tagged_record_data(UInt8* data, short attr, char readBeforeSwap) {
    int offset = 0;
    UInt8* p;
    short remaining;
    unsigned char op;
    short length;
    int size;
    UInt32* p32;
    UInt16* p16;
    unsigned char inSubscript = FALSE;

    if (readBeforeSwap) {
        length = *(short*)data;
        CTool_EndianConvertInPlaceWord16Ptr((UInt16*)data);
    } else {
        CTool_EndianConvertInPlaceWord16Ptr((UInt16*)data);
        length = *(short*)data;
    }
    remaining = length;
    offset += 2;
    if (length > 0) {
        do {
            op = *(p = data + offset);
            offset++;
            remaining--;
            if (attr == 0x23 || attr == 0x2a3 || attr == 0x2013 || attr == 0x293 || attr == 0x303) {
                switch (op) {
                    case 1:
                    case 2:
                    case 4:
                    case 0x80:
                    case 0x81:
                        if (readBeforeSwap) {
                            p32 = (UInt32*)(data + offset);
                            CTool_EndianConvertInPlaceWord32Ptr(p32);
                        } else {
                            p32 = (UInt32*)(data + offset);
                            CTool_EndianConvertInPlaceWord32Ptr(p32);
                        }
                        offset += 4;
                        remaining -= 4;
                        break;
                    case 3:
                        if (readBeforeSwap) {
                            p32 = (UInt32*)(data + offset);
                            CTool_EndianConvertInPlaceWord32Ptr(p32);
                        } else {
                            p32 = (UInt32*)(data + offset);
                            CTool_EndianConvertInPlaceWord32Ptr(p32);
                        }
                        offset += 4;
                        remaining -= 4;
                        break;
                    case 5:
                    case 6:
                    case 7:
                        break;
                    case 0xe0:
                        if (readBeforeSwap) {
                            p32 = (UInt32*)(data + offset);
                            CTool_EndianConvertInPlaceWord32Ptr(p32);
                        } else {
                            p32 = (UInt32*)(data + offset);
                            CTool_EndianConvertInPlaceWord32Ptr(p32);
                        }
                        offset += 4;
                        remaining -= 4;
                        break;
                }
            } else if (attr == 0x83) {
                if (remaining < 4) {
                    if (readBeforeSwap) {
                        p32 = (UInt32*)(data + offset - 1);
                        CTool_EndianConvertInPlaceWord32Ptr(p32);
                    } else {
                        p32 = (UInt32*)(data + offset - 1);
                        CTool_EndianConvertInPlaceWord32Ptr(p32);
                    }
                    offset += 3;
                    remaining -= 3;
                }
            } else if (attr == 0x63) {
                if (remaining < 2) {
                    if (readBeforeSwap) {
                        p16 = (UInt16*)(data + offset - 1);
                        CTool_EndianConvertInPlaceWord16Ptr(p16);
                    } else {
                        p16 = (UInt16*)(data + offset - 1);
                        CTool_EndianConvertInPlaceWord16Ptr(p16);
                    }
                    offset += 1;
                    remaining -= 1;
                }
            } else if (attr == 0xa3) {
                if (!inSubscript) {
                    inSubscript = TRUE;
                }
                switch (op) {
                    case 8:
                        if (readBeforeSwap) {
                            p16 = (UInt16*)(data + offset);
                            attr = *p16;
                            CTool_EndianConvertInPlaceWord16Ptr(p16);
                        } else {
                            p16 = (UInt16*)(data + offset);
                            CTool_EndianConvertInPlaceWord16Ptr(p16);
                            attr = *p16;
                        }
                        offset += 2;
                        remaining -= 2;
                        if (attr == 0x55) {
                            if (readBeforeSwap) {
                                p16 = (UInt16*)(data + offset);
                                CTool_EndianConvertInPlaceWord16Ptr(p16);
                            } else {
                                p16 = (UInt16*)(data + offset);
                                CTool_EndianConvertInPlaceWord16Ptr(p16);
                            }
                            offset += 2;
                            remaining -= 2;
                        } else if (attr == 0x63 || attr == 0x83) {
                            size = swap_tagged_record_data(data + offset, attr, readBeforeSwap);
                            offset += size;
                            remaining -= size;
                        } else if (attr == 0x72) {
                            if (readBeforeSwap) {
                                p32 = (UInt32*)(data + offset);
                                CTool_EndianConvertInPlaceWord32Ptr(p32);
                            } else {
                                p32 = (UInt32*)(data + offset);
                                CTool_EndianConvertInPlaceWord32Ptr(p32);
                            }
                            offset += 4;
                            remaining -= 4;
                        }
                        inSubscript = FALSE;
                        break;
                    case 0:
                        if (readBeforeSwap) {
                            p16 = (UInt16*)(data + offset);
                            CTool_EndianConvertInPlaceWord16Ptr(p16);
                        } else {
                            p16 = (UInt16*)(data + offset);
                            CTool_EndianConvertInPlaceWord16Ptr(p16);
                        }
                        if (readBeforeSwap) {
                            p32 = (UInt32*)((offset + 2) + data);
                            CTool_EndianConvertInPlaceWord32Ptr(p32);
                        } else {
                            CTool_EndianConvertInPlaceWord32Ptr(
                                p32 = (UInt32*)((offset + 2) + data)
                            );
                        }
                        if (readBeforeSwap) {
                            p32 = (UInt32*)((offset + 6) + data);
                            CTool_EndianConvertInPlaceWord32Ptr(p32);
                        } else {
                            CTool_EndianConvertInPlaceWord32Ptr(
                                p32 = (UInt32*)((offset + 6) + data)
                            );
                        }
                        offset += 10;
                        remaining -= 10;
                        break;
                    case 1:
                        if (readBeforeSwap) {
                            p16 = (UInt16*)(data + offset);
                            CTool_EndianConvertInPlaceWord16Ptr(p16);
                        } else {
                            p16 = (UInt16*)(data + offset);
                            CTool_EndianConvertInPlaceWord16Ptr(p16);
                        }
                        if (readBeforeSwap) {
                            p32 = (UInt32*)((offset + 2) + data);
                            CTool_EndianConvertInPlaceWord32Ptr(p32);
                        } else {
                            CTool_EndianConvertInPlaceWord32Ptr(
                                p32 = (UInt32*)((offset + 2) + data)
                            );
                        }
                        offset += 6;
                        remaining -= 6;
                        size = swap_tagged_record_data(data + offset, 0x23, readBeforeSwap);
                        offset += size;
                        remaining -= size;
                        break;
                    case 2:
                        if (readBeforeSwap) {
                            p16 = (UInt16*)(data + offset);
                            CTool_EndianConvertInPlaceWord16Ptr(p16);
                        } else {
                            p16 = (UInt16*)(data + offset);
                            CTool_EndianConvertInPlaceWord16Ptr(p16);
                        }
                        offset += 2;
                        remaining -= 2;
                        size = swap_tagged_record_data(data + offset, 0x23, readBeforeSwap);
                        offset += size;
                        remaining -= size;
                        if (readBeforeSwap) {
                            p32 = (UInt32*)(data + offset);
                            CTool_EndianConvertInPlaceWord32Ptr(p32);
                        } else {
                            p32 = (UInt32*)(data + offset);
                            CTool_EndianConvertInPlaceWord32Ptr(p32);
                        }
                        offset += 4;
                        remaining -= 4;
                        break;
                    case 3:
                        if (readBeforeSwap) {
                            p16 = (UInt16*)(data + offset);
                            CTool_EndianConvertInPlaceWord16Ptr(p16);
                        } else {
                            p16 = (UInt16*)(data + offset);
                            CTool_EndianConvertInPlaceWord16Ptr(p16);
                        }
                        offset += 2;
                        remaining -= 2;
                        size = swap_tagged_record_data(data + offset, 0x23, readBeforeSwap);
                        offset += size;
                        remaining -= size;
                        size = swap_tagged_record_data(data + offset, 0x23, readBeforeSwap);
                        offset += size;
                        remaining -= size;
                        break;
                    case 4:
                    case 5:
                    case 6:
                    case 7:
                        inSubscript = FALSE;
                        attr = 0;
                        break;
                }
            }
        } while (remaining > 0);
    }
    return offset;
}

static double read_double(UInt8* bytes, char readBeforeSwap) {
    union {
        double value;
        UInt8 bytes[8];
    } u;

    if (readBeforeSwap) {
        u.bytes[0] = bytes[0];
        u.bytes[1] = bytes[1];
        u.bytes[2] = bytes[2];
        u.bytes[3] = bytes[3];
        u.bytes[4] = bytes[4];
        u.bytes[5] = bytes[5];
        u.bytes[6] = bytes[6];
        u.bytes[7] = bytes[7];
        CTool_EndianConvertMem(&bytes, 8);
    } else {
        CTool_EndianConvertMem(&bytes, 8);
        u.bytes[0] = bytes[0];
        u.bytes[1] = bytes[1];
        u.bytes[2] = bytes[2];
        u.bytes[3] = bytes[3];
        u.bytes[4] = bytes[4];
        u.bytes[5] = bytes[5];
        u.bytes[6] = bytes[6];
        u.bytes[7] = bytes[7];
    }
    return u.value;
}

void Convert_DWARF_line_info(
    UInt8* data, int size, void (*errorHandler)(char*, int), char readBeforeSwap
) {
    unsigned int length;
    unsigned int offset;
    UInt32* p32;

    error_handler = errorHandler;
    offset = 0;
    while (size != 0) {
        if (readBeforeSwap) {
            length = *(UInt32*)(data + offset);
            CTool_EndianConvertInPlaceWord32Ptr((UInt32*)(data + offset));
        } else {
            length = CTool_EndianConvertInPlaceWord32Ptr((UInt32*)(data + offset));
        }
        CTool_EndianConvertInPlaceWord32Ptr((UInt32*)(data + (offset + 4)));
        offset += 8;
        size -= 8;
        do {
            if (readBeforeSwap) {
                p32 = (UInt32*)(data + offset);
                CTool_EndianConvertInPlaceWord32Ptr(p32);
            } else {
                p32 = (UInt32*)(data + offset);
                CTool_EndianConvertInPlaceWord32Ptr(p32);
            }
            CTool_EndianConvertInPlaceWord16Ptr((UInt16*)(data + (offset + 4)));
            CTool_EndianConvertInPlaceWord32Ptr((UInt32*)(data + (offset + 6)));
            offset += 10;
            size -= 10;
        } while (offset < length);
        data += offset;
        offset = 0;
    }
}

void Convert_Elf32_Rela(Elf32_Rela* rela) {
    rela->r_offset = CTool_EndianConvertWord32(rela->r_offset);
    rela->r_info = CTool_EndianConvertWord32(rela->r_info);
    rela->r_addend = CTool_EndianConvertWord32(rela->r_addend);
}

void Convert_Elf32_Sym(Elf32_Sym* sym) {
    sym->st_name = CTool_EndianConvertWord32(sym->st_name);
    sym->st_value = CTool_EndianConvertWord32(sym->st_value);
    sym->st_size = CTool_EndianConvertWord32(sym->st_size);
    sym->st_shndx = CTool_EndianConvertWord16(sym->st_shndx);
}

void Convert_Elf32_Shdr(Elf32_Shdr* shdr) {
    shdr->sh_name = CTool_EndianConvertWord32(shdr->sh_name);
    shdr->sh_type = CTool_EndianConvertWord32(shdr->sh_type);
    shdr->sh_flags = CTool_EndianConvertWord32(shdr->sh_flags);
    shdr->sh_addr = CTool_EndianConvertWord32(shdr->sh_addr);
    shdr->sh_offset = CTool_EndianConvertWord32(shdr->sh_offset);
    shdr->sh_size = CTool_EndianConvertWord32(shdr->sh_size);
    shdr->sh_link = CTool_EndianConvertWord32(shdr->sh_link);
    shdr->sh_info = CTool_EndianConvertWord32(shdr->sh_info);
    shdr->sh_addralign = CTool_EndianConvertWord32(shdr->sh_addralign);
    shdr->sh_entsize = CTool_EndianConvertWord32(shdr->sh_entsize);
}

void Convert_Elf32_Ehdr(Elf32_Ehdr* ehdr) {
    ehdr->e_type = CTool_EndianConvertWord16(ehdr->e_type);
    ehdr->e_machine = CTool_EndianConvertWord16(ehdr->e_machine);
    ehdr->e_version = CTool_EndianConvertWord32(ehdr->e_version);
    ehdr->e_entry = CTool_EndianConvertWord32(ehdr->e_entry);
    ehdr->e_phoff = CTool_EndianConvertWord32(ehdr->e_phoff);
    ehdr->e_shoff = CTool_EndianConvertWord32(ehdr->e_shoff);
    ehdr->e_flags = CTool_EndianConvertWord32(ehdr->e_flags);
    ehdr->e_ehsize = CTool_EndianConvertWord16(ehdr->e_ehsize);
    ehdr->e_phentsize = CTool_EndianConvertWord16(ehdr->e_phentsize);
    ehdr->e_phnum = CTool_EndianConvertWord16(ehdr->e_phnum);
    ehdr->e_shentsize = CTool_EndianConvertWord16(ehdr->e_shentsize);
    ehdr->e_shnum = CTool_EndianConvertWord16(ehdr->e_shnum);
    ehdr->e_shstrndx = CTool_EndianConvertWord16(ehdr->e_shstrndx);
}
