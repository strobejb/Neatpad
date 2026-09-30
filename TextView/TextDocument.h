#ifndef TEXTDOC_INCLUDED
#define TEXTDOC_INCLUDED

class TextDocument
{
public:
	TextDocument();
	~TextDocument();

	bool init(HANDLE hFile);
	bool init(char *filename);
	
	bool clear();

	bool  offset_to_line(ULONG fileoffset, ULONG *lineno, ULONG *offset);
	bool  getlineinfo(ULONG lineno, ULONG *fileoff, ULONG *length);	

	ULONG getline(ULONG lineno, char *buf, size_t len, ULONG *fileoff=0);
	ULONG getline(ULONG lineno, ULONG offset, char *buf, size_t len, ULONG *fileoff=0);
	ULONG getdata(ULONG offset, char *buf, size_t len);

	ULONG linecount();
	ULONG longestline(int tabwidth);
	ULONG size();

private:
	
	bool init_linebuffer();

	char *buffer;
	ULONG length;

	ULONG *linebuffer;
	ULONG  numlines;
};

#endif