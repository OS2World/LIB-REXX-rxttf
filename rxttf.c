
/***********************************************************************/
/*                                                                     */
/*   RxTTF - Copyright (C) 1999 Michal Necasek <mike@mendelu.cz>       */
/*           Specification - Daniel Hellerstein <danielh@econ.ag.gov>  */
/*                                                                     */
/*   This code is in the public domain                                 */
/*                                                                     */
/*   This code is based on the work of the FreeType Project            */
/*       http://www.freetype.org                                       */
/*                                                                     */
/***********************************************************************/

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define  INCL_REXXSAA
#include <os2.h>
#include <rexxsaa.h>

#include "freetype.h"

/* exported functions */
RexxFunctionHandler rxttf_image;


#define  INVALID_ROUTINE 40            /* Raise Rexx error           */
#define  VALID_ROUTINE    0            /* Successful completion      */

/* error codes */
#define  NO_UTIL_ERROR    "0"          /* No error whatsoever        */
#define  ERROR_NOMEM      "2"          /* Insufficient memory        */
#define  ERROR_FILEOPEN   "3"          /* Error opening text file    */
#define  ERROR_ENGINE     "4"          /* FreeType engine error      */
#define  ERROR_REXXVAR    "5"          /* Rexx variable pool error   */

#define  MAX            256        /* temporary buffer length        */
#define  IBUF_LEN       4096       /* Input buffer length            */

/*********************************************************************/
/* RxStemData                                                        */
/*   Structure which describes a generic                             */
/*   stem variable.                                                  */
/*********************************************************************/

typedef struct RxStemData {
    SHVBLOCK shvb;                     /* Request block for RxVar    */
    CHAR ibuf[IBUF_LEN];               /* Input buffer               */
    CHAR varname[MAX];                 /* Buffer for the variable    */
                                       /* name                       */
    CHAR stemname[MAX];                /* Buffer for the variable    */
                                       /* name                       */
    ULONG stemlen;                     /* Length of stem.            */
    ULONG vlen;                        /* Length of variable value   */
    ULONG j;                           /* Temp counter               */
    ULONG tlong;                       /* Temp counter               */
    ULONG count;                       /* Number of elements         */
                                       /* processed                  */
} RXSTEMDATA;

#define BUILDRXSTRING(t, s) { \
  strcpy((t)->strptr,(s));\
  (t)->strlength = strlen((s)); \
}

#define  Pi         3.1415926535

#define  MAXPTSIZE  500                 /* dtp */

char  Header[128];

TT_Engine    engine;
TT_Face      face;
TT_Instance  instance;
TT_Glyph     glyph;
TT_CharMap   char_map;

TT_Glyph_Metrics     metrics;
TT_Outline           outline;
TT_Face_Properties   properties;
TT_Instance_Metrics  imetrics;

int  num_glyphs;

int  ptsize;
int  hinted;

int            gray_render;
int            font_smoothing = FALSE;

short  glyph_code[1024];
int    num_codes;

/* the virtual palette */
unsigned char  virtual_palette[5] = { 0, 1, 2, 3, 4 };

/* Or-ing the possible palette values gets us from 0 to 7 */
/* We must bound check these...                           */
unsigned char  bounded_palette[8] = { 0, 1, 2, 3, 4, 4, 4, 4 };

/* The target bitmap or pixmap -- covering the full display window/screen */
TT_Raster_Map  Bit;

/* A smaller intermediate bitmap used to render individual glyphs when    */
/* font smoothing mode is activated.  It is then or-ed to `Bit'.          */
TT_Raster_Map  Small_Bit;

/* Clears the Small_Bit pixmap */
void  Clear_Small( void )
{
  memset( Small_Bit.bitmap, 0, Small_Bit.size );
}

/* Clears the Bit bitmap/pixmap */
void  Clear_Bit( void )
{
  memset( Bit.bitmap, 0, Bit.size );
}

TT_Error  Render_Single_Glyph( int       font_smoothing,
                               TT_Glyph  glyph,
                               int       x_offset,
                               int       y_offset )
{
  if ( !font_smoothing )
    return TT_Get_Glyph_Bitmap( glyph, &Bit,
                                (long)x_offset*64, (long)y_offset*64 );
  else
  {
    TT_Glyph_Metrics  metrics;

    TT_Error    error;
    TT_F26Dot6  x, y, xmin, ymin, xmax, ymax;
    int         ioff, iread;
    char        *off, *read, *_off, *_read;


    /* font-smoothing mode */

    /* we begin by grid-fitting the bounding box */
    TT_Get_Glyph_Metrics( glyph, &metrics );

    xmin = metrics.bbox.xMin & -64;
    ymin = metrics.bbox.yMin & -64;
    xmax = (metrics.bbox.xMax+63) & -64;
    ymax = (metrics.bbox.yMax+63) & -64;

    /* now render the glyph in the small pixmap */

    /* IMPORTANT NOTE: the offset parameters passed to the function     */
    /* TT_Get_Glyph_Bitmap() must be integer pixel values, i.e.,        */
    /* multiples of 64.  HINTING WILL BE RUINED IF THIS ISN'T THE CASE! */
    /* This is why we _did_ grid-fit the bounding box, especially xmin  */
    /* and ymin.                                                        */

    error = TT_Get_Glyph_Pixmap( glyph, &Small_Bit, -xmin, -ymin );
    if ( error )
      return error;

    /* Blit-or the resulting small pixmap into the biggest one */
    /* We do that by hand, and provide also clipping.          */

    xmin = (xmin >> 6) + x_offset;
    ymin = (ymin >> 6) + y_offset;
    xmax = (xmax >> 6) + x_offset;
    ymax = (ymax >> 6) + y_offset;

    /* Take care of comparing xmin and ymin with signed values!  */
    /* This was the cause of strange misplacements when Bit.rows */
    /* was unsigned.                                             */

    if ( xmin >= (int)Bit.width   ||
         ymin >= (int)Bit.rows    ||
         xmax < 0                 ||
         ymax < 0 )
      return TT_Err_Ok;  /* nothing to do */

    /* Note that the clipping check is performed _after_ rendering */
    /* the glyph in the small bitmap to let this function return   */
    /* potential error codes for all glyphs, even hidden ones.     */

    /* In exotic glyphs, the bounding box may be larger than the   */
    /* size of the small pixmap.  Take care of that here.          */

    if ( xmax-xmin + 1 > Small_Bit.width )
      xmax = xmin + Small_Bit.width - 1;

    if ( ymax-ymin + 1 > Small_Bit.rows )
      ymax = ymin + Small_Bit.rows - 1;

    /* set up clipping and cursors */

    iread = 0;
    if ( ymin < 0 )
    {
      iread -= ymin * Small_Bit.cols;
      ioff   = 0;
      ymin   = 0;
    }
    else
      ioff = ymin * Bit.cols;

    if ( ymax >= Bit.rows )
      ymax = Bit.rows-1;

    if ( xmin < 0 )
    {
      iread -= xmin;
      xmin   = 0;
    }
    else
      ioff += xmin;

    if ( xmax >= Bit.width )
      xmax = Bit.width - 1;

    _read = (char*)Small_Bit.bitmap + iread;
    _off  = (char*)Bit.bitmap       + ioff;

    for ( y = ymin; y <= ymax; y++ )
    {
      read = _read;
      off  = _off;

      for ( x = xmin; x <= xmax; x++ )
      {
        *off = bounded_palette[*off | *read];
        off++;
        read++;
      }
      _read += Small_Bit.cols;
      _off  += Bit.cols;
    }

    return TT_Err_Ok;
  }
}


/* Convert an ASCII string to a string of glyph indexes.              */
/*                                                                    */
/* IMPORTANT NOTE:                                                    */
/*                                                                    */
/* There is no portable way to convert from any system's char. code   */
/* to Unicode.  This function simply takes a char. string as argument */
/* and "interprets" each character as a Unicode char. index with no   */
/* further check.                                                     */
/*                                                                    */
/* This mapping is only valid for the ASCII character set (i.e.,      */
/* codes 32 to 127); all other codes (like accentuated characters)    */
/* will produce more or less random results, depending on the system  */
/* being run.                                                         */

static ULONG CharToUnicode( char*  source )
{
  unsigned short  i, n;
  unsigned short  platform, encoding;

  /* First, look for a Unicode charmap */

  n = properties.num_CharMaps;

  for ( i = 0; i < n; i++ )
  {
    TT_Get_CharMap_ID( face, i, &platform, &encoding );
    if ( (platform == 3 && encoding == 1 )  ||
         (platform == 0 && encoding == 0 ) )
    {
      TT_Get_CharMap( face, i, &char_map );
      i = n + 1;
    }
  }

  if ( i == n ) {
    return 1;   /* error - no Unicode cmap */
  }

  for ( n = 0; n < 1024 && source[n]; n++ )
    glyph_code[n] = TT_Char_Index( char_map, (short)source[n] );

#if 0
  /* Note, if you have a function, say ToUnicode(), to convert from     */
  /* char codes to Unicode, use the following line instead:             */

  glyph_code[n] = TT_Char_Index( char_map, ToUnicode( source[n] ) );
#endif

  num_codes = n;

  return 0;
}


static ULONG  Reset_Scale( int  pointSize )
{
  TT_Error  error;

  error = TT_Set_Instance_CharSize( instance, pointSize * 64 );
  if (error)
  {
    return 1; /* Error */
  }

  TT_Get_Instance_Metrics( instance, &imetrics );

  return 0;   /* OK */
}


static TT_Error  LoadTrueTypeChar( int  idx, int  hint )
{
  int  flags;


  flags = TTLOAD_SCALE_GLYPH;
  if ( hint )
    flags |= TTLOAD_HINT_GLYPH;

  return TT_Load_Glyph( instance, glyph, idx, flags );
}


static ULONG  Render_All( void )
{
  TT_F26Dot6  x, y, z, minx, miny, maxx, maxy;
  int         i, j;

  TT_Error    error;

  /* On the first pass, we compute the compound bounding box */

  x = y = 0;

  maxx = maxy = 0;
  minx = miny = 0xFFFF;

  for ( i = 0; i < num_codes; i++ )
  {
    if ( !(error = LoadTrueTypeChar( glyph_code[i], hinted )) )
    {
      TT_Get_Glyph_Metrics( glyph, &metrics );

      z = x + metrics.bbox.xMin;
      if ( minx > z )
        minx = z;

      z = x + metrics.bbox.yMax;
      if ( maxx < z )
        maxx = z;

      z = y + metrics.bbox.yMin;
      if ( miny > z )
        miny = z;

      z = y + metrics.bbox.yMax;
      if ( maxy < z )
        maxy = z;

      x += metrics.advance & -64;
    }
    else
      /* Fail++ */ ;
  }

  minx = ( minx & -64 ) >> 6;
  miny = ( miny & -64 ) >> 6;

  maxx = ( (maxx + 63) & -64 ) >> 6;
  maxy = ( (maxy + 63) & -64 ) >> 6;

  if (minx > 0)
    Bit.width = maxx + minx + 2;
  else
    Bit.width = maxx - minx + 2;

  Bit.rows  = maxy - miny;

  Bit.flow   = TT_Flow_Down;

  if ( font_smoothing )
    Bit.cols = (Bit.width+3) & -4;
  else
    Bit.cols = (Bit.width+7) >> 3;

  Bit.size   = (long)Bit.cols * Bit.rows;

  if ( Bit.bitmap )
    free( Bit.bitmap );
  Bit.bitmap = malloc( (int)Bit.size );
  if ( !Bit.bitmap )
    return 1;  /* Error */

  Clear_Bit();

  /* On the second pass, we render each glyph to its centered position. */
  /* This is slow, because we reload each glyph to render it!           */

  x = /*minx*/0;
  y = miny;

  for (i = 0; i < num_codes; i++)
  {
    if (!(error = LoadTrueTypeChar(glyph_code[i], hinted)))
    {
      TT_Get_Glyph_Metrics(glyph, &metrics);

      Render_Single_Glyph(gray_render, glyph, x - minx, -y);

      x += metrics.advance / 64;
    }
  }

  return 0;  /* OK */
}

/* Convert bitmap to REXX stem variable */
int Process_Bitmap(TT_Raster_Map  *Bit, RXSTEMDATA *ldp, RXSTRING *retstr) {
   int  i, j, k;
   char elem;

   ldp->vlen = Bit->width;
   ldp->shvb.shvnext = NULL;

   for (i = 0; i < Bit->rows; i++) {
      for (j = 0; j < Bit->cols; j++) {
         for (k = 7; k >= 0 ; k--) {
            elem = (((char *)(Bit->bitmap))[i * Bit->cols + j] >> k) & 0x01;
            ldp->ibuf[j * 8 + 7 - k] = elem ? 1 : 0;
         }
      }
      /* now create one 'line' of stem variable */
      sprintf(ldp->varname+ldp->stemlen, "%d", ldp->count);
      ldp->count++;

      ldp->shvb.shvname.strptr = ldp->varname;
      ldp->shvb.shvname.strlength = strlen(ldp->varname);
      ldp->shvb.shvnamelen = ldp->shvb.shvname.strlength;
      ldp->shvb.shvvalue.strptr = ldp->ibuf;
      ldp->shvb.shvvalue.strlength = ldp->vlen;
      ldp->shvb.shvvaluelen = ldp->vlen;
      ldp->shvb.shvcode = RXSHV_SET;
      ldp->shvb.shvret = 0;
      if (RexxVariablePool(&ldp->shvb) == RXSHV_BADN) {
         BUILDRXSTRING(retstr, ERROR_REXXVAR);
         return VALID_ROUTINE;        /* error on non-zero          */
      }
   }

                                     /* set stem.!rows to # of rows */
  sprintf(ldp->ibuf, "%d", Bit->rows);
  strcpy(&(ldp->varname[ldp->stemlen]), "!ROWS");
  ldp->shvb.shvnext = NULL;
  ldp->shvb.shvname.strptr = ldp->varname;
  ldp->shvb.shvname.strlength = strlen(ldp->varname);
  ldp->shvb.shvnamelen = strlen(ldp->varname);
  ldp->shvb.shvvalue.strptr = ldp->ibuf;
  ldp->shvb.shvvalue.strlength = strlen(ldp->ibuf);
  ldp->shvb.shvvaluelen = ldp->shvb.shvvalue.strlength;
  ldp->shvb.shvcode = RXSHV_SET;
  ldp->shvb.shvret = 0;
  if (RexxVariablePool(&ldp->shvb) == RXSHV_BADN) {
    BUILDRXSTRING(retstr, ERROR_REXXVAR);
    return VALID_ROUTINE;              /* error on non-zero          */
  }

                                  /* set stem.!cols to # of columns  */
  sprintf(ldp->ibuf, "%d", Bit->width);
  strcpy(&(ldp->varname[ldp->stemlen]), "!COLS");
  ldp->shvb.shvnext = NULL;
  ldp->shvb.shvname.strptr = ldp->varname;
  ldp->shvb.shvname.strlength = strlen(ldp->varname);
  ldp->shvb.shvnamelen = strlen(ldp->varname);
  ldp->shvb.shvvalue.strptr = ldp->ibuf;
  ldp->shvb.shvvalue.strlength = strlen(ldp->ibuf);
  ldp->shvb.shvvaluelen = ldp->shvb.shvvalue.strlength;
  ldp->shvb.shvcode = RXSHV_SET;
  ldp->shvb.shvret = 0;
  if (RexxVariablePool(&ldp->shvb) == RXSHV_BADN) {
    BUILDRXSTRING(retstr, ERROR_REXXVAR);
    return VALID_ROUTINE;              /* error on non-zero          */
  }

  return VALID_ROUTINE;                /* no error on call           */
}

/*  int  rxttf_image( int  argc, char**  argv )*/
ULONG rxttf_image(CHAR *name, ULONG numargs, RXSTRING args[],
                  CHAR *queuename, RXSTRING *retstr)
{
  int    i;
  int    XisSetup = 0;
  char   filename[128 + 4];
  char   *inputString;
  int    option;
  int    res = 96;

  TT_Error  error;

  RXSTEMDATA ldp;

  BUILDRXSTRING(retstr, NO_UTIL_ERROR); /* pass back result          */

  /* check  arguments */
  if (numargs !=  4 ||
      !RXVALIDSTRING(args[0]) ||
      !RXVALIDSTRING(args[1]) ||
      !RXVALIDSTRING(args[2]) ||
      !RXVALIDSTRING(args[3]))
    return INVALID_ROUTINE;            /* raise an error             */

  inputString = args[0].strptr;
  strncpy(filename, args[1].strptr, 128);
  ptsize = atoi(args[2].strptr); /* #### add error checking */

                                       /* Initialize data area       */
  ldp.count = 0;
  strcpy(ldp.varname, args[3].strptr);
  ldp.stemlen = args[3].strlength;
  strupr(ldp.varname);                 /* uppercase the name         */

  if (ldp.varname[ldp.stemlen-1] != '.')
    ldp.varname[ldp.stemlen++] = '.';

  /* Initialize engine */
  error = TT_Init_FreeType( &engine );
  if ( error ) {
    BUILDRXSTRING(retstr, ERROR_ENGINE);
    return VALID_ROUTINE;
  }

  hinted = 1;

  filename[128] = '\0';

  /* Load typeface */
  error = TT_Open_Face( engine, filename, &face );

  if ( error == TT_Err_Could_Not_Open_File ) {
    BUILDRXSTRING(retstr, ERROR_ENGINE);
    return VALID_ROUTINE;
  }
  else if (error) {
    BUILDRXSTRING(retstr, ERROR_FILEOPEN);
    return VALID_ROUTINE;
  }

  /* get face properties and allocate preload arrays */
  TT_Get_Face_Properties( face, &properties );

  num_glyphs = properties.num_Glyphs;

  /* create glyph */
  error = TT_New_Glyph( face, &glyph );
  if ( error ) {
    BUILDRXSTRING(retstr, ERROR_ENGINE);
    return VALID_ROUTINE;
  }

  /* create instance */
  error = TT_New_Instance( face, &instance );
  if ( error ) {
    BUILDRXSTRING(retstr, ERROR_ENGINE);
    return VALID_ROUTINE;
  }

  error = TT_Set_Instance_Resolutions( instance, res, res );
  if ( error ) {
    BUILDRXSTRING(retstr, ERROR_ENGINE);
    return VALID_ROUTINE;
  }

  if ( !XisSetup )
  {
    XisSetup = 1;

    if ( gray_render )
    {
      TT_Set_Raster_Gray_Palette( engine, virtual_palette );
    }
  }

  if (Reset_Scale( ptsize )) {
    BUILDRXSTRING(retstr, ERROR_ENGINE);
    return VALID_ROUTINE;
  }

  if (CharToUnicode(inputString)) {
    BUILDRXSTRING(retstr, ERROR_ENGINE);
    return VALID_ROUTINE;
  }

  if (Render_All()) {
    BUILDRXSTRING(retstr, ERROR_ENGINE);
    return VALID_ROUTINE;
  }

  TT_Done_FreeType( engine );

  return Process_Bitmap(&Bit, &ldp, retstr);

}

