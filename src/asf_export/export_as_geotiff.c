#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <limits.h>

#include <cla.h>
#include <envi.h>
#include <esri.h>
#include <geokeys.h>
#include <geotiff.h>
#include <geotiffio.h>
#include <gsl/gsl_math.h>
#include <gsl/gsl_matrix.h>
#include <gsl/gsl_statistics.h>
#include <jpeglib.h>
/*#include <proj_api.h>*/
#include <tiff.h>
#include <tiffio.h>
#include <xtiffio.h>

#include <asf.h>
#include <asf_endian.h>
#include <asf_meta.h>
#include "asf_nan.h"
#include "asf_export.h"

#define ASF_NAME_STRING "asf_export"

void
export_as_geotiff (const char *metadata_file_name,
                   const char *image_data_file_name,
                   const char *output_file_name,
		   scale_t scale)
{
  /* Get the image metadata.  */
  meta_parameters *md = meta_read (metadata_file_name);
  unsigned short sample_size = 4;
  unsigned short sample_format;
  unsigned int line_count = md->general->line_count;
  unsigned int sample_count = md->general->sample_count;
  size_t pixel_count = line_count * sample_count;
  float *daf;
  unsigned char *pixels;
  double mask;
  int jj;
  TIFF *otif;
  GTIF *ogtif;
  size_t ii;
  int return_code;

  /* This constant is from the GeoTIFF spec.  It basically means that
     the system which would normally be specified by the field
     (projected coordinate system, datum, ellipsoid, whatever), in
     instead going to be specified by more detailed low level
     tags.  */
  const int user_defined_value_code = 32767;
  /* Major and minor ellipse axis lengths.  This shows up in two
     different places in our metadata, we want the projected one if
     its available, otherwise the one from general.  */
  double re_major, re_minor;
  /* Nail down which ellipsoid we are on exactly.  The ASF metadata
     doesn't specify this though, so we take a look at the major and
     minor axis values and try to find a matching ellipsoid.  */
  asf_export_ellipsoid_t ellipsoid;
  const double clarke1866_major_axis = 6378206.4;
  const double clarke1866_minor_axis = 6356583.8;
  const double gem10c_major_axis = 6378144;
  const double gem10c_minor_axis = 6356759;
  const double wgs66_major_axis = 6378145.0;
  const double wgs66_minor_axis = 6356759.769356;
  const double wgs84_major_axis = 6378137;
  const double wgs84_flattening = 1.0 / 298.257223563;
  const double wgs84_minor_axis = wgs84_major_axis * (1 - wgs84_flattening);

  /* Insist that the minor axis match what we are expecting to within
     this tolerance.  */
  double axis_tolerance = 0.2;

  assert (md->general->data_type == REAL32);
  assert (sizeof (unsigned short) == 2);
  assert (sizeof (unsigned int) == 4);

  /* Get the image data.  */
  assert (md->general->data_type == REAL32);
  daf = get_image_data (md, image_data_file_name);
  /* It supposed to be big endian data, this converts to host byte
     order.  */
  for ( jj = 0 ; jj < pixel_count ; jj++ ) {
    ieee_big32 (daf[jj]);
  }

  /* Open output tiff file and GeoKey file descriptor.  */
  otif = XTIFFOpen (output_file_name, "w");
  assert (otif != NULL);
  ogtif = GTIFNew (otif);
  assert (ogtif != NULL);

  /* Scale float image down to bytes, if required */
  if (scale != NONE) {
    if (md->general->image_data_type == SIGMA_IMAGE ||
	md->general->image_data_type == GAMMA_IMAGE ||
	md->general->image_data_type == BETA_IMAGE ||
	strcmp(md->general->mode, "SNA") == 0 ||
	strcmp(md->general->mode, "SNB") == 0 ||
	strcmp(md->general->mode, "SWA") == 0 ||
	strcmp(md->general->mode, "SWB") == 0)
      mask = 0.0;
    else
      mask = NAN;
    pixels = floats_to_bytes (daf, pixel_count, mask, scale);
    sample_size = 1;
  }

  /* Set the normal TIFF image tags.  */
  TIFFSetField(otif, TIFFTAG_SUBFILETYPE, 0);
  TIFFSetField(otif, TIFFTAG_IMAGEWIDTH, sample_count);
  TIFFSetField(otif, TIFFTAG_IMAGELENGTH, line_count);
  TIFFSetField(otif, TIFFTAG_BITSPERSAMPLE, sample_size * 8);
  TIFFSetField(otif, TIFFTAG_COMPRESSION, COMPRESSION_NONE);
  TIFFSetField(otif, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_MINISBLACK);
  TIFFSetField(otif, TIFFTAG_SAMPLESPERPIXEL, 1);
  TIFFSetField(otif, TIFFTAG_ROWSPERSTRIP,1);
  TIFFSetField(otif, TIFFTAG_XRESOLUTION,1);
  TIFFSetField(otif, TIFFTAG_YRESOLUTION,1);
  TIFFSetField(otif, TIFFTAG_RESOLUTIONUNIT, RESUNIT_NONE);
  TIFFSetField(otif, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);
  switch ( md->general->data_type ) {
  case BYTE:
    sample_format = SAMPLEFORMAT_UINT;
    break;
  case INTEGER16:
    sample_format = SAMPLEFORMAT_INT;
    break;
  case INTEGER32:
    sample_format = SAMPLEFORMAT_INT;
    break;
  case REAL32:
    sample_format = SAMPLEFORMAT_IEEEFP;
    break;
  case REAL64:
    assert (FALSE);             /* Not sure which format we want here.  */
    break;
  default:
    assert (FALSE);             /* Shouldn't be here.  */
    break;
  }
  TIFFSetField(otif, TIFFTAG_SAMPLEFORMAT, sample_format);
  TIFFSetField(otif, TIFFTAG_DATATYPE, sample_format);

  /* Set the GeoTIFF extension image tags.  */

  /* FIXME: its not good to say all our products are
     RasterPixelIsArea, but for now that's what we do.  */
  GTIFKeySet (ogtif, GTRasterTypeGeoKey, TYPE_SHORT, 1, RasterPixelIsArea);

  if ( md->sar->image_type == 'P' ) {
    re_major = md->projection->re_major;
    re_minor = md->projection->re_minor;
  }
  else {
    re_major = md->general->re_major;
    re_minor = md->general->re_minor;
  }

  if ( FLOAT_COMPARE_TOLERANCE (re_major, clarke1866_major_axis,
                                axis_tolerance)
       && FLOAT_COMPARE_TOLERANCE (re_minor, clarke1866_minor_axis,
                                   axis_tolerance) ) {
    ellipsoid = CLARKE1866;
  }
  else if ( FLOAT_COMPARE_TOLERANCE (re_major, wgs84_major_axis,
                                     axis_tolerance)
            && FLOAT_COMPARE_TOLERANCE (re_minor, wgs84_minor_axis,
                                        axis_tolerance) ) {
    ellipsoid = WGS84;
  }
  else if ( FLOAT_COMPARE_TOLERANCE (re_major, gem10c_major_axis,
                                     axis_tolerance)
            && FLOAT_COMPARE_TOLERANCE (re_minor, gem10c_minor_axis,
                                        axis_tolerance) ) {
    ellipsoid = GEM10C;
  }
  /* FIXME: I have some ellipsoid that looks slightly like this thing,
     so in order to give us a datum to use for geotiffs we pretend its
     this.  The geolocation is bad because of range migration
     anyway.  */
  else if ( FLOAT_COMPARE_TOLERANCE (re_major, wgs66_major_axis, 2)
            && FLOAT_COMPARE_TOLERANCE (re_minor, wgs66_minor_axis, 5) ) {
    ellipsoid = WGS66;
  }
  else {
    /* FIXME: we badly need to get the ellipsoid/datum mess sorted
       out.  This problem goes deeper than asf_export, however.  */
    fprintf (stderr, "%s: warning: couldn't conclude which ellipsoid is "
             "being used from ellipsoid axis dimensions in metadata, "
             "using user defined ellipsoid\n", ASF_NAME_STRING);
    ellipsoid = USER_DEFINED;
  }

  /* If we have a map projected image, write the projection
     information into the GeoTIFF.  */
  /* FIXME: this is a terrible hack to deal with scansar crap.  */
  if ( md->sar->image_type == 'P'
       && md->projection->type != SCANSAR_PROJECTION) {
    /* Tie points for image corners.  There is space for four tie
       points, each consisting of three raster coordinates, followed
       by three geospatial coordinates.  */
    double tie_points[4][6];
    double pixel_scale[3];
    short projection_code;
    int max_citation_length = 500;
    char *citation;
    int citation_length;

    if ( FLOAT_COMPARE_TOLERANCE (md->projection->re_major,
                                  clarke1866_major_axis, axis_tolerance)
         && FLOAT_COMPARE_TOLERANCE (md->projection->re_minor,
                                     clarke1866_minor_axis,
                                     axis_tolerance) ) {
      ellipsoid = CLARKE1866;
    }
    else if ( FLOAT_COMPARE_TOLERANCE (md->projection->re_major,
                                       wgs84_major_axis, axis_tolerance)
              && FLOAT_COMPARE_TOLERANCE (md->projection->re_minor,
                                          wgs84_minor_axis, axis_tolerance) ) {
      ellipsoid = WGS84;
    }
    else if ( FLOAT_COMPARE_TOLERANCE (md->projection->re_major,
                                       gem10c_major_axis, axis_tolerance)
              && FLOAT_COMPARE_TOLERANCE (md->projection->re_minor,
                                          gem10c_minor_axis,
                                          axis_tolerance) ) {
      ellipsoid = GEM10C;
    }
    else {
      /* FIXME: we badly need to get the ellipsoid/datum mess sorted
         out.  This problem goes deeper than asf_export, however.  */
      const int max_error_string_length = 2000;
      char *temp = malloc ((max_error_string_length + 1) * sizeof (char));
      sprintf (temp, "\n\nWARNING: Couldn't conclude which ellipsoid is "
               "being used from ellipsoid axis dimensions in metadata, "
               "assuming WGS84 ellipsoid\n");
      printf (temp);
      ellipsoid = WGS84;
    }

    /* We will tie down the corner of the image (which has raster coordinates
       0, 0, 0).  */
    tie_points[0][0] = 0.0;
    tie_points[0][1] = 0.0;
    tie_points[0][2] = 0.0;
    /* FIXME: we should be getting the actual corner of the image
       here, not the center of the corner pixel, and I'm not sure that
       startX and startY are what we want (verify and fix if
       needed.  */
    tie_points[0][3] = md->projection->startX;
    tie_points[0][4] = md->projection->startY;
    tie_points[0][5] = 0.0;
    /* Some applications (e.g., ArcView) won't handle GeoTIFF images
       with more than one tie point pair.  Therefore, only the upper
       left corner is being written to the GeoTIFF file.  In order to
       write all computed tie points to the GeoTIFF, change the 6 to
       size in the line below.  */
    TIFFSetField(otif, TIFFTAG_GEOTIEPOINTS, 6, tie_points);

    /* Set the scale of the pixels, in projection coordinates.  */
    pixel_scale[0] = md->projection->perX;
    /* Note: we take -perY here because our tools treat the upper left
       as startX, startY, and then count down in the y direction with
       lower scan lines of the image.  The GeoTIFF world uses
       increasing y coordinates in the downward direction.  */
    pixel_scale[1] = -md->projection->perY;
    pixel_scale[2] = 0;
    TIFFSetField (otif, TIFFTAG_GEOPIXELSCALE, 3, pixel_scale);

    GTIFKeySet (ogtif, GTModelTypeGeoKey, TYPE_SHORT, 1,
                ModelTypeProjected);

    switch ( md->projection->type ) {
      case UNIVERSAL_TRANSVERSE_MERCATOR:
        /* For now we only handle UTM data that is referenced to the
           WGS84 ellipsoid.  */
        assert (ellipsoid == WGS84);

        /* This weird paranoid assertion is because I remember once
           when we couln't figure out how to set some datum code
           right, we set it to -1.  */
        assert (md->projection->param.utm.zone != -1);

        /* Here we use some funky arithmetic to get the correct
           geotiff coordinate system type key from our zone code.
           Here are a few assertions to try to ensure that the
           convention used for the libgeotiff constants is as
           expected.  Also note that we have already verified that we
           are on a WGS84 ellipsoid.  */
        assert (PCS_WGS84_UTM_zone_60N - PCS_WGS84_UTM_zone_1N == 59);
        assert (PCS_WGS84_UTM_zone_60S - PCS_WGS84_UTM_zone_1S== 59);

        if ( md->projection->hem == 'N' ) {
          const int northern_utm_zone_base = PCS_WGS84_UTM_zone_1N - 1;
          projection_code = northern_utm_zone_base;
        }
        else if ( md->projection->hem == 'S' ) {
          const int southern_utm_zone_base = PCS_WGS84_UTM_zone_1S - 1;
          projection_code = southern_utm_zone_base;
        }
        else {
          assert (FALSE);               /* Shouldn't be here.  */
        }
        projection_code += md->projection->param.utm.zone;

        GTIFKeySet (ogtif, ProjectedCSTypeGeoKey, TYPE_SHORT, 1,
                    projection_code);
        GTIFKeySet (ogtif, GeogLinearUnitsGeoKey, TYPE_SHORT, 1, Linear_Meter);
        citation = MALLOC ((max_citation_length + 1) * sizeof (char));
        citation_length
          = snprintf (citation, max_citation_length + 1,
                      "UTM zone %d %c projected GeoTIFF written by Alaska "
                      "Satellite Facility tools",
                      md->projection->param.utm.zone,
                      md->projection->hem);
        assert (citation_length >= 0
                && citation_length <= max_citation_length);
        GTIFKeySet (ogtif, PCSCitationGeoKey, TYPE_ASCII, 1, citation);
        free (citation);
        break;
      case POLAR_STEREOGRAPHIC:
        GTIFKeySet (ogtif, ProjectedCSTypeGeoKey, TYPE_SHORT, 1,
                    user_defined_value_code);
        GTIFKeySet (ogtif, ProjectionGeoKey, TYPE_SHORT, 1,
                    user_defined_value_code);
        GTIFKeySet (ogtif, ProjLinearUnitsGeoKey, TYPE_SHORT, 1, Linear_Meter);
        GTIFKeySet (ogtif, ProjCoordTransGeoKey, TYPE_SHORT, 1,
                    CT_PolarStereographic);
        GTIFKeySet (ogtif, ProjStraightVertPoleLongGeoKey, TYPE_DOUBLE, 1,
                    md->projection->param.ps.slon);
        GTIFKeySet (ogtif, ProjOriginLatGeoKey, TYPE_DOUBLE, 1,
                    md->projection->param.ps.slat);
        GTIFKeySet (ogtif, ProjFalseEastingGeoKey, TYPE_DOUBLE, 1, 0.0);
        GTIFKeySet (ogtif, ProjFalseNorthingGeoKey, TYPE_DOUBLE, 1, 0.0);
        GTIFKeySet (ogtif, GeogLinearUnitsGeoKey, TYPE_SHORT, 1, Linear_Meter);
        GTIFKeySet (ogtif, GeogAngularUnitsGeoKey, TYPE_SHORT, 1, 
                    Angular_Degree);
/*
*        ///////////////////////////////////////////////////////////////////
*       //
*       // Here we emplay a slightly weird strategy: we always use a
*       // WGS84 datum, no matter what the ASF metadata for the
*       // product we are exporting says.  This is ok because the
*       // error introduced is very small compared to other error
*       // sources, and the geotiff viewers handle WGS84 datums
*       // much better than user defined ones.
*       //
*       ///////////////////////////////////////////////////////////////////
*
*       ** Maximum lenght of coordinate system description strings
*          used with the proj library. **
*       const size_t max_coordinate_system_description_length = 1000;
*       ** The coordinate system as described by the ASF metadata. **
*       char *tmp = malloc (max_coordinate_system_description_length + 1);
*       int write_count 
*         = snprintf (tmp, max_coordinate_system_description_length + 1,
*                     "+proj=stere +a=%lf +b=%lf +lat_0=%lf +lon_0=%lf "
*                     "+lat_ts=%lf", re_major, re_minor, 
*                     (md->projection->param.ps.slat > 0.0 ? 90.0 : -90.0),
*                     md->projection->param.ps.slon, 
*                     md->projection->param.ps.slat);
*       assert (write_count < max_coordinate_system_description_length + 1);
*       projPJ input_coordinate_system = pj_init_plus (tmp);
*       assert (input_coordinate_system != NULL);
*       ** The coordinate system to be used for the output geotiff.  **
*       write_count
*         = snprintf (tmp, max_coordinate_system_description_length + 1,
*                     "+proj=stere +datum=WGS84 +lat_0=%lf +lon_0=%lf "
*                     "+lat_ts=%lf", 
*                     (md->projection->param.ps.slat > 0.0 ? 90.0 : -90.0),
*                     md->projection->param.ps.slon, 
*                     md->projection->param.ps.slat);
*       assert (write_count < max_coordinate_system_description_length + 1);
*       projPJ geotiff_coordinate_system = pj_init_plus (tmp);
*       assert (geotiff_coordinate_system != NULL);
*       double tmp1 = md->projection->startX;
*       double tmp2 = md->projection->startY;
*       double tmp3 = 0;
*       return_code = pj_transform (input_coordinate_system, 
*                                   geotiff_coordinate_system, 1, 1, &tmp1, 
*                                   &tmp2, &tmp3);
*       assert (return_code == 0);
*       ** The maximum allowable projection error.  If changing the
*          datum from the one in the metadata to the WGS84 datum moves
*          the projection corner point by this amount or more in
*          projection coordinates, an exception is triggered.  This
*          value was chosen based ona seat-of-the-pants feel which
*          accounts for the various error sources: range migration,
*          etc.  If the geolocation process is anywhere near this
*          accurate, we are doing really good. **
*       const double max_allowable_projection_error = 30.0;
*       assert (sqrt (pow (fabs (tmp1 - md->projection->startX), 2)
*                     + pow (fabs (tmp2 - md->projection->startY), 2))
*               < max_allowable_projection_error);
*       free (tmp);
*
*        ///////////////////////////////////////////////////////////////////
*/
        /* Fill in the details of the geographic coordinate system used.  */
        switch ( ellipsoid ) {
        case CLARKE1866:
          GTIFKeySet (ogtif, GeographicTypeGeoKey, TYPE_SHORT, 1,
                      GCSE_Clarke1866);
          break;
        case GEM10C:
          GTIFKeySet (ogtif, GeographicTypeGeoKey, TYPE_SHORT, 1, GCSE_GEM10C);
          break;
        case WGS84:
          GTIFKeySet (ogtif, GeographicTypeGeoKey, TYPE_SHORT, 1, GCS_WGS_84);
          break;
        case USER_DEFINED:
          GTIFKeySet (ogtif, GeographicTypeGeoKey, TYPE_SHORT, 1,
                      user_defined_value_code);
          GTIFKeySet (ogtif, GeogGeodeticDatumGeoKey, TYPE_SHORT, 1,
                      user_defined_value_code);
          /* The angular units are degrees and the meridian is
             Greenwitch, so we don't need to define them explicitly.
             The GeogCitation key will be filled in later.  */
          GTIFKeySet (ogtif, GeogEllipsoidGeoKey, TYPE_SHORT, 1,
                      user_defined_value_code);
          GTIFKeySet (ogtif, GeogSemiMajorAxisGeoKey, TYPE_DOUBLE, 1,
                      re_major);
          GTIFKeySet (ogtif, GeogSemiMinorAxisGeoKey, TYPE_DOUBLE, 1,
                      re_minor);
          citation = MALLOC ((max_citation_length + 1) * sizeof (char));
          citation_length
            = snprintf (citation, max_citation_length + 1,
                        "Geographic coordinate system using reference "
                        "ellipsoid with semimajor axis of %f meters and "
                        "semiminor axis of %f meters",
                        re_major, re_minor);
          GTIFKeySet (ogtif, GeogCitationGeoKey, TYPE_ASCII, 1, citation);
          free (citation);
          break;
        default:
          assert (FALSE);       /* Shouldn't be here.  */
        }

        /* Set the citation key.  */
        citation = MALLOC ((max_citation_length + 1) * sizeof (char));
        citation_length
          = snprintf (citation, max_citation_length + 1,
                      "Polar stereographic projected GeoTIFF using ");
        switch ( ellipsoid ) {
        case CLARKE1866:
          citation_length
            += snprintf (citation + citation_length,
                         max_citation_length - citation_length + 1,
                         "CLARKE1866");
          assert (citation_length >= 0
                  && citation_length <= max_citation_length);
          break;
        case GEM10C:
          citation_length
            += snprintf (citation + citation_length,
                         max_citation_length - citation_length + 1,
                         "GEM10C ");
          assert (citation_length >= 0
                  && citation_length <= max_citation_length);
          break;
        case WGS84:
          citation_length
            += snprintf (citation + citation_length,
                         max_citation_length - citation_length + 1,
                         "WGS84 ");
          assert (citation_length >= 0
                  && citation_length <= max_citation_length);
          break;
        case USER_DEFINED:
          citation_length
            += snprintf (citation + citation_length,
                         max_citation_length - citation_length + 1,
                         "user defined ");
          assert (citation_length >= 0
                  && citation_length <= max_citation_length);
          break;
        default:
          assert (FALSE);       /* Shouldn't be here.  */
        }
        citation_length
          += snprintf (citation + citation_length,
                       max_citation_length - citation_length + 1,
                       "ellipsoid datum written by Alaska Satellite Facility "
                       "tools");
        assert (citation_length >= 0
                && citation_length <= max_citation_length);
        GTIFKeySet (ogtif, PCSCitationGeoKey, TYPE_ASCII, 1, citation);
        free (citation);

        break;
      default:
        assert (FALSE);         /* Shouldn't be here.  */
    }
  }

  /* FIXME: this is a terrible hack to deal with scansar crap.  */
  else if ( md->sar->image_type == 'G' 
            || (md->sar->image_type == 'P' 
                && md->projection->type == SCANSAR_PROJECTION) ) {
    GTIFKeySet (ogtif, GTModelTypeGeoKey, TYPE_SHORT, 1, ModelTypeGeographic);

    /*    if ( ellipsoid == WGS84 ) {*/
      GTIFKeySet (ogtif, GeographicTypeGeoKey, TYPE_SHORT, 1, GCSE_WGS84);
      /*    }
       *    else {
       *      ** User defined geographic coordinate system.  **
       *      GTIFKeySet (ogtif, GeographicTypeGeoKey, TYPE_SHORT, 1,
       *                  user_defined_value_code);
       *      switch ( ellipsoid ) {
       *      case CLARKE1866:
       *        GTIFKeySet (ogtif, GeogGeodeticDatumGeoKey, TYPE_SHORT, 1,
       *                    DatumE_Clarke1866);
       *        break;
       *      case GEM10C:
       *        GTIFKeySet (ogtif, GeogGeodeticDatumGeoKey, TYPE_SHORT, 1,
       *                    DatumE_GEM10C);
       *        break;
       *      case WGS66:
       *        ** Set to a newrby available ellipsoid.  We have far worse
       *           problems than the ellipsoid being a bit wrong.  **
       *        GTIFKeySet (ogtif, GeogGeodeticDatumGeoKey, TYPE_SHORT, 1,
       *                    DatumE_WGS84);
       *        break;
       *      case WGS84:
       *        ** Shouldn't be here (this should have been handled using the
       *           non-user defined GeographicTypeGeoKey).  **
       *        assert (FALSE);
       *        break;
       *      default:
       *        assert (FALSE);         ** Shouldn't be here.  **
       *      }
       *    }
       */
    GTIFKeySet (ogtif, GeogPrimeMeridianGeoKey, TYPE_SHORT, 1, PM_Greenwich);
    GTIFKeySet (ogtif, GeogAngularUnitsGeoKey, TYPE_SHORT, 1, Angular_Degree);
    {
      /* Tie points for image corners.  There is space for four tie
         points, each consisting of three raster coordinates, followed
         by three geospatial coordinates.  */
      double tie_points[4][6];

      /* Get the lat/longs of three image corners.  */
      double c1_lat, c1_long, c2_lat, c2_long, c3_lat, c3_long;
      meta_get_latLon (md, 0, 0, 0, &c1_lat, &c1_long);
      meta_get_latLon (md, 0, md->general->sample_count, 0, &c2_lat, &c2_long);
      meta_get_latLon (md, md->general->line_count, 0, 0, &c3_lat, &c3_long);

      /* Put three tie points in the image, as described in 2.6.2 of the
         geotiff spec..  */
      tie_points[0][0] = 0.0;
      tie_points[0][1] = 0.0;
      tie_points[0][2] = 0.0;
      tie_points[0][3] = c1_lat;
      tie_points[0][4] = c1_long;
      tie_points[0][5] = 0.0;
      tie_points[1][0] = 0.0;
      tie_points[1][1] = md->general->sample_count;
      tie_points[1][2] = 0.0;
      tie_points[1][4] = c2_lat;
      tie_points[1][5] = c2_long;
      tie_points[1][6] = 0.0;
      tie_points[2][0] = md->general->line_count;
      tie_points[2][1] = 0.0;
      tie_points[2][2] = 0.0;
      tie_points[2][4] = c3_lat;
      tie_points[2][5] = c3_long;
      tie_points[2][6] = 0.0;

      /* Write the eighteen values that make up the three tie
         points.  */
      TIFFSetField(otif, TIFFTAG_GEOTIEPOINTS, 18, tie_points);
    }
  }

  else if ( md->sar->image_type == 'S' ) {
    /* Slant range image conversion not implemented yet.  */
    assert (FALSE);
  }

  else {
    /* Shouldn't be here (unrecognized image type). */
    assert (FALSE);
  }

  /* Write the actual image data.  */
  for ( ii = 0 ; ii < line_count ; ii++ ) {
    if (scale == NONE) {
      if ( TIFFWriteScanline (otif, daf + sample_count * ii, ii, 0) < 0 ) {
	char* temp;
	sprintf(temp, "Error writing to output geotiff file %s", output_file_name);
	print_error(temp);
	exit (EXIT_FAILURE);
      }
    }
    else {
      if ( TIFFWriteScanline (otif, pixels + sample_count * ii, ii, 0) < 0 ) {
	char* temp;
	sprintf(temp, "Error writing to output geotiff file %s", output_file_name);
	print_error(temp);
	exit (EXIT_FAILURE);
      }
    }
  }

  return_code = GTIFWriteKeys (ogtif);
  assert (return_code);

  GTIFFree (ogtif);
  XTIFFClose (otif);
  free (daf);
  meta_free (md);
}

