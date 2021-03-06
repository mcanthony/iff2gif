typedef UBYTE Masking;		/* Choice of masking technique. */

#define mskNone					0
#define mskHasMask				1
#define mskHasTransparentColor	2
#define mskLasso				3

typedef UBYTE Compression;	/* Choice of compression algorithm
	applied to the rows of all source and mask planes. "cmpByteRun1"
	is the byte run encoding described in Appendix C. Do not compress
	across rows! */
#define cmpNone			0
#define cmpByteRun1		1

/* defines used for Modes in Amiga viewports (and therefore CAMG chunks) */

#define GENLOCK_VIDEO	0x0002
#define LACE			0x0004
#define SUPERHIRES		0x0020
#define PFBA			0x0040
#define EXTRA_HALFBRITE	0x0080
#define GENLOCK_AUDIO	0x0100
#define DUALPF			0x0400
#define HAM				0x0800
#define EXTENDED_MODE	0x1000
#define VP_HIDE			0x2000
#define SPRITES			0x4000
#define HIRES			0x8000

typedef struct {
	UWORD		w, h;			/* raster width & height in pixels			*/
	WORD		x, y;			/* pixel position for this image			*/
	UBYTE		nPlanes;		/* # source bitplanes						*/
	Masking		masking;
	Compression	compression;
	UBYTE		pad1;			/* unused; ignore on read, write as 0		*/
	UWORD		transparentColor; /* transparent "color number" (sort of)	*/
	UBYTE		xAspect, yAspect; /* pixel aspect, a ratio width : height	*/
	WORD		pageWidth, pageHeight; /* source "page" size in pixels		*/
} BitmapHeader;

typedef struct {
	UBYTE red, green, blue;			/* color intensities 0..255 */
} ColorRegister;					/* size = 3 bytes			*/

typedef struct {
	UBYTE depth;		/* # bitplanes in the original source				*/
	UBYTE pad1;			/* unused; for consistency put 0 here				*/
	UWORD planePick;	/* how to scatter source bitplanes into destination	*/
	UWORD planeOnOff;	/* default bitplane data for planePick				*/
	UWORD planeMask;	/* selects which bitplanes to store into			*/
} Destmerge;

typedef UWORD SpritePrecedence;	/* relative precedence, 0 is the highest	*/

#define ID_FORM		MAKE_ID('F','O','R','M')
#define ID_ILBM		MAKE_ID('I','L','B','M')
#define ID_BMHD		MAKE_ID('B','M','H','D')
#define ID_CMAP		MAKE_ID('C','M','A','P')
#define ID_GRAB		MAKE_ID('G','R','A','B')
#define ID_DEST		MAKE_ID('D','E','S','T')
#define	ID_SPRT		MAKE_ID('S','P','R','T')
#define ID_CAMG		MAKE_ID('C','A','M','G')
#define ID_BODY		MAKE_ID('B','O','D','Y')
#define ID_ANNO		MAKE_ID('A','N','N','O')

/* values for AnimeHeader bits (mostly just for mode 4) */
#define ANIM_LONG_DATA	1	/* else short */
#define ANIM_XOR		2	/* else set */
#define ANIM_1INFOLIST	4	/* else separate info */
#define ANIM_RLC		8	/* else not RLC */
#define ANIM_VERT		16	/* else horizontal */
#define ANIM_LONGOFFS	32	/* else short offsets */

typedef struct {
	UBYTE	operation;	/* the compression method							*/	
	UBYTE	mask;		/* mode 1 only: plane mask where data is			*/
	UWORD	w, h;		/* mode 1 only: size of the changed area			*/
	WORD	x, y;		/* mode 1 only: position of the changed area		*/
	ULONG	abstime;	/* unused											*/
	ULONG	reltime;	/* jiffies (1/60 sec) to wait before flipping		*/
	UBYTE	interleave;	/* how many frames back this data is to modify		*/
	UBYTE	pad0;
	ULONG	bits;		/* option bits										*/
} AnimHeader;

typedef struct {
	UWORD version;		/* current version=4 */
	UWORD nframes;		/* number of frames in the animation.*/
	UBYTE speed;		/* speed in fps */
	UBYTE pad[3];		/* Not used */
} DPAnimChunk;

#define ID_ANIM		MAKE_ID('A','N','I','M')
#define ID_ANHD		MAKE_ID('A','N','H','D')
#define ID_DPAN		MAKE_ID('D','P','A','N')
#define ID_DLTA		MAKE_ID('D','L','T','A')
