#include "libasf_proj.h"
#include "asf.h"
#include "asf_reporting.h"

#include <stdio.h>
#include <assert.h>
#include <math.h>

#include "projects.h"
#include "proj_api.h"

static double round(double d)
{
    return floor(d + 0.5);
}

/*
  We aren't really using this function... just a basic test from
  right out of the libproj manual, to get the ball rolling.  Only
  used in the test driver as a sanity check
*/

int project_poly(double phi_deg, double lam_deg, double * xx, double *yy)
{
    char * args[] = { "proj=poly", "ellps=clrk66", "lon_0=90W" };

    projPJ ref;
    projUV idata;
    projUV odata;

    ref = pj_init(3, args);
    if (pj_errno != 0)
	asfPrintError("libproj Error: %s\n", pj_strerrno(pj_errno));
    if (!ref)
	asfPrintError("libproj Error: null ref returned from pj_init\n");

    idata.v = phi_deg * DEG_TO_RAD;
    idata.u = lam_deg * DEG_TO_RAD;

    odata = pj_fwd(idata, ref);
    if (pj_errno != 0)
	asfPrintError("libproj Error: %s\n", pj_strerrno(pj_errno));

    *xx = odata.u;
    *yy = odata.v;

    pj_free(ref);
    if (pj_errno != 0)
	asfPrintError("libproj Error: %s\n", pj_strerrno(pj_errno));

    return 1;
}

static int project_worker_arr(char * projection_description,
			      double *lat, double *lon,
			      double *projected_x, double *projected_y,
			      long length)
{
  double * tmp3;
  projPJ geographic_projection, output_projection;
  int i, ok = TRUE;

  for (i = 0; i < length; ++i)
  {
      projected_x[i] = lon[i];
      projected_y[i] = lat[i];
  }

  geographic_projection
      = pj_init_plus ("+proj=latlong +a=6378136.3 +b=6356751.600563");
  
  if (pj_errno != 0)
  {
      asfPrintError("libproj Error: %s\n", pj_strerrno(pj_errno));
      ok = FALSE;
  }
  
  if (ok)
  {
      assert (geographic_projection != NULL);
      
      output_projection = pj_init_plus (projection_description);
      
      if (pj_errno != 0)
      {
	  asfPrintError("libproj Error: %s\n", pj_strerrno(pj_errno));
	  ok = FALSE;
      }
  
      if (ok)
      {
	  assert (output_projection != NULL);
	  
	  tmp3 = (double *) MALLOC(sizeof(double) * length);
	  memset(tmp3, 0, sizeof(double) * length);

	  pj_transform (geographic_projection, output_projection, length, 1, 
			projected_x, projected_y, tmp3);
	  
	  if (pj_errno != 0)
	  {
	      asfPrintWarning("libproj Error: %s\n", pj_strerrno(pj_errno));
	      ok = FALSE;
	  }
	  
	  FREE(tmp3);
	  pj_free(output_projection);
      }
      
      pj_free(geographic_projection);
  }

  return ok;
}

static int project_worker_arr_inv(char * projection_description,
				  double *x, double *y,
				  double *lat, double *lon,
				  long length)
{
  double * tmp3;
  projPJ geographic_projection, output_projection;
  int i, ok = TRUE;

  for (i = 0; i < length; ++i)
  {
      lat[i] = x[i];
      lon[i] = y[i];
  }

  geographic_projection
      = pj_init_plus ("+proj=latlong +a=6378136.3 +b=6356751.600563");
  
  if (pj_errno != 0)
  {
      asfPrintError("libproj Error: %s\n", pj_strerrno(pj_errno));
      ok = FALSE;
  }
  
  if (ok)
  {
      assert (geographic_projection != NULL);
  
      output_projection = pj_init_plus (projection_description);
      
      if (pj_errno != 0)
      {
	  asfPrintError("libproj Error: %s\n", pj_strerrno(pj_errno));
	  ok = FALSE;
      }
      
      if (ok)
      {
	  assert (output_projection != NULL);
	  
	  tmp3 = (double *) MALLOC(sizeof(double) * length);
	  memset(tmp3, 0, sizeof(double) * length);
	  
	  pj_transform (output_projection, geographic_projection, length, 1, 
			lat, lon, tmp3);
	  	  
	  if (pj_errno != 0)
	  {
	      asfPrintWarning("libproj Error: %s\n", pj_strerrno(pj_errno));
	      ok = FALSE;
	  }
	  
	  if (ok)
	  {	  
	      for (i = 0; i < length; ++i)
	      {
		  double t = lat[i];
		  lat[i] = lon[i];
		  lon[i] = t;
	      }
	  }

	  FREE(tmp3);
	  pj_free(output_projection);
      }

      pj_free(geographic_projection);
  }

  return ok;
}

/****************************************************************************
 Universal Transverse Mercator (UTM)
****************************************************************************/
static double utm_nudge(double lon_0)
{
  double tiny_value;
  double lon_deg = lon_0 * RAD_TO_DEG;

  /* Nudge cases which are marginal in terms of which utm zone they
     fall in towards zero a bit.  The proj documentation tells us we
     should avoid the marginal cases. */
  tiny_value = 0.00001;    /* Random small number of degrees. */
  if ( fabs(round(lon_deg / 6.0) - lon_deg / 6) < tiny_value ) {
    if ( lon_deg > 0 ) {
      lon_deg -= tiny_value;
    }
    else {
      lon_deg += tiny_value;
    }
  }

  return lon_deg * DEG_TO_RAD;
}

static char * utm_projection_description(double lon_0)
{
  static char utm_wgs84_projection_description[128];

  /* Establish description of output projection. */
  sprintf(utm_wgs84_projection_description,
	  "+proj=utm +lon_0=%f +datum=WGS84",
	  lon_0 * RAD_TO_DEG);

  return utm_wgs84_projection_description;
}

int
project_utm (double lon_0, double lat, double lon, double *x, double *y)
{
  return project_worker_arr(utm_projection_description(utm_nudge(lon_0)),
			    &lat, &lon, x, y, 1);
}

int
project_utm_arr (double lon_0,
		 double *lat, double *lon, 
		 double *projected_x, double *projected_y,
		 long length)
{
  return project_worker_arr(utm_projection_description(utm_nudge(lon_0)),
			    lat, lon, projected_x, projected_y, length);
}

int
project_utm_inv (double lon_0, double x, double y,  double *lat, double *lon)
{
  return project_worker_arr_inv(utm_projection_description(utm_nudge(lon_0)),
				&x, &y, lat, lon, 1);
}

int
project_utm_arr_inv (double lon_0, 
		     double *x, double *y,
		     double *lat, double *lon,
		     long length)
{
    /* it is natural to call this function with latitude in x,
       and longitude in y -- however the transform wants longitude
       in y and latitude in x.  So here we switch */
  return project_worker_arr_inv(utm_projection_description(utm_nudge(lon_0)),
			    x, y, lat, lon, length);
}

/****************************************************************************
 Polar Sterographic (PS)
****************************************************************************/
static char * ps_projection_desc(double lat_ts, double lon_0,
				 int is_north_pole)
{
  static char ps_wgs84_projection_description[128];

  /* Establish description of output projection. */
  sprintf(ps_wgs84_projection_description,
	  "+proj=stere +lat_0=%s +lat_ts=%f +lon_0=%f +datum=WGS84",
	  is_north_pole ? "90" : "-90",
	  lat_ts * RAD_TO_DEG,
	  lon_0 * RAD_TO_DEG);

  return ps_wgs84_projection_description;
}

static int
project_ps_imp (double lat_ts, double lon_0, int is_north_pole,
		double lat, double lon,
		double *x, double *y)
{
    return  project_worker_arr(
	ps_projection_desc(lat_ts, lon_0, is_north_pole),
	&lat, &lon, x, y, 1);
}

int
project_ps(project_parameters_t * pps,
	   double lat, double lon,
	   double *x, double *y)
{
    return project_ps_imp(pps->ps.slat, pps->ps.slon, pps->ps.is_north_pole,
			  lat, lon, x, y);
}

static int
project_ps_arr_imp(double lat_ts, double lon_0, int is_north_pole,
		   double *lat, double *lon,
		   double *x, double *y,
		   long length)
{
    return project_worker_arr(ps_projection_desc(lat_ts, lon_0, is_north_pole),
			      lat, lon, x, y, length);
}

int
project_ps_arr(project_parameters_t * pps,
	       double *lat, double *lon,
	       double *x, double *y,
	       long length)
{
    return project_ps_arr_imp(
	pps->ps.slat, pps->ps.slon, pps->ps.is_north_pole,
	lat, lon, x, y, length);
}

static int
project_ps_inv_imp(double lat_ts, double lon_0, int is_north_pole,
		   double x, double y, double *lat, double *lon)
{
    return project_worker_arr_inv(
	ps_projection_desc(lat_ts, lon_0, is_north_pole),
	&x, &y, lat, lon, 1);
}

int
project_ps_inv(project_parameters_t * pps,
	       double x, double y, double *lat, double *lon)
{
    return project_ps_inv_imp(
	pps->ps.slat, pps->ps.slon, pps->ps.is_north_pole,
	x, y, lat, lon);
}

static int
project_ps_arr_inv_imp(double lat_ts, double lon_0, int is_north_pole,
		       double *x, double *y,
		       double *lat, double *lon,
		       long length)
{
  return project_worker_arr_inv(
      ps_projection_desc(lat_ts, lon_0, is_north_pole),
      x, y, lat, lon, length);
}

int
project_ps_arr_inv(project_parameters_t * pps,
		   double *x, double *y,
		   double *lat, double *lon,
		   long length)
{
  return project_ps_arr_inv_imp(
      pps->ps.slat, pps->ps.slon, pps->ps.is_north_pole,
      x, y, lat, lon, length);
}

/****************************************************************************
 Lambert Azimuthal Equal Area
****************************************************************************/
static char * lamaz_projection_desc(double lat_0, double lon_0)
{
  static char lamaz_projection_description[128];

  /* Establish description of output projection. */
  sprintf(lamaz_projection_description,
	  "+proj=laea +lat_0=%f +lon_0=%f +datum=WGS84",
	  lat_0, lon_0);

  return lamaz_projection_description;
}

int
project_lamaz(double lat_0, double lon_0, double lat, double lon, 
	      double *x, double *y)
{
    char * d = lamaz_projection_desc(lat_0, lon_0);
 /*
    return project_worker(lamaz_projection_description(lat_0, lon_0),
			  lat, lon, x, y);
 */
    return d ? TRUE : FALSE;
}

static int
project_lamaz_s0(proj_lamaz * lamaz,
		double lat, double lon, 
		double *x, double *y)
{
    return project_lamaz(lamaz->center_lat, lamaz->center_lon,
			 lat, lon, x, y);
}

int
project_lamaz_s(project_parameters_t * proj,
		double lat, double lon, 
		double *x, double *y)
{
    return project_lamaz_s0(&proj->lamaz,
			   lat, lon, x, y);
}

int
project_lamaz_arr(double lat_0, double lon_0, double *x, double *y, int length)
{
  char lamaz_projection_description[128];

  /* Establish description of output projection. */
  sprintf(lamaz_projection_description,
	  "+proj=laea +lat_0=%f +lon_0=%f +datum=WGS84",
	  lat_0, lon_0);

  return project_worker_arr(lamaz_projection_description, x, y, x,y, length);
}

/*
  lat_1 : First standard parallel
  lat_2 : Second standard parallel
  lat_0 : Latitude of false origin
  lon_0 : Longitude of false origin
*/

int
project_lamcc(double lat_1, double lat_2, double lat_0, double lon_0, 
	      double lat, double lon, double *x, double *y)
{
  char lamcc_projection_description[128];

  /* Establish description of output projection. */
  sprintf(lamcc_projection_description,
	  "+proj=lcc +lat_1=%f +lat_2=%f +lat_0=%f +lon_0=%f +datum=WGS84",
	  lat_1, lat_2, lat_0, lon_0);
/*
  return project_worker(lamcc_projection_description, lat, lon, x, y);
*/
  return FALSE;
}

int
project_lamcc_s(proj_lamcc * lamcc,
		double lat, double lon, double *x, double *y)
{
    return project_lamcc(lamcc->plat1, lamcc->plat2, lamcc->lat0, lamcc->lon0,
			 lat, lon, x, y);
}

int
project_lamcc_arr(double lat_1, double lat_2, double lat_0, double lon_0,
		  double *x, double *y, int length)
{
  char lamcc_projection_description[128];

  /* Establish description of output projection. */
  sprintf(lamcc_projection_description,
	  "+proj=lcc +lat_1=%f +lat_2=%f +lat_0=%f +lon_0=%f +datum=WGS84",
	  lat_1, lat_2, lat_0, lon_0);

  return project_worker_arr(lamcc_projection_description, x, y,x,y, length);
}

/*
  lat_1 : Latitude of first standard parallel
  lat_2 : Latitude of second standard parallel
  lat_0 : Latitude at false origin
  lon_0 : Longitude at false origin
 */

int
project_albers(double lat_1, double lat_2, double lat_0, double lon_0,
	       double lat, double lon, double *x, double *y)
{
  char aea_projection_description[128];

  /* Establish description of output projection. */
  sprintf(aea_projection_description,
	  "+proj=aea +lat_1=%f +lat_2=%f +lat_0=%f +lon_0=%f +datum=WGS84",
	  lat_1, lat_2, lat_0, lon_0);
/*
  return project_worker(aea_projection_description, lat, lon, x, y);
*/
  return FALSE;
}

int
project_albers_s(proj_albers * alb,
		 double lat, double lon, double *x, double *y)
{
    return project_albers(
	  alb->std_parallel1,
	  alb->std_parallel2,
	  alb->orig_latitude,
	  alb->center_meridian,
	  lat, lon, x, y);
}

int
project_albers_arr(double lat_1, double lat_2, double lat_0, double lon_0,
		   double *x, double *y, int length)
{
  char aea_projection_description[128];

  /* Establish description of output projection. */
  sprintf(aea_projection_description,
	  "+proj=aea +lat_1=%f +lat_2=%f +lat_0=%f +lon_0=%f +datum=WGS84",
	  lat_1, lat_2, lat_0, lon_0);

  return project_worker_arr(aea_projection_description, x, y,x,y, length);
}
