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

	ULONG getline(ULONG lineno, char *buf, size_t len, ULONG *fileoff=0);
	ULONG getline(ULONG lineno, ULONG offset, char *buf, size_t len, ULONG *fileoff=0);

	ULONG linecount();
	ULONG longestline(int tabwidth);

private:
	
	bool init_linebuffer();

	char *buffer;
	ULONG length;

	ULONG *linebuffer;
	ULONG  numlines;
};

#endif