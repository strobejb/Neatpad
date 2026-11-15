// Copyright (c) 2004-2026 James Brown, Catch22 Solutions
// SPDX-License-Identifier: MIT

#ifndef SAVEFILE_INCLUDED
#define SAVEFILE_INCLUDED

#include <stdint.h>
#include <stdio.h>
#include <string>
#include "sequence.h"		// seqfilechar

//
//	A file that is saved safely, as with Qt's QSaveFile: everything is written
//	to a new file beside the target, which replaces the target at commit(),
//	once all of it is on the disk. Until then the target is untouched, and if
//	anything fails, or commit() is never reached, it stays as it was.
//
class SaveFile
{
public:
	SaveFile();
	~SaveFile();						// discards a file that wasn't committed

	bool	open(const seqfilechar *filename);
	bool	write(const void *data, size_t length);
	bool	commit();

private:
	void	discard();

	std::basic_string<seqfilechar> m_target;
	std::basic_string<seqfilechar> m_tempname;	// empty until the new file exists
	FILE   *m_fp;
};

//
//	Keeps the system's last error (errno, and GetLastError on Windows) over
//	cleanup that could change it, so it still says why something failed
//
class KeepLastError
{
public:
	KeepLastError();
	~KeepLastError();

private:
	int		 m_errno;
	uint32_t m_error;
};

//
//	A temporary file written at its end while a TextDocument reads it, as the
//	view's filter writes the lines it finds. Others can read it as it grows,
//	and it's gone once it's closed and nothing else has it open.
//
class TempAppendFile
{
public:
	TempAppendFile();
	~TempAppendFile();					// closes it

	bool	create();					// a new, empty file, closing any before
	bool	write(const void *data, size_t length);
	void	close();

	const seqfilechar *path() const;

private:
	std::basic_string<seqfilechar> m_path;
	intptr_t m_file;					// the system's handle or descriptor, or -1
};

#endif
