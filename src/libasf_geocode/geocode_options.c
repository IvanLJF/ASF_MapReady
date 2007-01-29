#include "asf_geocode.h"
#include "libasf_proj.h"
#include "asf_nan.h"
#include "asf_meta.h"
#include "proj_api.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

static const double DEFAULT_POLAR_STERO_NORTH_CENTRAL_MERIDIAN = -45;
static const double DEFAULT_POLAR_STERO_SOUTH_CENTRAL_MERIDIAN = -90;

static const double DEFAULT_POLAR_STERO_NORTH_STANDARD_PARALLEL = 70;
static const double DEFAULT_POLAR_STERO_SOUTH_STANDARD_PARALLEL = -70;

project_parameters_t * get_geocode_options(int *argc, char **argv[],
										   projection_type_t * proj_type,
										   double *height, double *pixel_size,
										   datum_type_t *datum,
										   resample_method_t *resample_method,
										   int *override_checks)
{
	/* projection parameters obtained from the command line */
	project_parameters_t * pps;

	/* TRUE if we did a write-proj-file */
	int did_write_proj_file = FALSE;

	/* must pull out logfile first, so we can log projection parsing errors */
	parse_log_options(argc, argv);

	/* get the projection params out of the cmd line & remove from cmd line */
	pps = parse_projection_options(argc, argv, proj_type,
		&did_write_proj_file);

	if (pps)
	{
		/* "other" options include: 'height', 'pixel-size', 'force'
		and 'resample_method'.  */
		parse_other_options(argc, argv, height, pixel_size, datum,
			resample_method, override_checks);

		/* here the semantics of the projection parameters are applied */
		sanity_check(*proj_type, pps);
	}

	/* Exit now if no input/output files are specified, and the
	"write-proj-file" option was specified */
	if (did_write_proj_file && *argc == 1) {
		asfPrintStatus("No input files.\n");
		exit(EXIT_SUCCESS);
	}

	return pps;
}

static double
local_earth_radius (spheroid_type_t spheroid, double geodetic_lat)
{
  double a, b;
  spheroid_axes_lengths (spheroid, &a, &b);
  
  double e2 = 1 - (pow (b, 2.0) / pow (a, 2.0));
  
  // Geocentric latitude.
  double gcl = atan (tan (geodetic_lat) * (1 - e2));
  
  return (a * b) / sqrt (pow (b * cos (gcl), 2.0) + pow (a * sin (gcl), 2.0));
}

// Return the arc length of an arc covering angle at geodetic_lat on
// spheroid, assuming a constant radius of curvature approximation
// over the arc.
static double
arc_length_at_latitude (spheroid_type_t spheroid, double geodetic_lat,
			double angle)
{
  assert (angle < 2 * M_PI);

  double er = local_earth_radius (spheroid, geodetic_lat);

  return angle * er;
}

int calc_utm_zone(double lon)
{
	return((int)(((lon + 180.0) / 6.0) + 1.0));
}

static void verify_valid_latitude(double lat)
{
	if (ISNAN(lat))
		return;

	if (lat > 90 || lat < -90)
	{
		asfPrintWarning("Invalid Latitude: %f\n", lat);
		lat = NAN;
	}
}

static void verify_valid_longitude(double lon)
{
	if (ISNAN(lon))
		return;

	if (lon > 360 || lon < -360)
	{
		asfPrintWarning("Invalid Longitude: %f\n", lon);
		lon = NAN;
	}
}

void sanity_check(projection_type_t pt, project_parameters_t * pps)
{
	switch (pt)
	{
	case UNIVERSAL_TRANSVERSE_MERCATOR:

		if (pps->utm.zone != MAGIC_UNSET_INT)
		{
			if ((abs(pps->utm.zone) < 1) || (abs(pps->utm.zone) > 60))
			{
				asfPrintError("Illegal zone number: %d\n", pps->utm.zone);
			}
		}

		verify_valid_latitude(pps->utm.lat0);
		verify_valid_longitude(pps->utm.lon0);

		break;

	case POLAR_STEREOGRAPHIC:

		verify_valid_latitude(pps->ps.slat);
		verify_valid_longitude(pps->ps.slon);

		break;

	case ALBERS_EQUAL_AREA:

		verify_valid_latitude(pps->albers.std_parallel1);
		verify_valid_latitude(pps->albers.std_parallel2);
		verify_valid_latitude(pps->albers.orig_latitude);
		verify_valid_longitude(pps->albers.center_meridian);

		break;

	case LAMBERT_AZIMUTHAL_EQUAL_AREA:

		verify_valid_latitude(pps->lamaz.center_lat);
		verify_valid_longitude(pps->lamaz.center_lon);

		break;

	case LAMBERT_CONFORMAL_CONIC:

		verify_valid_latitude(pps->lamcc.plat1);
		verify_valid_latitude(pps->lamcc.plat2);
		verify_valid_latitude(pps->lamcc.lat0);
		verify_valid_longitude(pps->lamcc.lon0);

		break;

	default:
		asfPrintError("sanity_check: illegal projection type!");
	}
}

void apply_defaults(projection_type_t pt, project_parameters_t * pps,
					meta_parameters * meta, double * average_height,
					double * pixel_size)
{
  double lat, lon;

	if ( ISNAN (*average_height) )
		*average_height = 0.0;

	if ( ISNAN (*pixel_size) ) {
	  // If the input image is pseudoprojected, we use the arc
	  // length between pixels at the image center as the
	  // approximate pixel size.  There are comments relating to
	  // pixel size in the import_usgs_seamless.c file that should
	  // change if this approach is changed.
	  if ( meta->projection != NULL 
	       && meta->projection->type == LAT_LONG_PSEUDO_PROJECTION ) {
	    *pixel_size 
	      = arc_length_at_latitude (meta->projection->spheroid,
					meta->general->center_latitude,
					meta->general->y_pixel_size * D2R);
	  } else {
	    *pixel_size = meta->general->x_pixel_size;
	  }
	}

	switch (pt)
	{
	case UNIVERSAL_TRANSVERSE_MERCATOR:
	  meta_get_latLon(meta, meta->general->line_count/2,
			  meta->general->sample_count/2, 0, &lat, &lon);
	  fill_in_utm(lat, lon, pps);
	  break;

	case POLAR_STEREOGRAPHIC:
		/* SMMI standard values */
		if (ISNAN(pps->ps.slon))
			pps->ps.slon = pps->ps.is_north_pole ?
DEFAULT_POLAR_STERO_NORTH_CENTRAL_MERIDIAN :
		DEFAULT_POLAR_STERO_SOUTH_CENTRAL_MERIDIAN;

		/* default standard parallels are +/- 70 */
		if (ISNAN(pps->ps.slat))
			pps->ps.slat = pps->ps.is_north_pole ?
DEFAULT_POLAR_STERO_NORTH_STANDARD_PARALLEL :
		DEFAULT_POLAR_STERO_SOUTH_STANDARD_PARALLEL;

		if (ISNAN(pps->ps.false_easting))
			pps->ps.false_easting = 0;
		if (ISNAN(pps->ps.false_northing))
			pps->ps.false_northing = 0;

		break;

	case ALBERS_EQUAL_AREA:
		if (ISNAN(pps->albers.false_easting))
			pps->albers.false_easting = 0;
		if (ISNAN(pps->albers.false_northing))
			pps->albers.false_northing = 0;

		if (ISNAN(pps->albers.orig_latitude))
			pps->albers.orig_latitude = meta->general->center_latitude;
		if (ISNAN(pps->albers.center_meridian))
			pps->albers.center_meridian = meta->general->center_longitude;

		break;

	case LAMBERT_AZIMUTHAL_EQUAL_AREA:
		if (ISNAN(pps->lamaz.false_easting))
			pps->lamaz.false_easting = 0;
		if (ISNAN(pps->lamaz.false_northing))
			pps->lamaz.false_northing = 0;

		if (ISNAN(pps->lamaz.center_lat))
			pps->lamaz.center_lat = meta->general->center_latitude;
		if (ISNAN(pps->lamaz.center_lon))
			pps->lamaz.center_lon = meta->general->center_longitude;

		break;

	case LAMBERT_CONFORMAL_CONIC:
		if (ISNAN(pps->lamcc.false_easting))
			pps->lamcc.false_easting = 0;
		if (ISNAN(pps->lamcc.false_northing))
			pps->lamcc.false_northing = 0;

		if (ISNAN(pps->lamcc.lat0))
			pps->lamcc.lat0 = meta->general->center_latitude;
		if (ISNAN(pps->lamcc.lon0))
			pps->lamcc.lon0 = meta->general->center_longitude;

		break;

	default:
		asfPrintError("apply_defaults: illegal projection type!");
	}
}
