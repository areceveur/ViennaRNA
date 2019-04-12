/*
 *                    Heat Capacity of RNA molecule
 *
 *                  c Ivo Hofacker and Peter Stadler
 *                        Vienna RNA package
 *
 *
 *          calculates specific heat using C = - T d^2/dT^2 G(T)
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <unistd.h>
#include "ViennaRNA/utils/basic.h"
#include "ViennaRNA/utils/strings.h"
#include "ViennaRNA/fold_vars.h"
#include "ViennaRNA/params/basic.h"
#include "ViennaRNA/fold.h"
#include "ViennaRNA/part_func.h"
#include "ViennaRNA/params/io.h"
#include "ViennaRNA/io/file_formats.h"
#include "RNAheat_cmdl.h"
#include "gengetopt_helper.h"
#include "input_id_helpers.h"

#include "ViennaRNA/color_output.inc"


#define MAXWIDTH  201

PRIVATE float
ddiff(float f[],
      float h,
      int   m);


PRIVATE void
heat_capacity(vrna_fold_compound_t  *fc,
              float                 T_min,
              float                 T_max,
              float                 h,
              int                   m);


int
main(int  argc,
     char *argv[])
{
  struct RNAheat_args_info  args_info;
  char                      *ns_bases, *c, *ParamFile, *rec_sequence, *rec_id, **rec_rest,
                            *orig_sequence;
  unsigned int              rec_type, read_opt;
  int                       i, length, sym, mpoints, istty, noconv, filename_full;
  float                     T_min, T_max, h;
  dataset_id                id_control;
  vrna_md_t                 md;

  ParamFile     = ns_bases = NULL;
  T_min         = 0.;
  T_max         = 100.;
  h             = 1;
  mpoints       = 2;
  noconv        = 0;
  dangles       = 2; /* dangles can be 0 (no dangles) or 2, default is 2 */
  rec_type      = read_opt = 0;
  rec_id        = rec_sequence = orig_sequence = NULL;
  rec_rest      = NULL;
  filename_full = 0;

  /*
   #############################################
   # check the command line parameters
   #############################################
   */
  if (RNAheat_cmdline_parser(argc, argv, &args_info) != 0)
    exit(1);

  /* parse options for ID manipulation */
  ggo_get_id_control(args_info, id_control, "Sequence", "sequence", "_", 4, 1);

  /* do not take special tetra loop energies into account */
  if (args_info.noTetra_given)
    tetra_loop = 0;

  /* set dangle model */
  if (args_info.dangles_given) {
    if ((args_info.dangles_arg != 0) && (args_info.dangles_arg != 2))
      vrna_message_warning(
        "required dangle model not implemented, falling back to default dangles=2");
    else
      dangles = args_info.dangles_arg;
  }

  /* do not allow weak pairs */
  if (args_info.noLP_given)
    noLonelyPairs = 1;

  /* do not allow wobble pairs (GU) */
  if (args_info.noGU_given)
    noGU = 1;

  /* do not allow weak closing pairs (AU,GU) */
  if (args_info.noClosingGU_given)
    no_closingGU = 1;

  /* do not convert DNA nucleotide "T" to appropriate RNA "U" */
  if (args_info.noconv_given)
    noconv = 1;

  /* set energy model */
  if (args_info.energyModel_given)
    energy_set = args_info.energyModel_arg;

  /* take another energy parameter set */
  if (args_info.paramFile_given)
    ParamFile = strdup(args_info.paramFile_arg);

  /* Allow other pairs in addition to the usual AU,GC,and GU pairs */
  if (args_info.nsp_given)
    ns_bases = strdup(args_info.nsp_arg);

  ggo_get_gquad(args_info, gquad);

  /* Tmin */
  if (args_info.Tmin_given)
    T_min = args_info.Tmin_arg;

  /* Tmax */
  if (args_info.Tmax_given)
    T_max = args_info.Tmax_arg;

  /* step size */
  if (args_info.stepsize_given)
    h = args_info.stepsize_arg;

  /* ipoints */
  if (args_info.ipoints_given) {
    mpoints = args_info.ipoints_arg;
    if (mpoints < 1)
      mpoints = 1;

    if (mpoints > 100)
      mpoints = 100;
  }

  /* free allocated memory of command line data structure */
  RNAheat_cmdline_parser_free(&args_info);

  /*
   #############################################
   # begin initializing
   #############################################
   */
  if (ParamFile != NULL)
    read_parameter_file(ParamFile);

  if (ns_bases != NULL) {
    nonstandards  = vrna_alloc(33);
    c             = ns_bases;
    i             = sym = 0;
    if (*c == '-') {
      sym = 1;
      c++;
    }

    while (*c != '\0') {
      if (*c != ',') {
        nonstandards[i++] = *c++;
        nonstandards[i++] = *c;
        if ((sym) && (*c != *(c - 1))) {
          nonstandards[i++] = *c;
          nonstandards[i++] = *(c - 1);
        }
      }

      c++;
    }
  }

  set_model_details(&md);

  md.backtrack    = 0;
  md.compute_bpp  = 0;

  istty = isatty(fileno(stdout)) && isatty(fileno(stdin));

  read_opt |= VRNA_INPUT_NO_REST;
  if (istty) {
    vrna_message_input_seq_simple();
    read_opt |= VRNA_INPUT_NOSKIP_BLANK_LINES;
  }

  /*
   #############################################
   # main loop: continue until end of file
   #############################################
   */
  while (
    !((rec_type = vrna_file_fasta_read_record(&rec_id, &rec_sequence, &rec_rest, NULL, read_opt))
      & (VRNA_INPUT_ERROR | VRNA_INPUT_QUIT))) {
    vrna_fold_compound_t  *fc;

    char                  *SEQ_ID = NULL;
    /*
     ########################################################
     # init everything according to the data we've read
     ########################################################
     */
    if (rec_id) /* remove '>' from FASTA header */
      rec_id = memmove(rec_id, rec_id + 1, strlen(rec_id));

    /* construct the sequence ID */
    set_next_id(&rec_id, id_control);
    SEQ_ID = fileprefix_from_id(rec_id, id_control, filename_full);

    /* convert DNA alphabet to RNA if not explicitely switched off */
    if (!noconv)
      vrna_seq_toRNA(rec_sequence);

    /* store case-unmodified sequence */
    orig_sequence = strdup(rec_sequence);
    /* convert sequence to uppercase letters only */
    vrna_seq_toupper(rec_sequence);

    if (!istty)
      print_fasta_header(stdout, rec_id);

    length = (int)strlen(rec_sequence);

    if (istty)
      vrna_message_info(stdout, "length = %d", length);

    /*
     ########################################################
     # done with 'stdin' handling
     ########################################################
     */

    fc = vrna_fold_compound(rec_sequence, &md, VRNA_OPTION_DEFAULT);

    heat_capacity(fc, T_min, T_max, h, mpoints);
    (void)fflush(stdout);

    vrna_fold_compound_free(fc);

    /* clean up */
    free(rec_id);
    free(SEQ_ID);
    free(rec_sequence);
    free(orig_sequence);
    rec_id    = rec_sequence = orig_sequence = NULL;
    rec_rest  = NULL;

    /* print user help for the next round if we get input from tty */
    if (istty)
      vrna_message_input_seq_simple();
  }

  free_id_data(id_control);

  return EXIT_SUCCESS;
}


PRIVATE void
heat_capacity(vrna_fold_compound_t  *fc,
              float                 T_min,
              float                 T_max,
              float                 h,
              int                   m)
{
  int       length, i;
  float     hc;
  float     F[MAXWIDTH];
  double    min_en;

  length = (int)fc->length;

  vrna_md_t md = fc->params->model_details;

  /* required for vrna_exp_param_rescale() in subsequent calls */
  md.sfact = 1.;

  md.temperature = T_min - m * h;
  vrna_params_reset(fc, &md);

  /* initialize_fold(length); <- obsolete */
  min_en  = (double)vrna_mfe(fc, NULL);
  min_en  *= md.sfact;

  vrna_exp_params_rescale(fc, &min_en);

  for (i = 0; i < 2 * m + 1; i++) {
    F[i] = vrna_pf(fc, NULL);
    /* increase temperature */
    md.temperature += h;
    /* reset all energy parameters according to temperature changes */
    vrna_params_reset(fc, &md);

    min_en = F[i] + h * 0.00727 * length;

    vrna_exp_params_rescale(fc, &min_en);
  }

  while (md.temperature <= (T_max + m * h + h)) {
    hc = -ddiff(F, h, m) * (md.temperature + K0 - m * h - h);
    char *tline = vrna_strdup_printf("%g\t%g", (md.temperature - m * h - h), hc);
    print_table(stdout, NULL, tline);
    free(tline);

    for (i = 0; i < 2 * m; i++)
      F[i] = F[i + 1];

    F[2 * m] = vrna_pf(fc, NULL);

    /*       printf("%g\n", F[2*m]);*/
    md.temperature += h;

    vrna_params_reset(fc, &md);

    min_en = F[i] + h * 0.00727 * length;

    vrna_exp_params_rescale(fc, &min_en);
  }
}


/* ------------------------------------------------------------------------- */

PRIVATE float
ddiff(float f[],
      float h,
      int   m)
{
  float fp;
  int   i;
  float A, B;

  A = (float)(m * (m + 1) * (2 * m + 1) / 3);                                     /* 2*sum(x^2) */
  B = (float)(m * (m + 1) * (2 * m + 1)) * (float)(3 * m * m + 3 * m - 1) / 15.;  /* 2*sum(x^4) */

  fp = 0.;
  for (i = 0; i < 2 * m + 1; i++)
    fp += f[i] * (A - (float)((2 * m + 1) * (i - m) * (i - m)));

  fp /= ((A * A - B * ((float)(2 * m + 1))) * h * h / 2.);
  return (float)fp;
}
