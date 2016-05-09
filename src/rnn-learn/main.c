/*
    Copyright (c) 2011, Jun Namikawa <jnamika@gmail.com>

    Permission to use, copy, modify, and/or distribute this software for any
    purpose with or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
    MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>
#include <stdio.h>
#include <limits.h>
#include <math.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#ifdef ENABLE_MTRACE
#include <mcheck.h>
#endif

#include "parameter.h"
#include "utils.h"
#include "main.h"
#include "target.h"
#include "parse.h"
#include "training.h"


#define TO_STRING_I(s) #s
#define TO_STRING(s) TO_STRING_I(s)

static void display_help (void)
{
    puts("rnn-learn  - an implementation of a gradient-based learning "
            "algorithm for recurrent neural networks focusing on the problem "
            "of time-series prediction and generation");
    puts("");
    puts("Usage: rnn-learn [-s seed] [-n neurons] [-t time-constant] "
            "[-d feedback-delay] [-k output-type] [-e epochs] "
            "[-l log-interval] [-x learning-rate] [-m momentum] [-a] "
            "[-p prior-strength] [-i input-file] [-o output-file] "
            "[-c config-file] [-V] [target-file ...]");
    puts("Usage: rnn-learn [-v] [-h]");
    puts("");
    puts("Available options are:");
    puts("-s seed");
    puts("    `seed' is the seed for the initialization of random number "
            "generator, which specifies a starting point for the random number "
            "sequence, and provides for restarting at the same point. If this "
            "option is omitted, the current system time is used.");
    puts("-n neurons");
    puts("    Number of context neurons in recurrent a neural network. "
            "Default is " TO_STRING(C_STATE_SIZE) ".");
    puts("-r representative points");
    puts("    Number of representative points of initial state. "
            "Default is " TO_STRING(REP_INIT_SIZE) ".");
    puts("-t time-constant");
    puts("    Time constant for each neuron primarily determines the time "
            "scale of the activation dynamics of the neuron. Default is "
            TO_STRING(INIT_TAU) ".");
    puts("-d feedback-delay");
    puts("    Time delay in a self-feedback from output to input. This "
            "parameter also means that a model learns to predict future values "
            "of the time series from past values with the delay interval. "
            "Default is " TO_STRING(DELAY_LENGTH) ".");
    puts("-k output-type");
    puts("    Type of output function");
    puts("        0: tanh (Default)");
    puts("        1: softmax activation function");
    puts("-e epochs");
    puts("    Number of the training iterations to optimize model parameters. "
            "Default is " TO_STRING(EPOCH_SIZE) ".");
    puts("-l log-interval");
    puts("    `log-interval' is the learning step between data samples being "
            "logged. Default is " TO_STRING(PRINT_INTERVAL) ".");
    puts("-x learning-rate");
    puts("    Learning rate for the gradient-descent method. "
            "Default is " TO_STRING(RHO) ".");
    puts("-m momentum");
    puts("    Learning momentum for the gradient-descent method. "
            "Default is " TO_STRING(MOMENTUM) ".");
    puts("-a");
    puts("    Use an algorithm to update the learning rate adaptively with "
            "respect to the error.");
    puts("-p prior-strength");
    puts("    Effect of the normal prior distribution over the parameter "
            "space. Default is " TO_STRING(PRIOR_STRENGTH)". (Note: setting "
            "`prior-strength' <= 0.001 is recommended.)");
    puts("-i input-file");
    puts("    The input file `input-file' being generated by rnn-learn is used "
            "for the initialization of model parameters. If there are no "
            "target-files named on the command line, rnn-learn restarts "
            "training at the end of previous trial. Otherwise rnn-learn "
            "resets training examples by means of the files.");
    puts("-o output-file");
    puts("    The optimized model parameters are written to `output-file'. "
            "Default is `" SAVE_FILENAME "'.");
    puts("-c config-file");
    puts("    The configuration file `config-file' is read.");
    puts("-V");
    puts("    Verbose.");
    puts("-v");
    puts("    Prints the version information and exit.");
    puts("-h");
    puts("    Prints this help and exit.");
    puts("");
    puts("Program execution:");
    puts("rnn-learn first reads parameters from the command line, or from a "
            "configuration file. Next, rnn-learn proceeds to read each "
            "target-file which contains training examples. If there are no "
            "target-files named on the command line, rnn-learn reads the "
            "standard input. After all the input from target-files is "
            "exhausted, rnn-learn executes training iterations.");
    puts("");
    puts("Target-file format which rnn-learn can recognize:");
    puts("In a target-file, the data columns are separated by a space, tab or "
            "comma. The number of data columns means the dimension of time "
            "series of training examples. Comments begin at a sign \"#\" and "
            "continue to the end of the line. If data are separated by a blank "
            "line, each data block is recognized as a different time series.");
}

static void display_version (void)
{
    printf("rnn-learn version %s\n", TO_STRING(VERSION));
}


static char* salloc (
        char *dst,
        const char *src)
{
    dst = realloc(dst, strlen(src) + 1);
    if (dst == NULL) {
        print_error_msg("cannot copy string: %s", src);
        exit(EXIT_FAILURE);
    }
    strcpy(dst, src);
    return dst;
}


static void init_parameters (struct general_parameters *gp)
{
    // 0 < seed < 4294967296
    gp->mp.seed = (((unsigned long)(time(NULL) * getpid())) % 4294967295) + 1;
    gp->mp.epoch_size = EPOCH_SIZE;
    gp->mp.use_adaptive_lr = 0;
    gp->mp.rho = RHO;
    gp->mp.momentum = MOMENTUM;
    gp->mp.c_state_size = C_STATE_SIZE;
    gp->mp.rep_init_size = REP_INIT_SIZE;
    gp->mp.delay_length = DELAY_LENGTH;
    gp->mp.output_type = OUTPUT_TYPE;
    gp->mp.fixed_weight = 0;
    gp->mp.fixed_threshold = 0;
    gp->mp.fixed_tau = 0;
    gp->mp.fixed_init_c_state = 0;
    gp->mp.connection_i2c = salloc(NULL, "-t-");
    gp->mp.connection_c2c = salloc(NULL, "-t-");
    gp->mp.connection_c2o = salloc(NULL, "-t-");
    gp->mp.connection_c2v = salloc(NULL, "-t-");
    gp->mp.const_init_c = salloc(NULL, "");
    gp->mp.softmax_group = salloc(NULL, "");
    gp->mp.init_tau = salloc(NULL, TO_STRING(INIT_TAU));
    gp->mp.prior_strength = PRIOR_STRENGTH;
    gp->mp.rep_init_variance = REP_INIT_VARIANCE;
    gp->mp.lambda = LAMBDA;
    gp->mp.alpha = ALPHA;

    gp->ap.truncate_length = TRUNCATE_LENGTH;
    gp->ap.block_length = BLOCK_LENGTH;
    gp->ap.divide_num = DIVIDE_NUM;
    gp->ap.lyapunov_spectrum_size = LYAPUNOV_SPECTRUM_SIZE;
    gp->ap.threshold_period = THRESHOLD_PERIOD;

    gp->iop.state_filename = salloc(NULL, STATE_FILENAME);
    gp->iop.closed_state_filename = salloc(NULL, CLOSED_STATE_FILENAME);
    gp->iop.weight_filename = salloc(NULL, WEIGHT_FILENAME);
    gp->iop.threshold_filename = salloc(NULL, THRESHOLD_FILENAME);
    gp->iop.tau_filename = salloc(NULL, TAU_FILENAME);
    gp->iop.init_filename = salloc(NULL, INIT_FILENAME);
    gp->iop.rep_init_filename = salloc(NULL, REP_INIT_FILENAME);
    gp->iop.adapt_lr_filename = salloc(NULL, ADAPT_LR_FILENAME);
    gp->iop.error_filename = salloc(NULL, ERROR_FILENAME);
    gp->iop.closed_error_filename = salloc(NULL, CLOSED_ERROR_FILENAME);
    gp->iop.lyapunov_filename = salloc(NULL, LYAPUNOV_FILENAME);
    gp->iop.entropy_filename = salloc(NULL, ENTROPY_FILENAME);
    gp->iop.period_filename = salloc(NULL, PERIOD_FILENAME);
    gp->iop.save_filename = salloc(NULL, SAVE_FILENAME);
    gp->iop.load_filename = salloc(NULL, LOAD_FILENAME);
    struct print_interval default_interval = {
        .interval = PRINT_INTERVAL,
        .init = 0,
        .end = LONG_MAX,
        .use_logscale_interval = 0,
    };
    gp->iop.default_interval = default_interval;
    gp->iop.interval_for_state_file = default_interval;
    gp->iop.interval_for_closed_state_file = default_interval;
    gp->iop.interval_for_weight_file = default_interval;
    gp->iop.interval_for_threshold_file = default_interval;
    gp->iop.interval_for_tau_file = default_interval;
    gp->iop.interval_for_init_file = default_interval;
    gp->iop.interval_for_rep_init_file = default_interval;
    gp->iop.interval_for_adapt_lr_file = default_interval;
    gp->iop.interval_for_error_file = default_interval;
    gp->iop.interval_for_closed_error_file = default_interval;
    gp->iop.interval_for_lyapunov_file = default_interval;
    gp->iop.interval_for_entropy_file = default_interval;
    gp->iop.interval_for_period_file = default_interval;
    gp->iop.verbose = 0;
}

static void free_parameters (struct general_parameters *gp)
{
    if (strlen(gp->iop.load_filename) == 0) {
        FREE2(gp->inp.has_connection_ci);
        FREE2(gp->inp.has_connection_cc);
        FREE2(gp->inp.has_connection_oc);
        FREE2(gp->inp.has_connection_vc);
        FREE2(gp->inp.connectivity_ci);
        FREE2(gp->inp.connectivity_cc);
        FREE2(gp->inp.connectivity_oc);
        FREE2(gp->inp.connectivity_vc);
        FREE(gp->inp.const_init_c);
        FREE(gp->inp.softmax_group_id);
        FREE(gp->inp.init_tau);
    }
    FREE(gp->mp.connection_i2c);
    FREE(gp->mp.connection_c2c);
    FREE(gp->mp.connection_c2o);
    FREE(gp->mp.connection_c2v);
    FREE(gp->mp.const_init_c);
    FREE(gp->mp.softmax_group);
    FREE(gp->mp.init_tau);
    FREE(gp->iop.state_filename);
    FREE(gp->iop.closed_state_filename);
    FREE(gp->iop.weight_filename);
    FREE(gp->iop.threshold_filename);
    FREE(gp->iop.tau_filename);
    FREE(gp->iop.init_filename);
    FREE(gp->iop.rep_init_filename);
    FREE(gp->iop.adapt_lr_filename);
    FREE(gp->iop.error_filename);
    FREE(gp->iop.closed_error_filename);
    FREE(gp->iop.lyapunov_filename);
    FREE(gp->iop.entropy_filename);
    FREE(gp->iop.period_filename);
    FREE(gp->iop.save_filename);
    FREE(gp->iop.load_filename);
}

static void set_seed (const char *opt, struct general_parameters *gp)
{
    gp->mp.seed = strtoul(opt, NULL, 0);
}

static void set_epoch_size (const char *opt, struct general_parameters *gp)
{
    gp->mp.epoch_size = atol(opt);
}

static void set_use_adaptive_lr (const char *opt, struct general_parameters *gp)
{
    gp->mp.use_adaptive_lr = 1;
}

static void set_rho (const char *opt, struct general_parameters *gp)
{
    gp->mp.rho = atof(opt);
}

static void set_momentum (const char *opt, struct general_parameters *gp)
{
    gp->mp.momentum = atof(opt);
}

static void set_c_state_size (const char *opt, struct general_parameters *gp)
{
    gp->mp.c_state_size = atoi(opt);
}

static void set_rep_init_size (const char *opt, struct general_parameters *gp)
{
    gp->mp.rep_init_size = atoi(opt);
}

static void set_delay_length (const char *opt, struct general_parameters *gp)
{
    gp->mp.delay_length = atoi(opt);
}

static void set_output_type (const char *opt, struct general_parameters *gp)
{
    gp->mp.output_type = atoi(opt);
}

static void set_fixed_weight (const char *opt, struct general_parameters *gp)
{
    gp->mp.fixed_weight = 1;
}

static void set_fixed_threshold (const char *opt, struct general_parameters *gp)
{
    gp->mp.fixed_threshold = 1;
}

static void set_fixed_tau (const char *opt, struct general_parameters *gp)
{
    gp->mp.fixed_tau = 1;
}

static void set_fixed_init_c_state (
        const char *opt,
        struct general_parameters *gp)
{
    gp->mp.fixed_init_c_state = 1;
}

static void set_connection_i2c (const char *opt, struct general_parameters *gp)
{
    gp->mp.connection_i2c = salloc(gp->mp.connection_i2c, opt);
}

static void set_connection_c2c (const char *opt, struct general_parameters *gp)
{
    gp->mp.connection_c2c = salloc(gp->mp.connection_c2c, opt);
}

static void set_connection_c2o (const char *opt, struct general_parameters *gp)
{
    gp->mp.connection_c2o = salloc(gp->mp.connection_c2o, opt);
}

static void set_connection_c2v (const char *opt, struct general_parameters *gp)
{
    gp->mp.connection_c2v = salloc(gp->mp.connection_c2v, opt);
}

static void set_const_init_c (const char *opt, struct general_parameters *gp)
{
    gp->mp.const_init_c = salloc(gp->mp.const_init_c, opt);
}

static void set_softmax_group (const char *opt, struct general_parameters *gp)
{
    gp->mp.softmax_group = salloc(gp->mp.softmax_group, opt);
}

static void set_init_tau (const char *opt, struct general_parameters *gp)
{
    gp->mp.init_tau = salloc(gp->mp.init_tau, opt);
}

static void set_prior_strength (const char *opt, struct general_parameters *gp)
{
    gp->mp.prior_strength = atof(opt);
}

static void set_rep_init_variance (
        const char *opt,
        struct general_parameters *gp)
{
    gp->mp.rep_init_variance = atof(opt);
}

static void set_lambda (const char *opt, struct general_parameters *gp)
{
    gp->mp.lambda = atof(opt);
}

static void set_alpha (const char *opt, struct general_parameters *gp)
{
    gp->mp.alpha = atof(opt);
}

static void set_truncate_length (
        const char *opt,
        struct general_parameters *gp)
{
    gp->ap.truncate_length = atoi(opt);
}

static void set_block_length (const char *opt, struct general_parameters *gp)
{
    gp->ap.block_length = atoi(opt);
}

static void set_divide_num (const char *opt, struct general_parameters *gp)
{
    gp->ap.divide_num = atoi(opt);
}

static void set_lyapunov_spectrum_size (
        const char *opt,
        struct general_parameters *gp)
{
    gp->ap.lyapunov_spectrum_size = atoi(opt);
}

static void set_threshold_period (
        const char *opt,
        struct general_parameters *gp)
{
    gp->ap.threshold_period = atof(opt);
}

static void set_state_file (const char *opt, struct general_parameters *gp)
{
    gp->iop.state_filename = salloc(gp->iop.state_filename, opt);
}

static void set_closed_state_file (
        const char *opt,
        struct general_parameters *gp)
{
    gp->iop.closed_state_filename = salloc(gp->iop.closed_state_filename, opt);
}

static void set_weight_file (const char *opt, struct general_parameters *gp)
{
    gp->iop.weight_filename = salloc(gp->iop.weight_filename, opt);
}

static void set_threshold_file (const char *opt, struct general_parameters *gp)
{
    gp->iop.threshold_filename = salloc(gp->iop.threshold_filename, opt);
}

static void set_tau_file (const char *opt, struct general_parameters *gp)
{
    gp->iop.tau_filename = salloc(gp->iop.tau_filename, opt);
}

static void set_init_file (const char *opt, struct general_parameters *gp)
{
    gp->iop.init_filename = salloc(gp->iop.init_filename, opt);
}

static void set_rep_init_file (const char *opt, struct general_parameters *gp)
{
    gp->iop.rep_init_filename = salloc(gp->iop.rep_init_filename, opt);
}

static void set_adapt_lr_file (const char *opt, struct general_parameters *gp)
{
    gp->iop.adapt_lr_filename = salloc(gp->iop.adapt_lr_filename, opt);
}

static void set_error_file (const char *opt, struct general_parameters *gp)
{
    gp->iop.error_filename = salloc(gp->iop.error_filename, opt);
}

static void set_closed_error_file (
        const char *opt,
        struct general_parameters *gp)
{
    gp->iop.closed_error_filename = salloc(gp->iop.closed_error_filename, opt);
}

static void set_lyapunov_file (const char *opt, struct general_parameters *gp)
{
    gp->iop.lyapunov_filename = salloc(gp->iop.lyapunov_filename, opt);
}

static void set_entropy_file (const char *opt, struct general_parameters *gp)
{
    gp->iop.entropy_filename = salloc(gp->iop.entropy_filename, opt);
}

static void set_period_file (const char *opt, struct general_parameters *gp)
{
    gp->iop.period_filename = salloc(gp->iop.period_filename, opt);
}

static void set_save_file (const char *opt, struct general_parameters *gp)
{
    gp->iop.save_filename = salloc(gp->iop.save_filename, opt);
}

static void set_load_file (const char *opt, struct general_parameters *gp)
{
    gp->iop.load_filename = salloc(gp->iop.load_filename, opt);
}

#define SET_DEFAULT_VALUE_OF_PRINT_INTERVAL_I(FILENAME,OPT) \
    do { \
        if (!gp->iop.interval_for_##FILENAME._set_##OPT##_flag) { \
            gp->iop.interval_for_##FILENAME.OPT = \
                gp->iop.default_interval.OPT; \
        } \
    } while(0)

#define SET_DEFAULT_VALUE_OF_PRINT_INTERVAL(OPT) \
    do { \
        SET_DEFAULT_VALUE_OF_PRINT_INTERVAL_I(state_file,OPT); \
        SET_DEFAULT_VALUE_OF_PRINT_INTERVAL_I(closed_state_file,OPT); \
        SET_DEFAULT_VALUE_OF_PRINT_INTERVAL_I(weight_file,OPT); \
        SET_DEFAULT_VALUE_OF_PRINT_INTERVAL_I(threshold_file,OPT); \
        SET_DEFAULT_VALUE_OF_PRINT_INTERVAL_I(tau_file,OPT); \
        SET_DEFAULT_VALUE_OF_PRINT_INTERVAL_I(init_file,OPT); \
        SET_DEFAULT_VALUE_OF_PRINT_INTERVAL_I(rep_init_file,OPT); \
        SET_DEFAULT_VALUE_OF_PRINT_INTERVAL_I(adapt_lr_file,OPT); \
        SET_DEFAULT_VALUE_OF_PRINT_INTERVAL_I(error_file,OPT); \
        SET_DEFAULT_VALUE_OF_PRINT_INTERVAL_I(closed_error_file,OPT); \
        SET_DEFAULT_VALUE_OF_PRINT_INTERVAL_I(lyapunov_file,OPT); \
        SET_DEFAULT_VALUE_OF_PRINT_INTERVAL_I(entropy_file,OPT); \
        SET_DEFAULT_VALUE_OF_PRINT_INTERVAL_I(period_file,OPT); \
    } while(0)

static void set_print_interval (const char *opt, struct general_parameters *gp)
{
    gp->iop.default_interval.interval = atol(opt);
    SET_DEFAULT_VALUE_OF_PRINT_INTERVAL(interval);
}

static void set_print_init (const char *opt, struct general_parameters *gp)
{
    gp->iop.default_interval.init = atol(opt);
    SET_DEFAULT_VALUE_OF_PRINT_INTERVAL(init);
}

static void set_print_end (const char *opt, struct general_parameters *gp)
{
    gp->iop.default_interval.end = atol(opt);
    SET_DEFAULT_VALUE_OF_PRINT_INTERVAL(end);
}

static void set_use_logscale_interval (
        const char *opt,
        struct general_parameters *gp)
{
    gp->iop.default_interval.use_logscale_interval = 1;
    SET_DEFAULT_VALUE_OF_PRINT_INTERVAL(use_logscale_interval);
}

#define GEN_PRINT_INTERVAL_SETTER_I(FILENAME,NAME,OPT,IN) \
static void set_##NAME##_for_##FILENAME (const char *opt, \
        struct general_parameters *gp) \
{ \
    gp->iop.interval_for_##FILENAME.OPT = IN; \
    gp->iop.interval_for_##FILENAME._set_##OPT##_flag = 1; \
}

#define GEN_PRINT_INTERVAL_SETTER(FILENAME) \
    GEN_PRINT_INTERVAL_SETTER_I(FILENAME, print_interval, interval, \
            atol(opt)) \
    GEN_PRINT_INTERVAL_SETTER_I(FILENAME, print_init, init, atol(opt)) \
    GEN_PRINT_INTERVAL_SETTER_I(FILENAME, print_end, end, atol(opt)) \
    GEN_PRINT_INTERVAL_SETTER_I(FILENAME, use_logscale_interval, \
            use_logscale_interval, 1)

GEN_PRINT_INTERVAL_SETTER(state_file)
GEN_PRINT_INTERVAL_SETTER(closed_state_file)
GEN_PRINT_INTERVAL_SETTER(weight_file)
GEN_PRINT_INTERVAL_SETTER(threshold_file)
GEN_PRINT_INTERVAL_SETTER(tau_file)
GEN_PRINT_INTERVAL_SETTER(init_file)
GEN_PRINT_INTERVAL_SETTER(rep_init_file)
GEN_PRINT_INTERVAL_SETTER(adapt_lr_file)
GEN_PRINT_INTERVAL_SETTER(error_file)
GEN_PRINT_INTERVAL_SETTER(closed_error_file)
GEN_PRINT_INTERVAL_SETTER(lyapunov_file)
GEN_PRINT_INTERVAL_SETTER(entropy_file)
GEN_PRINT_INTERVAL_SETTER(period_file)

static void set_verbose (const char *opt, struct general_parameters *gp)
{
    gp->iop.verbose = 1;
}



static void read_config_file (FILE *fp, struct general_parameters *gp);

static void set_config_file (const char *opt, struct general_parameters *gp)
{
    FILE *fp;
    if ((fp = fopen(opt, "r")) == NULL) {
        print_error_msg("cannot open %s", opt);
        exit(EXIT_FAILURE);
    }
    read_config_file(fp, gp);
    fclose(fp);
}


#define ENTRY_PRINT_INTERVAL_SETTER_I(OPTION,FUNC,HAS_ARG) \
    {#OPTION, HAS_ARG, FUNC}
#define ENTRY_PRINT_INTERVAL_SETTER_II(FILENAME,NAME,HAS_ARG) \
    ENTRY_PRINT_INTERVAL_SETTER_I(NAME##_for_##FILENAME, \
            set_##NAME##_for_##FILENAME, HAS_ARG)
#define ENTRY_PRINT_INTERVAL_SETTER(FILENAME) \
    ENTRY_PRINT_INTERVAL_SETTER_II(FILENAME, print_interval, 1), \
    ENTRY_PRINT_INTERVAL_SETTER_II(FILENAME, print_init, 1), \
    ENTRY_PRINT_INTERVAL_SETTER_II(FILENAME, print_end, 1), \
    ENTRY_PRINT_INTERVAL_SETTER_II(FILENAME, use_logscale_interval, 0)

static struct option_information {
    const char *name;
    int has_arg;
    void (*setter)(const char*, struct general_parameters*);
} opt_info[] = {
    {"seed", 1, set_seed},
    {"epoch_size", 1, set_epoch_size},
    {"use_adaptive_lr", 0, set_use_adaptive_lr},
    {"rho", 1, set_rho},
    {"momentum", 1, set_momentum},
    {"c_state_size", 1, set_c_state_size},
    {"rep_init_size", 1, set_rep_init_size},
    {"delay_length", 1, set_delay_length},
    {"output_type", 1, set_output_type},
    {"fixed_weight", 0, set_fixed_weight},
    {"fixed_threshold", 0, set_fixed_threshold},
    {"fixed_tau", 0, set_fixed_tau},
    {"fixed_init_c_state", 0, set_fixed_init_c_state},
    {"connection_i2c", 1, set_connection_i2c},
    {"connection_c2c", 1, set_connection_c2c},
    {"connection_c2o", 1, set_connection_c2o},
    {"connection_c2v", 1, set_connection_c2v},
    {"const_init_c", 1, set_const_init_c},
    {"softmax_group", 1, set_softmax_group},
    {"init_tau", 1, set_init_tau},
    {"prior_strength", 1, set_prior_strength},
    {"rep_init_variance", 1, set_rep_init_variance},
    {"lambda", 1, set_lambda},
    {"alpha", 1, set_alpha},
    {"truncate_length", 1, set_truncate_length},
    {"block_length", 1, set_block_length},
    {"divide_num", 1, set_divide_num},
    {"lyapunov_spectrum_size", 1, set_lyapunov_spectrum_size},
    {"threshold_period", 1, set_threshold_period},
    {"state_file", 1, set_state_file},
    {"closed_state_file", 1, set_closed_state_file},
    {"weight_file", 1, set_weight_file},
    {"threshold_file", 1, set_threshold_file},
    {"tau_file", 1, set_tau_file},
    {"init_file", 1, set_init_file},
    {"rep_init_file", 1, set_rep_init_file},
    {"adapt_lr_file", 1, set_adapt_lr_file},
    {"error_file", 1, set_error_file},
    {"closed_error_file", 1, set_closed_error_file},
    {"lyapunov_file", 1, set_lyapunov_file},
    {"entropy_file", 1, set_entropy_file},
    {"period_file", 1, set_period_file},
    {"save_file", 1, set_save_file},
    {"load_file", 1, set_load_file},
    {"print_interval", 1, set_print_interval},
    {"print_init", 1, set_print_init},
    {"print_end", 1, set_print_end},
    {"use_logscale_interval", 0, set_use_logscale_interval},
    ENTRY_PRINT_INTERVAL_SETTER(state_file),
    ENTRY_PRINT_INTERVAL_SETTER(closed_state_file),
    ENTRY_PRINT_INTERVAL_SETTER(weight_file),
    ENTRY_PRINT_INTERVAL_SETTER(threshold_file),
    ENTRY_PRINT_INTERVAL_SETTER(tau_file),
    ENTRY_PRINT_INTERVAL_SETTER(init_file),
    ENTRY_PRINT_INTERVAL_SETTER(rep_init_file),
    ENTRY_PRINT_INTERVAL_SETTER(adapt_lr_file),
    ENTRY_PRINT_INTERVAL_SETTER(error_file),
    ENTRY_PRINT_INTERVAL_SETTER(closed_error_file),
    ENTRY_PRINT_INTERVAL_SETTER(lyapunov_file),
    ENTRY_PRINT_INTERVAL_SETTER(entropy_file),
    ENTRY_PRINT_INTERVAL_SETTER(period_file),
    {"verbose", 0, set_verbose},
    {"config_file", 1, set_config_file},
    {0, 0, NULL}
};


static char* strip (char *str, int c)
{
    if (str == NULL) {
        return NULL;
    }
    while (*str == c) {
        str++;
    }
    size_t len = strlen(str);
    if (len > 0) {
        for (char *p = str + len - 1; *p == c; p--) {
            *p = '\0';
            if (p == str) {
                break;
            }
        }
    }
    return str;
}

static void parse_option_and_arg (char *str, char **opt, char **arg)
{
    char *p;
    if ((p = strpbrk(str, "#\n")) != NULL) { *p = '\0'; }
    if (strlen(str) > 0) {
        if ((p = strchr(str, '=')) != NULL) {
            *p = '\0';
            p++;
        } else {
            p = NULL;
        }
        *opt = strip(str, ' ');
        *arg = strip(p, ' ');
    } else {
        *opt = NULL;
        *arg = NULL;
    }
}

static void read_config_file (FILE *fp, struct general_parameters *gp)
{
    char *str = NULL;
    size_t str_size = 0;
    int line = 0;
    while(getline(&str, &str_size, fp) != -1) {
        line++;
        char *opt, *arg;
        parse_option_and_arg(str, &opt, &arg);
        if (opt != NULL && strlen(opt) > 0) {
            int i = 0;
            while (opt_info[i].setter != NULL) {
                if (strcmp(opt, opt_info[i].name) == 0) {
                    if (opt_info[i].has_arg && arg == NULL) {
                        print_error_msg("warning: option `%s' requires an "
                                "argument at line %d", opt, line);
                    } else {
                        opt_info[i].setter(arg, gp);
                    }
                    break;
                }
                i++;
            }
            if (opt_info[i].setter == NULL) {
                print_error_msg("warning: unknown option `%s' at line %d",
                        opt, line);
            }
        }
    }
    FREE(str);
}

static void read_options (int argc, char *argv[], struct general_parameters *gp)
{
    int opt;
    while ((opt = getopt(argc, argv, "s:n:r:t:d:k:e:l:x:m:ap:f:i:o:c:Vvh"))
            != -1) {
        switch (opt) {
            case 's':
                set_seed(optarg, gp);
                break;
            case 'n':
                set_c_state_size(optarg, gp);
                break;
            case 'r':
                set_rep_init_size(optarg, gp);
                break;
            case 't':
                set_init_tau(optarg, gp);
                break;
            case 'd':
                set_delay_length(optarg, gp);
                break;
            case 'k':
                set_output_type(optarg, gp);
                break;
            case 'e':
                set_epoch_size(optarg, gp);
                break;
            case 'l':
                set_print_interval(optarg, gp);
                break;
            case 'x':
                set_rho(optarg, gp);
                break;
            case 'm':
                set_momentum(optarg, gp);
                break;
            case 'a':
                set_use_adaptive_lr(NULL, gp);
                break;
            case 'p':
                set_prior_strength(optarg, gp);
                break;
            case 'i':
                set_load_file(optarg, gp);
                break;
            case 'o':
                set_save_file(optarg, gp);
                break;
            case 'c':
                set_config_file(optarg, gp);
                break;
            case 'V':
                set_verbose(NULL, gp);
                break;
            case 'v':
                display_version();
                exit(EXIT_SUCCESS);
            case 'h':
                display_help();
                exit(EXIT_SUCCESS);
            default: /* '?' */
                fprintf(stderr, "Try `rnn-learn -h' for more information.\n");
                exit(EXIT_SUCCESS);
        }
    }
}


static void setup_target (
        int argc,
        char *argv[],
        const struct general_parameters *gp,
        struct target_reader *t_reader)
{
    init_target_reader(t_reader);
    if (argc == optind && strlen(gp->iop.load_filename) == 0) {
        if (read_target_from_file(t_reader, " \t,", stdin) == -1) {
            print_error_msg("error in the standard input");
            exit(EXIT_FAILURE);
        }
    } else {
        for (int i = optind; i < argc; i++) {
            FILE *fp;
            if ((fp = fopen(argv[i], "r")) == NULL) {
                print_error_msg("cannot open %s", argv[i]);
                exit(EXIT_FAILURE);
            }
            if (read_target_from_file(t_reader, " \t,", fp) == -1) {
                print_error_msg("error in %s", argv[i]);
                exit(EXIT_FAILURE);
            }
            fclose(fp);
        }
    }
}

static void setup_parameters (
        struct general_parameters *gp,
        const struct target_reader *t_reader)
{
    gp->inp.adapt_lr = 1.0;
    gp->inp.init_epoch = 0;
    if (strlen(gp->iop.load_filename) == 0 && t_reader->num) {
        MALLOC2(gp->inp.has_connection_ci, gp->mp.c_state_size,
                t_reader->dimension);
        MALLOC2(gp->inp.has_connection_cc, gp->mp.c_state_size,
                gp->mp.c_state_size);
        MALLOC2(gp->inp.has_connection_oc, t_reader->dimension,
                gp->mp.c_state_size);
        MALLOC2(gp->inp.has_connection_vc, t_reader->dimension,
                gp->mp.c_state_size);
        MALLOC2(gp->inp.connectivity_ci, gp->mp.c_state_size,
                t_reader->dimension);
        MALLOC2(gp->inp.connectivity_cc, gp->mp.c_state_size,
                gp->mp.c_state_size);
        MALLOC2(gp->inp.connectivity_oc, t_reader->dimension,
                gp->mp.c_state_size);
        MALLOC2(gp->inp.connectivity_vc, t_reader->dimension,
                gp->mp.c_state_size);
        str_to_connection(gp->mp.connection_i2c, t_reader->dimension,
                gp->mp.c_state_size, gp->inp.has_connection_ci,
                gp->inp.connectivity_ci);
        str_to_connection(gp->mp.connection_c2c, gp->mp.c_state_size,
                gp->mp.c_state_size, gp->inp.has_connection_cc,
                gp->inp.connectivity_cc);
        str_to_connection(gp->mp.connection_c2o, gp->mp.c_state_size,
                t_reader->dimension, gp->inp.has_connection_oc,
                gp->inp.connectivity_oc);
        str_to_connection(gp->mp.connection_c2v, gp->mp.c_state_size,
                t_reader->dimension, gp->inp.has_connection_vc,
                gp->inp.connectivity_vc);
        MALLOC(gp->inp.softmax_group_id, t_reader->dimension);
        str_to_softmax_group(gp->mp.softmax_group, t_reader->dimension,
                &gp->inp.softmax_group_num, gp->inp.softmax_group_id);
        MALLOC(gp->inp.const_init_c, gp->mp.c_state_size);
        str_to_const_init_c(gp->mp.const_init_c, gp->mp.c_state_size,
                gp->inp.const_init_c);
        MALLOC(gp->inp.init_tau, gp->mp.c_state_size);
        str_to_init_tau(gp->mp.init_tau, gp->mp.c_state_size, gp->inp.init_tau);
    }
}

static void check_parameters (
        const struct general_parameters *gp,
        const struct target_reader *t_reader)
{
    if (gp->mp.seed <= 0) {
        print_error_msg("seed for random number generator not in valid "
                "range: x >= 1 (integer)");
        exit(EXIT_FAILURE);
    }
    if (gp->mp.rho < 0) {
        print_error_msg("learning rate not in valid range: x >= 0 (float)");
        exit(EXIT_FAILURE);
    }
    if (gp->mp.momentum < 0) {
        print_error_msg("learning momentum not in valid range: x >= 0 (float)");
        exit(EXIT_FAILURE);
    }
    if (gp->mp.c_state_size <= 0) {
        print_error_msg("number of context neurons must be greater than zero.");
        exit(EXIT_FAILURE);
    }
    if (gp->mp.rep_init_size <= 0) {
        print_error_msg("number of representative points of initial state "
                "must be greater than zero.");
        exit(EXIT_FAILURE);
    }
    if (gp->mp.delay_length <= 0) {
        print_error_msg("time delay in a self-feedback not in valid range: "
                "x > 0 (integer)");
        exit(EXIT_FAILURE);
    }
    if (gp->mp.output_type != 0 && gp->mp.output_type != 1) {
        print_error_msg("type of output function must be 0(tanh) or "
                "1(softmax activation function)");
        exit(EXIT_FAILURE);
    }
    if (gp->mp.prior_strength < 0) {
        print_error_msg("effect of the normal prior distribution not in "
                "valid range: x >= 0 (float)");
        exit(EXIT_FAILURE);
    }
    if (gp->mp.rep_init_variance <= 0) {
        print_error_msg("variance for representative points of initial state "
                "not in valid range: x >= 0 (float)");
        exit(EXIT_FAILURE);
    }
    if (gp->mp.lambda < 0) {
        print_error_msg("`lambda' not in valid range: x >= 0 (float)");
        exit(EXIT_FAILURE);
    }
    if (gp->mp.alpha < 0) {
        print_error_msg("`alpha' not in valid range: x >= 0 (float)");
        exit(EXIT_FAILURE);
    }
    if (gp->ap.truncate_length < 0) {
        print_error_msg("`truncate_length' not in valid range: "
                "x >= 0 (integer)");
        exit(EXIT_FAILURE);
    }
    if (gp->ap.block_length < 0) {
        print_error_msg("`block_length' not in valid range: x >= 0 (integer)");
        exit(EXIT_FAILURE);
    }
    if (gp->ap.divide_num <= 0) {
        print_error_msg("`divide_num' not in valid range: x >= 1 (integer)");
        exit(EXIT_FAILURE);
    }
    if (t_reader->num == 0 && strlen(gp->iop.load_filename) == 0) {
        print_error_msg("training data is empty.");
        exit(EXIT_FAILURE);
    }
}



int main (int argc, char *argv[])
{
#ifdef ENABLE_MTRACE
    mtrace();
#endif

    struct general_parameters gp;
    init_parameters(&gp);

    read_options(argc, argv, &gp);

    struct target_reader t_reader;
    setup_target(argc, argv, &gp, &t_reader);

    setup_parameters(&gp, &t_reader);
    check_parameters(&gp, &t_reader);

    training_main(&gp, &t_reader);

    free_target_reader(&t_reader);
    free_parameters(&gp);

#ifdef ENABLE_MTRACE
    muntrace();
#endif
    return EXIT_SUCCESS;
}

