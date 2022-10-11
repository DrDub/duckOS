/*
    This file is part of duckOS.
    
    duckOS is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.
    
    duckOS is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.
    
    You should have received a copy of the GNU General Public License
    along with duckOS.  If not, see <https://www.gnu.org/licenses/>.
    
    Copyright (c) Byteduck 2016-2020. All rights reserved.
*/

#include <cstdio>
#include <cerrno>
#include "ld.h"
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <cstdlib>
#include <map>
#include <sys/mem.h>
#include <libduck/Log.h>

using Duck::Log;

std::unordered_map<std::string, uintptr_t> global_symbols;
std::unordered_map<std::string, uintptr_t> symbols;
std::map<std::string, Object*> objects;
size_t current_brk = 0;
bool debug = false;
Object* executable;

extern "C" [[noreturn]] void call_main(int argc, char** argv, char** envp, main_t main);

int main(int argc, char** argv, char** envp) {
	if(argc < 2) {
		fprintf(stderr, "No binary specified. Usage: ld-duckos.so BINARY\n");
		return -1;
	}

	executable = new Object();
	objects[std::string(argv[1])] = executable;

	//Open the executable
	executable->fd  = open(argv[1], O_RDONLY);
	if(executable->fd < 0) {
		perror("ld-duckos.so");
		return errno;
	}

	//Load the executable and its dependencies
	if(executable->load(argv[1], true) < 0)
		return errno;

	//Read the symbols from the libraries and executable
	auto rev_it = objects.rbegin();
	while(rev_it != objects.rend()) {
		auto* object = rev_it->second;
		object->read_symbols();
		rev_it++;
	}

	//Relocate the libraries and executable and close their file descriptors
	rev_it = objects.rbegin();
	while(rev_it != objects.rend()) {
		auto* object = rev_it->second;
		object->relocate();
		close(object->fd);
		rev_it++;
	}

	//Call __init_stdio for libc.so before any other initializer
	if(symbols["__init_stdio"] != 0) {
		((void(*)()) symbols["__init_stdio"])();
	}

	//Call the initializer methods for the libraries and executable
	rev_it = objects.rbegin();
	while(rev_it != objects.rend()) {
		auto* object = rev_it->second;

		if(object->init_func) {
			if(debug)
				Log::dbgf("Calling init @ {#x} for {}", (size_t) object->init_func - object->memloc, object->name);
			object->init_func();
		}

		if(object->init_array) {
			for(size_t i = 0; i < object->init_array_size; i++) {
				if(debug)
					Log::dbgf("Calling initializer @ {#x} for {}", (size_t) object->init_array[i] - object->memloc, object->name);
				object->init_array[i]();
			}
		}

		rev_it++;
	}

	//Finally, jump to the executable's entry point!
	if(debug)
		Log::dbg("Calling entry point {#x}", executable->header.e_entry);
	auto main = reinterpret_cast<main_t>(executable->header.e_entry);
	call_main(argc - 2, argv + 2, envp, main);

	return 0;
}

int Object::load(char* name_cstr, bool is_main_executable) {
	if(loaded)
		return 0;

	name = name_cstr;

	//Read the header
	if(read_header() < 0) {
		Log::err("Failed to read header of ", name_cstr, ": ", strerror(errno));
		return -1;
	}

	//Read the program headers
	if(read_pheaders() < 0) {
		Log::err("Failed to read segment headers of ", name_cstr, ": ", strerror(errno));
		return -1;
	}

	//Calculate the object size
	if(calculate_memsz() < 0) {
		Log::err("Failed to calculate size of ", name_cstr);
		errno = ENOEXEC;
		return -1;
	}

	//Allocate memory to hold the object
	if(is_main_executable) {
		memloc = 0;
		size_t alloc_start = (calculated_base / PAGE_SIZE) * PAGE_SIZE;
		size_t alloc_size = ((memsz + (calculated_base - alloc_start) + PAGE_SIZE - 1) / PAGE_SIZE) * PAGE_SIZE;
		if (memacquire((void*) alloc_start, alloc_size) < (void*) nullptr) {
			Log::err("Failed to allocate memory for ", name_cstr, ": ", strerror(errno));
			return -1;
		}
		current_brk = alloc_start + alloc_size;
	} else {
		memloc = current_brk;
		size_t alloc_size = ((memsz + PAGE_SIZE - 1) / PAGE_SIZE) * PAGE_SIZE;
		if (memacquire((void*) current_brk, alloc_size) < (void*) nullptr) {
			Log::err("Failed to allocate memory for ", name_cstr, ": ", strerror(errno));
			return -1;
		}
		current_brk += alloc_size;
	}

	//Load the object
	if(load_sections() < 0) {
		Log::err("Failed to load ", name_cstr, " into memory: ", strerror(errno));
		return -1;
	}

	//Read the dynamic table and figure out the required libraries
	if(read_dynamic_table() < 0) {
		Log::err("Failed to read dynamic table of ", name_cstr, ": ", strerror(errno));
		return -1;
	}

	//Read the shared header table
	if(read_sheaders() < 0) {
		Log::err("Failed to read sheaders of ", name_cstr, ": ", strerror(errno));
		return -1;
	}

	//Read the copy relocations of the main executable
	if(is_main_executable && read_copy_relocations() < 0) {
		Log::err("Failed to read copy relocations of ", name_cstr, ": ", strerror(errno));
		return -1;
	}

	//Load the required libraries
	for(auto& library_name : required_libraries) {
		//Open the library
		auto* library = Object::open_library(library_name);
		if(library == nullptr) {
			Log::err("Failed to open required library ", library_name, ": ", strerror(errno));
			return -1;
		}

		//Load the library
		if(library->load(library_name, false) < 0) {
			Log::err("Failed to load required library ", library_name, ": ", strerror(errno));
		}
	}

	loaded = true;

	return 0;
}

Object* Object::open_library(char* library_name) {
	//If it's already loaded, just return the loaded one
	if(objects.find(library_name) != objects.end()) {
		return objects[library_name];
	}

	//Find the library
	auto library_loc = find_library(library_name);
	if(library_loc.empty())
		return nullptr;

	//Open the library
	int fd = open(library_loc.c_str(), O_RDONLY);
	if(fd < 0)
		return nullptr;

	//Add it to the objects map
	auto* object = new Object();
	objects[library_name] = object;
	object->fd = fd;
	object->name = library_name;

	return object;
}

int Object::read_header() {
	lseek(fd, 0, SEEK_SET);
	if(read(fd, &header, sizeof(elf32_ehdr)) < 0) {
		return -1;
	}

	if(*((uint32_t*)header.e_ident) != ELF_MAGIC) {
		errno = ENOEXEC;
		return -1;
	}

	return 0;
}

int Object::calculate_memsz() {
	size_t base = -1;
	size_t brk = 0;
	for(auto & pheader : pheaders) {
		switch(pheader.p_type) {
			case PT_LOAD:
				if(pheader.p_vaddr < base)
					base = pheader.p_vaddr;
				if(pheader.p_vaddr + pheader.p_memsz > brk)
					brk = pheader.p_vaddr + pheader.p_memsz;
				break;
			default:
				break;
		}
	}

	if(base == -1) return -ENOEXEC;
	memsz = brk - base;
	calculated_base = base;
	return 0;
}

int Object::read_pheaders() {
	uint32_t pheader_loc = header.e_phoff;
	uint32_t pheader_size = header.e_phentsize;
	uint32_t num_pheaders = header.e_phnum;

	//Seek to the pheader_loc
	if(lseek(fd, pheader_loc, SEEK_SET) < -1)
		return -1;

	//Create the segment header vector and read the headers into it
	pheaders.resize(num_pheaders);
	if(read(fd, (uint8_t*) pheaders.data(), pheader_size * num_pheaders) < 0)
		return -1;

	return 0;
}

int Object::read_dynamic_table() {
	bool did_read = false;
	for(auto & pheader : pheaders) {
		if(pheader.p_type == PT_DYNAMIC) {
			//Read the dynamic table
			did_read = true;
			std::vector<elf32_dynamic> dynamic_table(pheader.p_filesz / sizeof(elf32_dynamic));
			if(lseek(fd, pheader.p_offset, SEEK_SET) < 0)
				return -1;
			if(read(fd, dynamic_table.data(), pheader.p_filesz) < 0)
				return -1;

			//Iterate over dynamic table entries
			for(auto & dynamic : dynamic_table) {
				if(dynamic.d_tag == DT_NULL) break;
				switch(dynamic.d_tag) {
					case DT_HASH:
						hash = (uint32_t*) (memloc + dynamic.d_val);
						//Size of symbol table should be the same as the number of entries in the symbol hash table
						symbol_table_size = hash[1];
						break;

					case DT_STRTAB:
						string_table = (char*) (memloc + dynamic.d_val);
						break;

					case DT_SYMTAB:
						symbol_table = (elf32_sym*) (memloc + dynamic.d_val);
						break;

					case DT_STRSZ:
						string_table_size = dynamic.d_val;
						break;

					case DT_INIT:
						init_func = (void(*)()) (memloc + dynamic.d_val);
						break;

					case DT_INIT_ARRAY:
						init_array = (void(**)()) (memloc + dynamic.d_val);
						break;

					case DT_INIT_ARRAYSZ:
						init_array_size = dynamic.d_val / sizeof(uintptr_t);
						break;
				}
			}

			//Now that the string table is loaded, we can iterate again and find the required libraries
			required_libraries.resize(0);
			for(auto & dynamic : dynamic_table) {
				if(dynamic.d_tag == DT_NEEDED) {
					required_libraries.push_back(string_table + dynamic.d_val);
				}
			}
		}
	}

	if(!did_read)
		return -ENOENT;

	return 0;
}

int Object::load_sections() {
	for(auto & pheader : pheaders) {
			switch(pheader.p_type) {
			case PT_LOAD: {
				//Load the section into memory
				if(lseek(fd, pheader.p_offset, SEEK_SET) < 0)
					return -1;
				if(read(fd, (void*) (memloc + pheader.p_vaddr), pheader.p_filesz) < 0)
					return -1;

				//Zero out the remaining bytes
				size_t bytes_left = pheader.p_memsz - pheader.p_filesz;
				if(bytes_left)
					memset((void*) (memloc + pheader.p_vaddr + pheader.p_filesz), 0, bytes_left);
			}
		}
	}

	return 0;
}

int Object::read_sheaders() {
	//Read shared headers into a vector
	sheaders.resize(header.e_shnum);
	if(lseek(fd, header.e_shoff, SEEK_SET) < 0)
		return -1;
	if(read(fd, sheaders.data(), sizeof(elf32_sheader) * header.e_shnum) < 0)
		return -1;
	return 0;
}

int Object::read_copy_relocations() {
	//In the relocation table, find all of the copy relocations (ELF32_R_TYPE == STT_COMMON) and put them in the global symbols
	for(auto & shdr : sheaders) {
		if(shdr.sh_type == SHT_REL) {
			auto* rel_table = (elf32_rel*) (shdr.sh_addr + memloc);
			for(size_t i = 0; i < shdr.sh_size / sizeof(elf32_rel); i++) {
				auto& rel = rel_table[i];
				if(ELF32_R_TYPE(rel.r_info) == R_386_COPY) {
					auto& symbol = symbol_table[ELF32_R_SYM(rel.r_info)];
					auto* symbol_name = (char*)((uintptr_t)string_table + symbol.st_name);
					global_symbols[symbol_name] = rel.r_offset;
				}
			}
		}
	}
	return 0;
}

int Object::read_symbols() {
	//Put all the symbols into the symbols map if they aren't there already
	for(size_t i = 0; i < symbol_table_size; i++) {
		auto* symbol = &symbol_table[i];
		char* symbol_name = (char*)((uintptr_t) string_table + symbol->st_name);
		if(symbol->st_shndx && symbols.find(symbol_name) == symbols.end()) {
			symbols[symbol_name] = symbol->st_value + memloc;
		}
	}
	return 0;
}

int Object::relocate() {
	//Relocate the symbols
	for(auto & shdr : sheaders) {
		if(shdr.sh_type == SHT_REL) {
			auto* rel_table = (elf32_rel*) (shdr.sh_addr + memloc);
			for(size_t i = 0; i < shdr.sh_size / sizeof(elf32_rel); i++) {
				auto& rel = rel_table[i];
				uint8_t rel_type = ELF32_R_TYPE(rel.r_info);
				uint32_t rel_symbol = ELF32_R_SYM(rel.r_info);

				if(rel_type == R_386_NONE)
					continue;

				auto& symbol = symbol_table[rel_symbol];
				uintptr_t symbol_loc = memloc + symbol.st_value;
				char* symbol_name = (char *)((uintptr_t) string_table + symbol.st_name);

				//If this kind of relocation is a symbol, look it up
				if(rel_type == R_386_32 || rel_type == R_386_PC32 || rel_type == R_386_COPY || rel_type == R_386_GLOB_DAT || rel_type == R_386_JMP_SLOT) {
					if(symbol_name) {
						auto map_it = symbols.find(symbol_name);
						if(map_it == symbols.end()) {
							if(debug)
								Log::warn("Symbol ", symbol_name, " not found for ", name);
							symbol_loc = 0x0;
						} else {
							symbol_loc = map_it->second;
						}
					}
				}

				//If this is a global symbol, try finding it in the global symbol table
				if(rel_type == R_386_GLOB_DAT) {
					if(symbol_name) {
						auto map_it = global_symbols.find(symbol_name);
						if(map_it != symbols.end()) {
							symbol_loc = map_it->second;
						}
					}
				}

				//Perform the actual relocation
				auto* reloc_loc = (void*) (memloc + rel.r_offset);
				switch(rel_type) {
					case R_386_32:
						symbol_loc += *((ssize_t*) reloc_loc);
						*((uintptr_t*)reloc_loc) = (uintptr_t) symbol_loc;
						break;

					case R_386_PC32:
						symbol_loc += *((ssize_t*) reloc_loc);
						symbol_loc -= memloc + rel.r_offset;
						*((uintptr_t*)reloc_loc) = (uintptr_t) symbol_loc;
						break;

					case R_386_COPY:
						memcpy(reloc_loc, (const void*) symbol_loc, symbol.st_size);
						break;

					case R_386_GLOB_DAT:
					case R_386_JMP_SLOT:
						*((uintptr_t*) reloc_loc) = (uintptr_t) symbol_loc;
						break;

					case R_386_RELATIVE:
						symbol_loc = memloc + *((ssize_t*) reloc_loc);
						*((uintptr_t*) reloc_loc) = (uintptr_t) symbol_loc;
						break;

					default:
						if(debug)
							Log::warn("Unknown relocation type ", (int) rel_type, " for ",  (int) rel_symbol);
						break;
				}
			}
		}
	}

	return 0;
}

std::string find_library(char* library_name) {
	if(strchr(library_name, '/')) return library_name;

	char* ld_library_path = getenv("LD_LIBRARY_PATH");
	std::string default_ld = "/lib:/usr/lib";
	if(!ld_library_path)
		ld_library_path = const_cast<char*>(default_ld.c_str());

	char* cpath = strtok(ld_library_path, ":");
	struct stat stat_buf {};
	while(cpath != nullptr) {
		std::string file = std::string(cpath) + "/" + std::string(library_name);
		if(stat(file.c_str(), &stat_buf) < 0) {
			cpath = strtok(nullptr, ":");
			continue;
		}
		return file;
	}

	return "";
}