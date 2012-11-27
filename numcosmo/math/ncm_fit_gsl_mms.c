/***************************************************************************
 *            ncm_fit_gsl_mms.c
 *
 *  Tue Jun 19 10:55:06 2007
 *  Copyright  2007  Sandro Dias Pinto Vitenti
 *  <sandro@isoftware.com.br>
 ****************************************************************************/
/*
 * numcosmo
 * Copyright (C) 2012 Sandro Dias Pinto Vitenti <sandro@isoftware.com.br>
 * 
 * numcosmo is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * numcosmo is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * SECTION:ncm_fit_gsl_mms
 * @title: Non-linear Simplex Minimization -- GSL
 * @short_description: FIXME
 *
 * FIXME
 * 
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif /* HAVE_CONFIG_H */
#include "build_cfg.h"

#include "math/ncm_fit_gsl_mms.h"
#include "math/ncm_cfg.h"
#include "ncm_enum_types.h"

#include <gsl/gsl_blas.h>

enum
{
  PROP_0,
  PROP_ALGO,
  PROP_SIZE,
};

G_DEFINE_TYPE (NcmFitGSLMMS, ncm_fit_gsl_mms, NCM_TYPE_FIT);

static void
ncm_fit_gsl_mms_init (NcmFitGSLMMS *fit_gsl_mms)
{
  fit_gsl_mms->mms = NULL;
  fit_gsl_mms->algo = 0;
  fit_gsl_mms->desc = NULL;
  fit_gsl_mms->ss = NULL;
}

static gdouble nc_residual_multimin_f (const gsl_vector *x, gpointer p);

static void
_ncm_fit_gsl_mms_constructed (GObject *object)
{
  /* Chain up : start */
  G_OBJECT_CLASS (ncm_fit_gsl_mms_parent_class)->constructed (object);
  {
    NcmFitGSLMMS *fit_gsl_mms = NCM_FIT_GSL_MMS (object);
    NcmFit *fit = NCM_FIT (fit_gsl_mms);
    gint i;

    fit_gsl_mms->f.f      = &nc_residual_multimin_f;
    fit_gsl_mms->f.n      = fit->fparam_len;
    fit_gsl_mms->f.params = fit_gsl_mms;

    fit_gsl_mms->ss       = ncm_vector_new (fit->fparam_len); 

    for (i = 0; i < ncm_mset_fparams_len (fit->mset); i++)
    {
      gdouble pscale = ncm_mset_fparam_get_scale (fit->mset, i);
      ncm_vector_set (fit_gsl_mms->ss, i, pscale * 1e-3);
    }
    
    ncm_fit_gsl_mms_set_algo (fit_gsl_mms, fit_gsl_mms->algo);
  }
}

static void
_ncm_fit_gsl_mms_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
  NcmFitGSLMMS *fit_gsl_mms = NCM_FIT_GSL_MMS (object);
  g_return_if_fail (NCM_IS_FIT_GSL_MMS (object));

  switch (prop_id)
  {
    case PROP_ALGO:
    {
      if (fit_gsl_mms->mms == NULL)
        fit_gsl_mms->algo = g_value_get_enum (value);
      else
        ncm_fit_gsl_mms_set_algo (fit_gsl_mms, g_value_get_enum (value));
      break;
    }
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
_ncm_fit_gsl_mms_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
  NcmFitGSLMMS *fit_gsl_mms = NCM_FIT_GSL_MMS (object);
  g_return_if_fail (NCM_IS_FIT_GSL_MMS (object));

  switch (prop_id)
  {
    case PROP_ALGO:
    g_value_set_enum (value, fit_gsl_mms->algo);
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
ncm_fit_gsl_mms_finalize (GObject *object)
{
  NcmFitGSLMMS *fit_gsl_mms = NCM_FIT_GSL_MMS (object);

  gsl_multimin_fminimizer_free (fit_gsl_mms->mms);
  fit_gsl_mms->mms = NULL;

  /* Chain up : end */
  G_OBJECT_CLASS (ncm_fit_gsl_mms_parent_class)->finalize (object);
}

static NcmFit *_ncm_fit_gsl_mms_copy_new (NcmFit *fit, NcmLikelihood *lh, NcmMSet *mset, NcmFitGradType gtype);
static gboolean _ncm_fit_gsl_mms_run (NcmFit *fit, NcmFitRunMsgs mtype);
static const gchar *_ncm_fit_gsl_mms_get_desc (NcmFit *fit);

static void
ncm_fit_gsl_mms_class_init (NcmFitGSLMMSClass *klass)
{
  GObjectClass* object_class = G_OBJECT_CLASS (klass);
  NcmFitClass* fit_class     = NCM_FIT_CLASS (klass);

  object_class->constructed  = &_ncm_fit_gsl_mms_constructed;
  object_class->set_property = &_ncm_fit_gsl_mms_set_property;
  object_class->get_property = &_ncm_fit_gsl_mms_get_property;
  object_class->finalize     = &ncm_fit_gsl_mms_finalize;

  g_object_class_install_property (object_class,
                                   PROP_ALGO,
                                   g_param_spec_enum ("algorithm",
                                                      NULL,
                                                      "GSL multidimensional minimization algorithm [simplex]",
                                                      NCM_TYPE_FIT_GSLMMS_ALGOS, NCM_FIT_GSL_MMS_NMSIMPLEX2,
                                                      G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_NAME | G_PARAM_STATIC_BLURB));
  
  fit_class->copy_new = &_ncm_fit_gsl_mms_copy_new;
  fit_class->run      = &_ncm_fit_gsl_mms_run;
  fit_class->get_desc = &_ncm_fit_gsl_mms_get_desc;
}

static NcmFit *
_ncm_fit_gsl_mms_copy_new (NcmFit *fit, NcmLikelihood *lh, NcmMSet *mset, NcmFitGradType gtype)
{
  NcmFitGSLMMS *fit_gsl_mms = NCM_FIT_GSL_MMS (fit);
  return ncm_fit_gsl_mms_new (lh, mset, gtype, fit_gsl_mms->algo);
}

static gboolean
_ncm_fit_gsl_mms_run (NcmFit *fit, NcmFitRunMsgs mtype)
{
  NcmFitGSLMMS *fit_gsl_mms = NCM_FIT_GSL_MMS (fit);
  gint status;
  gdouble prec = NCM_FIT_DEFAULT_M2LNL_RELTOL;
  gdouble last_size = 1e300;
  gulong still_count = 0;

  ncm_mset_fparams_get_vector (fit->mset, fit->x);
  gsl_multimin_fminimizer_set (fit_gsl_mms->mms, &fit_gsl_mms->f, ncm_vector_gsl (fit->x), ncm_vector_gsl (fit_gsl_mms->ss));

  do
  {
    gdouble size;
    fit->niter++;
    status = gsl_multimin_fminimizer_iterate (fit_gsl_mms->mms);

    if (fit->niter == 1 && !gsl_finite(fit_gsl_mms->mms->fval))
    {
      ncm_mset_fparams_set_vector (fit->mset, fit->x);
      return FALSE;
    }

    if (status)
    {
      if (mtype > NCM_FIT_RUN_MSGS_NONE)
        ncm_fit_log_step_error (fit, gsl_strerror (status));
    }

    size = gsl_multimin_fminimizer_size (fit_gsl_mms->mms);

    if (size == last_size)
    {
      if (++still_count == 3)
      {
        ncm_fit_log_step_error (fit, "size do not improve [prec: %8.5e size: %8.5e]", prec, size);
        status = GSL_SUCCESS;
      }
      else
        status = GSL_CONTINUE;
    }
    else
    {
      still_count = 0;
      last_size = size;
      status = gsl_multimin_test_size (size, prec);
      if (status == GSL_ETOL)
        status = GSL_CONTINUE;
    }
    ncm_fit_log_step (fit, fit_gsl_mms->mms->fval);
  }
  while ( (status == GSL_CONTINUE) && (fit->niter < fit->maxeval) );

  ncm_mset_fparams_set_gsl_vector (fit->mset, fit_gsl_mms->mms->x);

  fit->m2lnL = fit_gsl_mms->mms->fval;
  fit->sqrt_m2lnL = sqrt (fit_gsl_mms->mms->fval);
  fit->m2lnL_dof = fit->m2lnL / fit->dof;
  fit->m2lnL_prec = last_size;

  return TRUE;
}

static gdouble
nc_residual_multimin_f (const gsl_vector *x, gpointer p)
{
  NcmFit *fit = NCM_FIT (p);
  gdouble result;
  
  ncm_mset_fparams_set_gsl_vector (fit->mset, x);
  if (!ncm_mset_params_valid (fit->mset))
    return GSL_NAN;

  ncm_fit_m2lnL_val (fit, &result);
  return result;
}

static const gchar *
_ncm_fit_gsl_mms_get_desc (NcmFit *fit)
{
  NcmFitGSLMMS *fit_gsl_mms = NCM_FIT_GSL_MMS (fit);
  if (fit_gsl_mms->desc == NULL)
  {
    fit_gsl_mms->desc = g_strdup_printf ("GSL Multidimensional Minimization:%s", 
                                        gsl_multimin_fminimizer_name (fit_gsl_mms->mms));
  }
  return fit_gsl_mms->desc;
}

/**
 * ncm_fit_gsl_mms_new:
 * @lh: FIXME
 * @mset: FIXME
 * @gtype: FIXME
 * @algo: FIXME
 *
 * FIXME
 * 
 * Returns: FIXME
 */
NcmFit *
ncm_fit_gsl_mms_new (NcmLikelihood *lh, NcmMSet *mset, NcmFitGradType gtype, NcmFitGSLMMSAlgos algo)
{
  return g_object_new (NCM_TYPE_FIT_GSL_MMS, 
                       "likelihood", lh,
                       "mset", mset,
                       "grad-type", gtype,
                       "algorithm", algo,
                       NULL
                       );
}

/**
 * ncm_fit_gsl_mms_new_default:
 * @lh: FIXME
 * @mset: FIXME
 * @gtype: FIXME
 *
 * FIXME
 * 
 * Returns: FIXME
 */
NcmFit *
ncm_fit_gsl_mms_new_default (NcmLikelihood *lh, NcmMSet *mset, NcmFitGradType gtype)
{
  return g_object_new (NCM_TYPE_FIT_GSL_MMS, 
                       "likelihood", lh,
                       "mset", mset,
                       "grad-type", gtype,
                       NULL
                       );
}

/**
 * ncm_fit_gsl_mms_new_by_name:
 * @lh: FIXME
 * @mset: FIXME
 * @gtype: FIXME
 * @algo_name: FIXME
 *
 * FIXME
 * 
 * Returns: FIXME
 */
NcmFit *
ncm_fit_gsl_mms_new_by_name (NcmLikelihood *lh, NcmMSet *mset, NcmFitGradType gtype, gchar *algo_name)
{
  if (algo_name != NULL)
  {
    const GEnumValue *algo = ncm_cfg_get_enum_by_id_name_nick (NCM_TYPE_FIT_GSLMMS_ALGOS,
                                                               algo_name);
    if (algo == NULL)
      g_error ("ncm_fit_gsl_mms_new_by_name: algorithm %s not found.", algo_name);
    return ncm_fit_gsl_mms_new (lh, mset, gtype, algo->value);
  }
  else
    return ncm_fit_gsl_mms_new_default (lh, mset, gtype);
}

/**
 * ncm_fit_gsl_mms_set_algo:
 * @fit_gsl_mms: a #NcmFitGSLMMS.
 * @algo: a #gsl_mms_algorithm.
 *
 * FIXME
 *
 */
void
ncm_fit_gsl_mms_set_algo (NcmFitGSLMMS *fit_gsl_mms, NcmFitGSLMMSAlgos algo)
{
  const gsl_multimin_fminimizer_type *ncm_fit_gsl_mms_algos[] = {
    gsl_multimin_fminimizer_nmsimplex2,
    gsl_multimin_fminimizer_nmsimplex,
    gsl_multimin_fminimizer_nmsimplex2rand,
  };
  NcmFit *fit = NCM_FIT (fit_gsl_mms);

  g_assert (fit_gsl_mms->algo < NCM_FIT_GSL_MMS_NUM_ALGOS);

  if (fit_gsl_mms->algo != algo)
  {
    fit_gsl_mms->algo = algo;
    if (fit_gsl_mms->mms != NULL)
      gsl_multimin_fminimizer_free (fit_gsl_mms->mms);
    fit_gsl_mms->mms = NULL;
    if (fit_gsl_mms->desc != NULL)
      g_free (fit_gsl_mms->desc);
  }

  if (fit_gsl_mms->mms == NULL)
    fit_gsl_mms->mms = gsl_multimin_fminimizer_alloc (ncm_fit_gsl_mms_algos[fit_gsl_mms->algo], fit->fparam_len);
}