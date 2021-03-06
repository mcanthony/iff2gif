/* This file is part of iff2gif.
**
** Copyright 2015 - Randy Heit
**
** iff2gif is free software : you can redistribute it and / or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation, either version 2 of the License, or
** (at your option) any later version.
**
** Foobar is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with iff2gif. If not, see <http://www.gnu.org/licenses/>.
*/

#include <algorithm>
#include <unordered_map>
#include <assert.h>
#include <stdio.h>

#include "iff2gif.h"

// GIF restricts codes to 12 bits max
#define CODE_LIMIT (1 << 12)

class CodeStream
{
public:
	CodeStream(UBYTE mincodesize, std::vector<UBYTE> &codes);
	~CodeStream();
	void AddByte(UBYTE code);
	void WriteCode(UWORD p);
	void Dump();

private:
	std::vector<UBYTE> &Codes;
	ULONG Accum;
	UWORD ClearCode;
	UWORD EOICode;
	UWORD NextCode;		// next code to assign
	WORD Match;			// code of string matched so far
	UBYTE CodeSize;		// in bits
	UBYTE MinCodeSize;
	BYTE BitPos;
	UBYTE Chunk[256];	// first byte is length
	typedef std::unordered_map<ULONG, UWORD> DictType;
	DictType Dict;

	void ResetDict();
	void DumpAccum(bool full);
};

void LZWCompress(std::vector<UBYTE> &vec, const ImageDescriptor &imd, const UBYTE *prev, const UBYTE *chunky,
	int pitch, UBYTE mincodesize, int trans);

GIFWriter::GIFWriter(const _TCHAR *filename)
{
	Filename = filename;
	File = NULL;
	FrameCount = 0;
	TotalTicks = 0;
	GIFTime = 0;
	FrameRate = 50;		// Default to PAL!
	PrevFrame = NULL;
	BkgColor = 0;
}

GIFWriter::~GIFWriter()
{
	if (FrameCount == 1)
	{
		// The header is not normally written until we reach the second frame of the
		// input. For a single frame image, we need to write it now.
		WriteHeader(false);
	}
	if (File != NULL)
	{
		if (!WriteQueue.Flush() ||
			fputc(0x3B, File) == EOF)	// Add the trailer byte to terminate the GIF
		{
			BadWrite();
		}
		else
		{
			fclose(File);
		}
	}
	if (PrevFrame != NULL)
	{
		delete[] PrevFrame;
	}
}

void GIFWriter::BadWrite()
{
	_ftprintf(stderr, _T("Could not wite to %s: %s\n"), Filename, _tcserror(errno));
	fclose(File);
	File = NULL;
	WriteQueue.SetFile(NULL);
}

void GIFWriter::AddFrame(PlanarBitmap *bitmap)
{
	UBYTE *chunky = new UBYTE[bitmap->Width * bitmap->Height];
	bitmap->ToChunky(chunky);
	if (FrameCount == 0)
	{
		printf("%dx%dx%d\n", bitmap->Width, bitmap->Height, bitmap->NumPlanes);
		PageWidth = bitmap->Width;
		PageHeight = bitmap->Height;
		GlobalPalBits = ExtendPalette(GlobalPal, bitmap->Palette, bitmap->PaletteSize);
		DetectBackgroundColor(bitmap, chunky);
	}
	else if (FrameCount == 1)
	{ // This is the second frame, so we know we can loop this GIF.
		WriteHeader(true);
	}
	if (bitmap->Rate > 0)
	{
		FrameRate = bitmap->Rate;
	}
	MakeFrame(bitmap, chunky);
	FrameCount++;
}

void GIFWriter::WriteHeader(bool loop)
{
	LogicalScreenDescriptor lsd = { LittleShort(PageWidth), LittleShort(PageHeight), 0, BkgColor, 0 };

	if (GlobalPalBits > 0)
	{
		lsd.Flags = 0xF0 | (GlobalPalBits - 1);
	}

	assert(File == NULL);
	File = _tfopen(Filename, _T("wb"));
	if (File == NULL)
	{
		_ftprintf(stderr, _T("Could not open %s: %s\n"), Filename, _tcserror(errno));
		return;
	}
	WriteQueue.SetFile(File);
	if (fwrite("GIF89a", 6, 1, File) != 1 || fwrite(&lsd, 7, 1, File) != 1)
	{
		BadWrite();
		return;
	}
	// Write (or skip) palette
	if (lsd.Flags & 0x80)
	{
		if (fwrite(GlobalPal, 3, 1 << GlobalPalBits, File) != 1 << GlobalPalBits)
		{
			BadWrite();
			return;
		}
	}
	// Write (or skip) the looping extension
	if (loop)
	{
		if (fwrite("\x21\xFF\x0BNETSCAPE2.0\x03\x01\x00\x00", 19, 1, File) != 1)
		{
			BadWrite();
			return;
		}
	}
}

// GIF palettes must be a power of 2 in size. CMAP chunks have no such restriction.
int GIFWriter::ExtendPalette(ColorRegister *dest, const ColorRegister *src, int numsrc)
{
	if (numsrc <= 0)
	{
		return 0;
	}
	// What's the closest power of 2 the palette fits in?
	UBYTE p = 1;
	while (1 << p < numsrc && p < 8)
		++p;

	int numdest = 1 << p, i;
	// The source could potentially have more colors than we need, but also
	// might not have enough.
	for (i = 0; i < numsrc && i < numdest; ++i)
	{
		dest[i] = src[i];
	}
	// Set extras to grayscale
	for (; i < numdest; ++i)
	{
		dest[i].blue = dest[i].green = dest[i].red = (i * 255) >> p;
	}
	return p;
}

void GIFWriter::MakeFrame(PlanarBitmap *bitmap, UBYTE *chunky)
{
	GIFFrame newframe, *oldframe;

	WriteQueue.SetDropFrames(bitmap->Interleave);
	newframe.IMD.Width = LittleShort(bitmap->Width);
	newframe.IMD.Height = LittleShort(bitmap->Height);

	// Is there a transparent color?
	if (bitmap->TransparentColor >= 0)
	{
		newframe.GCE.Flags = 1;
		newframe.GCE.TransparentColor = bitmap->TransparentColor;
	}
	// Update properties on the preceding frame that couldn't be determined
	// until this frame.
	oldframe = WriteQueue.MostRecent();
	if (oldframe != NULL)
	{
		oldframe->GCE.Flags |= SelectDisposal(bitmap, newframe.IMD, chunky) << 2;
		if (bitmap->Delay != 0)
		{
			// GIF timing is in 1/100 sec. ANIM timing is in multiples of an FPS clock.
			ULONG tick = TotalTicks + bitmap->Delay;
			ULONG lasttime = GIFTime;
			ULONG nowtime = tick * 100 / FrameRate;
			int delay = nowtime - lasttime;
			oldframe->SetDelay(delay);
			TotalTicks = tick;
			GIFTime += delay;
		}
	}
	// Identify the minimum rectangle that needs to be updated.
	if (PrevFrame != NULL)
	{
		MinimumArea(PrevFrame, chunky, newframe.IMD);
	}
	// Replaces unchanged pixels with a transparent color, if there's room in the palette.
	int trans;
	bool temptrans = false;
	if (FrameCount == 0 || PrevFrame == NULL || (newframe.GCE.Flags & 0x1C0) == 0x80)
	{
		trans = -1;
	}
	else if (newframe.GCE.Flags & 1)
	{
		trans = newframe.GCE.TransparentColor;
	}
	else
	{
		trans = SelectTransparentColor(PrevFrame, chunky, newframe.IMD, bitmap->Width);
		if (trans >= 0)
		{
			newframe.GCE.Flags |= 1;
			newframe.GCE.TransparentColor = trans;
			temptrans = true;
		}
	}
	// Compressed the image data
	LZWCompress(newframe.LZW, newframe.IMD, PrevFrame, chunky, bitmap->Width, bitmap->NumPlanes, trans);
	// If we did transparent substitution, try again without. Sometimes it compresses
	// better if we don't do that.
	if (trans >= 0)
	{
		std::vector<UBYTE> try2;
		LZWCompress(try2, newframe.IMD, PrevFrame, chunky, bitmap->Width, bitmap->NumPlanes, -1);
		size_t l = newframe.LZW.size();
		size_t r = try2.size();
		if (try2.size() <= newframe.LZW.size())
		{
			newframe.LZW = std::move(try2);
			if (temptrans)
			{ // Undo the transparent color
				newframe.GCE.Flags &= 0xFE;
				newframe.GCE.TransparentColor = 0;
			}
		}
	}
	// Queue this frame for later writing, possibly flushing one frame to disk.
	if (!WriteQueue.Enqueue(std::move(newframe)))
	{
		BadWrite();
	}
	// Remember this frame's pixels
	if (PrevFrame != NULL)
	{
		delete[] PrevFrame;
	}
	PrevFrame = chunky;
}

void GIFWriter::DetectBackgroundColor(PlanarBitmap *bitmap, const UBYTE *chunky)
{
	// The GIF specification includes a background color. CompuServe probably actually
	// used this. In practice, modern viewers just make the background be transparent
	// and completely ignore the background color. Which means that if an image is
	// surrounded by a solid border, we can't optimize by turning that into the
	// background color and only writing the non-border area of the image unless
	// the border is transparent.

	// If there is a transparent color, let it be the background.
	if (bitmap->TransparentColor >= 0)
	{
		BkgColor = bitmap->TransparentColor;
		assert(PrevFrame == NULL);
		PrevFrame = new UBYTE[bitmap->Width * bitmap->Height];
		memset(PrevFrame, BkgColor, bitmap->Width * bitmap->Height);
	}
	// Else, what the fuck ever. It doesn't matter.
	else
	{
		BkgColor = 0;
	}
}

void GIFWriter::MinimumArea(const UBYTE *prev, const UBYTE *cur, ImageDescriptor &imd)
{
	LONG start = -1;
	LONG end = imd.Width * imd.Height;
	LONG p;
	int top, bot, left, right, x;

	// Scan from beginning to find first changed pixel.
	while (++start < end)
	{
		if (prev[start] != cur[start])
			break;
	}
	if (start == end)
	{ // Nothing changed! Use a dummy 1x1 rectangle in case a GIF viewer would choke
	  // on no image data at all in a frame.
		imd.Width = 1;
		imd.Height = 1;
		return;
	}
	// Scan from end to find last changed pixel.
	while (--end > start)
	{
		if (prev[end] != cur[end])
			break;
	}
	// Now we know the top and bottom of the changed area, but not the left and right.
	top = start / imd.Width;
	bot = end / imd.Width;
	// Find left edge.
	for (x = 0; x < imd.Width - 1; ++x)
	{
		p = top * imd.Width + x;
		for (int y = top; y <= bot; ++y, p += imd.Width)
		{
			if (prev[p] != cur[p])
				goto gotleft;
		}
	}
gotleft:
	left = x;
	// Find right edge.
	for (x = imd.Width - 1; x > 0; --x)
	{
		p = top * imd.Width + x;
		for (int y = top; y <= bot; ++y, p += imd.Width)
		{
			if (prev[p] != cur[p])
				goto gotright;
		}
	}
gotright:
	right = x;

	imd.Left = left;
	imd.Top = top;
	imd.Width = right - left + 1;
	imd.Height = bot - top + 1;
}

// Select the disposal method for this frame.
UBYTE GIFWriter::SelectDisposal(PlanarBitmap *planar, ImageDescriptor &imd, const UBYTE *chunky)
{
	// If there is no transparent color, then we can keep the old frame intact.
	if (planar->TransparentColor < 0 || PrevFrame == NULL)
	{
		return 1;
	}
	// If no pixels are being changed to a transparent color, we can keep the old frame intact.
	// Otherwise, we must dispose it to the background color, since that's the only way to
	// set a pixel transparent after it's been rendered opaque.
	const UBYTE *src = PrevFrame + imd.Left + imd.Top * planar->Width;
	const UBYTE *dest = chunky + imd.Left + imd.Top * planar->Width;
	const UBYTE trans = planar->TransparentColor;
	for (int y = 0; y < imd.Height; ++y)
	{
		for (int x = 0; x < imd.Width; ++x)
		{
			if (src[x] != trans && dest[x] == trans)
			{
				// Dispose the preceding frame.
				if (PrevFrame != NULL)
				{
					memset(PrevFrame, planar->TransparentColor, planar->Width * planar->Height);
				}
				return 2;
			}
		}
		src += planar->Width;
		dest += planar->Width;
	}
	return 1;
}

// Compares pixels in the changed region and returns a color that is not used in the destination.
// This can be used as a transparent color for this frame for better compression, since the
// underlying unchanged pixels can be collapsed into a run of a single color.
int GIFWriter::SelectTransparentColor(const UBYTE *prev, const UBYTE *now, const ImageDescriptor &imd, int pitch)
{
	UBYTE used[256 / 8] = { 0 };
	UBYTE c;

	prev += imd.Left + imd.Top * pitch;
	now += imd.Left + imd.Top * pitch;
	// Set a bit for every color used in the dest that changed from the preceding frame
	for (int y = 0; y < imd.Height; ++y)
	{
		for (int x = 0; x < imd.Width; ++x)
		{
			if (prev[x] != (c = now[x]))
			{
				used[c >> 3] |= 1 << (c & 7);
			}
		}
		prev += pitch;
		now += pitch;
	}
	// Return the first unused color found. Returns -1 if they were all used.
	for (int i = 0; i < 256 / 8; ++i)
	{
		if (used[i] != 255)
		{
			int bits = used[i], j;
			for (j = 0; bits & 1; ++j)
			{
				bits >>= 1;
			}
			// The color must be a part of the palette, if the palette has
			// fewer than 256 colors.
			int color = (i << 3) + j;
			return (color < (1 << GlobalPalBits)) ? color : -1;
		}
	}
	return -1;
}

void LZWCompress(std::vector<UBYTE> &vec, const ImageDescriptor &imd, const UBYTE *prev, const UBYTE *chunky,
	int pitch, UBYTE mincodesize, int trans)
{
	if (mincodesize < 2)
	{
		mincodesize = 2;
	}
	vec.push_back(mincodesize);
	CodeStream codes(mincodesize, vec);
	const UBYTE *in = chunky + imd.Left + imd.Top * pitch;
	if (trans < 0)
	{
		for (int y = 0; y < imd.Height; ++y)
		{
			for (int x = 0; x < imd.Width; ++x)
			{
				codes.AddByte(in[x]);
			}
			in += pitch;
		}
	}
	else
	{
		const UBYTE transcolor = trans;
		prev += imd.Left + imd.Top * pitch;
		for (int y = 0; y < imd.Height; ++y)
		{
			for (int x = 0; x < imd.Width; ++x)
			{
				codes.AddByte(prev[x] != in[x] ? in[x] : transcolor);
			}
			in += pitch;
			prev += pitch;
		}
	}
}

CodeStream::CodeStream(UBYTE mincodesize, std::vector<UBYTE> &codes)
	: Codes(codes)
{
	assert(mincodesize >= 2);
	MinCodeSize = mincodesize;
	CodeSize = MinCodeSize + 1;
	ClearCode = 1 << mincodesize;
	EOICode = ClearCode + 1;
	BitPos = 0;
	Accum = 0;
	memset(Chunk, 0, sizeof(Chunk));
	WriteCode(ClearCode);
}

CodeStream::~CodeStream()
{
	// Finish output
	if (Match >= 0)
	{
		WriteCode(Match);
	}
	WriteCode(EOICode);
	DumpAccum(true);
	Dump();
	// Write block terminator
	Codes.push_back(0);
}

void CodeStream::Dump()
{
	if (Chunk[0] > 0)
	{
		Codes.insert(Codes.end(), Chunk, Chunk + Chunk[0] + 1);
		Chunk[0] = 0;
	}
}

void CodeStream::WriteCode(UWORD code)
{
	Accum |= code << BitPos;
	BitPos += CodeSize;
	assert(Chunk[0] < 255);
	DumpAccum(false);
	if (code == ClearCode)
	{
		ResetDict();
	}
}

// If <full> is true, dump every accumulated bit.
// If <full> is false, only dump every complete accumulated byte.
void CodeStream::DumpAccum(bool full)
{
	BYTE stop = full ? 0 : 7;
	while (BitPos > stop)
	{
		Chunk[1 + Chunk[0]] = Accum & 0xFF;
		Accum >>= 8;
		BitPos -= 8;
		if (++Chunk[0] == 255)
		{
			Dump();
		}
	}
}

void CodeStream::AddByte(UBYTE p)
{
	if (Match < 0)
	{ // Start a new run. We know p is always in the dictionary.
		Match = p;
	}
	else
	{ // Is Match..p in the dictionary?
		ULONG str = Match | (p << 16) | (1 << 24);
		DictType::const_iterator got = Dict.find(str);
		if (got != Dict.end())
		{ // Yes, so continue matching it.
			Match = got->second;
		}
		else
		{ // No, so write out the matched code and add this new string to the dictionary.
			WriteCode(Match);
			Dict[str] = NextCode++;
			if (NextCode == CODE_LIMIT)
			{
				WriteCode(ClearCode);
			}
			else if (NextCode == (1 << CodeSize) + 1)
			{
				CodeSize++;
			}

			// Start a new match string on this byte.
			Match = p;
		}
	}
}

void CodeStream::ResetDict()
{
	CodeSize = MinCodeSize + 1;
	NextCode = EOICode + 1;
	Match = -1;
	Dict.clear();
	// Initialize the dictionary with the raw bytes that can be in the image.
	for (int i = (1 << MinCodeSize) - 1; i >= 0; --i)
	{
		Dict[i] = i;
	}
}

GIFFrame::GIFFrame()
{
	GCE.ExtensionIntroducer = 0x21;
	GCE.GraphicControlLabel = 0xF9;
	GCE.BlockSize = 4;
	GCE.Flags = 0;
	GCE.DelayTime = 0;
	GCE.TransparentColor = 0;
	GCE.BlockTerminator = 0;

	IMD.Left = 0;
	IMD.Top = 0;
	IMD.Width = 0;
	IMD.Height = 0;
	IMD.Flags = 0;
}

GIFFrame &GIFFrame::operator= (const GIFFrame &o)
{
	GCE = o.GCE;
	IMD = o.IMD;
	LZW = o.LZW;
	return *this;
}

GIFFrame &GIFFrame::operator= (GIFFrame &&o)
{
	GCE = o.GCE;
	IMD = o.IMD;
	LZW = std::move(o.LZW);
	return *this;
}

bool GIFFrame::Write(FILE *file)
{
	if (file != NULL)
	{
		// Write Graphic Control Extension, if needed
		if (GCE.Flags != 0 || GCE.DelayTime != 0)
		{
			if (fwrite(&GCE, 8, 1, file) != 1)
			{
				return false;
			}
		}
		// Write the image descriptor
		if (fputc(0x2C, file) /* Identify the Image Separator */ == EOF ||
			fwrite(&IMD, 9, 1, file) != 1 ||
			// Write the compressed image data
			fwrite(&LZW[0], 1, LZW.size(), file) != LZW.size())
		{
			return false;
		}
		return true;
	}
	// Pretend success if no file open
	return true;
}

GIFFrameQueue::GIFFrameQueue()
{
	File = NULL;
	QueueCount = 0;
	FinalFramesToDrop = 0;
}

GIFFrameQueue::~GIFFrameQueue()
{
	Flush();
}

bool GIFFrameQueue::Flush()
{
	bool wrote = true;
	for (int i = 0; i < QueueCount - FinalFramesToDrop; ++i)
	{
		if (!Queue[i].Write(File))
		{
			wrote = false;
			break;
		}
	}
	QueueCount = 0;
	return wrote;
}

bool GIFFrameQueue::Enqueue(GIFFrame &&frame)
{
	bool wrote = true;
	if (QueueCount == QUEUE_SIZE)
	{
		wrote = Shift();
	}
	Queue[QueueCount++] = frame;
	return wrote;
}

// Write out one frame and shift the others left.
bool GIFFrameQueue::Shift()
{
	bool wrote = true;
	if (QueueCount > 0)
	{
		if (!Queue[0].Write(File))
		{
			wrote = false;
		}
		QueueCount--;
		for (int i = 0; i < QueueCount; ++i)
		{
			Queue[i] = std::move(Queue[i + 1]);
		}
	}
	return wrote;
}
