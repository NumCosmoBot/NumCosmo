/* -*- Mode: C; indent-tabs-mode: nil; c-basic-offset: 2; tab-width: 2 -*-  */
/***************************************************************************
 *            ncm_qm_prop.c
 *
 *  Thu February 15 14:44:56 2018
 *  Copyright  2018  Sandro Dias Pinto Vitenti
 *  <sandro@isoftware.com.br>
 ****************************************************************************/
/*
 * ncm_qm_prop.c
 * Copyright (C) 2018 Sandro Dias Pinto Vitenti <sandro@isoftware.com.br>
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
 * SECTION:ncm_qm_prop
 * @title: NcmQMProp
 * @short_description: Numerical QM propagator object
 *
 * FIXME
 *
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif /* HAVE_CONFIG_H */
#include "build_cfg.h"

#include "math/ncm_qm_prop.h"
#include "math/integral.h"
#include "math/memory_pool.h"
#include "math/ncm_integral1d.h"
#include "math/ncm_integral1d_ptr.h"
#include "math/ncm_util.h"
#include "math/ncm_spline_cubic_notaknot.h"
#include "math/ncm_spline_func.h"

#if HAVE_SUNDIALS_MAJOR == 3
#ifdef HAVE_ACB_H

#ifndef NUMCOSMO_GIR_SCAN
#include <complex.h>
#include <math.h>
#include <stdio.h>
#include <gsl/gsl_sf_bessel.h>
#include <gsl/gsl_sf_hyperg.h>
#include <gsl/gsl_integration.h>
#include <gsl/gsl_vector.h>
#include <gsl/gsl_blas.h>
#include <gsl/gsl_multifit_nlinear.h>
#include <gsl/gsl_dht.h>
#include <gsl/gsl_poly.h>
#include <acb.h>
#include <acb_hypgeom.h>
#endif /* NUMCOSMO_GIR_SCAN */

#include <arkode/arkode.h>
#include <nvector/nvector_serial.h>
#include <sunlinsol/sunlinsol_pcg.h>
#include <arkode/arkode_spils.h>
#include <arkode/arkode_bandpre.h>
#include <sundials/sundials_types.h>
#include <sundials/sundials_math.h>

struct _NcmQMPropPrivate
{
  gdouble lambda;
  gdouble nu;
  gsize n;
  gsize np;
  gdouble abstol;
  gdouble reltol;
  complex double *weights;
  complex double *G;
  gdouble *nodes;
  gdouble *Jnu;
  NcmIntegral1d *Re_int;
  NcmIntegral1d *Im_int;
  /* PDE */
  NcmSpline *psi0_s;
  guint nknots;
  NcmVector *knots;
  NcmVector *spec_knots;
  NcmVector *dS_v;
  NcmVector *rho_v;
  NcmVector *Re_psi_v;
  NcmVector *Im_psi_v;
  NcmSpline *dS_s;
  NcmSpline *rho_s;
  NcmSpline *Re_psi_s;
  NcmSpline *Im_psi_s;   
  gboolean up_splines;
  gboolean noboundary;
  NcmSpline *YNp1_Re_R;
  NcmSpline *YNp1_Im_R;
  GArray *YNp1;
  gdouble h;
  gdouble aN;
  gdouble bN;
  gdouble cN;
  gdouble sqrt_aNcN;
  gdouble sqrt_aN_cN;
  gdouble ti;
  gdouble xi, xf;
  N_Vector y;
  N_Vector y2;
  N_Vector yt;
  SUNLinearSolver LS;
  gpointer arkode;
  gsl_dht *dht;
  NcmVector *specReY;
  NcmVector *specImY;
  NcmVector *specReYt;
  NcmVector *specImYt;
  NcmVector *specReW;
  NcmVector *specImW;
};

enum
{
  PROP_0,
  PROP_LAMBDA,
  PROP_NP,
  PROP_ABSTOL,
  PROP_RELTOL,
  PROP_NKNOTS,
  PROP_NOBOUNDARY,
};

G_DEFINE_TYPE (NcmQMProp, ncm_qm_prop, G_TYPE_OBJECT);
G_DEFINE_BOXED_TYPE (NcmQMPropGauss, ncm_qm_prop_gauss, ncm_qm_prop_gauss_dup, ncm_qm_prop_gauss_free);
G_DEFINE_BOXED_TYPE (NcmQMPropExp,   ncm_qm_prop_exp,   ncm_qm_prop_exp_dup,   ncm_qm_prop_exp_free);

static void
ncm_qm_prop_init (NcmQMProp *qm_prop)
{
  NcmQMPropPrivate * const self = qm_prop->priv = G_TYPE_INSTANCE_GET_PRIVATE (qm_prop, NCM_TYPE_QM_PROP, NcmQMPropPrivate);

  self->np      = 0;
  self->n       = 0;
  self->abstol  = 0.0;
  self->reltol  = 0.0;
  self->weights = NULL;
  self->G       = NULL;
  self->nodes   = NULL;
  self->Jnu     = NULL;
  self->Re_int  = NULL;
  self->Im_int  = NULL;

  /* PDE */
  self->psi0_s     = NCM_SPLINE (ncm_spline_cubic_notaknot_new ());
  self->nknots     = 0;
  self->knots      = NULL;
  self->spec_knots = NULL;
  self->dS_v       = NULL;
  self->rho_v      = NULL;
  self->Re_psi_v   = NULL;
  self->Im_psi_v   = NULL;
  self->dS_s       = NULL;
  self->rho_s      = NULL;
  self->Re_psi_s   = NULL;
  self->Im_psi_s   = NULL;
  self->up_splines = FALSE;
  self->noboundary = FALSE;
  self->YNp1_Re_R  = ncm_spline_cubic_notaknot_new ();
  self->YNp1_Im_R  = ncm_spline_cubic_notaknot_new ();
  self->YNp1       = g_array_new (FALSE, FALSE, sizeof (complex double));
  self->ti         = 0.0;
  self->y          = NULL;
  self->y2         = NULL;
  self->yt         = NULL;
  self->LS         = NULL;
  self->arkode     = NULL;

  self->dht        = NULL;
  self->specReY    = NULL;
  self->specImY    = NULL;
  self->specReYt   = NULL;
  self->specImYt   = NULL;
  self->specReW    = NULL;
  self->specImW    = NULL;
}

static void
_ncm_qm_prop_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
  NcmQMProp *qm_prop = NCM_QM_PROP (object);
  NcmQMPropPrivate * const self = qm_prop->priv;
  g_return_if_fail (NCM_IS_QM_PROP (object));

  switch (prop_id)
  {
    case PROP_LAMBDA:
      self->lambda = g_value_get_double (value);
      self->nu     = sqrt (self->lambda + 0.25);
      break;
    case PROP_NP:
      self->np = g_value_get_uint (value);
      break;
    case PROP_ABSTOL:
      self->abstol = g_value_get_double (value);
      break;
    case PROP_RELTOL:
      self->reltol = g_value_get_double (value);
      break;
    case PROP_NKNOTS:
      ncm_qm_prop_set_nknots (qm_prop, g_value_get_uint (value));
      break;
    case PROP_NOBOUNDARY:
      self->noboundary = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
_ncm_qm_prop_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
  NcmQMProp *qm_prop = NCM_QM_PROP (object);
  NcmQMPropPrivate * const self = qm_prop->priv;
  g_return_if_fail (NCM_IS_QM_PROP (object));

  switch (prop_id)
  {
    case PROP_LAMBDA:
      g_value_set_double (value, self->lambda);
      break;
    case PROP_NP:
      g_value_set_uint (value, self->np);
      break;
    case PROP_ABSTOL:
      g_value_set_double (value, self->abstol);
      break;
    case PROP_RELTOL:
      g_value_set_double (value, self->reltol);
      break;
    case PROP_NKNOTS:
      g_value_set_uint (value, ncm_qm_prop_get_nknots (qm_prop));
      break;
    case PROP_NOBOUNDARY:
      g_value_set_boolean (value, self->noboundary);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
_ncm_qm_prop_dispose (GObject *object)
{
  NcmQMProp *qm_prop = NCM_QM_PROP (object);
  NcmQMPropPrivate * const self = qm_prop->priv;

  g_clear_pointer (&self->nodes,   g_free);
  g_clear_pointer (&self->weights, g_free);
  g_clear_pointer (&self->G,       g_free);
  g_clear_pointer (&self->Jnu,     g_free);

  ncm_integral1d_clear (&self->Re_int);
  ncm_integral1d_clear (&self->Im_int);

  g_clear_pointer (&self->YNp1,    g_array_unref);

  g_clear_pointer (&self->y, N_VDestroy);
  g_clear_pointer (&self->LS, SUNLinSolFree);

  ncm_vector_clear (&self->knots);
  ncm_vector_clear (&self->spec_knots);
  ncm_vector_clear (&self->dS_v);
  ncm_vector_clear (&self->rho_v);
  ncm_vector_clear (&self->Re_psi_v);
  ncm_vector_clear (&self->Im_psi_v);
  
  ncm_spline_clear (&self->dS_s);
  ncm_spline_clear (&self->rho_s);

  ncm_spline_clear (&self->Re_psi_s);
  ncm_spline_clear (&self->Im_psi_s);

  ncm_spline_clear (&self->YNp1_Re_R);
  ncm_spline_clear (&self->YNp1_Im_R);

  ncm_vector_clear (&self->specReY);
  ncm_vector_clear (&self->specImY);

  ncm_vector_clear (&self->specReYt);
  ncm_vector_clear (&self->specImYt);

  ncm_vector_clear (&self->specReW);
  ncm_vector_clear (&self->specImW);

  /* Chain up : end */
  G_OBJECT_CLASS (ncm_qm_prop_parent_class)->dispose (object);
}

static void
_ncm_qm_prop_finalize (GObject *object)
{
  NcmQMProp *qm_prop = NCM_QM_PROP (object);
  NcmQMPropPrivate * const self = qm_prop->priv;

  if (self->arkode != NULL)
  {
    ARKodeFree (&self->arkode);
    self->arkode = NULL;
  }

  g_clear_pointer (&self->dht, gsl_dht_free);
  
  /* Chain up : end */
  G_OBJECT_CLASS (ncm_qm_prop_parent_class)->finalize (object);
}

static void
ncm_qm_prop_class_init (NcmQMPropClass *klass)
{
  GObjectClass* object_class = G_OBJECT_CLASS (klass);

  g_type_class_add_private (klass, sizeof (NcmQMPropPrivate));

  object_class->set_property = &_ncm_qm_prop_set_property;
  object_class->get_property = &_ncm_qm_prop_get_property;
  object_class->dispose      = &_ncm_qm_prop_dispose;
  object_class->finalize     = &_ncm_qm_prop_finalize;
  
  g_object_class_install_property (object_class,
                                   PROP_LAMBDA,
                                   g_param_spec_double ("lambda",
                                                        NULL,
                                                        "\\lambda",
                                                        0.0, G_MAXDOUBLE, 0.5,
                                                        G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_NAME | G_PARAM_STATIC_BLURB));
  g_object_class_install_property (object_class,
                                   PROP_NP,
                                   g_param_spec_uint ("np",
                                                      NULL,
                                                      "n_p",
                                                      10, G_MAXUINT, 20000,
                                                      G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_NAME | G_PARAM_STATIC_BLURB));
  g_object_class_install_property (object_class,
                                   PROP_ABSTOL,
                                   g_param_spec_double ("abstol",
                                                        NULL,
                                                        "absolute tolerance",
                                                        0.0, G_MAXDOUBLE, 1.0e-50,
                                                        G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_NAME | G_PARAM_STATIC_BLURB));
  g_object_class_install_property (object_class,
                                   PROP_RELTOL,
                                   g_param_spec_double ("reltol",
                                                        NULL,
                                                        "relative tolerance",
                                                        0.0, 1.0, 1.0e-9,
                                                        G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_NAME | G_PARAM_STATIC_BLURB));
  g_object_class_install_property (object_class,
                                   PROP_NKNOTS,
                                   g_param_spec_uint ("nknots",
                                                      NULL,
                                                      "n_k",
                                                      6, G_MAXUINT, 50,
                                                      G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_NAME | G_PARAM_STATIC_BLURB));
  g_object_class_install_property (object_class,
                                   PROP_NOBOUNDARY,
                                   g_param_spec_boolean ("noboundary",
                                                         NULL,
                                                         "no boundary condition at x_f",
                                                         TRUE,
                                                         G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_NAME | G_PARAM_STATIC_BLURB));
  
}

/**
 * ncm_qm_prop_gauss_new:
 * @mean: gaussian mean
 * @alpha: power-law 
 * @sigma: Gaussian width 
 * @Hi: linear imaginary phase
 * 
 * Creates a new Gaussian wave function.
 * 
 * Returns: (transfer full): a new #NcmQMPropGauss
 */
NcmQMPropGauss *
ncm_qm_prop_gauss_new (const gdouble mean, const gdouble alpha, const gdouble sigma, const gdouble Hi)
{
  NcmQMPropGauss *qm_gauss = g_new (NcmQMPropGauss, 1);

  qm_gauss->mean  = mean;
  qm_gauss->alpha = alpha;
  qm_gauss->sigma = sigma;
  qm_gauss->Hi    = Hi;

  g_assert_cmpfloat (alpha, >, -0.5);
  
  {
    gint signp             = 0;
    const gdouble f1       = (alpha - 0.5) * M_LN2 + (2.0 * alpha + 1.0) * log (sigma);
    const gdouble ap12     = (alpha + 0.5);
    const gdouble lng_ap12 = lgamma_r (ap12, &signp);
    if (mean != 0.0)
    {
      const gdouble ap1     = (alpha + 1.0);
      const gdouble arg     = mean / (M_SQRT2 * sigma);
      const gdouble arg2    = arg * arg;
      const gdouble lng_ap1 = lgamma_r (ap1,  &signp);
      const gdouble t1      = log (2.0 * fabs (arg)) + lng_ap1  + log (gsl_sf_hyperg_1F1 (0.5 - alpha, 1.5, - arg2));
      const gdouble t2      =                          lng_ap12 + log (gsl_sf_hyperg_1F1 (    - alpha, 0.5, - arg2));

      qm_gauss->lnNorm = log (exp (t2) + GSL_SIGN (arg) * exp (t1)) + f1;
    }
    else
    {
      qm_gauss->lnNorm = lng_ap12 + f1;
    }
  }
  
  return qm_gauss;
}

/**
 * ncm_qm_prop_gauss_dup:
 * @qm_gauss: a #NcmQMPropGauss
 * 
 * Duplicates @qm_gauss.
 * 
 * Returns: (transfer full): a duplicate of @qm_gauss.
 */
NcmQMPropGauss *
ncm_qm_prop_gauss_dup (NcmQMPropGauss *qm_gauss)
{
  NcmQMPropGauss *qm_gauss_dup = g_new (NcmQMPropGauss, 1);
  qm_gauss_dup[0] = qm_gauss[0];

  return qm_gauss_dup;
}

/**
 * ncm_qm_prop_gauss_free:
 * @qm_gauss: a #NcmQMPropGauss
 * 
 * Frees @qm_gauss.
 * 
 */
void 
ncm_qm_prop_gauss_free (NcmQMPropGauss *qm_gauss)
{
  g_free (qm_gauss);
}

/**
 * ncm_qm_prop_gauss_eval:
 * @qm_gauss: a #NcmQMPropGauss
 * @x: the point where to evaluate $\psi(x)$
 * @psi: (out caller-allocates) (array fixed-size=2) (element-type gdouble): $\psi$
 * 
 * Evaluates @qm_gauss at @x.
 * 
 */
void 
ncm_qm_prop_gauss_eval (NcmQMPropGauss *qm_gauss, const gdouble x, gdouble *psi)
{  
  const gdouble lnx     = log (x);
  const gdouble xmean   = x - qm_gauss->mean;
  const gdouble xmean2  = xmean * xmean;
  const gdouble sigma2  = qm_gauss->sigma * qm_gauss->sigma;
  complex double psi0c  = cexp (- 0.5 * qm_gauss->lnNorm + qm_gauss->alpha * lnx - 0.25 * xmean2 / sigma2 + 0.5 * xmean2 * I * qm_gauss->Hi);
  
  psi[0] = creal (psi0c);
  psi[1] = cimag (psi0c);
}

/**
 * ncm_qm_prop_gauss_eval_hermit:
 * @qm_gauss: a #NcmQMPropGauss
 * @x: the point where to evaluate $\psi(x)$
 * @psi: (out caller-allocates) (array fixed-size=2) (element-type gdouble): $\psi$
 * 
 * Evaluates @qm_gauss at @x removing the Hemite weight.
 * 
 */
void 
ncm_qm_prop_gauss_eval_hermit (NcmQMPropGauss *qm_gauss, const gdouble x, gdouble *psi)
{  
  const gdouble xmean   = x - qm_gauss->mean;
  const gdouble xmean2  = xmean * xmean;
  complex double psi0c  = cexp (- 0.5 * qm_gauss->lnNorm + 0.5 * xmean2 * I * qm_gauss->Hi);

  psi[0] = creal (psi0c);
  psi[1] = cimag (psi0c);
}

/**
 * ncm_qm_prop_exp_new:
 * @n: power-law 
 * @V: Volume 
 * @pV: Volume momentum
 * 
 * Creates a new Exponential wave function.
 * 
 * Returns: (transfer full): a new #NcmQMPropExp
 */
NcmQMPropExp *
ncm_qm_prop_exp_new (const gdouble n, const gdouble V, const gdouble pV)
{
  NcmQMPropExp *qm_exp = g_new (NcmQMPropExp, 1);

  qm_exp->n  = n;
  qm_exp->V  = V;
  qm_exp->pV = pV;

  g_assert_cmpfloat (n, >, 2.0);
  g_assert_cmpfloat (V, >, 0.0);
  
  {
    gint signp     = 0;
    qm_exp->lnNorm = - n * log (n - 1.0) + lgamma_r (n, &signp) + log (V);
  }
  
  return qm_exp;
}

/**
 * ncm_qm_prop_exp_dup:
 * @qm_exp: a #NcmQMPropExp
 * 
 * Duplicates @qm_exp.
 * 
 * Returns: (transfer full): a duplicate of @qm_exp.
 */
NcmQMPropExp *
ncm_qm_prop_exp_dup (NcmQMPropExp *qm_exp)
{
  NcmQMPropExp *qm_exp_dup = g_new (NcmQMPropExp, 1);
  qm_exp_dup[0] = qm_exp[0];

  return qm_exp_dup;
}

/**
 * ncm_qm_prop_exp_free:
 * @qm_exp: a #NcmQMPropExp
 * 
 * Frees @qm_exp.
 * 
 */
void 
ncm_qm_prop_exp_free (NcmQMPropExp *qm_exp)
{
  g_free (qm_exp);
}

/**
 * ncm_qm_prop_exp_eval:
 * @qm_exp: a #NcmQMPropExp
 * @x: the point where to evaluate $\psi(x)$
 * @psi: (out caller-allocates) (array fixed-size=2) (element-type gdouble): $\psi$
 * 
 * Evaluates @qm_exp at @x.
 * 
 */
void 
ncm_qm_prop_exp_eval (NcmQMPropExp *qm_exp, const gdouble x, gdouble *psi)
{  
  const gdouble xV      = x / qm_exp->V;
  const gdouble lnxV    = log (xV);
  complex double psi0c  = cexp (-0.5 * qm_exp->lnNorm + 0.5 * (qm_exp->n - 1.0) * (lnxV - xV) + I * qm_exp->pV * x);
  
  psi[0] = creal (psi0c);
  psi[1] = cimag (psi0c);
}

/**
 * ncm_qm_prop_new:
 * 
 * Creates a new #NcmQMProp object.
 * 
 * Returns: a new #NcmQMProp.
 */
NcmQMProp *
ncm_qm_prop_new (void)
{
  NcmQMProp *qm_prop = g_object_new (NCM_TYPE_QM_PROP,
                                     NULL);
  return qm_prop;
}

/**
 * ncm_qm_prop_ref:
 * @qm_prop: a #NcmQMProp
 *
 * Increase the reference of @qm_prop by one.
 *
 * Returns: (transfer full): @qm_prop.
 */
NcmQMProp *
ncm_qm_prop_ref (NcmQMProp *qm_prop)
{
  return g_object_ref (qm_prop);
}

/**
 * ncm_qm_prop_free:
 * @qm_prop: a #NcmQMProp
 *
 * Decrease the reference count of @qm_prop by one.
 *
 */
void
ncm_qm_prop_free (NcmQMProp *qm_prop)
{
  g_object_unref (qm_prop);
}

/**
 * ncm_qm_prop_clear:
 * @qm_prop: a #NcmQMProp
 *
 * Decrease the reference count of @qm_prop by one, and sets the pointer *qm_prop to
 * NULL.
 *
 */
void
ncm_qm_prop_clear (NcmQMProp **qm_prop)
{
  g_clear_object (qm_prop);
}

/**
 * ncm_qm_prop_set_nknots:
 * @qm_prop: a #NcmQMProp
 * @nknots: number of knots
 *
 * Sets the initial number of knots to be used in the wave function mesh.
 * 
 */
void 
ncm_qm_prop_set_nknots (NcmQMProp *qm_prop, const guint nknots)
{
  NcmQMPropPrivate * const self = qm_prop->priv;
  self->nknots = nknots;
}

/**
 * ncm_qm_prop_get_nknots:
 * @qm_prop: a #NcmQMProp
 *
 * Gets the current number of knots used in the wave function mesh.
 * 
 * Returns: the current number of knots used in the wave function mesh.
 */
guint 
ncm_qm_prop_get_nknots (NcmQMProp *qm_prop)
{
  NcmQMPropPrivate * const self = qm_prop->priv;
  return self->nknots;
}

static gdouble 
_ncm_qm_prop_Jnu (const gdouble nu, const gdouble z)
{
  if (nu >= 0.0)
    return gsl_sf_bessel_Jnu (nu, z);
  else
    return gsl_sf_bessel_Jnu (-nu, z) * cos (- nu * M_PI) - gsl_sf_bessel_Ynu (-nu, z) * sin (- nu * M_PI);
}

/**
 * ncm_qm_prop_eval:
 * @qm_prop: a #NcmQMProp
 * @t: time difference
 * @x: $x$ coordinate
 * @y: $y$ coordinate
 * @G: (out caller-allocates) (array fixed-size=2) (element-type gdouble): $G$
 * 
 * Calculates the propagator at $G (x,\;y;\;t)$.
 * 
 */
void
ncm_qm_prop_eval (NcmQMProp *qm_prop, const gdouble x, const gdouble y, const gdouble t, gdouble *G)
{
  NcmQMPropPrivate * const self = qm_prop->priv;
  const gdouble x2  = x * x;
  const gdouble y2  = y * y;
  const gdouble f1  = 0.5 * sqrt (x * y) / t;
  const gdouble f2  = _ncm_qm_prop_Jnu (self->nu, 0.5 * x * y / t);
  complex double Gc = -I * f1 * f2 * cexp (I * (x2 + y2) * 0.25 / t - I * M_PI * 0.5 * self->nu);

/*printf ("% 22.15g\n", f2);*/
  
  G[0] = creal (Gc);
  G[1] = cimag (Gc);
}

/**
 * ncm_qm_prop_eval_array:
 * @qm_prop: a #NcmQMProp
 * @t: time difference
 * @x: $x$ coordinate
 * @ya: $y$ coordinate
 * @G: $G$
 * 
 * Calculates the propagator at $G (x,\;y;\;t)$.
 * 
 */
void
ncm_qm_prop_eval_array (NcmQMProp *qm_prop, const gdouble x, const gdouble *ya, gsize n, const gdouble t, gdouble *G)
{
  NcmQMPropPrivate * const self = qm_prop->priv;
  const gdouble x2   = x * x;
  complex double *Gc = (complex double *) G;

  gint i;

  if (FALSE)
  {
    acb_t nu, a, res;

    acb_init (nu);
    acb_init (a);
    acb_init (res);

    acb_set_d (nu, self->nu);

    for (i = 0; i < n; i++)
    {
      const gdouble arg = 0.5 * x * ya[i] / t;

      acb_set_d (a, arg);
      acb_hypgeom_bessel_j (res, nu, a, 120);

      self->Jnu[i] = arf_get_d (arb_midref (acb_realref (res)), ARF_RND_NEAR);

      /*printf ("% 22.15g % 22.15g\n", arg, _ncm_qm_prop_Jnu (self->nu, arg) / self->Jnu[i] - 1.0);*/
    }

    acb_clear (nu);
    acb_clear (a);
    acb_clear (res);
  }
  else if (FALSE)
  {
    for (i = 0; i < n; i++)
    {
      const gdouble arg = 0.5 * x * ya[i] / t;
      self->Jnu[i] = arg;
    }
    
    gsl_sf_bessel_sequence_Jnu_e (self->nu, GSL_PREC_DOUBLE, n, self->Jnu);
  }
  else
  {
    for (i = 0; i < n; i++)
    {
      const gdouble arg = 0.5 * x * ya[i] / t;
      self->Jnu[i] = _ncm_qm_prop_Jnu (self->nu, arg);
    }
  }
  
  //gsl_sf_bessel_sequence_Jnu_e (self->nu, GSL_PREC_DOUBLE, n, self->Jnu); GSL_PREC_APPROX
  //gsl_sf_bessel_sequence_Jnu_e (self->nu, GSL_PREC_APPROX, n, self->Jnu); 
  
  for (i = 0; i < n; i++)
  {
    const gdouble y   = ya[i];
    const gdouble y2  = y * y;
    const gdouble f1  = 0.5 * sqrt (x * y) / t;

    Gc[i] = -I * f1 * self->Jnu[i] * cexp (I * (x2 + y2) * 0.25 / t - I * M_PI * 0.5 * self->nu); 
  }  
}

/**
 * ncm_qm_prop_gauss_ini:
 * @qm_prop: a #NcmQMProp
 * @alpha: FIXME
 * @sigma: FIXME
 * @Hi: FIXME
 * 
 * Calculates the propagator at $G (x,\;y;\;t)$.
 * 
 */
void
ncm_qm_prop_gauss_ini (NcmQMProp *qm_prop, const gdouble mean, const gdouble alpha, const gdouble sigma, const gdouble Hi)
{
  NcmQMPropPrivate * const self = qm_prop->priv;
  NcmQMPropGauss *qm_gauss = ncm_qm_prop_gauss_new (mean, alpha, sigma, Hi);

  gsl_integration_fixed_workspace * ws = 
    gsl_integration_fixed_alloc (gsl_integration_fixed_hermite, self->np, 0.0, 0.25 / (sigma * sigma), alpha, 0.0);
  const gdouble *nodes   = gsl_integration_fixed_nodes (ws);
  const gdouble *weights = gsl_integration_fixed_weights (ws);
  gint i;

  self->n = 0;
  g_clear_pointer (&self->nodes,   g_free);
  g_clear_pointer (&self->weights, g_free);
  g_clear_pointer (&self->G,       g_free);
  g_clear_pointer (&self->Jnu,     g_free);

  self->nodes   = g_new0 (gdouble, self->np);
  self->weights = g_new0 (complex double, self->np);
  self->Jnu     = g_new0 (gdouble, self->np);
  self->G       = g_new0 (complex double, self->np);

  for (i = self->np / 2; i < self->np; i++)
  {
    if (weights[i] < self->abstol)
      break;
    else
    {
      const gdouble x = nodes[i];
      complex double psi0c;

      ncm_qm_prop_gauss_eval_hermit (qm_gauss, x, (gdouble *)&psi0c);

      self->nodes[self->n]   = x;
      self->weights[self->n] = psi0c * weights[i];

      self->n++;
      /*printf ("%d % 22.15g % 22.15g | % 22.15g % 22.15g\n", i, nodes[i], weights[i], creal (psic), cimag (psic));*/
    }
  }

  ncm_qm_prop_gauss_free (qm_gauss);
  gsl_integration_fixed_free (ws);
}

typedef struct _NcmQMPropInt
{
  NcmQMProp *qm_prop;
  NcmQMPropPrivate * const self;
  const gdouble t;
  const gdouble x;
  const gdouble alpha;
  const gdouble sigma;
  const gdouble Hi;
} NcmQMPropInt;

static gdouble 
_ncm_qm_prop_propto_Re_integ (gpointer userdata, const gdouble y, const gdouble w)
{
  NcmQMPropInt *integ   = (NcmQMPropInt *) userdata;
  gint signp            = 0;
  const gdouble x       = integ->x;
  const gdouble t       = integ->t;
  const gdouble alpha   = integ->alpha;
  const gdouble sigma   = integ->sigma;
  const gdouble Hi      = integ->Hi;
  const gdouble lny     = log (y);
  const gdouble x2      = x * x;
  const gdouble y2      = y * y;
  const gdouble ap12    = alpha + 0.5;
  const gdouble f1      = (1.0 - 2.0 * alpha) * 0.25 * M_LN2;
  const gdouble lnsigma = log (sigma);
  const gdouble lng     = lgamma_r (ap12, &signp);
  complex double psi0   = cexp (f1 - ap12 * lnsigma + (alpha - 1.0) * lny - 0.5 * lng + 0.5 * y2 * I * Hi);
  const gdouble f2      = 0.5 * sqrt (x * y) / t;
  complex double G      = -I * f2 *  _ncm_qm_prop_Jnu (integ->self->nu, 0.5 * x * y / t) * cexp (I * (x2 + y2) * 0.25 / t - I * M_PI * 0.5 * integ->self->nu); 

  /*printf ("% 22.15g % 22.15g % 22.15g\n", y, creal (psi0 * G), cimag (psi0 * G));*/
  
  return creal (psi0 * G);
}

static gdouble 
_ncm_qm_prop_propto_Im_integ (gpointer userdata, const gdouble y, const gdouble w)
{
  NcmQMPropInt *integ   = (NcmQMPropInt *) userdata;
  gint signp            = 0;
  const gdouble x       = integ->x;
  const gdouble t       = integ->t;
  const gdouble alpha   = integ->alpha;
  const gdouble sigma   = integ->sigma;
  const gdouble Hi      = integ->Hi;
  const gdouble lny     = log (y);
  const gdouble x2      = x * x;
  const gdouble y2      = y * y;
  const gdouble ap12    = alpha + 0.5;
  const gdouble f1      = (1.0 - 2.0 * alpha) * 0.25 * M_LN2;
  const gdouble lnsigma = log (sigma);
  const gdouble lng     = lgamma_r (ap12, &signp);
  complex double psi0   = cexp (f1 - ap12 * lnsigma + (alpha - 1.0) * lny - 0.5 * lng + 0.5 * y2 * I * Hi);
  const gdouble f2      = 0.5 * sqrt (x * y) / t;
  complex double G      = -I * f2 *  _ncm_qm_prop_Jnu (integ->self->nu, 0.5 * x * y / t) * cexp (I * (x2 + y2) * 0.25 / t - I * M_PI * 0.5 * integ->self->nu); 

  return cimag (psi0 * G);
}

/**
 * ncm_qm_prop_propto:
 * @qm_prop: a #NcmQMProp
 * @x: FIXME
 * @t: FIXME
 * @psi: (out caller-allocates) (array fixed-size=2) (element-type gdouble): $G$
 * 
 * Calculates the propagator at $G (x,\;y;\;t)$.
 * 
 */
void
ncm_qm_prop_propto (NcmQMProp *qm_prop, const gdouble x, const gdouble t, gdouble *psi)
{
  NcmQMPropPrivate * const self = qm_prop->priv;
  complex double psic = 0.0;
  gint i;

  if (TRUE)
  {
    ncm_qm_prop_eval_array (qm_prop, x, self->nodes, self->n, t, (gdouble *)self->G);

    for (i = 0; i < self->n; i++)
    {
      psic += self->weights[i] * self->G[i];
      //printf ("%d % 22.15g % 22.15g % 22.15g | % 22.15g % 22.15g\n", i, self->nodes[i], creal (self->weights[i]), cimag (self->weights[i]), creal (psic), cimag (psic));
    }

    psi[0] = creal (psic);
    psi[1] = cimag (psic);
  }
  else if (FALSE)
  {
    const gdouble alpha = 1.0;
    const gdouble sigma = 1.0;
    const gdouble Hi    = 1.0;
    NcmQMPropInt integ = {qm_prop, self, t, x, alpha, sigma, Hi};
    gdouble err = 0.0;
    
    ncm_integral1d_clear (&self->Re_int);
    ncm_integral1d_clear (&self->Im_int);

    self->Re_int = NCM_INTEGRAL1D (ncm_integral1d_ptr_new (&_ncm_qm_prop_propto_Re_integ, NULL));
    self->Im_int = NCM_INTEGRAL1D (ncm_integral1d_ptr_new (&_ncm_qm_prop_propto_Im_integ, NULL));

    ncm_integral1d_set_reltol (self->Re_int, 1.0e-5);
    ncm_integral1d_set_reltol (self->Im_int, 1.0e-5);
    
    ncm_integral1d_ptr_set_userdata (NCM_INTEGRAL1D_PTR (self->Re_int), &integ);
    ncm_integral1d_ptr_set_userdata (NCM_INTEGRAL1D_PTR (self->Im_int), &integ);

    psi[0] = ncm_integral1d_eval_gauss_hermite1_r_p (self->Re_int, 0.25 / (sigma * sigma), &err);
    psi[1] = ncm_integral1d_eval_gauss_hermite1_r_p (self->Im_int, 0.25 / (sigma * sigma), &err);
  }
}

static gdouble 
_ncm_qm_prop_propto_norm (const gdouble x, gpointer p)
{
  NcmQMPropInt *integ = (NcmQMPropInt *) p;
  complex double psi;

  ncm_qm_prop_propto (integ->qm_prop, x, integ->t, (gdouble *)&psi);

  return creal (psi) * creal (psi) + cimag (psi) * cimag (psi);
}

/**
 * ncm_qm_prop_propto_norm:
 * @qm_prop: a #NcmQMProp
 * @t: FIXME
 * 
 * Calculates the propagator at $G (x,\;y;\;t)$.
 * 
 */
gdouble
ncm_qm_prop_propto_norm (NcmQMProp *qm_prop, const gdouble t)
{
  NcmQMPropPrivate * const self = qm_prop->priv;
  NcmQMPropInt integ = {qm_prop, self, t};
  gsl_integration_workspace **w = ncm_integral_get_workspace ();
  gdouble result, abserr;
  gsl_function F;

  F.params   = &integ;
  F.function = &_ncm_qm_prop_propto_norm;

  gsl_integration_qagiu (&F, 0.0, 1.0e-7, 1.0e-7, NCM_INTEGRAL_PARTITION, *w, &result, &abserr);

  ncm_memory_pool_return (w);

  return result;
}

typedef struct _NcmQMPropInitCond
{
  NcmQMPropPsi psi0;
  gpointer psi_data;
} NcmQMPropInitCond;

static gdouble
_ncm_qm_prop_set_init_cond_real (gdouble x, gpointer p)
{
  NcmQMPropInitCond *ic = (NcmQMPropInitCond *) p;
  complex double psi0_x;

  ic->psi0 (ic->psi_data, x, (gdouble *) &psi0_x);
  
  return creal (psi0_x);
}

static complex double
_ncm_qm_prop_set_init_cond_complex (gdouble x, gpointer p)
{
  NcmQMPropInitCond *ic = (NcmQMPropInitCond *) p;
  complex double psi0_x;

  ic->psi0 (ic->psi_data, x, (gdouble *) &psi0_x);

  return psi0_x;
}

void _ncm_qm_prop_init_solver (NcmQMProp *qm_prop);
void _ncm_qm_prop_init_spec_solver (NcmQMProp *qm_prop, NcmQMPropInitCond *ic);

complex double
_ncm_qm_prop_get_YN (NcmQMProp *qm_prop, const gdouble t)
{
  NcmQMPropPrivate * const self = qm_prop->priv;
  if (self->YNp1->len == 0)
  {
    return 0.0;
  }
  if (t == 0.0)
  {
    return g_array_index (self->YNp1, complex double, 0);
  }
  else
  {
    complex double A      = 1.0 / self->sqrt_aN_cN;
    complex double T      = -I / self->sqrt_aN_cN;
    complex double YN     = 0.0;
    const gdouble arg     = t * self->sqrt_aNcN;
    /*const gdouble abstol  = GSL_DBL_EPSILON;*/
    const gdouble epsilon = 1.0;
    const gint last_m     = self->YNp1->len - 1;
    const gint asym_m     = GSL_MAX (gsl_pow_2 (2.0 * arg / epsilon), 1);
    gdouble Jn, Jnm1, Jnp1;
    gint n, m;

    if (asym_m > last_m)
    {
      m = last_m;
    }
    else
    {
      const gdouble logabstol = log (self->abstol);
      const gdouble logarg    = log (arg);
      gint newm, oldm = asym_m;
      gint c = 0;
      gint signp;

      while (TRUE)
      {
        newm =  logabstol / (logarg - lgamma_r (oldm, &signp) / oldm);
        c++;
        if (abs (oldm - newm) <= 1)
        {
          newm = GSL_MAX (oldm, newm);
          m    = GSL_MIN (last_m, newm);
          break;
        }
        else
        {
          oldm = newm;
        }
      }
      
      printf ("[%3d] %3d %3d %3d | %3d %3d % 22.15g % 22.15g\n", c, m, asym_m, last_m, newm, oldm, jn (newm, 2.0 * arg), jn (oldm, 2.0 * arg));
    }

    Jn   = jn (m + 0, 2.0 * arg);
    Jnp1 = jn (m + 1, 2.0 * arg);
    
    for (n = m; n > 0; n--)
    {
      const gdouble t1        = n * Jn / (t * self->cN);
      const complex double dY = g_array_index (self->YNp1, complex double, n - 1) * A * t1;

      YN += dY;
/*
      if (
          (fabs (creal (dY) / creal (YN)) < abstol) && 
          (fabs (cimag (dY) / cimag (YN)) < abstol)
          )
        break;
*/
      /*printf ("%d % 22.15g % 22.15g\n", n, cabs (YN), cabs (g_array_index (self->YNp1, complex double, i) * A * t1));*/
      /*printf ("%d % 22.15g % 22.15g %e | % 22.15g\n", n, Jn, jn (n, 2.0 * arg), Jn / jn (n, 2.0 * arg) - 1.0, cabs (YN));*/

      Jnm1 = n * 1.0 / arg * Jn - Jnp1;
      Jnp1 = Jn;
      Jn   = Jnm1;
      
      A *= T;
    }

    return YN * cexp (-2.0 * I * t * self->bN);
  }
}

gdouble
_ncm_qm_prop_get_Re_YN (const gdouble t, gpointer qm_prop)
{
  /*printf ("% 22.15g % 22.15g\n", t, creal (_ncm_qm_prop_get_YN (qm_prop, t)));*/
  return creal (_ncm_qm_prop_get_YN (qm_prop, t));
}

gdouble
_ncm_qm_prop_get_Im_YN (const gdouble t, gpointer qm_prop)
{
  return cimag (_ncm_qm_prop_get_YN (qm_prop, t));
}

/**
 * ncm_qm_prop_set_init_cond:
 * @qm_prop: a #NcmQMProp
 * @psi0: (scope call): Initial wave-function
 * @psi_data: Initial wave-function data
 * @xi: initial point
 * @xf: final point
 * 
 * Sets the initial condition using @psi0, it calculates the best
 * mesh for the initial condition using the real part of @psi0.
 * 
 */
void
ncm_qm_prop_set_init_cond (NcmQMProp *qm_prop, NcmQMPropPsi psi0, gpointer psi_data, const gdouble xi, const gdouble xf)
{
  NcmQMPropPrivate * const self = qm_prop->priv;
  NcmQMPropInitCond ic = {psi0, psi_data};
  gboolean use_spline  = FALSE;
  gsl_function F;
  gint i;

  g_assert_cmpfloat (xi, <, xf);

  self->ti = 0.0;
  self->xi = xi;
  self->xf = xf;

  ncm_vector_clear (&self->knots);
  
  if (use_spline)
  {
    F.params   = &ic;
    F.function = _ncm_qm_prop_set_init_cond_real;
    ncm_spline_set_func (self->psi0_s, NCM_SPLINE_FUNCTION_SPLINE, &F, xi, xf, 0, 1.0e-2/*self->reltol*/);

    self->knots  = ncm_spline_get_xv (self->psi0_s);
    self->nknots = ncm_vector_len (self->knots);
  }
  else
  {
    self->knots = ncm_vector_new (self->nknots);
    for (i = 0; i < self->nknots; i++)
    {
      const gdouble x = xi + (xf - xi) / (self->nknots - 1.0) * i;
      ncm_vector_fast_set (self->knots, i, x);
    }
  }
  
  {
    ncm_vector_clear (&self->spec_knots);

    ncm_vector_clear (&self->dS_v);
    ncm_vector_clear (&self->rho_v);

    ncm_vector_clear (&self->Re_psi_v);
    ncm_vector_clear (&self->Im_psi_v);

    ncm_spline_clear (&self->dS_s);
    ncm_spline_clear (&self->rho_s);

    ncm_spline_clear (&self->Re_psi_s);
    ncm_spline_clear (&self->Im_psi_s);
    
    self->rho_s      = ncm_spline_cubic_notaknot_new ();
    self->dS_s       = ncm_spline_cubic_notaknot_new ();

    self->Re_psi_s   = ncm_spline_cubic_notaknot_new ();
    self->Im_psi_s   = ncm_spline_cubic_notaknot_new ();

    self->spec_knots = ncm_vector_new (self->nknots + 1);

    self->rho_v      = ncm_vector_new (self->nknots + 1);
    self->dS_v       = ncm_vector_new (self->nknots + 1);

    self->Re_psi_v   = ncm_vector_new (self->nknots + 1);
    self->Im_psi_v   = ncm_vector_new (self->nknots + 1);
    
    ncm_spline_set (self->rho_s, self->spec_knots, self->rho_v, FALSE);
    ncm_spline_set (self->dS_s,  self->spec_knots, self->dS_v,  FALSE);

    ncm_spline_set (self->Re_psi_s, self->spec_knots, self->Re_psi_v, FALSE);
    ncm_spline_set (self->Im_psi_s, self->spec_knots, self->Im_psi_v, FALSE);

    self->up_splines = FALSE;
  }

  g_clear_pointer (&self->y, N_VDestroy);
  self->y = N_VNew_Serial (2 * self->nknots);
  NCM_CVODE_CHECK (&self->y, "N_VNew_Serial", 0, );

  {
    complex double *Y = (complex double *) N_VGetArrayPointer (self->y);
    printf ("# USING %d knots!\n", self->nknots);
    for (i = 0; i < self->nknots; i++)
    {
      const gdouble x = ncm_vector_fast_get (self->knots, i);
      Y[i] = _ncm_qm_prop_set_init_cond_complex (x, &ic);
    }
    /*Y[self->nknots - 2] = 0.0;*/
    if (!self->noboundary)
      Y[self->nknots - 1] = 0.0;
    else
    {
      const gdouble xNm1 = ncm_vector_fast_get (self->knots, self->nknots - 1);
      const gdouble xNm2 = ncm_vector_fast_get (self->knots, self->nknots - 2);
      const gdouble h    = xNm1 - xNm2;
      gdouble x          = xNm1 + h;
      gdouble absY;

      g_array_set_size (self->YNp1, 0);
      self->h          = h;
      self->aN         = +1.0 / (h * h);
      self->cN         = +1.0 / (h * h);
      self->bN         = -2.0 / (h * h) + self->lambda / gsl_pow_2 (xNm1);
      self->sqrt_aNcN  = sqrt (self->aN * self->cN);
      self->sqrt_aN_cN = sqrt (self->aN / self->cN);

      Y[self->nknots - 1] = _ncm_qm_prop_set_init_cond_complex (xNm1, &ic);
      
      while (TRUE)
      {
        const complex double Yx = _ncm_qm_prop_set_init_cond_complex (x, &ic);
        absY = cabs (Yx);

        if (absY > self->abstol)
        {
          g_array_append_val (self->YNp1, Yx);
          x += h;
        }
        else
          break;
      }

      if (FALSE)
      {
        gsl_function F;

        F.params   = qm_prop;
        
        F.function = &_ncm_qm_prop_get_Re_YN;
        ncm_spline_set_func (self->YNp1_Re_R, NCM_SPLINE_FUNCTION_SPLINE, &F, 0.0, 10.0, 0, 1.0e-2);

        F.function = &_ncm_qm_prop_get_Im_YN;
        ncm_spline_set_func (self->YNp1_Im_R, NCM_SPLINE_FUNCTION_SPLINE, &F, 0.0, 10.0, 0, 1.0e-2);      
      }
    }
  }

  _ncm_qm_prop_init_spec_solver (qm_prop, &ic);
  _ncm_qm_prop_init_solver (qm_prop);
}

/**
 * ncm_qm_prop_set_init_cond_gauss:
 * @qm_prop: a #NcmQMProp
 * @qm_gauss: Initial wave-function data
 * @xi: initial point
 * @xf: final point
 * 
 * Sets the initial condition using @psi0 and ncm_qm_prop_gauss_eval(), 
 * it calculates the best mesh for the initial condition using the real 
 * part of @psi0.
 * 
 */
void
ncm_qm_prop_set_init_cond_gauss (NcmQMProp *qm_prop, NcmQMPropGauss *qm_gauss, const gdouble xi, const gdouble xf)
{
  ncm_qm_prop_set_init_cond (qm_prop, (NcmQMPropPsi) &ncm_qm_prop_gauss_eval, qm_gauss, xi, xf);
}

/**
 * ncm_qm_prop_set_init_cond_exp:
 * @qm_prop: a #NcmQMProp
 * @qm_exp: Initial wave-function data
 * @xi: initial point
 * @xf: final point
 * 
 * Sets the initial condition using @psi0 and ncm_qm_prop_exp_eval(), 
 * it calculates the best mesh for the initial condition using the real 
 * part of @psi0.
 * 
 */
void
ncm_qm_prop_set_init_cond_exp (NcmQMProp *qm_prop, NcmQMPropExp *qm_exp, const gdouble xi, const gdouble xf)
{
  ncm_qm_prop_set_init_cond (qm_prop, (NcmQMPropPsi) &ncm_qm_prop_exp_eval, qm_exp, xi, xf);
}

static gint _ncm_qm_prop_f (gdouble t, N_Vector y, N_Vector ydot, gpointer user_data);
static gint _ncm_qm_prop_J (N_Vector v, N_Vector Jv, gdouble t, N_Vector y, N_Vector fy, gpointer user_data, N_Vector tmp);

void
_ncm_qm_prop_init_solver (NcmQMProp *qm_prop)
{
  NcmQMPropPrivate * const self = qm_prop->priv;
  const gdouble t0 = 0.0;
  gint flag;

  g_clear_pointer (&self->LS, SUNLinSolFree);

  if (self->arkode != NULL)
    ARKodeFree (&self->arkode);

  self->arkode = ARKodeCreate ();
  NCM_CVODE_CHECK (&self->arkode, "ARKodeCreate", 0, );

  flag = ARKodeInit (self->arkode, NULL, &_ncm_qm_prop_f, t0, self->y);
  NCM_CVODE_CHECK (&flag, "ARKodeInit", 1, );

  flag = ARKodeSetUserData (self->arkode, (void *) qm_prop);
  NCM_CVODE_CHECK (&flag, "ARKodeSetUserData", 1, );

  flag = ARKodeSetMaxNumSteps (self->arkode, 10000);
  NCM_CVODE_CHECK (&flag, "ARKodeSetMaxNumSteps", 1, );

  flag = ARKodeSStolerances (self->arkode, self->reltol, self->abstol);
  NCM_CVODE_CHECK (&flag, "ARKodeSStolerances", 1, );

  //flag = ARKodeSetAdaptivityMethod (self->arkode, 2, 1, 0, NULL);
  //NCM_CVODE_CHECK (&flag, "ARKodeSetAdaptivityMethod", 1, );
  
  flag = ARKodeSetPredictorMethod (self->arkode, 0);
  NCM_CVODE_CHECK (&flag, "ARKodeSetPredictorMethod", 1, );

  flag = ARKodeSetLinear (self->arkode, 0);
  NCM_CVODE_CHECK (&flag, "ARKodeSetLinear", 1, );
  
  self->LS = SUNPCG (self->y, 0, self->nknots);
  NCM_CVODE_CHECK (&flag, "SUNPCG", 1, );

  flag = ARKSpilsSetLinearSolver (self->arkode, self->LS);
  NCM_CVODE_CHECK (&flag, "ARKSpilsSetLinearSolver", 1, );

  flag = ARKSpilsSetJacTimes (self->arkode, NULL, _ncm_qm_prop_J);
  NCM_CVODE_CHECK (&flag, "ARKSpilsSetJacTimes", 1, );
/*
  flag = ARKBandPrecInit (self->arkode, 2 * self->nknots, 4, 4);
  NCM_CVODE_CHECK (&flag, "ARKBandPrecInit", 1, );
*/
}

void
_ncm_qm_prop_init_spec_solver (NcmQMProp *qm_prop, NcmQMPropInitCond *ic)
{
  NcmQMPropPrivate * const self = qm_prop->priv;
  complex double *Y = (complex double *) N_VGetArrayPointer (self->y);

  gint i;

  g_clear_pointer (&self->dht, gsl_dht_free);
  
  ncm_vector_clear (&self->specReY);
  ncm_vector_clear (&self->specImY);

  ncm_vector_clear (&self->specReYt);
  ncm_vector_clear (&self->specImYt);

  ncm_vector_clear (&self->specReW);
  ncm_vector_clear (&self->specImW);

  self->dht = gsl_dht_alloc (self->nknots);
  gsl_dht_init (self->dht, self->nu, self->xf);

  self->specReY    = ncm_vector_new (self->nknots);
  self->specImY    = ncm_vector_new (self->nknots);

  self->specReYt   = ncm_vector_new (self->nknots);
  self->specImYt   = ncm_vector_new (self->nknots);

  self->specReW    = ncm_vector_new (self->nknots);
  self->specImW    = ncm_vector_new (self->nknots);

  ncm_vector_fast_set (self->spec_knots, 0, 0.0);
  
  for (i = 0; i < self->nknots; i++)
  {
    const gdouble x   = gsl_dht_x_sample (self->dht, i);
    complex double Yn = _ncm_qm_prop_set_init_cond_complex (x, ic) / sqrt (x);

    ncm_vector_fast_set (self->spec_knots, i + 1, x);

    /*printf ("%d % 22.15g % 22.15g % 22.15g\n", i, x, creal (Yn), cimag (Yn));*/
    Y[i] = _ncm_qm_prop_set_init_cond_complex (x, ic);
    
    ncm_vector_fast_set (self->specReY, i, creal (Yn));
    ncm_vector_fast_set (self->specImY, i, cimag (Yn));
  }
  
  gsl_dht_apply (self->dht, ncm_vector_data (self->specReY), ncm_vector_data (self->specReW));
  gsl_dht_apply (self->dht, ncm_vector_data (self->specImY), ncm_vector_data (self->specImW));

/*
  for (i = 0; i < self->nknots; i++)
  {
    printf ("%d % 22.15g % 22.15g % 22.15g\n", i, 
            gsl_dht_k_sample (self->dht, i), 
            ncm_vector_fast_get (self->specReW, i), 
            ncm_vector_fast_get (self->specImW, i)
            );
  }
*/
}

/*
static gdouble
_ncm_qm_prop_diff (const gdouble fp2, const gdouble fp1, const gdouble f, const gdouble dx2, const gdouble dx1)
{
  const gdouble ddx = dx2 - dx1;
  const gdouble a   = -dx1 / (ddx * dx2);
  const gdouble c   = +dx2 / (ddx * dx1);
  const gdouble b   = -(dx2 + dx1) / (dx1 * dx2);

  return a * fp2 + b * f + c * fp1;
}
*/

static complex double
_ncm_qm_prop_cdiff2 (const complex double fp2, const complex double fp1, const complex double f, const gdouble dx2, const gdouble dx1)
{
  const gdouble ddx = dx2 - dx1;
  const gdouble a   = +2.0 / (ddx * dx2);
  const gdouble c   = -2.0 / (ddx * dx1);
  const gdouble b   = +2.0 / (dx1 * dx2);
  
  return a * fp2 + b * f + c * fp1;
}

static gint 
_ncm_qm_prop_f (gdouble t, N_Vector y, N_Vector ydot, gpointer user_data) 
{
  NcmQMProp *qm_prop = NCM_QM_PROP (user_data);
  NcmQMPropPrivate * const self = qm_prop->priv;
  complex double *Y    = NULL;
  complex double *Ydot = NULL;
  sunindextype i;
  
  Y    = (complex double *) N_VGetArrayPointer (y);
  Ydot = (complex double *) N_VGetArrayPointer (ydot);

  N_VConst (0.0, ydot);

  Ydot[0] = 0.0;
  for (i = 1; i < self->nknots - 1; i++)
  {
    const gdouble xi          = ncm_vector_fast_get (self->knots, i);
    const gdouble xim1        = ncm_vector_fast_get (self->knots, i - 1);
    const gdouble xip1        = ncm_vector_fast_get (self->knots, i + 1);
    const complex double d2Yi = _ncm_qm_prop_cdiff2 (Y[i + 1], Y[i - 1], Y[i], xip1 - xi, xim1 - xi); 

    Ydot[i] = -I * (- d2Yi + self->lambda * Y[i] / (xi * xi));

/*
    const gdouble dxR  = xip1 - xi;
    const gdouble dxL  = xi - xim1;
    Ydot[i] = -I * (- 1.0 * (
                             + Y[i - 1] * 2.0 / (dxL * (dxL + dxR)) 
                             - Y[i]     * 2.0 / (dxL * dxR) 
                             + Y[i + 1] * 2.0 / (dxR * (dxL + dxR)))
                    + lambda * Y[i] / (xi * xi));
*/
    /*printf ("%ld % 22.15g % 22.15g % 22.15g\n", i, xi, creal (Ydot[i]), cimag (Ydot[i]));*/
  }
  i = self->nknots - 1;
  if (self->noboundary)
  {
    const gdouble xi          = ncm_vector_fast_get (self->knots, i);
    const gdouble xim1        = ncm_vector_fast_get (self->knots, i - 1);
    const complex double YN   = _ncm_qm_prop_get_YN (qm_prop, t);
    const complex double d2Yi = _ncm_qm_prop_cdiff2 (YN, Y[i - 1], Y[i], self->h, xim1 - xi); 

    Ydot[i] = -I * (- d2Yi + self->lambda * Y[i] / (xi * xi));

    /*printf ("%ld % 22.15g % 22.15g % 22.15g\n", i, xi, creal (Ydot[i]), cimag (Ydot[i]));*/
  }
  else
    Ydot[i] = 0.0;
  
  return 0; 
}

static gint 
_ncm_qm_prop_J (N_Vector v, N_Vector Jv, gdouble t, N_Vector y, N_Vector fy, gpointer user_data, N_Vector tmp) 
{ 
  NcmQMProp *qm_prop = NCM_QM_PROP (user_data);
  NcmQMPropPrivate * const self = qm_prop->priv;
  complex double *V  = NULL;
  complex double *JV = NULL;
  sunindextype i;
  
  V  = (complex double *) N_VGetArrayPointer (v);
  JV = (complex double *) N_VGetArrayPointer (Jv);

  N_VConst (0.0, Jv);

  JV[0] = 0.0;
  for (i = 1; i < self->nknots - 1; i++)
  {
    const gdouble xi          = ncm_vector_fast_get (self->knots, i);
    const gdouble xim1        = ncm_vector_fast_get (self->knots, i - 1);
    const gdouble xip1        = ncm_vector_fast_get (self->knots, i + 1);
    const complex double d2Vi = _ncm_qm_prop_cdiff2 (V[i + 1], V[i - 1], V[i], xip1 - xi, xim1 - xi); 

    JV[i] = -I * (- d2Vi + self->lambda * V[i] / (xi * xi));

    /*printf ("%ld % 22.15g % 22.15g % 22.15g\n", i, xi, creal (JV[i]), cimag (JV[i]));*/
  }
  i = self->nknots - 1;
  if (self->noboundary)
  {
    const gdouble xi          = ncm_vector_fast_get (self->knots, i);
    const gdouble xim1        = ncm_vector_fast_get (self->knots, i - 1);
    const complex double d2Vi = _ncm_qm_prop_cdiff2 (0.0, V[i - 1], V[i], self->h, xim1 - xi); 

    JV[i] = -I * (- d2Vi + self->lambda * V[i] / (xi * xi));

    /*printf ("%ld % 22.15g % 22.15g % 22.15g\n", i, xi, creal (JV[i]), cimag (JV[i]));*/
  }
  else
    JV[i] = 0.0;
  
  return 0; 
}

static gdouble
_ncm_qm_prop_calc_dS (gdouble x, NcmQMPropPrivate * const self)
{
  const gdouble x0 = ncm_vector_fast_get (self->spec_knots, 1);
  
  if (FALSE && (x < x0))
  {
    NcmSplineCubic *Re_sc = NCM_SPLINE_CUBIC (self->Re_psi_s);
    NcmSplineCubic *Im_sc = NCM_SPLINE_CUBIC (self->Im_psi_s);
    const gdouble br      = ncm_vector_get (Re_sc->b, 0);
    const gdouble bi      = ncm_vector_get (Im_sc->b, 0);
    const gdouble cr      = ncm_vector_get (Re_sc->c, 0);
    const gdouble ci      = ncm_vector_get (Im_sc->c, 0);
    const gdouble dr      = ncm_vector_get (Re_sc->d, 0);
    const gdouble di      = ncm_vector_get (Im_sc->d, 0);
    const gdouble rho_x2  = gsl_pow_2 (br + (cr + dr * x) * x) + gsl_pow_2 (bi + (ci + di * x) * x);
    const gdouble dS      = ((br * ci - bi * cr) + (2.0 * (br * di - bi * dr) + (cr * di - ci * dr) * x) * x) / rho_x2;

    if (FALSE)
    {
      const gdouble Re_psi  = ncm_spline_eval (self->Re_psi_s, x);
      const gdouble Im_psi  = ncm_spline_eval (self->Im_psi_s, x);
      const gdouble Re_dpsi = ncm_spline_eval_deriv (self->Re_psi_s, x);
      const gdouble Im_dpsi = ncm_spline_eval_deriv (self->Im_psi_s, x);
      const gdouble rho     = gsl_pow_2 (Re_psi) + gsl_pow_2 (Im_psi);
      const gdouble dS2     = (Re_psi * Im_dpsi - Re_dpsi * Im_psi) / rho;

      printf ("% 22.15g % 22.15g % 22.15g %e\n", x, dS, dS2, dS / dS2 - 1.0);
    }
    
    return dS;
  }
  else
  {
    const gdouble Re_psi  = ncm_spline_eval (self->Re_psi_s, x);
    const gdouble Im_psi  = ncm_spline_eval (self->Im_psi_s, x);
    const gdouble Re_dpsi = ncm_spline_eval_deriv (self->Re_psi_s, x);
    const gdouble Im_dpsi = ncm_spline_eval_deriv (self->Im_psi_s, x);
    const gdouble rho     = gsl_pow_2 (Re_psi) + gsl_pow_2 (Im_psi);
    const gdouble dS      = (Re_psi * Im_dpsi - Re_dpsi * Im_psi) / rho;

    /*printf ("% 22.15g % 22.15g\n", x, dS);*/
    return dS;
  }
}

static void
_ncm_qm_prop_prepare_dS_rho (NcmQMPropPrivate * const self, const gdouble t, gdouble *Y)
{
  if (!self->up_splines)
  {
    gdouble maxrho = GSL_NEGINF, minrho = GSL_POSINF, xf = 0.0;
    gdouble gamma, lna;
    gdouble Rgamma, Rlna;
    gdouble Igamma, Ilna;
    gdouble Rs, Is;
    gint i;

    {
      const complex double *Yc  = (const complex double *)Y;
      const gdouble x1          = ncm_vector_fast_get (self->spec_knots, 1);
      const gdouble x2          = ncm_vector_fast_get (self->spec_knots, 2);
      const gdouble x3          = ncm_vector_fast_get (self->spec_knots, 3);
      const gdouble lnx1        = log (x1);
      const gdouble lnx2        = log (x2);
      const gdouble lnx3        = log (x3);
      const complex double psi1 = Yc[0];
      const complex double psi2 = Yc[1];
      const complex double psi3 = Yc[2];
      const gdouble R1          = cabs (psi1);
      const gdouble R2          = cabs (psi2);
      const gdouble R3          = cabs (psi3);
      gdouble lnxa[2]           = {lnx1, lnx2};
      gdouble lnrhoa[3]         = {log (R1), log (R2), log (R3)};
      gdouble dd[2], c[2] = {0.0, }, w[2], b1;

      gsl_poly_dd_init (dd, lnxa, lnrhoa, 2);
      gsl_poly_dd_taylor (c, 0.0, dd, lnxa, 2, w);

      lna   = c[0];
      gamma = c[1];

      gsl_poly_dd_init (dd, lnxa, lnrhoa, 2);
      gsl_poly_dd_taylor (c, 0.0, dd, lnxa, 2, w);

      lnrhoa[0] = log (fabs (creal (Yc[0])));
      lnrhoa[1] = log (fabs (creal (Yc[1])));

      gsl_poly_dd_init (dd, lnxa, lnrhoa, 2);
      gsl_poly_dd_taylor (c, 0.0, dd, lnxa, 2, w);

      Rlna   = c[0];
      Rgamma = c[1];
    
      lnrhoa[0] = log (fabs (cimag (Yc[0])));
      lnrhoa[1] = log (fabs (cimag (Yc[1])));
      lnrhoa[2] = log (fabs (cimag (Yc[2])));

      gsl_poly_dd_init (dd, lnxa, lnrhoa, 2);
      gsl_poly_dd_taylor (c, 0.0, dd, lnxa, 2, w);

      Ilna   = c[0];
      Igamma = c[1];

      Rs = GSL_SIGN (creal (Yc[0]));
      Is = GSL_SIGN (cimag (Yc[0]));

      b1 = log (hypot (exp (Rlna), exp (Ilna)));

      printf ("%.6f | % 17.10g % 17.10g | % 17.10g % 17.10g | % 17.10g % 17.10g % 17.10g % 17.10g % 17.10g\n", 
              t, lna / b1 - 1.0, gamma, Rlna / b1 - 1.0, Rgamma, Ilna / b1 - 1.0, Igamma, b1,
              (lnrhoa[2] * (-x1 + x2) + lnrhoa[1] * (x1 - x3) + lnrhoa[0] * (-x2 + x3)) / ((-x2 + x3) * lnx1 + (x1 - x3) * lnx2 + (-x1 + x2) * lnx3),
              b1
              );
    }
    
    ncm_vector_fast_set (self->Re_psi_v, 0, Rs * exp (Rlna));
    ncm_vector_fast_set (self->Im_psi_v, 0, Is * exp (Ilna));
    
    for (i = 0; i < self->nknots; i++)
    {
      const gdouble x   = ncm_vector_fast_get (self->spec_knots, i + 1);
      const gdouble lnx = log (x);
      ncm_vector_fast_set (self->Re_psi_v, i + 1, Y[2 * i]     * exp (-Rgamma * lnx));
      ncm_vector_fast_set (self->Im_psi_v, i + 1, Y[2 * i + 1] * exp (-Igamma * lnx));
    }

    ncm_spline_prepare (self->Re_psi_s);
    ncm_spline_prepare (self->Im_psi_s);

    {
      NcmSplineCubic *Re_sc = NCM_SPLINE_CUBIC (self->Re_psi_s);
      NcmSplineCubic *Im_sc = NCM_SPLINE_CUBIC (self->Im_psi_s);
      const gdouble br      = ncm_vector_get (Re_sc->b, 0);
      const gdouble bi      = ncm_vector_get (Im_sc->b, 0);
      const gdouble cr      = ncm_vector_get (Re_sc->c, 0);
      const gdouble ci      = ncm_vector_get (Im_sc->c, 0);
      const gdouble dS_0    = (ci * br - cr * bi) / (bi * bi + br * br);
      
      ncm_vector_fast_set (self->rho_v, 0, exp (lna));
      ncm_vector_fast_set (self->dS_v,  0, dS_0);
    }

    for (i = 1; i < self->nknots + 1; i++)
    {
      const gdouble x_i       = ncm_vector_fast_get (self->spec_knots, i);
      const gdouble Re_psi_i  = ncm_spline_eval (self->Re_psi_s, x_i);
      const gdouble Im_psi_i  = ncm_spline_eval (self->Im_psi_s, x_i);
      const gdouble Re_dpsi_i = ncm_spline_eval_deriv (self->Re_psi_s, x_i);
      const gdouble Im_dpsi_i = ncm_spline_eval_deriv (self->Im_psi_s, x_i);
      const gdouble rho_i     = gsl_pow_2 (Re_psi_i) + gsl_pow_2 (Im_psi_i);
      const gdouble dS_i      = (Re_psi_i * Im_dpsi_i - Re_dpsi_i * Im_psi_i) / rho_i;

      /*        
       if (xf == 0.0 && ((fabs (Re_psi_i * Im_dpsi_i / (Re_dpsi_i * Im_psi_i) - 1.0) < 1.0e-6) || (rho_i < 1.0e-280)))
       xf = ncm_vector_fast_get (self->spec_knots, i - 1);
       */        
      /*printf ("% 22.15g % 22.15g % 22.15g %e % 22.15g\n", Re_psi_i * Im_dpsi_i, Re_dpsi_i * Im_psi_i, dS_i, fabs (Re_psi_i * Im_dpsi_i / (Re_dpsi_i * Im_psi_i) - 1.0), xf);*/

      minrho = MIN (minrho, rho_i);
      maxrho = MAX (maxrho, rho_i);

      ncm_vector_fast_set (self->rho_v, i, rho_i * exp (-gamma * log (x_i)));
      ncm_vector_fast_set (self->dS_v,  i, dS_i);
    }    

    ncm_spline_prepare (self->rho_s);
    /*ncm_spline_prepare (self->dS_s);*/
    
    {
      gsl_function F;

      /*printf ("A minrho % 22.15g % 22.15g\n", minrho, maxrho);*/

      if (minrho == 0.0)
        minrho = maxrho * self->abstol;
      else
        minrho *= 1.0e15;

      /*printf ("D minrho % 22.15g % 22.15g\n", minrho, maxrho);*/
      
      if (TRUE)
      {
        for (i = 1; i < self->nknots + 1; i++)
        {
          /*printf ("MIN % 22.15g % 22.15g\n", minrho, ncm_vector_fast_get (self->rho_v, i));*/
          if (ncm_vector_fast_get (self->rho_v, i) < minrho)
          {
            xf = ncm_vector_fast_get (self->spec_knots, i);
            break;
          }
        }
      }
      
      F.params   = self;
      F.function = (gdouble (*) (gdouble, gpointer)) &_ncm_qm_prop_calc_dS;

      ncm_spline_set_func (self->dS_s, NCM_SPLINE_FUNCTION_SPLINE, &F, 0.0, xf, 0, self->reltol);
    }
    
    /*
     printf ("% 22.15g % 22.15g % 22.15g % 22.15g % 22.15g % 22.15g % 22.15g\n", t, 
             ncm_spline_eval_integ (self->rho_s, self->xi, self->xf), 
             ncm_spline_eval_integ (self->dS_s,  self->xi, self->xf),
             Y[2], Y[3], Y[4], Y[5]);
     */
    self->up_splines = TRUE;
  }
}

/**
 * ncm_qm_prop_evolve:
 * @qm_prop: a #NcmQMProp
 * @tf: final time
 * 
 * Evolve the wave-function to @tf.
 * 
 */
void
ncm_qm_prop_evolve (NcmQMProp *qm_prop, const gdouble tf)
{
  NcmQMPropPrivate * const self = qm_prop->priv;
  gdouble t  = self->ti;
  /*gdouble *Y = N_VGetArrayPointer (self->y);*/
  gint flag;

  flag = ARKodeSetStopTime (self->arkode, tf);
  NCM_CVODE_CHECK (&flag, "ARKodeSetStopTime", 1, );

#if 0
  if (t == 0.0)
  {
    printf ("% 22.15g", t);
    for (i = 0; i < self->nknots; i++)
    {
      printf (" % 22.15g % 22.15g", Y[2 * i], Y[2 * i + 1]);
    }
    printf ("\n");
  }
#endif
/*
  glong nni, nni_cur=0, nni_tot=0, nli, nli_tot=0, iout = 0, nncfails;
  gdouble olddt, newdt;  
*/  
  while (t < tf) 
  {
    flag = ARKode (self->arkode, tf, self->y, &t, ARK_ONE_STEP);
    NCM_CVODE_CHECK (&flag, "ARKode", 1, );

    /*_ncm_qm_prop_prepare_dS_rho (self, t, Y);*/
    self->up_splines = FALSE;
    /*printf ("STEP\n");*/
/*
    flag = ARKodeGetLastStep (self->arkode, &olddt);
    NCM_CVODE_CHECK (&flag, "ARKodeGetLastStep", 1, );
    
    flag = ARKodeGetCurrentStep (self->arkode, &newdt);
    NCM_CVODE_CHECK (&flag, "ARKodeGetCurrentStep", 1, );
    
    flag = ARKodeGetNumNonlinSolvIters (self->arkode, &nni);
    NCM_CVODE_CHECK (&flag, "ARKodeGetNumNonlinSolvIters", 1, );
    
    flag = ARKSpilsGetNumLinIters (self->arkode, &nli);
    NCM_CVODE_CHECK (&flag, "ARKSpilsGetNumLinIters", 1, );

    flag = ARKodeGetNumNonlinSolvConvFails (self->arkode, &nncfails);
    NCM_CVODE_CHECK (&flag, "ARKodeGetNumNonlinSolvConvFails", 1, );
      
    // print current solution stats 
    iout++;
    printf(" %4ld  %19.15g  %19.15g  %2ld  %3ld %3ld\n", 
           iout, olddt, newdt, nni - nni_cur, nli, nncfails);

    nni_cur = nni;
    nni_tot = nni;
    nli_tot += nli;

*/

#if 0
    printf ("% 22.15g", t);
    for (i = 0; i < self->nknots; i++)
    {
      printf (" % 22.15g % 22.15g", Y[2 * i], Y[2 * i + 1]);
    }
    printf ("\n");
#endif

  }
  self->ti = t;
}

/**
 * ncm_qm_prop_get_knots:
 * @qm_prop: a #NcmQMProp
 * 
 * Gets the current state knots. 
 * 
 * Returns: (transfer full) (element-type gdouble): the knots array
 */
GArray *
ncm_qm_prop_get_knots (NcmQMProp *qm_prop)
{
  NcmQMPropPrivate * const self = qm_prop->priv;
  return ncm_vector_dup_array (self->knots);
}

/**
 * ncm_qm_prop_get_spec_knots:
 * @qm_prop: a #NcmQMProp
 * 
 * Gets the current state knots. 
 * 
 * Returns: (transfer full) (element-type gdouble): the knots array
 */
GArray *
ncm_qm_prop_get_spec_knots (NcmQMProp *qm_prop)
{
  NcmQMPropPrivate * const self = qm_prop->priv;
  return ncm_vector_dup_array (self->spec_knots);
}

/**
 * ncm_qm_prop_get_psi:
 * @qm_prop: a #NcmQMProp
 * 
 * Gets the current state $\psi$. 
 * 
 * Returns: (transfer full) (element-type gdouble): $\psi$
 */
GArray *
ncm_qm_prop_get_psi (NcmQMProp *qm_prop)
{
  NcmQMPropPrivate * const self = qm_prop->priv;
  GArray *y_a = g_array_new (FALSE, FALSE, sizeof (gdouble));

  g_array_append_vals (y_a, N_VGetArrayPointer (self->y), self->nknots * 2);

  return y_a;
}

/**
 * ncm_qm_prop_get_rho:
 * @qm_prop: a #NcmQMProp
 * 
 * Gets the current state density $\rho_\psi \equiv \psi^*\psi$. 
 * 
 * Returns: (transfer none): $\rho_\psi$
 */
NcmSpline *
ncm_qm_prop_get_rho (NcmQMProp *qm_prop)
{
  NcmQMPropPrivate * const self = qm_prop->priv;
  _ncm_qm_prop_prepare_dS_rho (self, self->ti, N_VGetArrayPointer (self->y));

  return self->rho_s;
}

/**
 * ncm_qm_prop_get_dS:
 * @qm_prop: a #NcmQMProp
 * 
 * Gets the current state phase gradient $\partial_x S \equiv -i/2(\psi^*\partial_x\psi - \partial_x\psi^*\psi)$.
 * 
 * Returns: (transfer none): $\rho_\psi$
 */
NcmSpline *
ncm_qm_prop_get_dS (NcmQMProp *qm_prop)
{
  NcmQMPropPrivate * const self = qm_prop->priv;
  _ncm_qm_prop_prepare_dS_rho (self, self->ti, N_VGetArrayPointer (self->y));

  return self->dS_s;
}

/**
 * ncm_qm_prop_get_Re_psi:
 * @qm_prop: a #NcmQMProp
 * 
 * Gets the current state real part $\Re\psi$. 
 * 
 * Returns: (transfer none): $\Re\psi$
 */
NcmSpline *
ncm_qm_prop_get_Re_psi (NcmQMProp *qm_prop)
{
  NcmQMPropPrivate * const self = qm_prop->priv;
  _ncm_qm_prop_prepare_dS_rho (self, self->ti, N_VGetArrayPointer (self->y));

  return self->Re_psi_s;
}

/**
 * ncm_qm_prop_get_Im_psi:
 * @qm_prop: a #NcmQMProp
 * 
 * Gets the current state imaginary part $\Re\psi$. 
 * 
 * Returns: (transfer none): $\Im\psi$
 */
NcmSpline *
ncm_qm_prop_get_Im_psi (NcmQMProp *qm_prop)
{
  NcmQMPropPrivate * const self = qm_prop->priv;
  _ncm_qm_prop_prepare_dS_rho (self, self->ti, N_VGetArrayPointer (self->y));

  return self->Im_psi_s;
}

/**
 * ncm_qm_prop_evolve_spec:
 * @qm_prop: a #NcmQMProp
 * 
 * Evolves the system using spectral methods.
 * 
 */
void
ncm_qm_prop_evolve_spec (NcmQMProp *qm_prop, const gdouble t)
{
  NcmQMPropPrivate * const self = qm_prop->priv;
  complex double *Y   = (complex double *) N_VGetArrayPointer (self->y);
  const gdouble norma = gsl_pow_2 (gsl_dht_k_sample (self->dht, 0) / gsl_dht_x_sample (self->dht, 0));
  gint i;

  for (i = 0; i < self->nknots; i++)
  {
    const gdouble li  = gsl_dht_k_sample (self->dht, i);
    complex double W  = ncm_vector_fast_get (self->specReW, i) + I * ncm_vector_fast_get (self->specImW, i);
    complex double Wt = cexp (-I * li * li * t) * W;

    /*printf ("%d % 22.15g % 22.15g % 22.15g % 22.15g % 22.15g\n", i, li, creal (Wt), cimag (Wt), creal (W), cimag (W));*/

    ncm_vector_fast_set (self->specReY, i, creal (Wt));
    ncm_vector_fast_set (self->specImY, i, cimag (Wt));
  }

  gsl_dht_apply (self->dht, ncm_vector_data (self->specReY), ncm_vector_data (self->specReYt));
  gsl_dht_apply (self->dht, ncm_vector_data (self->specImY), ncm_vector_data (self->specImYt));

  for (i = 0; i < self->nknots; i++)
  {
    const gdouble x   = gsl_dht_x_sample (self->dht, i);    
    complex double Yt = norma * sqrt (x) * (ncm_vector_fast_get (self->specReYt, i) + I * ncm_vector_fast_get (self->specImYt, i));

    /*printf ("%d % 22.15g % 22.15g % 22.15g % 22.15g\n", i, x, creal (Yt), cimag (Yt), norma);*/
    
    Y[i] = Yt;
  }

  self->ti = t;
  self->up_splines = FALSE;
}

#endif /* HAVE_ACB_H  */
#endif /* HAVE_SUNDIALS_MAJOR == 3 */