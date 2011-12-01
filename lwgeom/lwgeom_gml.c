/**********************************************************************
 * $Id$
 *
 * PostGIS - Spatial Types for PostgreSQL
 * http://postgis.refractions.net
 * Copyright 2001-2003 Refractions Research Inc.
 *
 * This is free software; you can redistribute and/or modify it under
 * the terms of hte GNU General Public Licence. See the COPYING file.
 *
 **********************************************************************
 *
 * GML output routines.
 *
 **********************************************************************/


#include "postgres.h"
#include "executor/spi.h"

#include "lwgeom_pg.h"
#include "liblwgeom.h"

Datum LWGEOM_asGML(PG_FUNCTION_ARGS);

char *geometry_to_gml2(uchar *srl, char *srs);

static size_t asgml2_point_size(LWPOINT *point, char *srs);
static char *asgml2_point(LWPOINT *point, char *srs);
static size_t asgml2_line_size(LWLINE *line, char *srs);
static char *asgml2_line(LWLINE *line, char *srs);
static size_t asgml2_poly_size(LWPOLY *poly, char *srs);
static char *asgml2_poly(LWPOLY *poly, char *srs);
static size_t asgml2_inspected_size(LWGEOM_INSPECTED *geom, char *srs);
static char *asgml2_inspected(LWGEOM_INSPECTED *geom, char *srs);
static size_t pointArray_toGML2(POINTARRAY *pa, char *buf);

char *geometry_to_gml3(uchar *srl, char *srs);

static size_t asgml3_point_size(LWPOINT *point, char *srs);
static char *asgml3_point(LWPOINT *point, char *srs);
static size_t asgml3_line_size(LWLINE *line, char *srs);
static char *asgml3_line(LWLINE *line, char *srs);
static size_t asgml3_poly_size(LWPOLY *poly, char *srs);
static char *asgml3_poly(LWPOLY *poly, char *srs);
static size_t asgml3_inspected_size(LWGEOM_INSPECTED *geom, char *srs);
static char *asgml3_inspected(LWGEOM_INSPECTED *geom, char *srs);
static size_t pointArray_toGML3(POINTARRAY *pa, char *buf);

static size_t pointArray_GMLsize(POINTARRAY *pa);
static char *getSRSbySRID(int SRID);

/* Add dot, sign, exponent sign, 'e', exponent digits */
#define SHOW_DIGS (precision + 8)

/* Globals */
int precision;


/**
 * Encode feature in GML 
 */
PG_FUNCTION_INFO_V1(LWGEOM_asGML);
Datum LWGEOM_asGML(PG_FUNCTION_ARGS)
{
	PG_LWGEOM *geom;
	char *gml;
	text *result;
	int len;
	int version;
	char *srs;
	int SRID;


    /* Get the version */
    version = PG_GETARG_INT32(0);
	if ( version != 2 && version != 3 )
	{
		elog(ERROR, "Only GML 2 and GML 3 are supported");
		PG_RETURN_NULL();
	}

    /* Get the geometry */
	if ( PG_ARGISNULL(1) ) PG_RETURN_NULL();
	geom = (PG_LWGEOM *)PG_DETOAST_DATUM(PG_GETARG_DATUM(1));

	/* Get precision  */
	precision = PG_GETARG_INT32(2);
	if ( precision < 1 || precision > 15 )
	{
		elog(ERROR, "Precision out of range 1..15");
		PG_RETURN_NULL();
	}
	
	SRID = lwgeom_getsrid(SERIALIZED_FORM(geom));
	if ( SRID != -1 ) srs = getSRSbySRID(SRID);
	else srs = NULL;

	/*elog(NOTICE, "srs=%s", srs); */

	if (version == 2)
	  gml = geometry_to_gml2(SERIALIZED_FORM(geom), srs);
	else
	  gml = geometry_to_gml3(SERIALIZED_FORM(geom), srs);
	
	PG_FREE_IF_COPY(geom, 1);

	len = strlen(gml) + VARHDRSZ;

	result = palloc(len);
	SET_VARSIZE(result, len);

	memcpy(VARDATA(result), gml, len-VARHDRSZ);

	pfree(gml);

	PG_RETURN_POINTER(result);
}



/*
 * VERSION GML 2 
 */

/* takes a GEOMETRY and returns a GML representation */
char *
geometry_to_gml2(uchar *geom, char *srs)
{
	int type;
	LWPOINT *point;
	LWLINE *line;
	LWPOLY *poly;
	LWGEOM_INSPECTED *inspected;

	type = lwgeom_getType(geom[0]);

	switch (type)
	{

		case POINTTYPE:
			point = lwpoint_deserialize(geom);
			return asgml2_point(point, srs);

		case LINETYPE:
			line = lwline_deserialize(geom);
			return asgml2_line(line, srs);

		case POLYGONTYPE:
			poly = lwpoly_deserialize(geom);
			return asgml2_poly(poly, srs);

		case MULTIPOINTTYPE:
		case MULTILINETYPE:
		case MULTIPOLYGONTYPE:	
		case COLLECTIONTYPE:
			inspected = lwgeom_inspect(geom);
			return asgml2_inspected(inspected, srs);

		default:
                        lwerror("geometry_to_gml2: '%s' geometry type not supported", lwgeom_typename(type));
                        return NULL;
	}
}

static size_t
asgml2_point_size(LWPOINT *point, char *srs)
{
	int size;
	size = pointArray_GMLsize(point->point);
	size += sizeof("<gml:point><gml:coordinates>/") * 2;
	if ( srs ) size += strlen(srs) + sizeof(" srsName=..");
	return size;
}

static size_t
asgml2_point_buf(LWPOINT *point, char *srs, char *output)
{
	char *ptr = output;

	if ( srs ) {
		ptr += sprintf(ptr, "<gml:Point srsName=\"%s\">", srs);
	} else {
		ptr += sprintf(ptr, "<gml:Point>");
	}
	ptr += sprintf(ptr, "<gml:coordinates>");
	ptr += pointArray_toGML2(point->point, ptr);
	ptr += sprintf(ptr, "</gml:coordinates></gml:Point>");

	return (ptr-output);
}

static char *
asgml2_point(LWPOINT *point, char *srs)
{
	char *output;
	int size;
	
	size = asgml2_point_size(point, srs);
	output = palloc(size);
	asgml2_point_buf(point, srs, output);
	return output;
}

static size_t
asgml2_line_size(LWLINE *line, char *srs)
{
	int size;
	size = pointArray_GMLsize(line->points);
	size += sizeof("<gml:linestring><gml:coordinates>/") * 2;
	if ( srs ) size += strlen(srs) + sizeof(" srsName=..");
	return size;
}

static size_t
asgml2_line_buf(LWLINE *line, char *srs, char *output)
{
	char *ptr=output;

	if ( srs ) {
		ptr += sprintf(ptr, "<gml:LineString srsName=\"%s\">", srs);
	} else {
		ptr += sprintf(ptr, "<gml:LineString>");
	}
	ptr += sprintf(ptr, "<gml:coordinates>");
	ptr += pointArray_toGML2(line->points, ptr);
	ptr += sprintf(ptr, "</gml:coordinates></gml:LineString>");

	return (ptr-output);
}

static char *
asgml2_line(LWLINE *line, char *srs)
{
	char *output;
	int size;

	size = asgml2_line_size(line, srs);
	output = palloc(size);
	asgml2_line_buf(line, srs, output);
	return output;
}

static size_t
asgml2_poly_size(LWPOLY *poly, char *srs)
{
	size_t size;
	int i;

	size = sizeof("<gml:polygon></gml:polygon>");
	size += sizeof("<gml:outerboundaryis><gml:linearring><gml:coordinates>/") * 2;
	size += sizeof("<gml:innerboundaryis><gml:linearring><gml:coordinates>/") * 2 *
		poly->nrings;
	if ( srs ) size += strlen(srs) + sizeof(" srsName=..");

	for (i=0; i<poly->nrings; i++)
		size += pointArray_GMLsize(poly->rings[i]);

	return size;
}

static size_t
asgml2_poly_buf(LWPOLY *poly, char *srs, char *output)
{
	int i;
	char *ptr=output;

	if ( srs ) {
 		ptr += sprintf(ptr, "<gml:Polygon srsName=\"%s\">", srs);
	} else {
 		ptr += sprintf(ptr, "<gml:Polygon>");
	}
 	ptr += sprintf(ptr, "<gml:outerBoundaryIs><gml:LinearRing><gml:coordinates>");
	ptr += pointArray_toGML2(poly->rings[0], ptr);
 	ptr += sprintf(ptr, "</gml:coordinates></gml:LinearRing></gml:outerBoundaryIs>");
	for (i=1; i<poly->nrings; i++)
	{
 		ptr += sprintf(ptr, "<gml:innerBoundaryIs><gml:LinearRing><gml:coordinates>");
		ptr += pointArray_toGML2(poly->rings[i], ptr);
 		ptr += sprintf(ptr, "</gml:coordinates></gml:LinearRing></gml:innerBoundaryIs>");
	}
	ptr += sprintf(ptr, "</gml:Polygon>");

	return (ptr-output);
}

static char *
asgml2_poly(LWPOLY *poly, char *srs)
{
	char *output;
	int size;

	size = asgml2_poly_size(poly, srs);
	output = palloc(size);
	asgml2_poly_buf(poly, srs, output);
	return output;
}

/*
 * Compute max size required for GML version of this 
 * inspected geometry. Will recurse when needed.
 * Don't call this with single-geoms inspected.
 */
static size_t
asgml2_inspected_size(LWGEOM_INSPECTED *insp, char *srs)
{
	int i;
	size_t size;

	/* the longest possible multi version */
	size = sizeof("<gml:MultiLineString></gml:MultiLineString>");

	if ( srs ) size += strlen(srs) + sizeof(" srsName=..");

	for (i=0; i<insp->ngeometries; i++)
	{
		LWPOINT *point;
		LWLINE *line;
		LWPOLY *poly;
		LWGEOM_INSPECTED *subinsp;
		uchar *subgeom;

		if ((point=lwgeom_getpoint_inspected(insp, i)))
		{
			size += sizeof("<gml:pointMember>/") * 2;
			size += asgml2_point_size(point, 0);
			pfree_point(point);
		}
		else if ((line=lwgeom_getline_inspected(insp, i)))
		{
			size += sizeof("<gml:lineStringMember>/") * 2;
			size += asgml2_line_size(line, 0);
			pfree_line(line);
		}
		else if ((poly=lwgeom_getpoly_inspected(insp, i)))
		{
			size += sizeof("<gml:polygonMember>/") * 2;
			size += asgml2_poly_size(poly, 0);
			pfree_polygon(poly);
		}
		else
		{
			subgeom = lwgeom_getsubgeometry_inspected(insp, i);
			subinsp = lwgeom_inspect(subgeom);
			size += asgml2_inspected_size(subinsp, 0);
			pfree_inspected(subinsp);
		}
	}

	return size;
}

/*
 * Don't call this with single-geoms inspected!
 */
static size_t
asgml2_inspected_buf(LWGEOM_INSPECTED *insp, char *srs, char *output)
{
	int type = lwgeom_getType(insp->serialized_form[0]);
	char *ptr, *gmltype;
	int i;

	ptr = output;

	if (type == MULTIPOINTTYPE) gmltype = "MultiPoint";
	else if (type == MULTILINETYPE) gmltype = "MultiLineString";
	else if (type == MULTIPOLYGONTYPE) gmltype = "MultiPolygon";
	else gmltype = "MultiGeometry";

	/* Open outmost tag */
	if ( srs ) {
		ptr += sprintf(ptr, "<gml:%s srsName=\"%s\">", gmltype, srs);
	} else {
		ptr += sprintf(ptr, "<gml:%s>", gmltype);
	}

	for (i=0; i<insp->ngeometries; i++)
	{
		LWPOINT *point;
		LWLINE *line;
		LWPOLY *poly;
		LWGEOM_INSPECTED *subinsp;
		uchar *subgeom;

		if ((point=lwgeom_getpoint_inspected(insp, i)))
		{
		  ptr += sprintf(ptr, "<gml:pointMember>");
			ptr += asgml2_point_buf(point, 0, ptr);
			pfree_point(point);
			ptr += sprintf(ptr, "</gml:pointMember>");
		}
		else if ((line=lwgeom_getline_inspected(insp, i)))
		{
		  ptr += sprintf(ptr, "<gml:lineStringMember>");
			ptr += asgml2_line_buf(line, 0, ptr);
			pfree_line(line);
			ptr += sprintf(ptr, "</gml:lineStringMember>");
		}
		else if ((poly=lwgeom_getpoly_inspected(insp, i)))
		{
		  ptr += sprintf(ptr, "<gml:polygonMember>");
			ptr += asgml2_poly_buf(poly, 0, ptr);
			pfree_polygon(poly);
			ptr += sprintf(ptr, "</gml:polygonMember>");
		}
		else
		{
			subgeom = lwgeom_getsubgeometry_inspected(insp, i);
			subinsp = lwgeom_inspect(subgeom);
			ptr += asgml2_inspected_buf(subinsp, 0, ptr);
			pfree_inspected(subinsp);
		}
	}

	/* Close outmost tag */
	ptr += sprintf(ptr, "</gml:%s>", gmltype);

	return (ptr-output);
}

/*
 * Don't call this with single-geoms inspected!
 */
static char *
asgml2_inspected(LWGEOM_INSPECTED *insp, char *srs)
{
	char *gml;
	size_t size;

	size = asgml2_inspected_size(insp, srs);
	gml = palloc(size);
	asgml2_inspected_buf(insp, srs, gml);
	return gml;
}

static size_t
pointArray_toGML2(POINTARRAY *pa, char *output)
{
	int i;
	char *ptr;

	ptr = output;

	if ( ! TYPE_HASZ(pa->dims) )
	{
		for (i=0; i<pa->npoints; i++)
		{
			POINT2D pt;
			getPoint2d_p(pa, i, &pt);
			if ( i ) ptr += sprintf(ptr, " ");
			ptr += sprintf(ptr, "%.*g,%.*g",
				precision, pt.x,
				precision, pt.y);
		}
	}
	else 
	{
		for (i=0; i<pa->npoints; i++)
		{
			POINT4D pt;
			getPoint4d_p(pa, i, &pt);
			if ( i ) ptr += sprintf(ptr, " ");
			ptr += sprintf(ptr, "%.*g,%.*g,%.*g",
				precision, pt.x,
				precision, pt.y,
				precision, pt.z);
		}
	}

	return ptr-output;
}


/*
 * VERSION GML 3.1.1 
 */


/* takes a GEOMETRY and returns a GML representation */
char *
geometry_to_gml3(uchar *geom, char *srs)
{
	int type;
	LWPOINT *point;
	LWLINE *line;
	LWPOLY *poly;
	LWGEOM_INSPECTED *inspected;

	type = lwgeom_getType(geom[0]);

	switch (type)
	{

		case POINTTYPE:
			point = lwpoint_deserialize(geom);
			return asgml3_point(point, srs);

		case LINETYPE:
			line = lwline_deserialize(geom);
			return asgml3_line(line, srs);

		case POLYGONTYPE:
			poly = lwpoly_deserialize(geom);
			return asgml3_poly(poly, srs);

		default:
			inspected = lwgeom_inspect(geom);
			return asgml3_inspected(inspected, srs);

	}
}

static size_t
asgml3_point_size(LWPOINT *point, char *srs)
{
	int size;
	size = pointArray_GMLsize(point->point);
	size += sizeof("<gml:point><gml:pos>/") * 2;
	if ( srs ) size += strlen(srs) + sizeof(" srsName=..");
	return size;
}

static size_t
asgml3_point_buf(LWPOINT *point, char *srs, char *output)
{
	char *ptr = output;

	if ( srs ) {
		ptr += sprintf(ptr, "<gml:Point srsName=\"%s\">", srs);
	} else {
		ptr += sprintf(ptr, "<gml:Point>");
	}
	ptr += sprintf(ptr, "<gml:pos>");
	ptr += pointArray_toGML3(point->point, ptr);
	ptr += sprintf(ptr, "</gml:pos></gml:Point>");

	return (ptr-output);
}

static char *
asgml3_point(LWPOINT *point, char *srs)
{
	char *output;
	int size;
	
	size = asgml3_point_size(point, srs);
	output = palloc(size);
	asgml3_point_buf(point, srs, output);
	return output;
}


static size_t
asgml3_line_size(LWLINE *line, char *srs)
{
	int size;
	size = pointArray_GMLsize(line->points);
	size += sizeof("<gml:Curve><gml:segments><gml:LineStringSegment><gml:posList>/") * 2;
	if ( srs ) size += strlen(srs) + sizeof(" srsName=..");
	return size;
}

static size_t
asgml3_line_buf(LWLINE *line, char *srs, char *output)
{
	char *ptr=output;

	if ( srs ) {
		ptr += sprintf(ptr, "<gml:Curve srsName=\"%s\">", srs);
	} else {
		ptr += sprintf(ptr, "<gml:Curve>");
	}
	ptr += sprintf(ptr, "<gml:segments>");
	ptr += sprintf(ptr, "<gml:LineStringSegment>");
	ptr += sprintf(ptr, "<gml:posList>");
	ptr += pointArray_toGML3(line->points, ptr);
	ptr += sprintf(ptr, "</gml:posList></gml:LineStringSegment>");
	ptr += sprintf(ptr, "</gml:segments>");
	ptr += sprintf(ptr, "</gml:Curve>");

	return (ptr-output);
}

static char *
asgml3_line(LWLINE *line, char *srs)
{
	char *output;
	int size;

	size = asgml3_line_size(line, srs);
	output = palloc(size);
	asgml3_line_buf(line, srs, output);
	return output;
}


static size_t
asgml3_poly_size(LWPOLY *poly, char *srs)
{
	size_t size;
	int i;

	size = sizeof("<gml:Polygon>");

	size += sizeof("<gml:exterior><gml:LinearRing><gml:posList>");
	size += sizeof("</gml:posList></gml:LinearRing></gml:exterior>");

	size += sizeof("<gml:interior><gml:LinearRing><gml:posList>") * (poly->nrings - 1);
	size += sizeof("</gml:posList></gml:LinearRing></gml:interior>") * (poly->nrings - 1);

	size += sizeof("</gml:Polygon>");

	if ( srs ) size += strlen(srs) + sizeof(" srsName=..");

	for (i=0; i<poly->nrings; i++)
		size += pointArray_GMLsize(poly->rings[i]);

	return size;
}

static size_t
asgml3_poly_buf(LWPOLY *poly, char *srs, char *output)
{
	int i;
	char *ptr=output;

	if ( srs ) {
 		ptr += sprintf(ptr, "<gml:Polygon srsName=\"%s\">", srs);
	} else {
 		ptr += sprintf(ptr, "<gml:Polygon>");
	}
 	ptr += sprintf(ptr, "<gml:exterior><gml:LinearRing><gml:posList>");
	ptr += pointArray_toGML3(poly->rings[0], ptr);
 	ptr += sprintf(ptr, "</gml:posList></gml:LinearRing></gml:exterior>");
	for (i=1; i<poly->nrings; i++)
	{
 		ptr += sprintf(ptr, "<gml:interior><gml:LinearRing><gml:posList>");
		ptr += pointArray_toGML3(poly->rings[i], ptr);
 		ptr += sprintf(ptr, "</gml:posList></gml:LinearRing></gml:interior>");
	}
	ptr += sprintf(ptr, "</gml:Polygon>");

	return (ptr-output);
}

static char *
asgml3_poly(LWPOLY *poly, char *srs)
{
	char *output;
	int size;

	size = asgml3_poly_size(poly, srs);
	output = palloc(size);
	asgml3_poly_buf(poly, srs, output);
	return output;
}

/*
 * Compute max size required for GML version of this 
 * inspected geometry. Will recurse when needed.
 * Don't call this with single-geoms inspected.
 */
static size_t
asgml3_inspected_size(LWGEOM_INSPECTED *insp, char *srs)
{
	int i;
	size_t size;

	/* the longest possible multi version */
	size = sizeof("<gml:MultiLineString></gml:MultiLineString>");

	if ( srs ) size += strlen(srs) + sizeof(" srsName=..");

	for (i=0; i<insp->ngeometries; i++)
	{
		LWPOINT *point;
		LWLINE *line;
		LWPOLY *poly;
		LWGEOM_INSPECTED *subinsp;
		uchar *subgeom;

		if ((point=lwgeom_getpoint_inspected(insp, i)))
		{
			size += sizeof("<gml:pointMember>/") * 2;
			size += asgml3_point_size(point, 0);
			pfree_point(point);
		}
		else if ((line=lwgeom_getline_inspected(insp, i)))
		{
			size += sizeof("<gml:curveMember>/") * 2;
			size += asgml3_line_size(line, 0);
			pfree_line(line);
		}
		else if ((poly=lwgeom_getpoly_inspected(insp, i)))
		{
			size += sizeof("<gml:surfaceMember>/") * 2;
			size += asgml3_poly_size(poly, 0);
			pfree_polygon(poly);
		}
		else
		{
			subgeom = lwgeom_getsubgeometry_inspected(insp, i);
			subinsp = lwgeom_inspect(subgeom);
			size += asgml3_inspected_size(subinsp, 0);
			pfree_inspected(subinsp);
		}
	}

	return size;
}

/*
 * Don't call this with single-geoms inspected!
 */
static size_t
asgml3_inspected_buf(LWGEOM_INSPECTED *insp, char *srs, char *output)
{
	int type = lwgeom_getType(insp->serialized_form[0]);
	char *ptr, *gmltype;
	int i;

	ptr = output;

	if (type == MULTIPOINTTYPE) gmltype = "MultiPoint";
	else if (type == MULTILINETYPE) gmltype = "MultiCurve";
	else if (type == MULTIPOLYGONTYPE) gmltype = "MultiSurface";
	else gmltype = "MultiGeometry";

	/* Open outmost tag */
	if ( srs ) {
		ptr += sprintf(ptr, "<gml:%s srsName=\"%s\">", gmltype, srs);
	} else {
		ptr += sprintf(ptr, "<gml:%s>", gmltype);
	}

	for (i=0; i<insp->ngeometries; i++)
	{
		LWPOINT *point;
		LWLINE *line;
		LWPOLY *poly;
		LWGEOM_INSPECTED *subinsp;
		uchar *subgeom;

		if ((point=lwgeom_getpoint_inspected(insp, i)))
		{
		  ptr += sprintf(ptr, "<gml:pointMember>");
			ptr += asgml3_point_buf(point, 0, ptr);
			pfree_point(point);
			ptr += sprintf(ptr, "</gml:pointMember>");
		}
		else if ((line=lwgeom_getline_inspected(insp, i)))
		{
		  ptr += sprintf(ptr, "<gml:curveMember>");
			ptr += asgml3_line_buf(line, 0, ptr);
			pfree_line(line);
			ptr += sprintf(ptr, "</gml:curveMember>");
		}
		else if ((poly=lwgeom_getpoly_inspected(insp, i)))
		{
		  ptr += sprintf(ptr, "<gml:surfaceMember>");
			ptr += asgml3_poly_buf(poly, 0, ptr);
			pfree_polygon(poly);
			ptr += sprintf(ptr, "</gml:surfaceMember>");
		}
		else
		{
			subgeom = lwgeom_getsubgeometry_inspected(insp, i);
			subinsp = lwgeom_inspect(subgeom);
			ptr += asgml3_inspected_buf(subinsp, 0, ptr);
			pfree_inspected(subinsp);
		}
	}

	/* Close outmost tag */
	ptr += sprintf(ptr, "</gml:%s>", gmltype);

	return (ptr-output);
}

/*
 * Don't call this with single-geoms inspected!
 */
static char *
asgml3_inspected(LWGEOM_INSPECTED *insp, char *srs)
{
	char *gml;
	size_t size;

	size = asgml3_inspected_size(insp, srs);
	gml = palloc(size);
	asgml3_inspected_buf(insp, srs, gml);
	return gml;
}

/* In GML3, inside <posList> or <pos>, coordinates are separated by a space separator*/
static size_t
pointArray_toGML3(POINTARRAY *pa, char *output)
{
	int i;
	char *ptr;

	ptr = output;

	if ( ! TYPE_HASZ(pa->dims) )
	{
		for (i=0; i<pa->npoints; i++)
		{
			POINT2D pt;
			getPoint2d_p(pa, i, &pt);
			if ( i ) ptr += sprintf(ptr, " ");
			ptr += sprintf(ptr, "%.*g %.*g",
				precision, pt.x,
				precision, pt.y);
		}
	}
	else 
	{
		for (i=0; i<pa->npoints; i++)
		{
			POINT4D pt;
			getPoint4d_p(pa, i, &pt);
			if ( i ) ptr += sprintf(ptr, " ");
			ptr += sprintf(ptr, "%.*g %.*g,%.*g",
				precision, pt.x,
				precision, pt.y,
				precision, pt.z);
		}
	}

	return ptr-output;
}



/*
 * Common GML routines 
 */

static char *
getSRSbySRID(int SRID)
{
	char query[128];
	char *srs, *srscopy;
	int size, err;

	/* connect to SPI */
	if (SPI_OK_CONNECT != SPI_connect ()) {
		elog(NOTICE, "getSRSbySRID: could not connect to SPI manager");
		SPI_finish();
		return NULL;
	}

	/* write query */
	sprintf(query, "SELECT textcat(auth_name, textcat(':', auth_srid::text)) \
		FROM spatial_ref_sys WHERE srid = '%d'", SRID);
#ifdef PGIS_DEBUG
	elog(NOTICE, "Query: %s", query);
#endif

	/* execute query */
	err = SPI_exec(query, 1);
	if ( err < 0 ) {
		elog(NOTICE, "getSRSbySRID: error executing query %d", err);
		SPI_finish();
		return NULL;
	}

	/* no entry in spatial_ref_sys */
	if (SPI_processed <= 0) {
		/*elog(NOTICE, "getSRSbySRID: no record for SRID %d", SRID); */
		SPI_finish();
		return NULL;
	}

	/* get result  */
	srs = SPI_getvalue(SPI_tuptable->vals[0],
		SPI_tuptable->tupdesc, 1);
	
	/* NULL result */
	if ( ! srs ) {
		/*elog(NOTICE, "getSRSbySRID: null result"); */
		SPI_finish();
		return NULL;
	}

	/* copy result to upper executor context */
	size = strlen(srs)+1;
	srscopy = SPI_palloc(size);
	memcpy(srscopy, srs, size);

	/* disconnect from SPI */
	SPI_finish();

	return srscopy;
}

/*
 * Returns maximum size of rendered pointarray in bytes.
 */
static size_t
pointArray_GMLsize(POINTARRAY *pa)
{
	return TYPE_NDIMS(pa->dims) * pa->npoints * (SHOW_DIGS+(TYPE_NDIMS(pa->dims)-1));
}


/**********************************************************************
 * $Log$
 * Revision 1.13  2006/01/09 15:55:55  strk
 * ISO C90 comments (finished in lwgeom/)
 *
 * Revision 1.12  2005/12/30 18:14:53  strk
 * Fixed all signedness warnings
 *
 * Revision 1.11  2005/11/18 10:16:21  strk
 * Removed casts on lwalloc return.
 * Used varlena macros when appropriate.
 *
 * Revision 1.10  2005/02/10 17:41:55  strk
 * Dropped getbox2d_internal().
 * Removed all castings of getPoint() output, which has been renamed
 * to getPoint_internal() and commented about danger of using it.
 * Changed SERIALIZED_FORM() macro to use VARDATA() macro.
 * All this changes are aimed at taking into account memory alignment
 * constraints which might be the cause of recent crash bug reports.
 *
 * Revision 1.9  2005/02/07 13:21:10  strk
 * Replaced DEBUG* macros with PGIS_DEBUG*, to avoid clashes with postgresql DEBUG
 *
 * Revision 1.8  2004/11/19 17:29:09  strk
 * precision made of type signed int (for %.*d correct use).
 *
 * Revision 1.7  2004/10/28 16:23:17  strk
 * More cleanups.
 *
 * Revision 1.6  2004/10/28 16:10:25  strk
 * Fixed a bug in LWGEOM_asGML.
 *
 * Revision 1.5  2004/10/05 16:28:34  strk
 * Added ZM dimensions flags knowledge.
 *
 * Revision 1.4  2004/09/29 10:50:30  strk
 * Big layout change.
 * lwgeom.h is public API
 * liblwgeom.h is private header
 * lwgeom_pg.h is for PG-links
 * lw<type>.c contains type-specific functions
 *
 * Revision 1.3  2004/09/29 06:31:42  strk
 * Changed LWGEOM to PG_LWGEOM.
 * Changed LWGEOM_construct to PG_LWGEOM_construct.
 *
 * Revision 1.2  2004/09/23 15:09:07  strk
 * Modified GML output as suggested by Martin Daly.
 *
 * Revision 1.1  2004/09/23 11:12:47  strk
 * Initial GML output routines.
 *
 **********************************************************************/
