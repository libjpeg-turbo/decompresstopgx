/*
 * Copyright (C)2017 D. R. Commander.  All Rights Reserved.
 * Copyright (C)2017 Fraunhofer IIS.  All Rights Reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * - Redistributions of source code must retain the above copyright notice,
 *   this list of conditions and the following disclaimer.
 * - Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the documentation
 *   and/or other materials provided with the distribution.
 * - Neither the name of the libjpeg-turbo Project nor the names of its
 *   contributors may be used to endorse or promote products derived from this
 *   software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS",
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT HOLDERS OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/* This program uses the libjpeg API to save the YCbCr planes from an arbitrary
   JPEG file into a PGX file set (used in JPEG compliance testing.) */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <jpeglib.h>

#define THROW(msg) { \
  printf("ERROR in line %d:\n%s\n", __LINE__, msg); \
  retval = -1;  goto bailout; \
}

#define DSTATE_START  200

#define PAD(v, p)  ((v + (p) - 1) & (~((p) - 1)))

int main(int argc, char **argv)
{
  struct jpeg_error_mgr jerr;
  struct jpeg_decompress_struct dinfo;
  unsigned char *jpegBuf = NULL;
  FILE *file = NULL;
  int retval = 0, i, pw[MAX_COMPONENTS], ph[MAX_COMPONENTS], row;
  /* The real component dimensions after clipping, using the formulae in
     A.1.1 */
  int rw[MAX_COMPONENTS], rh[MAX_COMPONENTS];
  long jpegSize = 0;
  JSAMPROW *outbuf[MAX_COMPONENTS];
  JSAMPLE *plane[MAX_COMPONENTS];
  J12SAMPROW *outbuf12[MAX_COMPONENTS];
  J12SAMPLE *plane12[MAX_COMPONENTS];
  char filename[256];
  FILE *pgxheader = NULL, *pgxfiles[MAX_COMPONENTS] = { NULL };

  if (argc < 3) {
    printf("USAGE: %s <JPEG file> <output file (.pgx)\n", argv[0]);
    return 1;
  }

  for (i = 0; i < MAX_COMPONENTS; i++) {
    outbuf[i] = NULL;  plane[i] = NULL;
  }
  jpeg_create_decompress(&dinfo);

  if ((file = fopen(argv[1], "rb")) == NULL)
    THROW(strerror(errno));
  if (fseek(file, 0, SEEK_END) < 0)
    THROW(strerror(errno));
  if ((jpegSize = ftell(file)) < 0)
    THROW(strerror(errno));
  if (jpegSize < 1)
    THROW("Input file contains no data");
  if (fseek(file, 0, SEEK_SET) < 0)
    THROW(strerror(errno));
  if ((jpegBuf = (unsigned char *)malloc(jpegSize)) == NULL)
    THROW(strerror(errno));
  if (fread(jpegBuf, jpegSize, 1, file) < 1)
    THROW(strerror(errno));
  fclose(file);  file = NULL;

  dinfo.err = jpeg_std_error(&jerr);
  jpeg_mem_src(&dinfo, jpegBuf, jpegSize);
  jpeg_read_header(&dinfo, TRUE);
  jpeg_calc_output_dimensions(&dinfo);

  if (!(pgxheader = fopen(argv[2], "wb")))
    THROW(strerror(errno));

  printf("Image dimensions: %ux%u pixels\n", dinfo.output_width,
    dinfo.output_height);
  /* For the purpose of reference testing, we need the *precise* output sample
   * dimensions and not the dimensions rounded up to the nearest multiple of
   * the DCT block size.  For that, we need to know the maximum sampling
   * factors.
   */
  printf("Maximum sampling factors: %dx%d\n",
         dinfo.max_h_samp_factor, dinfo.max_v_samp_factor);

  for (i = 0; i < dinfo.num_components; i++) {
    jpeg_component_info *compptr = &dinfo.comp_info[i];
    FILE *hdr;

    pw[i] = compptr->width_in_blocks * DCTSIZE;
    ph[i] = compptr->height_in_blocks * DCTSIZE;
    if (dinfo.data_precision == 12) {
      if ((plane12[i] = (J12SAMPLE *)malloc(sizeof(J12SAMPLE) * pw[i] *
                                            ph[i])) == NULL)
        THROW(strerror(errno));
      if ((outbuf12[i] = (J12SAMPROW *)malloc(sizeof(J12SAMPROW) *
                                              ph[i])) == NULL)
        THROW(strerror(errno));
    } else if (dinfo.data_precision == 8) {
      if ((plane[i] = (JSAMPLE *)malloc(sizeof(JSAMPLE) * pw[i] *
                                        ph[i])) == NULL)
        THROW(strerror(errno));
      if ((outbuf[i] = (JSAMPROW *)malloc(sizeof(JSAMPROW) * ph[i])) == NULL)
        THROW(strerror(errno));
    } else
      THROW("Unsupported data precision");
    for (row = 0; row < ph[i]; row++) {
      if (dinfo.data_precision == 12)
        outbuf12[i][row] = &plane12[i][row * pw[i]];
      else
        outbuf[i][row] = &plane[i][row * pw[i]];
    }

    /* Compute the component dimensions following A.1.1 */
    rw[i] = (dinfo.output_width  * compptr->h_samp_factor +
             dinfo.max_h_samp_factor - 1) / dinfo.max_h_samp_factor;
    rh[i] = (dinfo.output_height * compptr->v_samp_factor +
             dinfo.max_v_samp_factor - 1) / dinfo.max_v_samp_factor;

    printf("  Component %d: %dx%d samples, %dx%d pixels (sampling factor: %dx%d)\n", i,
           pw[i], ph[i], rw[i], rh[i],
           compptr->h_samp_factor, compptr->v_samp_factor);

    snprintf(filename, sizeof(filename), "%s_%d.h", argv[2], i);
    if (!(hdr = fopen(filename, "w")))
      THROW(strerror(errno));
    fprintf(hdr, "PG ML +%d %d %d\n", dinfo.data_precision, rw[i], rh[i]);
    fclose(hdr);
    snprintf(filename, sizeof(filename), "%s_%d.raw", argv[2], i);
    fprintf(pgxheader, "%s\n", filename);
    if (!(pgxfiles[i] = fopen(filename, "wb")))
      THROW(strerror(errno));
  }

  dinfo.raw_data_out = TRUE;
  jpeg_start_decompress(&dinfo);
  for (row = 0; row < (int)dinfo.output_height;
       row += dinfo.max_v_samp_factor * DCTSIZE) {
    JSAMPARRAY yuvptr[MAX_COMPONENTS];
    J12SAMPARRAY yuvptr12[MAX_COMPONENTS];

    for (i = 0; i < dinfo.num_components; i++) {
      jpeg_component_info *compptr = &dinfo.comp_info[i];

      if (dinfo.data_precision == 12)
        yuvptr12[i] = &outbuf12[i][row * compptr->v_samp_factor /
                                   dinfo.max_v_samp_factor];
      else
        yuvptr[i] = &outbuf[i][row * compptr->v_samp_factor /
                               dinfo.max_v_samp_factor];
    }
    if (dinfo.data_precision == 12)
      jpeg12_read_raw_data(&dinfo, yuvptr12,
                           dinfo.max_v_samp_factor * DCTSIZE);
    else
      jpeg_read_raw_data(&dinfo, yuvptr, dinfo.max_v_samp_factor * DCTSIZE);
  }
  jpeg_finish_decompress(&dinfo);

  for (i = 0; i < dinfo.num_components; i++) {
    int y;
    /* At the edges, extra data is included in the DCT blocks we do not want to
     * compare.
     */
    for (y = 0; y < rh[i]; y++) {
      JSAMPLE *data;
      J12SAMPLE *data12;

      if (dinfo.data_precision == 12) {
        data12 = plane12[i] + pw[i] * y;
        if (fwrite(data12, rw[i], 2, pgxfiles[i]) < 1)
          THROW(strerror(errno));
      } else {
        data = plane[i] + pw[i] * y;
        if (fwrite(data, rw[i], 1, pgxfiles[i]) < 1)
          THROW(strerror(errno));
      }
    }
  }

  bailout:
  if (dinfo.global_state > DSTATE_START) jpeg_abort_decompress(&dinfo);
  free(jpegBuf);
  if (file) fclose(file);
  if (pgxheader) fclose(pgxheader);
  for (i = 0; i < MAX_COMPONENTS; i++) {
    free(outbuf[i]);
    free(plane[i]);
    if (pgxfiles[i]) fclose(pgxfiles[i]);
  }

  return retval;
}
