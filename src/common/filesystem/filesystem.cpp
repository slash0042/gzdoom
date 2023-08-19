/*
** filesystem.cpp
**
**---------------------------------------------------------------------------
** Copyright 1998-2009 Randy Heit
** Copyright 2005-2020 Christoph Oelckers
** All rights reserved.
**
** Redistribution and use in source and binary forms, with or without
** modification, are permitted provided that the following conditions
** are met:
**
** 1. Redistributions of source code must retain the above copyright
**    notice, this list of conditions and the following disclaimer.
** 2. Redistributions in binary form must reproduce the above copyright
**    notice, this list of conditions and the following disclaimer in the
**    documentation and/or other materials provided with the distribution.
** 3. The name of the author may not be used to endorse or promote products
**    derived from this software without specific prior written permission.
**
** THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
** IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
** OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
** IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
** INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
** NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
** DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
** THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
** (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
** THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
**---------------------------------------------------------------------------
**
**
*/


// HEADER FILES ------------------------------------------------------------

#include <zlib.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>

#include "filesystem.h"
#include "fs_findfile.h"
#include "md5.hpp"

// MACROS ------------------------------------------------------------------

#define NULL_INDEX		(0xffffffff)

static void UpperCopy(char* to, const char* from)
{
	int i;

	for (i = 0; i < 8 && from[i]; i++)
		to[i] = toupper(from[i]);
	for (; i < 8; i++)
		to[i] = 0;
}


//djb2
static uint32_t MakeHash(const char* str, size_t length = SIZE_MAX)
{
	uint32_t hash = 5381;
	uint32_t c;
	while (length-- > 0 && (c = *str++)) hash = hash * 33 + (c | 32);
	return hash;
}

static void md5Hash(FileReader& reader, uint8_t* digest) 
{
	using namespace fs_private::md5;

	md5_state_t state;
	md5_init(&state);
	md5_byte_t buffer[4096];
	while (auto len = reader.Read(buffer, 4096))
	{
		md5_append(&state, buffer, len);
	}
	md5_finish(&state, digest);
}


struct FileSystem::LumpRecord
{
	FResourceLump *lump;
	LumpShortName shortName;
	std::string	longName;
	int			rfnum;
	int			Namespace;
	int			resourceId;
	int			flags;

	void SetFromLump(int filenum, FResourceLump* lmp)
	{
		lump = lmp;
		rfnum = filenum;
		flags = 0;

		if (lump->Flags & LUMPF_SHORTNAME)
		{
			UpperCopy(shortName.String, lump->getName());
			shortName.String[8] = 0;
			longName = "";
			Namespace = lump->GetNamespace();
			resourceId = -1;
		}
		else if ((lump->Flags & LUMPF_EMBEDDED) || !lump->getName() || !*lump->getName())
		{
			shortName.qword = 0;
			longName = "";
			Namespace = ns_hidden;
			resourceId = -1;
		}
		else
		{
			longName = lump->getName();
			resourceId = lump->GetIndexNum();

			// Map some directories to WAD namespaces.
			// Note that some of these namespaces don't exist in WADS.
			// CheckNumForName will handle any request for these namespaces accordingly.
			Namespace = !strncmp(longName.c_str(), "flats/", 6) ? ns_flats :
				!strncmp(longName.c_str(), "textures/", 9) ? ns_newtextures :
				!strncmp(longName.c_str(), "hires/", 6) ? ns_hires :
				!strncmp(longName.c_str(), "sprites/", 8) ? ns_sprites :
				!strncmp(longName.c_str(), "voxels/", 7) ? ns_voxels :
				!strncmp(longName.c_str(), "colormaps/", 10) ? ns_colormaps :
				!strncmp(longName.c_str(), "acs/", 4) ? ns_acslibrary :
				!strncmp(longName.c_str(), "voices/", 7) ? ns_strifevoices :
				!strncmp(longName.c_str(), "patches/", 8) ? ns_patches :
				!strncmp(longName.c_str(), "graphics/", 9) ? ns_graphics :
				!strncmp(longName.c_str(), "sounds/", 7) ? ns_sounds :
				!strncmp(longName.c_str(), "music/", 6) ? ns_music :
				!strchr(longName.c_str(), '/') ? ns_global :
				ns_hidden;

			if (Namespace == ns_hidden) shortName.qword = 0;
			else
			{
				ptrdiff_t encodedResID = longName.find_last_of(".{");
				if (resourceId == -1 && encodedResID != std::string::npos)
				{
					const char* p = longName.c_str() + encodedResID;
					char* q;
					int id = (int)strtoull(p+2, &q, 10);	// only decimal numbers allowed here.
					if (q[0] == '}' && (q[1] == '.' || q[1] == 0))
					{
						longName.erase(longName.begin() + encodedResID, longName.begin() + (q - p) + 1);
						resourceId = id;
					}
				}
				ptrdiff_t slash = longName.find_last_of('/');
				std::string base = (slash != std::string::npos) ? longName.c_str() + (slash + 1) : longName.c_str();
				auto dot = base.find_last_of('.');
				if (dot != std::string::npos) base.resize(dot);
				UpperCopy(shortName.String, base.c_str());
				shortName.String[8] = 0;

				// Since '\' can't be used as a file name's part inside a ZIP
				// we have to work around this for sprites because it is a valid
				// frame character.
				if (Namespace == ns_sprites || Namespace == ns_voxels || Namespace == ns_hires)
				{
					char* c;

					while ((c = (char*)memchr(shortName.String, '^', 8)))
					{
						*c = '\\';
					}
				}
			}
		}
	}
};

// PUBLIC FUNCTION PROTOTYPES ----------------------------------------------

// PRIVATE FUNCTION PROTOTYPES ---------------------------------------------

static void PrintLastError (FileSystemMessageFunc Printf);

// PUBLIC DATA DEFINITIONS -------------------------------------------------

FileSystem fileSystem;

// CODE --------------------------------------------------------------------

FileSystem::FileSystem()
{
	// Cannot be defaulted! This is needed to initialize the LumpRecord array, which depends on data only available here.
}

FileSystem::~FileSystem ()
{
	DeleteAll();
}

void FileSystem::DeleteAll ()
{
	Hashes.clear();
	NumEntries = 0;

	// explicitly delete all manually added lumps.
	for (auto &frec : FileInfo)
	{
		if (frec.rfnum == -1) delete frec.lump;
	}
	FileInfo.clear();
	for (int i = (int)Files.size() - 1; i >= 0; --i)
	{
		delete Files[i];
	}
	Files.clear();
}

//==========================================================================
//
// InitMultipleFiles
//
// Pass a null terminated list of files to use. All files are optional,
// but at least one file must be found. File names can appear multiple
// times. The name searcher looks backwards, so a later file can
// override an earlier one.
//
//==========================================================================

bool FileSystem::InitSingleFile(const char* filename, FileSystemMessageFunc Printf)
{
	std::vector<std::string> filenames = { filename };
	return InitMultipleFiles(filenames, nullptr, Printf);
}

bool FileSystem::InitMultipleFiles (std::vector<std::string>& filenames, LumpFilterInfo* filter, FileSystemMessageFunc Printf, bool allowduplicates, FILE* hashfile)
{
	int numfiles;

	// open all the files, load headers, and count lumps
	DeleteAll();
	numfiles = 0;

	// first, check for duplicates
	if (allowduplicates)
	{
		for (size_t i=0;i<filenames.size(); i++)
		{
			for (size_t j=i+1;j<filenames.size(); j++)
			{
				if (filenames[i] == filenames[j])
				{
					filenames.erase(filenames.begin() + j);
					j--;
				}
			}
		}
	}

	for(size_t i=0;i<filenames.size(); i++)
	{
		AddFile(filenames[i].c_str(), nullptr, filter, Printf, hashfile);

		if (i == (unsigned)MaxIwadIndex) MoveLumpsInFolder("after_iwad/");
		std::string path = "filter/%s";
		path += Files.back()->GetHash();
		MoveLumpsInFolder(path.c_str());
	}

	NumEntries = (uint32_t)FileInfo.size();
	if (NumEntries == 0)
	{
		return false;
	}
	if (filter && filter->postprocessFunc) filter->postprocessFunc();

	// [RH] Set up hash table
	InitHashChains ();
	return true;
}

//==========================================================================
//
// AddLump
//
// Adds a given lump to the directory. Does not perform rehashing
//
//==========================================================================

void FileSystem::AddLump(FResourceLump *lump)
{
	FileInfo.resize(FileInfo.size() + 1);
	FileInfo.back().SetFromLump(-1, lump);
}

//-----------------------------------------------------------------------
//
// Adds an external file to the lump list but not to the hash chains
// It's just a simple means to assign a lump number to some file so that
// the texture manager can read from it.
//
//-----------------------------------------------------------------------

int FileSystem::AddExternalFile(const char *filename)
{
	FResourceLump *lump = new FExternalLump(filename);
	AddLump(lump);
	return (int)FileInfo.size() - 1;	// later
}

//==========================================================================
//
// AddFromBuffer
//
// Adds an in-memory resource to the virtual directory
//
//==========================================================================

int FileSystem::AddFromBuffer(const char* name, const char* type, char* data, int size, int id, int flags)
{
	std::string fullname = name;
	fullname += ':';
	fullname += type;
	auto newlump = new FMemoryLump(data, size);
	newlump->LumpNameSetup(fullname.c_str());
	AddLump(newlump);
	FileInfo.back().resourceId = id;
	return (int)FileInfo.size()-1;
}

//==========================================================================
//
// AddFile
//
// Files with a .wad extension are wadlink files with multiple lumps,
// other files are single lumps with the base filename for the lump name.
//
// [RH] Removed reload hack
//==========================================================================

void FileSystem::AddFile (const char *filename, FileReader *filer, LumpFilterInfo* filter, FileSystemMessageFunc Printf, FILE* hashfile)
{
	int startlump;
	bool isdir = false;
	FileReader filereader;

	if (filer == nullptr)
	{
		// Does this exist? If so, is it a directory?
		if (!FS_DirEntryExists(filename, &isdir))
		{
			if (Printf)
			{
				Printf(FSMessageLevel::Error, "%s: File or Directory not found\n", filename);
				PrintLastError(Printf);
			}
			return;
		}

		if (!isdir)
		{
			if (!filereader.OpenFile(filename))
			{ // Didn't find file
				if (Printf)
				{
					Printf(FSMessageLevel::Error, "%s: File not found\n", filename);
					PrintLastError(Printf);
				}
				return;
			}
		}
	}
	else filereader = std::move(*filer);

	startlump = NumEntries;

	FResourceFile *resfile;


	if (!isdir)
		resfile = FResourceFile::OpenResourceFile(filename, filereader, false, filter, Printf);
	else
		resfile = FResourceFile::OpenDirectory(filename, filter, Printf);

	if (resfile != NULL)
	{
		if (Printf) 
			Printf(FSMessageLevel::Message, "adding %s, %d lumps\n", filename, resfile->LumpCount());

		uint32_t lumpstart = (uint32_t)FileInfo.size();

		resfile->SetFirstLump(lumpstart);
		for (uint32_t i=0; i < resfile->LumpCount(); i++)
		{
			FResourceLump *lump = resfile->GetLump(i);
			FileInfo.resize(FileInfo.size() + 1);
			FileSystem::LumpRecord* lump_p = &FileInfo.back();
			lump_p->SetFromLump((int)Files.size(), lump);
		}

		Files.push_back(resfile);

		for (uint32_t i=0; i < resfile->LumpCount(); i++)
		{
			FResourceLump *lump = resfile->GetLump(i);
			if (lump->Flags & LUMPF_EMBEDDED)
			{
				std::string path = filename;
				path += ':';
				path += lump->getName();
				auto embedded = lump->NewReader();
				AddFile(path.c_str(), &embedded, filter, Printf, hashfile);
			}
		}

		if (hashfile)
		{
			uint8_t cksum[16];
			char cksumout[33];
			memset(cksumout, 0, sizeof(cksumout));

			if (filereader.isOpen())
			{
				filereader.Seek(0, FileReader::SeekSet);
				md5Hash(filereader, cksum);

				for (size_t j = 0; j < sizeof(cksum); ++j)
				{
					snprintf(cksumout + (j * 2), 3, "%02X", cksum[j]);
				}

				fprintf(hashfile, "file: %s, hash: %s, size: %d\n", filename, cksumout, (int)filereader.GetLength());
			}

			else
				fprintf(hashfile, "file: %s, Directory structure\n", filename);

			for (uint32_t i = 0; i < resfile->LumpCount(); i++)
			{
				FResourceLump *lump = resfile->GetLump(i);

				if (!(lump->Flags & LUMPF_EMBEDDED))
				{
					auto reader = lump->NewReader();
					md5Hash(filereader, cksum);

					for (size_t j = 0; j < sizeof(cksum); ++j)
					{
						snprintf(cksumout + (j * 2), 3, "%02X", cksum[j]);
					}

					fprintf(hashfile, "file: %s, lump: %s, hash: %s, size: %d\n", filename, lump->getName(), cksumout, lump->LumpSize);
				}
			}
		}
		return;
	}
}

//==========================================================================
//
// CheckIfResourceFileLoaded
//
// Returns true if the specified file is loaded, false otherwise.
// If a fully-qualified path is specified, then the file must match exactly.
// Otherwise, any file with that name will work, whatever its path.
// Returns the file's index if found, or -1 if not.
//
//==========================================================================

int FileSystem::CheckIfResourceFileLoaded (const char *name) noexcept
{
	unsigned int i;

	if (strrchr (name, '/') != NULL)
	{
		for (i = 0; i < (unsigned)Files.size(); ++i)
		{
			if (stricmp (GetResourceFileFullName (i), name) == 0)
			{
				return i;
			}
		}
	}
	else
	{
		for (i = 0; i < (unsigned)Files.size(); ++i)
		{
			auto pth = ExtractBaseName(GetResourceFileName(i), true);
			if (stricmp (pth.c_str(), name) == 0)
			{
				return i;
			}
		}
	}
	return -1;
}

//==========================================================================
//
// CheckNumForName
//
// Returns -1 if name not found. The version with a third parameter will
// look exclusively in the specified wad for the lump.
//
// [RH] Changed to use hash lookup ala BOOM instead of a linear search
// and namespace parameter
//==========================================================================

int FileSystem::CheckNumForName (const char *name, int space)
{
	union
	{
		char uname[8];
		uint64_t qname;
	};
	uint32_t i;

	if (name == NULL)
	{
		return -1;
	}

	// Let's not search for names that are longer than 8 characters and contain path separators
	// They are almost certainly full path names passed to this function.
	if (strlen(name) > 8 && strpbrk(name, "/."))
	{
		return -1;
	}

	UpperCopy (uname, name);
	i = FirstLumpIndex[MakeHash(uname, 8) % NumEntries];

	while (i != NULL_INDEX)
	{

		if (FileInfo[i].shortName.qword == qname)
		{
			auto &lump = FileInfo[i];
			if (lump.Namespace == space) break;
			// If the lump is from one of the special namespaces exclusive to Zips
			// the check has to be done differently:
			// If we find a lump with this name in the global namespace that does not come
			// from a Zip return that. WADs don't know these namespaces and single lumps must
			// work as well.
			if (space > ns_specialzipdirectory && lump.Namespace == ns_global && 
				!((lump.lump->Flags ^lump.flags) & LUMPF_FULLPATH)) break;
		}
		i = NextLumpIndex[i];
	}

	return i != NULL_INDEX ? i : -1;
}

int FileSystem::CheckNumForName (const char *name, int space, int rfnum, bool exact)
{
	union
	{
		char uname[8];
		uint64_t qname;
	};
	uint32_t i;

	if (rfnum < 0)
	{
		return CheckNumForName (name, space);
	}

	UpperCopy (uname, name);
	i = FirstLumpIndex[MakeHash (uname, 8) % NumEntries];

	// If exact is true if will only find lumps in the same WAD, otherwise
	// also those in earlier WADs.

	while (i != NULL_INDEX &&
		(FileInfo[i].shortName.qword != qname || FileInfo[i].Namespace != space ||
		 (exact? (FileInfo[i].rfnum != rfnum) : (FileInfo[i].rfnum > rfnum)) ))
	{
		i = NextLumpIndex[i];
	}

	return i != NULL_INDEX ? i : -1;
}

//==========================================================================
//
// GetNumForName
//
// Calls CheckNumForName, but bombs out if not found.
//
//==========================================================================

int FileSystem::GetNumForName (const char *name, int space)
{
	int	i;

	i = CheckNumForName (name, space);

	if (i == -1)
		throw FileSystemException("GetNumForName: %s not found!", name);

	return i;
}


//==========================================================================
//
// CheckNumForFullName
//
// Same as above but looks for a fully qualified name from a .zip
// These don't care about namespaces though because those are part
// of the path.
//
//==========================================================================

int FileSystem::CheckNumForFullName (const char *name, bool trynormal, int namespc, bool ignoreext)
{
	uint32_t i;

	if (name == NULL)
	{
		return -1;
	}
	if (*name == '/') name++;	// ignore leading slashes in file names.
	uint32_t *fli = ignoreext ? FirstLumpIndex_NoExt : FirstLumpIndex_FullName;
	uint32_t *nli = ignoreext ? NextLumpIndex_NoExt : NextLumpIndex_FullName;
	auto len = strlen(name);

	for (i = fli[MakeHash(name) % NumEntries]; i != NULL_INDEX; i = nli[i])
	{
		if (strnicmp(name, FileInfo[i].longName.c_str(), len)) continue;
		if (FileInfo[i].longName[len] == 0) break;	// this is a full match
		if (ignoreext && FileInfo[i].longName[len] == '.') 
		{
			// is this the last '.' in the last path element, indicating that the remaining part of the name is only an extension?
			if (strpbrk(FileInfo[i].longName.c_str() + len + 1, "./") == nullptr) break;	
		}
	}

	if (i != NULL_INDEX) return i;

	if (trynormal && strlen(name) <= 8 && !strpbrk(name, "./"))
	{
		return CheckNumForName(name, namespc);
	}
	return -1;
}

int FileSystem::CheckNumForFullName (const char *name, int rfnum)
{
	uint32_t i;

	if (rfnum < 0)
	{
		return CheckNumForFullName (name);
	}

	i = FirstLumpIndex_FullName[MakeHash (name) % NumEntries];

	while (i != NULL_INDEX && 
		(stricmp(name, FileInfo[i].longName.c_str()) || FileInfo[i].rfnum != rfnum))
	{
		i = NextLumpIndex_FullName[i];
	}

	return i != NULL_INDEX ? i : -1;
}

//==========================================================================
//
// GetNumForFullName
//
// Calls CheckNumForFullName, but bombs out if not found.
//
//==========================================================================

int FileSystem::GetNumForFullName (const char *name)
{
	int	i;

	i = CheckNumForFullName (name);

	if (i == -1)
		throw FileSystemException("GetNumForFullName: %s not found!", name);

	return i;
}

//==========================================================================
//
// FindFile
//
// Looks up a file by name, either with or without path and extension
//
//==========================================================================

int FileSystem::FindFileWithExtensions(const char* name, const char *const *exts, int count)
{
	uint32_t i;

	if (name == NULL)
	{
		return -1;
	}
	if (*name == '/') name++;	// ignore leading slashes in file names.
	uint32_t* fli = FirstLumpIndex_NoExt;
	uint32_t* nli = NextLumpIndex_NoExt;
	auto len = strlen(name);

	for (i = fli[MakeHash(name) % NumEntries]; i != NULL_INDEX; i = nli[i])
	{
		if (strnicmp(name, FileInfo[i].longName.c_str(), len)) continue;
		if (FileInfo[i].longName[len] != '.') continue;	// we are looking for extensions but this file doesn't have one.

		auto cp = FileInfo[i].longName.c_str() + len + 1;
		// is this the last '.' in the last path element, indicating that the remaining part of the name is only an extension?
		if (strpbrk(cp, "./") != nullptr) continue;	// No, so it cannot be a valid entry.

		for (int j = 0; j < count; j++)
		{
			if (!stricmp(cp, exts[j])) return i;	// found a match
		}
	}
	return -1;
}

//==========================================================================
//
// FindResource
//
// Looks for content based on Blood resource IDs.
//
//==========================================================================

int FileSystem::FindResource (int resid, const char *type, int filenum) const noexcept
{
	uint32_t i;

	if (type == NULL || resid < 0)
	{
		return -1;
	}

	uint32_t* fli = FirstLumpIndex_ResId;
	uint32_t* nli = NextLumpIndex_ResId;

	for (i = fli[resid % NumEntries]; i != NULL_INDEX; i = nli[i])
	{
		if (filenum > 0 && FileInfo[i].rfnum != filenum) continue;
		if (FileInfo[i].resourceId != resid) continue;
		auto extp = strrchr(FileInfo[i].longName.c_str(), '.');
		if (!extp) continue;
		if (!stricmp(extp + 1, type)) return i;
	}
	return -1;
}

//==========================================================================
//
// GetResource
//
// Calls GetResource, but bombs out if not found.
//
//==========================================================================

int FileSystem::GetResource (int resid, const char *type, int filenum) const
{
	int	i;

	i = FindResource (resid, type, filenum);

	if (i == -1)
	{
		throw FileSystemException("GetResource: %d of type %s not found!", resid, type);
	}
	return i;
}

//==========================================================================
//
// FileLength
//
// Returns the buffer size needed to load the given lump.
//
//==========================================================================

int FileSystem::FileLength (int lump) const
{
	if ((size_t)lump >= NumEntries)
	{
		return -1;
	}
	return FileInfo[lump].lump->LumpSize;
}

//==========================================================================
//
// GetFileOffset
//
// Returns the offset from the beginning of the file to the lump.
// Returns -1 if the lump is compressed or can't be read directly
//
//==========================================================================

int FileSystem::GetFileOffset (int lump)
{
	if ((size_t)lump >= NumEntries)
	{
		return -1;
	}
	return FileInfo[lump].lump->GetFileOffset();
}

//==========================================================================
//
// 
//
//==========================================================================

int FileSystem::GetFileFlags (int lump)
{
	if ((size_t)lump >= NumEntries)
	{
		return 0;
	}

	return FileInfo[lump].lump->Flags ^ FileInfo[lump].flags;
}

//==========================================================================
//
// InitHashChains
//
// Prepares the lumpinfos for hashing.
// (Hey! This looks suspiciously like something from Boom! :-)
//
//==========================================================================

void FileSystem::InitHashChains (void)
{
	unsigned int i, j;

	NumEntries = (uint32_t)FileInfo.size();
	Hashes.resize(8 * NumEntries);
	// Mark all buckets as empty
	memset(Hashes.data(), -1, Hashes.size() * sizeof(Hashes[0]));
	FirstLumpIndex = &Hashes[0];
	NextLumpIndex = &Hashes[NumEntries];
	FirstLumpIndex_FullName = &Hashes[NumEntries * 2];
	NextLumpIndex_FullName = &Hashes[NumEntries * 3];
	FirstLumpIndex_NoExt = &Hashes[NumEntries * 4];
	NextLumpIndex_NoExt = &Hashes[NumEntries * 5];
	FirstLumpIndex_ResId = &Hashes[NumEntries * 6];
	NextLumpIndex_ResId = &Hashes[NumEntries * 7];


	// Now set up the chains
	for (i = 0; i < (unsigned)NumEntries; i++)
	{
		j = MakeHash (FileInfo[i].shortName.String, 8) % NumEntries;
		NextLumpIndex[i] = FirstLumpIndex[j];
		FirstLumpIndex[j] = i;

		// Do the same for the full paths
		if (!FileInfo[i].longName.empty())
		{
			j = MakeHash(FileInfo[i].longName.c_str()) % NumEntries;
			NextLumpIndex_FullName[i] = FirstLumpIndex_FullName[j];
			FirstLumpIndex_FullName[j] = i;

			auto nameNoExt = FileInfo[i].longName;
			auto dot = nameNoExt.find_last_of('.');
			auto slash = nameNoExt.find_last_of('/');
			if ((dot > slash || slash == std::string::npos) && dot != std::string::npos) nameNoExt.resize(dot);

			j = MakeHash(nameNoExt.c_str()) % NumEntries;
			NextLumpIndex_NoExt[i] = FirstLumpIndex_NoExt[j];
			FirstLumpIndex_NoExt[j] = i;

			j = FileInfo[i].resourceId % NumEntries;
			NextLumpIndex_ResId[i] = FirstLumpIndex_ResId[j];
			FirstLumpIndex_ResId[j] = i;

		}
	}
	FileInfo.shrink_to_fit();
	Files.shrink_to_fit();
}

//==========================================================================
//
// should only be called before the hash chains are set up.
// If done later this needs rehashing.
//
//==========================================================================

LumpShortName& FileSystem::GetShortName(int i)
{
	if ((unsigned)i >= NumEntries) throw FileSystemException("GetShortName: Invalid index");
	return FileInfo[i].shortName;
}

void FileSystem::RenameFile(int num, const char* newfn)
{
	if ((unsigned)num >= NumEntries) throw FileSystemException("RenameFile: Invalid index");
	FileInfo[num].longName = newfn;
	// This does not alter the short name - call GetShortname to do that!
}

//==========================================================================
//
// MoveLumpsInFolder
//
// Moves all content from the given subfolder of the internal
// resources to the current end of the directory.
// Used to allow modifying content in the base files, this is needed
// so that Hacx and Harmony can override some content that clashes
// with localization, and to inject modifying data into mods, in case
// this is needed for some compatibility requirement.
//
//==========================================================================

static FResourceLump placeholderLump;

void FileSystem::MoveLumpsInFolder(const char *path)
{
	if (FileInfo.size() == 0)
	{
		return;
	}

	auto len = strlen(path);
	auto rfnum = FileInfo.back().rfnum;

	size_t i;
	for (i = 0; i < FileInfo.size(); i++)
	{
		auto& li = FileInfo[i];
		if (li.rfnum >= GetIwadNum()) break;
		if (strnicmp(li.longName.c_str(), path, len) == 0)
		{
			FileInfo.push_back(li);
			li.lump = &placeholderLump;			// Make the old entry point to something empty. We cannot delete the lump record here because it'd require adjustment of all indices in the list.
			auto &ln = FileInfo.back();
			ln.lump->LumpNameSetup(ln.longName.c_str() + len);
			ln.SetFromLump(rfnum, ln.lump);
		}
	}
}

//==========================================================================
//
// W_FindLump
//
// Find a named lump. Specifically allows duplicates for merging of e.g.
// SNDINFO lumps.
//
//==========================================================================

int FileSystem::FindLump (const char *name, int *lastlump, bool anyns)
{
	if (*lastlump >= FileInfo.size()) return -1;
	union
	{
		char name8[8];
		uint64_t qname;
	};
	LumpRecord *lump_p;

	UpperCopy (name8, name);

	assert(lastlump != NULL && *lastlump >= 0);
	lump_p = &FileInfo[*lastlump];
	while (lump_p <= &FileInfo.back())
	{
		if ((anyns || lump_p->Namespace == ns_global) && lump_p->shortName.qword == qname)
		{
			int lump = int(lump_p - FileInfo.data());
			*lastlump = lump + 1;
			return lump;
		}
		lump_p++;
	}

	*lastlump = NumEntries;
	return -1;
}

//==========================================================================
//
// W_FindLumpMulti
//
// Find a named lump. Specifically allows duplicates for merging of e.g.
// SNDINFO lumps. Returns everything having one of the passed names.
//
//==========================================================================

int FileSystem::FindLumpMulti (const char **names, int *lastlump, bool anyns, int *nameindex)
{
	LumpRecord *lump_p;

	assert(lastlump != NULL && *lastlump >= 0);
	lump_p = &FileInfo[*lastlump];
	while (lump_p <= &FileInfo.back())
	{
		if (anyns || lump_p->Namespace == ns_global)
		{

			for(const char **name = names; *name != NULL; name++)
			{
				if (!strnicmp(*name, lump_p->shortName.String, 8))
				{
					int lump = int(lump_p - FileInfo.data());
					*lastlump = lump + 1;
					if (nameindex != NULL) *nameindex = int(name - names);
					return lump;
				}
			}
		}
		lump_p++;
	}

	*lastlump = NumEntries;
	return -1;
}

//==========================================================================
//
// W_FindLump
//
// Find a named lump. Specifically allows duplicates for merging of e.g.
// SNDINFO lumps.
//
//==========================================================================

int FileSystem::FindLumpFullName(const char* name, int* lastlump, bool noext)
{
	assert(lastlump != NULL && *lastlump >= 0);
	auto lump_p = &FileInfo[*lastlump];

	if (!noext)
	{
		while (lump_p <= &FileInfo.back())
		{
			if (!stricmp(name, lump_p->longName.c_str()))
			{
				int lump = int(lump_p - FileInfo.data());
				*lastlump = lump + 1;
				return lump;
			}
			lump_p++;
		}
	}
	else
	{
		auto len = strlen(name);
		while (lump_p <= &FileInfo.back())
		{
			auto res = strnicmp(name, lump_p->longName.c_str(), len);
			if (res == 0)
			{
				auto p = lump_p->longName.c_str() + len;
				if (*p == 0 || (*p == '.' && strpbrk(p + 1, "./") == 0))
				{
					int lump = int(lump_p - FileInfo.data());
					*lastlump = lump + 1;
					return lump;
				}
			}
			lump_p++;
		}
	}


	*lastlump = NumEntries;
	return -1;
}

//==========================================================================
//
// W_CheckLumpName
//
//==========================================================================

bool FileSystem::CheckFileName (int lump, const char *name)
{
	if ((size_t)lump >= NumEntries)
		return false;

	return !strnicmp (FileInfo[lump].shortName.String, name, 8);
}

//==========================================================================
//
// GetLumpName
//
//==========================================================================

const char* FileSystem::GetFileShortName(int lump) const
{
	if ((size_t)lump >= NumEntries)
		return nullptr;
	else
		return FileInfo[lump].shortName.String;
}

//==========================================================================
//
// FileSystem :: GetFileFullName
//
// Returns the lump's full name if it has one or its short name if not.
//
//==========================================================================

const char *FileSystem::GetFileFullName (int lump, bool returnshort) const
{
	if ((size_t)lump >= NumEntries)
		return NULL;
	else if (!FileInfo[lump].longName.empty())
		return FileInfo[lump].longName.c_str();
	else if (returnshort)
		return FileInfo[lump].shortName.String;
	else return nullptr;
}

//==========================================================================
//
// FileSystem :: GetFileFullPath
//
// Returns the name of the lump's wad prefixed to the lump's full name.
//
//==========================================================================

std::string FileSystem::GetFileFullPath(int lump) const
{
	std::string foo;

	if ((size_t) lump <  NumEntries)
	{
		foo = GetResourceFileName(FileInfo[lump].rfnum);
		foo += ':';
		foo += +GetFileFullName(lump);
	}
	return foo;
}

//==========================================================================
//
// GetFileNamespace
//
//==========================================================================

int FileSystem::GetFileNamespace (int lump) const
{
	if ((size_t)lump >= NumEntries)
		return ns_global;
	else
		return FileInfo[lump].Namespace;
}

void FileSystem::SetFileNamespace(int lump, int ns)
{
	if ((size_t)lump < NumEntries) FileInfo[lump].Namespace = ns;
}

//==========================================================================
//
// FileSystem :: GetResourceId
//
// Returns the index number for this lump. This is *not* the lump's position
// in the lump directory, but rather a special value that RFF can associate
// with files. Other archive types will return 0, since they don't have it.
//
//==========================================================================

int FileSystem::GetResourceId(int lump) const
{
	if ((size_t)lump >= NumEntries)
		return -1;
	else
		return FileInfo[lump].resourceId;
}

//==========================================================================
//
// GetResourceType
//
// is equivalent with the extension
//
//==========================================================================

const char *FileSystem::GetResourceType(int lump) const
{
	if ((size_t)lump >= NumEntries)
		return nullptr;
	else
	{
		auto p = strrchr(FileInfo[lump].longName.c_str(), '.');
		if (!p) return "";	// has no extension
		if (strchr(p, '/')) return "";	// the '.' is part of a directory.
		return p + 1;
	}
}

//==========================================================================
//
// GetFileContainer
//
//==========================================================================

int FileSystem::GetFileContainer (int lump) const
{
	if ((size_t)lump >= FileInfo.size())
		return -1;
	return FileInfo[lump].rfnum;
}

//==========================================================================
//
// GetFilesInFolder
// 
// Gets all lumps within a single folder in the hierarchy.
// If 'atomic' is set, it treats folders as atomic, i.e. only the
// content of the last found resource file having the given folder name gets used.
//
//==========================================================================

static int folderentrycmp(const void *a, const void *b)
{
	auto A = (FolderEntry*)a;
	auto B = (FolderEntry*)b;
	return strcmp(A->name, B->name);
}

//==========================================================================
//
// 
//
//==========================================================================

unsigned FileSystem::GetFilesInFolder(const char *inpath, std::vector<FolderEntry> &result, bool atomic) const
{
	std::string path = inpath;
	FixPathSeparator(&path.front());
	for (auto& c : path) c = tolower(c);
	if (path.back() != '/') path += '/';
	result.clear();
	for (size_t i = 0; i < FileInfo.size(); i++)
	{
		if (FileInfo[i].longName.find(path) == 0)
		{
			// Only if it hasn't been replaced.
			if ((unsigned)fileSystem.CheckNumForFullName(FileInfo[i].longName.c_str()) == i)
			{
				FolderEntry fe{ FileInfo[i].longName.c_str(), (uint32_t)i };
				result.push_back(fe);
			}
		}
	}
	if (result.size())
	{
		int maxfile = -1;
		if (atomic)
		{
			// Find the highest resource file having content in the given folder.
			for (auto & entry : result)
			{
				int thisfile = fileSystem.GetFileContainer(entry.lumpnum);
				if (thisfile > maxfile) maxfile = thisfile;
			}
			// Delete everything from older files.
			for (int i = (int)result.size() - 1; i >= 0; i--)
			{
				if (fileSystem.GetFileContainer(result[i].lumpnum) != maxfile) result.erase(result.begin() + i);
			}
		}
		qsort(result.data(), result.size(), sizeof(FolderEntry), folderentrycmp);
	}
	return (unsigned)result.size();
}

//==========================================================================
//
// W_ReadFile
//
// Loads the lump into the given buffer, which must be >= W_LumpLength().
//
//==========================================================================

void FileSystem::ReadFile (int lump, void *dest)
{
	auto lumpr = OpenFileReader (lump);
	auto size = lumpr.GetLength ();
	auto numread = lumpr.Read (dest, size);

	if (numread != size)
	{
		throw FileSystemException("W_ReadFile: only read %ld of %ld on '%s'\n",
			numread, size, FileInfo[lump].longName.c_str());
	}
}


//==========================================================================
//
// ReadFile - variant 2
//
// Loads the lump into a newly created buffer and returns it.
//
//==========================================================================

FileData FileSystem::ReadFile (int lump)
{
	if ((unsigned)lump >= (unsigned)FileInfo.size())
	{
		throw FileSystemException("ReadFile: %u >= NumEntries", lump);
	}
	auto lumpp = FileInfo[lump].lump;

	return FileData(lumpp);
}

//==========================================================================
//
// OpenFileReader
//
// uses a more abstract interface to allow for easier low level optimization later
//
//==========================================================================


FileReader FileSystem::OpenFileReader(int lump)
{
	if ((unsigned)lump >= (unsigned)FileInfo.size())
	{
		throw FileSystemException("OpenFileReader: %u >= NumEntries", lump);
	}

	auto rl = FileInfo[lump].lump;
	auto rd = rl->GetReader();

	if (rl->RefCount == 0 && rd != nullptr && !rd->GetBuffer() && !(rl->Flags & LUMPF_COMPRESSED))
	{
		FileReader rdr;
		rdr.OpenFilePart(*rd, rl->GetFileOffset(), rl->LumpSize);
		return rdr;
	}
	return rl->NewReader();	// This always gets a reader to the cache
}

FileReader FileSystem::ReopenFileReader(int lump, bool alwayscache)
{
	if ((unsigned)lump >= (unsigned)FileInfo.size())
	{
		throw FileSystemException("ReopenFileReader: %u >= NumEntries", lump);
	}

	auto rl = FileInfo[lump].lump;
	auto rd = rl->GetReader();

	if (rl->RefCount == 0 && rd != nullptr && !rd->GetBuffer() && !alwayscache && !(rl->Flags & LUMPF_COMPRESSED))
	{
		int fileno = fileSystem.GetFileContainer(lump);
		const char *filename = fileSystem.GetResourceFileFullName(fileno);
		FileReader fr;
		if (fr.OpenFile(filename, rl->GetFileOffset(), rl->LumpSize))
		{
			return fr;
		}
	}
	return rl->NewReader();	// This always gets a reader to the cache
}

FileReader FileSystem::OpenFileReader(const char* name)
{
	auto lump = CheckNumForFullName(name);
	if (lump < 0) return FileReader();
	else return OpenFileReader(lump);
}

//==========================================================================
//
// GetFileReader
//
// Retrieves the File reader object to access the given WAD
// Careful: This is only useful for real WAD files!
//
//==========================================================================

FileReader *FileSystem::GetFileReader(int rfnum)
{
	if ((uint32_t)rfnum >= Files.size())
	{
		return NULL;
	}

	return Files[rfnum]->GetReader();
}

//==========================================================================
//
// GetResourceFileName
//
// Returns the name of the given wad.
//
//==========================================================================

const char *FileSystem::GetResourceFileName (int rfnum) const noexcept
{
	const char *name, *slash;

	if ((uint32_t)rfnum >= Files.size())
	{
		return NULL;
	}

	name = Files[rfnum]->FileName.c_str();
	slash = strrchr (name, '/');
	return (slash != nullptr && slash[1] != 0) ? slash+1 : name;
}

//==========================================================================
//
//
//==========================================================================

int FileSystem::GetFirstEntry (int rfnum) const noexcept
{
	if ((uint32_t)rfnum >= Files.size())
	{
		return 0;
	}

	return Files[rfnum]->GetFirstEntry();
}

//==========================================================================
//
//
//==========================================================================

int FileSystem::GetLastEntry (int rfnum) const noexcept
{
	if ((uint32_t)rfnum >= Files.size())
	{
		return 0;
	}

	return Files[rfnum]->GetFirstEntry() + Files[rfnum]->LumpCount() - 1;
}

//==========================================================================
//
//
//==========================================================================

int FileSystem::GetEntryCount (int rfnum) const noexcept
{
	if ((uint32_t)rfnum >= Files.size())
	{
		return 0;
	}

	return Files[rfnum]->LumpCount();
}


//==========================================================================
//
// GetResourceFileFullName
//
// Returns the name of the given wad, including any path
//
//==========================================================================

const char *FileSystem::GetResourceFileFullName (int rfnum) const noexcept
{
	if ((unsigned int)rfnum >= Files.size())
	{
		return nullptr;
	}

	return Files[rfnum]->FileName.c_str();
}


//==========================================================================
//
// Clones an existing resource with different properties
//
//==========================================================================

bool FileSystem::CreatePathlessCopy(const char *name, int id, int /*flags*/)
{
	std::string name2 = name, type2, path;

	// The old code said 'filename' and ignored the path, this looked like a bug.
	FixPathSeparator(&name2.front());
	auto lump = FindFile(name2.c_str());
	if (lump < 0) return false;		// Does not exist.

	auto oldlump = FileInfo[lump];
	ptrdiff_t slash = oldlump.longName.find_last_of('/');

	if (slash == std::string::npos)
	{
		FileInfo[lump].flags = LUMPF_FULLPATH;
		return true;	// already is pathless.
	}


	// just create a new reference to the original data with a different name.
	oldlump.longName.erase(oldlump.longName.begin(), oldlump.longName.begin() + (slash + 1));
	oldlump.resourceId = id;
	oldlump.flags = LUMPF_FULLPATH;
	FileInfo.push_back(oldlump);
	return true;
}

// FileData -----------------------------------------------------------------

FileData::FileData (const FileData &copy)
{
	Block = copy.Block;
}

FileData &FileData::operator = (const FileData &copy)
{
	Block = copy.Block;
	return *this;
}

FileData::FileData (FResourceLump* lump)
{
	auto size = lump->LumpSize;
	Block.resize(1 + size);
	memcpy(Block.data(), lump->Lock(), size);
	Block[size] = 0;
	lump->Unlock();
}


//==========================================================================
//
// PrintLastError
//
//==========================================================================

#ifdef _WIN32
//#define WIN32_LEAN_AND_MEAN
//#include <windows.h>

extern "C" {
__declspec(dllimport) unsigned long __stdcall FormatMessageA(
    unsigned long dwFlags,
    const void *lpSource,
    unsigned long dwMessageId,
    unsigned long dwLanguageId,
    char **lpBuffer,
    unsigned long nSize,
    va_list *Arguments
    );
__declspec(dllimport) void * __stdcall LocalFree (void *);
__declspec(dllimport) unsigned long __stdcall GetLastError ();
}

static void PrintLastError (FileSystemMessageFunc Printf)
{
	char *lpMsgBuf;
	FormatMessageA(0x1300 /*FORMAT_MESSAGE_ALLOCATE_BUFFER | 
							FORMAT_MESSAGE_FROM_SYSTEM | 
							FORMAT_MESSAGE_IGNORE_INSERTS*/,
		NULL,
		GetLastError(),
		1 << 10 /*MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT)*/, // Default language
		&lpMsgBuf,
		0,
		NULL 
	);
	Printf (FSMessageLevel::Error, "  %s\n", lpMsgBuf);
	// Free the buffer.
	LocalFree( lpMsgBuf );
}
#else
static void PrintLastError (FileSystemMessageFunc Printf)
{
	Printf(FSMessageLevel::Error, "  %s\n", strerror(errno));
}
#endif

//==========================================================================
//
// NBlood style lookup functions
//
//==========================================================================

FResourceLump* FileSystem::GetFileAt(int no)
{
	return FileInfo[no].lump;
}
