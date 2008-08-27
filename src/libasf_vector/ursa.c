#include "asf_vector.h"
#include "shapefil.h"
#include "asf_nan.h"
#include <assert.h>
#include <errno.h>
#include <ctype.h>
#include "ursa.h"

void ursa_init(ursa_type_t *ursa)
{
  strcpy(ursa->granule_name, MAGIC_UNSET_STRING);
  strcpy(ursa->granule_type, MAGIC_UNSET_STRING);
  strcpy(ursa->platform, MAGIC_UNSET_STRING);
  strcpy(ursa->sensor, MAGIC_UNSET_STRING);
  ursa->orbit = MAGIC_UNSET_INT;
  strcpy(ursa->beam_mode, MAGIC_UNSET_STRING);
  ursa->off_nadir_angle = MAGIC_UNSET_DOUBLE;
  strcpy(ursa->start_time, MAGIC_UNSET_STRING);
  strcpy(ursa->end_time, MAGIC_UNSET_STRING);
  ursa->near_start_lat = MAGIC_UNSET_DOUBLE;
  ursa->near_start_lon = MAGIC_UNSET_DOUBLE;
  ursa->far_start_lat = MAGIC_UNSET_DOUBLE;
  ursa->far_start_lon = MAGIC_UNSET_DOUBLE;
  ursa->near_end_lat = MAGIC_UNSET_DOUBLE;
  ursa->near_end_lon = MAGIC_UNSET_DOUBLE;
  ursa->far_end_lat = MAGIC_UNSET_DOUBLE;
  ursa->far_end_lon = MAGIC_UNSET_DOUBLE;
  ursa->center_lat = MAGIC_UNSET_DOUBLE;
  ursa->center_lon = MAGIC_UNSET_DOUBLE;
  ursa->path_number = MAGIC_UNSET_INT;
  ursa->frame_number = MAGIC_UNSET_INT;
  ursa->cloud_cover = MAGIC_UNSET_INT;
  ursa->faraday_rotation = MAGIC_UNSET_DOUBLE;
}

static void strip_end_whitesp(char *s)
{
    char *p = s + strlen(s) - 1;
    while (isspace(*p) && p>s)
        *p-- = '\0';
}

static void msg(const char *format, ...)
{
    char buf[1024];
    va_list ap;
    va_start(ap, format);
    vsprintf(buf, format, ap);
    va_end(ap);

    // in the future, we'll be putting this in a textview or something!!
    //GtkWidget *tv = get_widget_checked("messages_textview");
    //GtkTextBuffer *tb = gtk_text_view_get_buffer(GTK_TEXT_VIEW(tv));

    //GtkTextIter end;
    //gtk_text_buffer_get_end_iter(tb, &end);
    //gtk_text_buffer_insert(tb, &end, buf, -1);

    printf(buf);
}

static char *my_parse_string(char *p, char *s, int max_len)
{
    if (!p || *p == '\0') {
        strcpy(s, "");
        msg("  --> Unexpected end of string\n");
        return NULL;
    }

    // scan ahead to the comma, or end of string
    char *q = strchr(p, ',');
    if (q) {
      *q = '\0'; // temporarily...
      strncpy_safe(s, p, max_len);
      *q = ',';

      // point to beginning of next item
      return q+1;
    }
    else {
      strncpy_safe(s, p, max_len);

      // no more strings
      return NULL;
    }
}

static char *get_str(char *line, int column_num)
{
    int i;
    char *p = line;
    char *ret = (char *) MALLOC(sizeof(char)*255);;

    for (i=0; i<=column_num; ++i)
      p = my_parse_string(p,ret,256);

    ret[strlen(ret)-1] = '\0';
    ret++;
    return ret;
}

static int get_int(char *line, int column_num)
{
    if (column_num >= 0) {
        char *s = get_str(line, column_num);
        if (s)
          return atoi(s);
        else
          return 0;
    }
    else {
        return 0;
    }
}

static double get_double(char *line, int column_num)
{
    if (column_num >= 0) {
        char *s = get_str(line, column_num);
        if (s)
          return atof(s);
        else
          return 0.0;
    } else
        return 0.0;
}

static double get_req_double(char *line, int column_num, int *ok)
{
    if (column_num >= 0) {
        char *str = get_str(line, column_num);
        if (str && strlen(str)>0) {
            *ok=TRUE;
            return atof(str);
        }
        else {
            *ok=FALSE;
            return 0.0;
        }
    }
    else {
        *ok=FALSE;
        return 0.0;
    }
}

static int find_col(char *line, char *column_header)
{
    char *p = line;
    char val[256];
    int col=0;

    while (p) {
        p=my_parse_string(p,val,256);
        if (strncmp_case(val+1,column_header,strlen(column_header))==0)
          return col;
        ++col;
    }

    // column heading was not found
    return -1;
}

static void add_to_kml(FILE *fp, ursa_type_t *ursa, dbf_header_t *dbf,
               int nCols)
{
  int ii;
  char begin[10], end[10];

  // Print out according to configuration
  fprintf(fp, "<Placemark>\n");
  fprintf(fp, "  <description><![CDATA[\n");
  fprintf(fp, "<table width=\"350\"><tr><td>\n");
  fprintf(fp, "<!-- Format: URSA (generated by convert2vector "
          "(version %s)) -->\n", SVN_REV);
  for (ii=0; ii<nCols; ii++) {
    if (dbf[ii].visible == 0) {
      strcpy(begin, "<!--");
      strcpy(end, "-->\n");
    }
    else {
      strcpy(begin, "");
      strcpy(end, "\n");
    }
    if (strcmp(dbf[ii].header, "Granule_Name") == 0)
      fprintf(fp, "%s<strong>Granule Name</strong>: %s <br>%s",
          begin, ursa->granule_name, end);
    else if (strcmp(dbf[ii].header, "Granule_Type") == 0)
      fprintf(fp, "%s<strong>Granule Type</strong>: %s <br>%s",
          begin, ursa->granule_type, end);
    else if (strcmp(dbf[ii].header, "Platform") == 0)
      fprintf(fp, "%s<strong>Platform</strong>: %s <br>%s",
          begin, ursa->platform, end);
    else if (strcmp(dbf[ii].header, "Sensor") == 0)
      fprintf(fp, "%s<strong>Sensor</strong>: %s <br>%s",
          begin, ursa->sensor, end);
    else if (strcmp(dbf[ii].header, "Orbit") == 0)
      fprintf(fp, "%s<strong>Orbit</strong>: %d <br>%s",
          begin, ursa->orbit, end);
    else if (strcmp(dbf[ii].header, "Beam_mode") == 0)
      fprintf(fp, "%s<strong>Beam mode</strong>: %s <br>%s",
          begin, ursa->beam_mode, end);
    else if (strcmp(dbf[ii].header, "Off_Nadir_Angle") == 0)
      fprintf(fp, "%s<strong>Off Nadir Angle</strong>: %s <br>%s",
          begin, lf(ursa->off_nadir_angle), end);
    else if (strcmp(dbf[ii].header, "Start_Time") == 0)
      fprintf(fp, "%s<strong>Start Time</strong>: %s <br>%s",
          begin, ursa->start_time, end);
    else if (strcmp(dbf[ii].header, "End_Time") == 0)
      fprintf(fp, "%s<strong>End Time</strong>: %s <br>%s",
          begin, ursa->end_time, end);
    else if (strcmp(dbf[ii].header, "Near_Start_Lat") == 0)
      fprintf(fp, "%s<strong>Near Start Lat</strong>: %s <br>%s",
          begin, lf(ursa->near_start_lat), end);
    else if (strcmp(dbf[ii].header, "Near_Start_Lon") == 0)
      fprintf(fp, "%s<strong>Near Start Lon</strong>: %s <br>%s",
          begin, lf(ursa->near_start_lon), end);
    else if (strcmp(dbf[ii].header, "Far_Start_Lat") == 0)
      fprintf(fp, "%s<strong>Far Start Lat</strong>: %s <br>%s",
          begin, lf(ursa->far_start_lat), end);
    else if (strcmp(dbf[ii].header, "Far_Start_Lon") == 0)
      fprintf(fp, "%s<strong>Far Start Lon</strong>: %s <br>%s",
          begin, lf(ursa->far_start_lon), end);
    else if (strcmp(dbf[ii].header, "Near_End_Lat") == 0)
      fprintf(fp, "%s<strong>Near End Lat</strong>: %s <br>%s",
          begin, lf(ursa->near_end_lat), end);
    else if (strcmp(dbf[ii].header, "Near_End_Lon") == 0)
      fprintf(fp, "%s<strong>Near End Lon</strong>: %s <br>%s",
          begin, lf(ursa->near_end_lon), end);
    else if (strcmp(dbf[ii].header, "Far_End_Lat") == 0)
      fprintf(fp, "%s<strong>Far End Lat</strong>: %s <br>%s",
          begin, lf(ursa->far_end_lat), end);
    else if (strcmp(dbf[ii].header, "Far_End_Lon") == 0)
      fprintf(fp, "%s<strong>Far End Lon</strong>: %s <br>%s",
          begin, lf(ursa->far_end_lon), end);
    else if (strcmp(dbf[ii].header, "Center_Lat") == 0)
      fprintf(fp, "%s<strong>Center Lat</strong>: %s <br>%s",
          begin, lf(ursa->center_lat), end);
    else if (strcmp(dbf[ii].header, "Center_Lon") == 0)
      fprintf(fp, "%s<strong>Center Lon</strong>: %s <br>%s",
          begin, lf(ursa->center_lon), end);
    else if (strcmp(dbf[ii].header, "Path_Number") == 0)
      fprintf(fp, "%s<strong>Path Number</strong>: %d <br>%s",
          begin, ursa->path_number, end);
    else if (strcmp(dbf[ii].header, "Frame_Number") == 0)
      fprintf(fp, "%s<strong>Frame Number</strong>: %d <br>%s",
          begin, ursa->frame_number, end);
    else if (strcmp(dbf[ii].header, "Cloud_Cover") == 0)
      fprintf(fp, "%s<strong>Cloud Cover</strong>: %d <br>%s",
          begin, ursa->cloud_cover, end);
    else if (strcmp(dbf[ii].header, "Faraday_Rotation") == 0)
      fprintf(fp, "%s<strong>Faraday Rotation</strong>: %s <br>%s",
          begin, lf(ursa->faraday_rotation), end);
  }
  fprintf(fp, "</td></tr></table>\n");
  fprintf(fp, "  ]]></description>\n");
  fprintf(fp, "  <name>%s</name>\n", ursa->granule_name);
  fprintf(fp, "  <LookAt>\n");
  fprintf(fp, "    <longitude>%.10f</longitude>\n", ursa->center_lon);
  fprintf(fp, "    <latitude>%.10f</latitude>\n", ursa->center_lat);
  fprintf(fp, "    <range>400000</range>\n");
  fprintf(fp, "    <tilt>30</tilt>\n");
  fprintf(fp, "  </LookAt>\n");
  fprintf(fp, "  <visibility>1</visibility>\n");
  fprintf(fp, "  <open>1</open>\n");

  write_kml_style_keys(fp);

  fprintf(fp, "  <Polygon>\n");
  fprintf(fp, "    <extrude>1</extrude>\n");
  fprintf(fp, "    <altitudeMode>absolute</altitudeMode>\n");
  fprintf(fp, "    <outerBoundaryIs>\n");
  fprintf(fp, "     <LinearRing>\n");
  fprintf(fp, "      <coordinates>\n");
  fprintf(fp, "       %.12f,%.12f,7000\n",
      ursa->near_start_lon, ursa->near_start_lat);
  fprintf(fp, "       %.12f,%.12f,7000\n",
      ursa->far_start_lon, ursa->far_start_lat);
  fprintf(fp, "       %.12f,%.12f,7000\n",
      ursa->far_end_lon, ursa->far_end_lat);
  fprintf(fp, "       %.12f,%.12f,7000\n",
      ursa->near_end_lon, ursa->near_end_lat);
  fprintf(fp, "       %.12f,%.12f,7000\n",
      ursa->near_start_lon, ursa->near_start_lat);
  fprintf(fp, "      </coordinates>\n");
  fprintf(fp, "     </LinearRing>\n");
  fprintf(fp, "    </outerBoundaryIs>\n");
  fprintf(fp, "  </Polygon>\n");
  fprintf(fp, "</Placemark>\n");
}

static int read_ursa_line(char *header, int n,char *line, ursa_type_t *ursa)
{
  int ii, ok;
  char *test = (char *) MALLOC(sizeof(char)*255);
  for (ii=0; ii<n; ii++) {
    test = get_column(header, ii);
    test[strlen(test)-1] = '\0';
    test++;
    if (strcmp(test, "Granule Name") == 0)
      strcpy(ursa->granule_name, get_str(line, ii));
    else if (strcmp(test, "Granule Type") == 0)
      strcpy(ursa->granule_type, get_str(line, ii));
    else if (strcmp(test, "Platform") == 0)
      strcpy(ursa->platform, get_str(line, ii));
    else if (strcmp(test, "Sensor") == 0)
      strcpy(ursa->sensor, get_str(line, ii));
    else if (strcmp(test, "Orbit") == 0)
      ursa->orbit = get_int(line, ii);
    else if (strcmp(test, "Beam Mode") == 0)
      strcpy(ursa->beam_mode, get_str(line, ii));
    else if (strcmp(test, "Off Nadir Angle") == 0)
      ursa->off_nadir_angle = get_double(line, ii);
    else if (strcmp(test, "Start Time") == 0)
      strcpy(ursa->start_time, get_str(line, ii));
    else if (strcmp(test, "End Time") == 0)
      strcpy(ursa->end_time, get_str(line, ii));
    else if (strcmp(test, "Near Start Lat") == 0)
      ursa->near_start_lat = get_req_double(line, ii, &ok);
    else if (strcmp(test, "Near Start Lon") == 0)
      ursa->near_start_lon = get_req_double(line, ii, &ok);
    else if (strcmp(test, "Far Start Lat") == 0)
      ursa->far_start_lat = get_req_double(line, ii, &ok);
    else if (strcmp(test, "Far Start Lon") == 0)
      ursa->far_start_lon = get_req_double(line, ii, &ok);
    else if (strcmp(test, "Near End Lat") == 0)
      ursa->near_end_lat = get_req_double(line, ii, &ok);
    else if (strcmp(test, "Near End Lon") == 0)
      ursa->near_end_lon = get_req_double(line, ii, &ok);
    else if (strcmp(test, "Far End Lat") == 0)
      ursa->far_end_lat = get_req_double(line, ii, &ok);
    else if (strcmp(test, "Far End Lon") == 0)
      ursa->far_end_lon = get_req_double(line, ii, &ok);
    else if (strcmp(test, "Center Lat") == 0)
      ursa->center_lat = get_double(line, ii);
    else if (strcmp(test, "Center Lon") == 0)
      ursa->center_lon = get_double(line, ii);
    else if (strcmp(test, "Path Number") == 0)
      ursa->path_number = get_int(line, ii);
    else if (strcmp(test, "Frame Number") == 0)
      ursa->frame_number = get_int(line, ii);
    else if (strcmp(test, "Cloud Cover") == 0)
      ursa->cloud_cover = get_int(line, ii);
    else if (strcmp(test, "Faraday Rotation") == 0)
      ursa->faraday_rotation = get_double(line, ii);
  }

  return ok;
}

// Check location information
static int check_ursa_location(FILE *ifp, char **header_line, int *n)
{
  dbf_header_t *dbf;
  int ii, nCols;
  char *header = (char *) MALLOC(sizeof(char)*1024);
  fgets(header, 1024, ifp);
  strip_end_whitesp(header);
  int nColumns = get_number_columns(header);

  // Read configuration file
  read_header_config("URSA", &dbf, &nCols);

  // ensure we have the columns we need
  int granule_col = find_col(header, "Granule Name");
  int near_start_lat_col = find_col(header, "Near Start Lat");
  int near_start_lon_col = find_col(header, "Near Start Lon");
  int far_start_lat_col = find_col(header, "Far Start Lat");
  int far_start_lon_col = find_col(header, "Far Start Lon");
  int near_end_lat_col = find_col(header, "Near End Lat");
  int near_end_lon_col = find_col(header, "Near End Lon");
  int far_end_lat_col = find_col(header, "Far End Lat");
  int far_end_lon_col = find_col(header, "Far End Lon");

  // Check whether all visible columns are actually available in the file
  for (ii=0; ii<nCols; ii++) {
    if (find_col(header, dbf[ii].header) < 0)
      dbf[ii].visible = FALSE;
  }

  int all_ok=TRUE;
  if (granule_col < 0) {
    printf("Missing: Granule Name\n");
    all_ok=FALSE;
  }
  if (near_start_lat_col < 0) {
    printf("Missing: Near Start Lat\n");
    all_ok=FALSE;
  }
  if (near_start_lon_col < 0) {
    printf("Missing: Near Start Lon\n");
    all_ok=FALSE;
  }
  if (far_start_lat_col < 0) {
    printf("Missing: Far Start Lat\n");
    all_ok=FALSE;
  }
  if (far_start_lon_col < 0) {
    printf("Missing: Far Start Lon\n");
    all_ok=FALSE;
  }
  if (near_end_lat_col < 0) {
    printf("Missing: Near End Lat\n");
    all_ok=FALSE;
  }
  if (near_end_lon_col < 0) {
    printf("Missing: Near End Lon\n");
    all_ok=FALSE;
  }
  if (far_end_lat_col < 0) {
    printf("Missing: Far End Lat\n");
    all_ok=FALSE;
  }
  if (far_end_lon_col < 0) {
    printf("Missing: Far End Lon\n");
    all_ok=FALSE;
  }
  if (!all_ok) {
    printf("Required data columns missing, cannot process this file.\n");
    return 0;
  }
  *header_line = header;
  *n = nColumns;

  return 1;
}

// Convert ursa to kml file
int ursa2kml(char *in_file, char *out_file, int listFlag)
{
  ursa_type_t ursa;
  dbf_header_t *dbf;
  char *header;
  int nCols, nColumns;
  char line[1024];

  // Read configuration file
  read_header_config("URSA", &dbf, &nCols);

  FILE *ifp = FOPEN(in_file, "r");
  assert(ifp);
  check_ursa_location(ifp, &header, &nColumns);

  FILE *ofp = FOPEN(out_file, "w");
  if (!ofp) {
    printf("Failed to open output file %s: %s\n", out_file, strerror(errno));
    return 0;
  }

  kml_header(ofp);

  while (fgets(line, 1022, ifp) != NULL) {
    strip_end_whitesp(line);

    // ensure all lines end with a comma, that way the final column
    // does not need special treatment
    line[strlen(line)+1] = '\0';
    line[strlen(line)] = ',';

    // now get the individual column values
    ursa_init(&ursa);
    if (read_ursa_line(header, nColumns, line, &ursa))
      add_to_kml(ofp, &ursa, dbf, nCols);
  }

  kml_footer(ofp);

  fclose(ifp);
  fclose(ofp);

  return 1;
}

void shape_ursa_init(char *inFile, char *header)
{
  char *dbaseFile;
  DBFHandle dbase;
  SHPHandle shape;
  dbf_header_t *dbf;
  int ii, nCols, length=50;

  // Read configuration file
  read_header_config("URSA", &dbf, &nCols);

  // Open database for initialization
  dbaseFile = (char *) MALLOC(sizeof(char)*(strlen(inFile)+5));
  sprintf(dbaseFile, "%s.dbf", inFile);
  dbase = DBFCreate(dbaseFile);
  if (!dbase)
    asfPrintError("Could not create database file '%s'\n", dbaseFile);

  // Add fields to database
  for (ii=0; ii<nCols; ii++) {
    if (strcmp(dbf[ii].header, "Granule_Name") == 0 && dbf[ii].visible) {
      if (DBFAddField(dbase, "GRAN_NAME", FTString, length, 0) == -1)
        asfPrintError("Could not add GRAN_NAME field to database file\n");
    }
    else if (strcmp(dbf[ii].header, "Granule_Type") == 0 && dbf[ii].visible) {
      if (DBFAddField(dbase, "GRAN_TYPE", FTString, length, 0) == -1)
        asfPrintError("Could not add GRAN_TYPE field to database file\n");
    }
    else if (strcmp(dbf[ii].header, "Platform") == 0 && dbf[ii].visible) {
      if (DBFAddField(dbase, "PLATFORM", FTString, length, 0) == -1)
        asfPrintError("Could not add PLATFORM field to database file\n");
    }
    else if (strcmp(dbf[ii].header, "Sensor") == 0 && dbf[ii].visible) {
      if (DBFAddField(dbase, "SENSOR", FTString, length, 0) == -1)
        asfPrintError("Could not add SENSOR field to database file\n");
    }
    else if (strcmp(dbf[ii].header, "Orbit") == 0 && dbf[ii].visible) {
      if (DBFAddField(dbase, "ORBIT", FTInteger, 7, 0) == -1)
        asfPrintError("Could not add ORBIT field to database file\n");
    }
    else if (strcmp(dbf[ii].header, "Beam_Mode") == 0 && dbf[ii].visible) {
      if (DBFAddField(dbase, "BEAM_MODE", FTString, length, 0) == -1)
        asfPrintError("Could not add BEAM_MODE field to database file\n");
    }
    else if (strcmp(dbf[ii].header, "Off_Nadir_Angle") == 0 &&
         dbf[ii].visible) {
      if (DBFAddField(dbase, "OFF_NADIR", FTDouble, 16, 7) == -1)
        asfPrintError("Could not add OFF_NADIR field to database file\n");
    }
    else if (strcmp(dbf[ii].header, "Start_Time") == 0 && dbf[ii].visible) {
      if (DBFAddField(dbase, "START_TIME", FTString, length, 0) == -1)
        asfPrintError("Could not add START_TIME field to database file\n");
    }
    else if (strcmp(dbf[ii].header, "End_Time") == 0 && dbf[ii].visible) {
      if (DBFAddField(dbase, "END_TIME", FTString, length, 0) == -1)
        asfPrintError("Could not add END_TIME field to database file\n");
    }
    else if (strcmp(dbf[ii].header, "Near_Start_Lat") == 0 &&
         dbf[ii].visible) {
      if (DBFAddField(dbase, "NSTART_LAT", FTDouble, 16, 7) == -1)
        asfPrintError("Could not add NSTART_LAT field to database file\n");
    }
    else if (strcmp(dbf[ii].header, "Near_Start_Lon") == 0 &&
         dbf[ii].visible) {
      if (DBFAddField(dbase, "NSTART_LON", FTDouble, 16, 7) == -1)
        asfPrintError("Could not add NSTART_LON field to database file\n");
    }
    else if (strcmp(dbf[ii].header, "Far_Start_Lat") == 0 && dbf[ii].visible) {
      if (DBFAddField(dbase, "FSTART_LAT", FTDouble, 16, 7) == -1)
        asfPrintError("Could not add FSTART_LAT field to database file\n");
    }
    else if (strcmp(dbf[ii].header, "Far_Start_Lon") == 0 && dbf[ii].visible) {
      if (DBFAddField(dbase, "FSTART_LON", FTDouble, 16, 7) == -1)
        asfPrintError("Could not add FSTART_LON field to database file\n");
    }
    else if (strcmp(dbf[ii].header, "Near_End_Lat") == 0 && dbf[ii].visible) {
      if (DBFAddField(dbase, "N_END_LAT", FTDouble, 16, 7) == -1)
        asfPrintError("Could not add N_END_LAT field to database file\n");
    }
    else if (strcmp(dbf[ii].header, "Near_End_Lon") == 0 && dbf[ii].visible) {
      if (DBFAddField(dbase, "N_END_LON", FTDouble, 16, 7) == -1)
        asfPrintError("Could not add N_END_LON field to database file\n");
    }
    else if (strcmp(dbf[ii].header, "Far_End_Lat") == 0 && dbf[ii].visible) {
      if (DBFAddField(dbase, "F_END_LAT", FTDouble, 16, 7) == -1)
        asfPrintError("Could not add F_END_LAT field to database file\n");
    }
    else if (strcmp(dbf[ii].header, "Far_End_Lon") == 0 && dbf[ii].visible) {
      if (DBFAddField(dbase, "F_END_LON", FTDouble, 16, 7) == -1)
        asfPrintError("Could not add F_END_LON field to database file\n");
    }
    else if (strcmp(dbf[ii].header, "Center_Lat") == 0 && dbf[ii].visible) {
      if (DBFAddField(dbase, "CENTER_LAT", FTDouble, 16, 7) == -1)
        asfPrintError("Could not add CENTER_LAT field to database file\n");
    }
    else if (strcmp(dbf[ii].header, "Center_Lon") == 0 && dbf[ii].visible) {
      if (DBFAddField(dbase, "CENTER_LON", FTDouble, 16, 7) == -1)
        asfPrintError("Could not add CENTER_LON field to database file\n");
    }
    else if (strcmp(dbf[ii].header, "Path_Number") == 0 && dbf[ii].visible) {
      if (DBFAddField(dbase, "PATH_NUM", FTInteger, 10, 0) == -1)
        asfPrintError("Could not add PATH_NUM field to database file\n");
    }
    else if (strcmp(dbf[ii].header, "Frame_Number") == 0 && dbf[ii].visible) {
      if (DBFAddField(dbase, "FRAME_NUM", FTInteger, 10, 0) == -1)
        asfPrintError("Could not add FRAME_NUM field to database file\n");
    }
    else if (strcmp(dbf[ii].header, "Cloud_Cover") == 0 && dbf[ii].visible) {
      if (DBFAddField(dbase, "CLOUDCOVER", FTInteger, 5, 0) == -1)
        asfPrintError("Could not add CLOUDCOVER field to database file\n");
    }
    else if (strcmp(dbf[ii].header, "Faraday_Rotation") == 0 &&
         dbf[ii].visible) {
      if (DBFAddField(dbase, "FARADAYROT", FTDouble, 16, 7) == -1)
        asfPrintError("Could not add FARADAYROT field to database file\n");
    }
  }

  // Close the database for initialization
  DBFClose(dbase);

  // Open shapefile for initialization
  shape = SHPCreate(inFile, SHPT_POLYGON);
  if (!shape)
    asfPrintError("Could not create shapefile '%s'\n", inFile);

  // Close shapefile for initialization
  SHPClose(shape);

  FREE(dbaseFile);

  return;
}

static void add_to_shape(DBFHandle dbase, SHPHandle shape, ursa_type_t *ursa,
             dbf_header_t *dbf, int nCols, int n)
{
  int ii, field = 0;

  // Write fields into the database
  for (ii=0; ii<nCols; ii++) {
    if (strcmp(dbf[ii].header, "Granule_Name") == 0 && dbf[ii].visible) {
      DBFWriteStringAttribute(dbase, n, field, ursa->granule_name);
      field++;
    }
    else if (strcmp(dbf[ii].header, "Granule_Type") == 0 && dbf[ii].visible) {
      DBFWriteStringAttribute(dbase, n, field, ursa->granule_type);
      field++;
    }
    else if (strcmp(dbf[ii].header, "Platform") == 0 && dbf[ii].visible) {
      DBFWriteStringAttribute(dbase, n, field, ursa->platform);
      field++;
    }
    else if (strcmp(dbf[ii].header, "Sensor") == 0 && dbf[ii].visible) {
      DBFWriteStringAttribute(dbase, n, field, ursa->sensor);
      field++;
    }
    else if (strcmp(dbf[ii].header, "Orbit") == 0 && dbf[ii].visible) {
      DBFWriteIntegerAttribute(dbase, n, field, ursa->orbit);
      field++;
    }
    else if (strcmp(dbf[ii].header, "Beam_Mode") == 0 && dbf[ii].visible) {
      DBFWriteStringAttribute(dbase, n, field, ursa->beam_mode);
      field++;
    }
    else if (strcmp(dbf[ii].header, "Off_Nadir_Angle") == 0 &&
         dbf[ii].visible) {
      DBFWriteDoubleAttribute(dbase, n, field, ursa->off_nadir_angle);
      field++;
    }
    else if (strcmp(dbf[ii].header, "Start_Time") == 0 && dbf[ii].visible) {
      DBFWriteStringAttribute(dbase, n, field, ursa->start_time);
      field++;
    }
    else if (strcmp(dbf[ii].header, "End_Time") == 0 && dbf[ii].visible) {
      DBFWriteStringAttribute(dbase, n, field, ursa->end_time);
      field++;
    }
    else if (strcmp(dbf[ii].header, "Near_Start_Lat") == 0 &&
         dbf[ii].visible) {
      DBFWriteDoubleAttribute(dbase, n, field, ursa->near_start_lat);
      field++;
    }
    else if (strcmp(dbf[ii].header, "Near_Start_Lon") == 0 &&
         dbf[ii].visible) {
      DBFWriteDoubleAttribute(dbase, n, field, ursa->near_start_lon);
      field++;
    }
    else if (strcmp(dbf[ii].header, "Far_Start_Lat") == 0 && dbf[ii].visible) {
      DBFWriteDoubleAttribute(dbase, n, field, ursa->far_start_lat);
      field++;
    }
    else if (strcmp(dbf[ii].header, "Far_Start_Lon") == 0 && dbf[ii].visible) {
      DBFWriteDoubleAttribute(dbase, n, field, ursa->far_start_lon);
      field++;
    }
    else if (strcmp(dbf[ii].header, "Near_End_Lat") == 0 && dbf[ii].visible) {
      DBFWriteDoubleAttribute(dbase, n, field, ursa->near_end_lat);
      field++;
    }
    else if (strcmp(dbf[ii].header, "Near_End_Lon") == 0 && dbf[ii].visible) {
      DBFWriteDoubleAttribute(dbase, n, field, ursa->near_end_lon);
      field++;
    }
    else if (strcmp(dbf[ii].header, "Far_End_Lat") == 0 && dbf[ii].visible) {
      DBFWriteDoubleAttribute(dbase, n, field, ursa->far_end_lat);
      field++;
    }
    else if (strcmp(dbf[ii].header, "Far_End_Lon") == 0 && dbf[ii].visible) {
      DBFWriteDoubleAttribute(dbase, n, field, ursa->far_end_lon);
      field++;
    }
    else if (strcmp(dbf[ii].header, "Center_Lat") == 0 && dbf[ii].visible) {
      DBFWriteDoubleAttribute(dbase, n, field, ursa->center_lat);
      field++;
    }
    else if (strcmp(dbf[ii].header, "Center_Lon") == 0 && dbf[ii].visible) {
      DBFWriteDoubleAttribute(dbase, n, field, ursa->center_lon);
      field++;
    }
    else if (strcmp(dbf[ii].header, "Path_Number") == 0 && dbf[ii].visible) {
      DBFWriteIntegerAttribute(dbase, n, field, ursa->path_number);
      field++;
    }
    else if (strcmp(dbf[ii].header, "Frame_Number") == 0 && dbf[ii].visible) {
      DBFWriteIntegerAttribute(dbase, n, field, ursa->frame_number);
      field++;
    }
    else if (strcmp(dbf[ii].header, "Cloud_Cover") == 0 && dbf[ii].visible) {
      DBFWriteIntegerAttribute(dbase, n, field, ursa->cloud_cover);
      field++;
    }
    else if (strcmp(dbf[ii].header, "Faraday_Rotation") == 0 &&
         dbf[ii].visible) {
      DBFWriteDoubleAttribute(dbase, n, field, ursa->faraday_rotation);
      field++;
    }
  }

  double lat[5], lon[5];
  lat[0] = lat[4] = ursa->near_start_lat;
  lon[0] = lon[4] = ursa->near_start_lon;
  lat[1] = ursa->far_start_lat;
  lon[1] = ursa->far_start_lon;
  lat[2] = ursa->far_end_lat;
  lon[2] = ursa->far_end_lon;
  lat[3] = ursa->near_end_lat;
  lon[3] = ursa->near_end_lon;

  // Write shape object
  SHPObject *shapeObject=NULL;
  shapeObject = SHPCreateSimpleObject(SHPT_POLYGON, 5, lon, lat, NULL);
  SHPWriteObject(shape, -1, shapeObject);
  SHPDestroyObject(shapeObject);
}

int ursa2shape(char *inFile, char *outFile, int listFlag)
{
  DBFHandle dbase;
  SHPHandle shape;
  ursa_type_t ursa;
  dbf_header_t *dbf;
  char *header, line[1024];
  int nCols, nColumns, ii=0;

  // Read configuration file
  read_header_config("URSA", &dbf, &nCols);

  // Read ursa file
  FILE *ifp = FOPEN(inFile, "r");
  assert(ifp);
  check_ursa_location(ifp, &header, &nColumns);

  // Initalize the database file
  shape_ursa_init(outFile, header);
  open_shape(outFile, &dbase, &shape);

  while (fgets(line, 1022, ifp) != NULL) {
    strip_end_whitesp(line);

    // ensure all lines end with a comma, that way the final column
    // does not need special treatment
    line[strlen(line)+1] = '\0';
    line[strlen(line)] = ',';

    // now get the individual column values
    ursa_init(&ursa);
    if (read_ursa_line(header, nColumns, line, &ursa)) {
      add_to_shape(dbase, shape, &ursa, dbf, nCols, ii);
      ii++;
    }
  }

  // Clean up
  close_shape(dbase, shape);
  write_esri_proj_file(outFile);

  FCLOSE(ifp);

  return 1;
}
