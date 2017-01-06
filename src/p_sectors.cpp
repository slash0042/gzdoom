// Emacs style mode select	 -*- C++ -*- 
//-----------------------------------------------------------------------------
//
// $Id:$
//
// Copyright (C) 1993-1996 by id Software, Inc.
//
// This source is available for distribution and/or modification
// only under the terms of the DOOM Source Code License as
// published by id Software. All rights reserved.
//
// The source is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// FITNESS FOR A PARTICULAR PURPOSE. See the DOOM Source Code License
// for more details.
//
// $Log:$
//
// DESCRIPTION:
//		Sector utility functions.
//
//-----------------------------------------------------------------------------

#include "p_spec.h"
#include "p_lnspec.h"
#include "c_cvars.h"
#include "doomstat.h"
#include "g_level.h"
#include "nodebuild.h"
#include "p_terrain.h"
#include "po_man.h"
#include "serializer.h"
#include "r_utility.h"
#include "a_sharedglobal.h"
#include "p_local.h"
#include "r_sky.h"
#include "r_data/colormaps.h"


// [RH]
// P_NextSpecialSector()
//
// Returns the next special sector attached to this sector
// with a certain special.
sector_t *sector_t::NextSpecialSector (int type, sector_t *nogood) const
{
	sector_t *tsec;
	for (auto ln : Lines)
	{
		if (NULL != (tsec = getNextSector (ln, this)) &&
			tsec != nogood &&
			tsec->special == type)
		{
			return tsec;
		}
	}
	return NULL;
}

//
// P_FindLowestFloorSurrounding()
// FIND LOWEST FLOOR HEIGHT IN SURROUNDING SECTORS
//
double sector_t::FindLowestFloorSurrounding (vertex_t **v) const
{
	sector_t *other;
	double floor;
	double ofloor;
	vertex_t *spot;

	if (Lines.Size() == 0) return GetPlaneTexZ(sector_t::floor);

	spot = Lines[0]->v1;
	floor = floorplane.ZatPoint(spot);

	for (auto check : Lines)
	{
		if (NULL != (other = getNextSector (check, this)))
		{
			ofloor = other->floorplane.ZatPoint (check->v1);
			if (ofloor < floor && ofloor < floorplane.ZatPoint (check->v1))
			{
				floor = ofloor;
				spot = check->v1;
			}
			ofloor = other->floorplane.ZatPoint (check->v2);
			if (ofloor < floor && ofloor < floorplane.ZatPoint (check->v2))
			{
				floor = ofloor;
				spot = check->v2;
			}
		}
	}
	if (v != NULL)
		*v = spot;
	return floor;
}



//
// P_FindHighestFloorSurrounding()
// FIND HIGHEST FLOOR HEIGHT IN SURROUNDING SECTORS
//
double sector_t::FindHighestFloorSurrounding (vertex_t **v) const
{
	sector_t *other;
	double floor;
	double ofloor;
	vertex_t *spot;

	if (Lines.Size() == 0) return GetPlaneTexZ(sector_t::floor);

	spot = Lines[0]->v1;
	floor = -FLT_MAX;

	for (auto check : Lines)
	{
		if (NULL != (other = getNextSector (check, this)))
		{
			ofloor = other->floorplane.ZatPoint (check->v1);
			if (ofloor > floor)
			{
				floor = ofloor;
				spot = check->v1;
			}
			ofloor = other->floorplane.ZatPoint (check->v2);
			if (ofloor > floor)
			{
				floor = ofloor;
				spot = check->v2;
			}
		}
	}
	if (v != NULL)
		*v = spot;
	return floor;
}



//
// P_FindNextHighestFloor()
//
// Passed a sector and a floor height, returns the fixed point value
// of the smallest floor height in a surrounding sector larger than
// the floor height passed. If no such height exists the floorheight
// passed is returned.
//
// Rewritten by Lee Killough to avoid fixed array and to be faster
//
double sector_t::FindNextHighestFloor (vertex_t **v) const
{
	double height;
	double heightdiff;
	double ofloor, floor;
	sector_t *other;
	vertex_t *spot;

	if (Lines.Size() == 0) return GetPlaneTexZ(sector_t::floor);

	spot = Lines[0]->v1;
	height = floorplane.ZatPoint(spot);
	heightdiff = FLT_MAX;

	for (auto check : Lines)
	{
		if (NULL != (other = getNextSector (check, this)))
		{
			ofloor = other->floorplane.ZatPoint (check->v1);
			floor = floorplane.ZatPoint (check->v1);
			if (ofloor > floor && ofloor - floor < heightdiff && !IsLinked(other, false))
			{
				heightdiff = ofloor - floor;
				height = ofloor;
				spot = check->v1;
			}
			ofloor = other->floorplane.ZatPoint (check->v2);
			floor = floorplane.ZatPoint (check->v2);
			if (ofloor > floor && ofloor - floor < heightdiff && !IsLinked(other, false))
			{
				heightdiff = ofloor - floor;
				height = ofloor;
				spot = check->v2;
			}
		}
	}
	if (v != NULL)
		*v = spot;
	return height;
}


//
// P_FindNextLowestFloor()
//
// Passed a sector and a floor height, returns the fixed point value
// of the largest floor height in a surrounding sector smaller than
// the floor height passed. If no such height exists the floorheight
// passed is returned.
//
// jff 02/03/98 Twiddled Lee's P_FindNextHighestFloor to make this
//
double sector_t::FindNextLowestFloor (vertex_t **v) const
{
	double height;
	double heightdiff;
	double ofloor, floor;
	sector_t *other;
	vertex_t *spot;

	if (Lines.Size() == 0) return GetPlaneTexZ(sector_t::floor);

	spot = Lines[0]->v1;
	height = floorplane.ZatPoint (spot);
	heightdiff = FLT_MAX;

	for (auto check : Lines)
	{
		if (NULL != (other = getNextSector (check, this)))
		{
			ofloor = other->floorplane.ZatPoint (check->v1);
			floor = floorplane.ZatPoint (check->v1);
			if (ofloor < floor && floor - ofloor < heightdiff && !IsLinked(other, false))
			{
				heightdiff = floor - ofloor;
				height = ofloor;
				spot = check->v1;
			}
			ofloor = other->floorplane.ZatPoint (check->v2);
			floor = floorplane.ZatPoint(check->v2);
			if (ofloor < floor && floor - ofloor < heightdiff && !IsLinked(other, false))
			{
				heightdiff = floor - ofloor;
				height = ofloor;
				spot = check->v2;
			}
		}
	}
	if (v != NULL)
		*v = spot;
	return height;
}

//
// P_FindNextLowestCeiling()
//
// Passed a sector and a ceiling height, returns the fixed point value
// of the largest ceiling height in a surrounding sector smaller than
// the ceiling height passed. If no such height exists the ceiling height
// passed is returned.
//
// jff 02/03/98 Twiddled Lee's P_FindNextHighestFloor to make this
//
double sector_t::FindNextLowestCeiling (vertex_t **v) const
{
	double height;
	double heightdiff;
	double oceil, ceil;
	sector_t *other;
	vertex_t *spot;

	if (Lines.Size() == 0) return GetPlaneTexZ(sector_t::floor);

	spot = Lines[0]->v1;
	height = ceilingplane.ZatPoint(spot);
	heightdiff = FLT_MAX;

	for (auto check : Lines)
	{
		if (NULL != (other = getNextSector (check, this)))
		{
			oceil = other->ceilingplane.ZatPoint(check->v1);
			ceil = ceilingplane.ZatPoint(check->v1);
			if (oceil < ceil && ceil - oceil < heightdiff && !IsLinked(other, true))
			{
				heightdiff = ceil - oceil;
				height = oceil;
				spot = check->v1;
			}
			oceil = other->ceilingplane.ZatPoint(check->v2);
			ceil = ceilingplane.ZatPoint(check->v2);
			if (oceil < ceil && ceil - oceil < heightdiff && !IsLinked(other, true))
			{
				heightdiff = ceil - oceil;
				height = oceil;
				spot = check->v2;
			}
		}
	}
	if (v != NULL)
		*v = spot;
	return height;
}

//
// P_FindNextHighestCeiling()
//
// Passed a sector and a ceiling height, returns the fixed point value
// of the smallest ceiling height in a surrounding sector larger than
// the ceiling height passed. If no such height exists the ceiling height
// passed is returned.
//
// jff 02/03/98 Twiddled Lee's P_FindNextHighestFloor to make this
//
double sector_t::FindNextHighestCeiling (vertex_t **v) const
{
	double height;
	double heightdiff;
	double oceil, ceil;
	sector_t *other;
	vertex_t *spot;

	if (Lines.Size() == 0) return GetPlaneTexZ(sector_t::ceiling);

	spot = Lines[0]->v1;
	height = ceilingplane.ZatPoint(spot);
	heightdiff = FLT_MAX;

	for (auto check : Lines)
	{
		if (NULL != (other = getNextSector (check, this)))
		{
			oceil = other->ceilingplane.ZatPoint(check->v1);
			ceil = ceilingplane.ZatPoint(check->v1);
			if (oceil > ceil && oceil - ceil < heightdiff && !IsLinked(other, true))
			{
				heightdiff = oceil - ceil;
				height = oceil;
				spot = check->v1;
			}
			oceil = other->ceilingplane.ZatPoint(check->v2);
			ceil = ceilingplane.ZatPoint(check->v2);
			if (oceil > ceil && oceil - ceil < heightdiff && !IsLinked(other, true))
			{
				heightdiff = oceil - ceil;
				height = oceil;
				spot = check->v2;
			}
		}
	}
	if (v != NULL)
		*v = spot;
	return height;
}

//
// FIND LOWEST CEILING IN THE SURROUNDING SECTORS
//
double sector_t::FindLowestCeilingSurrounding (vertex_t **v) const
{
	double height;
	double oceil;
	sector_t *other;
	vertex_t *spot;

	if (Lines.Size() == 0) return GetPlaneTexZ(sector_t::ceiling);

	spot = Lines[0]->v1;
	height = FLT_MAX;

	for (auto check : Lines)
	{
		if (NULL != (other = getNextSector (check, this)))
		{
			oceil = other->ceilingplane.ZatPoint(check->v1);
			if (oceil < height)
			{
				height = oceil;
				spot = check->v1;
			}
			oceil = other->ceilingplane.ZatPoint(check->v2);
			if (oceil < height)
			{
				height = oceil;
				spot = check->v2;
			}
		}
	}
	if (v != NULL)
		*v = spot;
	return height;
}


//
// FIND HIGHEST CEILING IN THE SURROUNDING SECTORS
//
double sector_t::FindHighestCeilingSurrounding (vertex_t **v) const
{
	double height;
	double oceil;
	sector_t *other;
	vertex_t *spot;

	if (Lines.Size() == 0) return GetPlaneTexZ(sector_t::ceiling);

	spot = Lines[0]->v1;
	height = -FLT_MAX;

	for (auto check : Lines)
	{
		if (NULL != (other = getNextSector (check, this)))
		{
			oceil = other->ceilingplane.ZatPoint(check->v1);
			if (oceil > height)
			{
				height = oceil;
				spot = check->v1;
			}
			oceil = other->ceilingplane.ZatPoint(check->v2);
			if (oceil > height)
			{
				height = oceil;
				spot = check->v2;
			}
		}
	}
	if (v != NULL)
		*v = spot;
	return height;
}

//
// P_FindShortestTextureAround()
//
// Passed a sector number, returns the shortest lower texture on a
// linedef bounding the sector.
//
// jff 02/03/98 Add routine to find shortest lower texture
//

static inline void CheckShortestTex (FTextureID texnum, double &minsize)
{
	if (texnum.isValid() || (texnum.isNull() && (i_compatflags & COMPATF_SHORTTEX)))
	{
		FTexture *tex = TexMan[texnum];
		if (tex != NULL)
		{
			double h = tex->GetScaledHeight();
			if (h < minsize)
			{
				minsize = h;
			}
		}
	}
}

double sector_t::FindShortestTextureAround () const
{
	double minsize = FLT_MAX;

	for (auto check : Lines)
	{
		if (check->flags & ML_TWOSIDED)
		{
			CheckShortestTex (check->sidedef[0]->GetTexture(side_t::bottom), minsize);
			CheckShortestTex (check->sidedef[1]->GetTexture(side_t::bottom), minsize);
		}
	}
	return minsize < FLT_MAX ? minsize : TexMan[0]->GetHeight();
}


//
// P_FindShortestUpperAround()
//
// Passed a sector number, returns the shortest upper texture on a
// linedef bounding the sector.
//
// Note: If no upper texture exists MAXINT is returned.
//
// jff 03/20/98 Add routine to find shortest upper texture
//
double sector_t::FindShortestUpperAround () const
{
	double minsize = FLT_MAX;

	for (auto check : Lines)
	{
		if (check->flags & ML_TWOSIDED)
		{
			CheckShortestTex (check->sidedef[0]->GetTexture(side_t::top), minsize);
			CheckShortestTex (check->sidedef[1]->GetTexture(side_t::top), minsize);
		}
	}
	return minsize < FLT_MAX ? minsize : TexMan[0]->GetHeight();
}


//
// P_FindModelFloorSector()
//
// Passed a floor height and a sector number, return a pointer to a
// a sector with that floor height across the lowest numbered two sided
// line surrounding the sector.
//
// Note: If no sector at that height bounds the sector passed, return NULL
//
// jff 02/03/98 Add routine to find numeric model floor
//  around a sector specified by sector number
// jff 3/14/98 change first parameter to plain height to allow call
//  from routine not using floormove_t
//
sector_t *sector_t::FindModelFloorSector (double floordestheight) const
{
	sector_t *sec;

	for (auto check : Lines)
	{
		sec = getNextSector (check, this);
		if (sec != NULL &&
			(sec->floorplane.ZatPoint(check->v1) == floordestheight ||
			 sec->floorplane.ZatPoint(check->v2) == floordestheight))
		{
			return sec;
		}
	}
	return NULL;
}


//
// P_FindModelCeilingSector()
//
// Passed a ceiling height and a sector number, return a pointer to a
// a sector with that ceiling height across the lowest numbered two sided
// line surrounding the sector.
//
// Note: If no sector at that height bounds the sector passed, return NULL
//
// jff 02/03/98 Add routine to find numeric model ceiling
//  around a sector specified by sector number
//  used only from generalized ceiling types
// jff 3/14/98 change first parameter to plain height to allow call
//  from routine not using ceiling_t
//
sector_t *sector_t::FindModelCeilingSector (double floordestheight) const
{
	sector_t *sec;

	for (auto check : Lines)
	{
		sec = getNextSector (check, this);
		if (sec != NULL &&
			(sec->ceilingplane.ZatPoint(check->v1) == floordestheight ||
			 sec->ceilingplane.ZatPoint(check->v2) == floordestheight))
		{
			return sec;
		}
	}
	return NULL;
}

//
// Find minimum light from an adjacent sector
//
int sector_t::FindMinSurroundingLight (int min) const
{
	sector_t*	check;
		
	for (auto line : Lines)
	{
		if (NULL != (check = getNextSector (line, this)) &&
			check->lightlevel < min)
		{
			min = check->lightlevel;
		}
	}
	return min;
}

//
// Find the highest point on the floor of the sector
//
double sector_t::FindHighestFloorPoint (vertex_t **v) const
{
	double height = -FLT_MAX;
	double probeheight;
	vertex_t *spot = NULL;

	if (!floorplane.isSlope())
	{
		if (v != NULL)
		{
			if (Lines.Size() == 0) *v = &vertexes[0];
			else *v = Lines[0]->v1;
		}
		return -floorplane.fD();
	}

	for (auto line : Lines)
	{
		probeheight = floorplane.ZatPoint(line->v1);
		if (probeheight > height)
		{
			height = probeheight;
			spot = line->v1;
		}
		probeheight = floorplane.ZatPoint(line->v2);
		if (probeheight > height)
		{
			height = probeheight;
			spot = line->v2;
		}
	}
	if (v != NULL)
		*v = spot;
	return height;
}

//
// Find the lowest point on the ceiling of the sector
//
double sector_t::FindLowestCeilingPoint (vertex_t **v) const
{
	double height = FLT_MAX;
	double probeheight;
	vertex_t *spot = NULL;

	if (!ceilingplane.isSlope())
	{
		if (v != NULL)
		{
			if (Lines.Size() == 0) *v = &vertexes[0];
			else *v = Lines[0]->v1;
		}
		return ceilingplane.fD();
	}

	for (auto line : Lines)
	{
		probeheight = ceilingplane.ZatPoint(line->v1);
		if (probeheight < height)
		{
			height = probeheight;
			spot = line->v1;
		}
		probeheight = ceilingplane.ZatPoint(line->v2);
		if (probeheight < height)
		{
			height = probeheight;
			spot = line->v2;
		}
	}
	if (v != NULL)
		*v = spot;
	return height;
}


void sector_t::SetColor(int r, int g, int b, int desat)
{
	PalEntry color = PalEntry (r,g,b);
	ColorMap = GetSpecialLights (color, ColorMap->Fade, desat);
	P_RecalculateAttachedLights(this);
}

DEFINE_ACTION_FUNCTION(_Sector, SetColor)
{
	PARAM_SELF_STRUCT_PROLOGUE(sector_t);
	PARAM_COLOR(color);
	PARAM_INT(desat);
	self->ColorMap = GetSpecialLights(color, self->ColorMap->Fade, desat);
	P_RecalculateAttachedLights(self);
	return 0;
}

void sector_t::SetFade(int r, int g, int b)
{
	PalEntry fade = PalEntry (r,g,b);
	ColorMap = GetSpecialLights (ColorMap->Color, fade, ColorMap->Desaturate);
	P_RecalculateAttachedLights(this);
}

DEFINE_ACTION_FUNCTION(_Sector, SetFade)
{
	PARAM_SELF_STRUCT_PROLOGUE(sector_t);
	PARAM_COLOR(fade);
	self->ColorMap = GetSpecialLights(self->ColorMap->Color, fade, self->ColorMap->Desaturate);
	P_RecalculateAttachedLights(self);
	return 0;
}


//===========================================================================
//
// sector_t :: ClosestPoint
//
// Given a point (x,y), returns the point (ox,oy) on the sector's defining
// lines that is nearest to (x,y).
//
//===========================================================================

void sector_t::ClosestPoint(const DVector2 &in, DVector2 &out) const
{
	double x = in.X, y = in.Y;
	double bestdist = HUGE_VAL;
	double bestx = 0, besty = 0;

	for (auto check : Lines)
	{
		vertex_t *v1 = check->v1;
		vertex_t *v2 = check->v2;
		double a = v2->fX() - v1->fX();
		double b = v2->fY() - v1->fY();
		double den = a*a + b*b;
		double ix, iy, dist;

		if (den == 0)
		{ // Line is actually a point!
			ix = v1->fX();
			iy = v1->fY();
		}
		else
		{
			double num = (x - v1->fX()) * a + (y - v1->fY()) * b;
			double u = num / den;
			if (u <= 0)
			{
				ix = v1->fX();
				iy = v1->fY();
			}
			else if (u >= 1)
			{
				ix = v2->fX();
				iy = v2->fY();
			}
			else
			{
				ix = v1->fX() + u * a;
				iy = v1->fY() + u * b;
			}
		}
		a = (ix - x);
		b = (iy - y);
		dist = a*a + b*b;
		if (dist < bestdist)
		{
			bestdist = dist;
			bestx = ix;
			besty = iy;
		}
	}
	out = { bestx, besty };
}


bool sector_t::PlaneMoving(int pos)
{
	if (pos == floor)
		return (floordata != NULL || (planes[floor].Flags & PLANEF_BLOCKED));
	else
		return (ceilingdata != NULL || (planes[ceiling].Flags & PLANEF_BLOCKED));
}


int sector_t::GetFloorLight () const
{
	if (GetFlags(sector_t::floor) & PLANEF_ABSLIGHTING)
	{
		return GetPlaneLight(floor);
	}
	else
	{
		return ClampLight(lightlevel + GetPlaneLight(floor));
	}
}

int sector_t::GetCeilingLight () const
{
	if (GetFlags(ceiling) & PLANEF_ABSLIGHTING)
	{
		return GetPlaneLight(ceiling);
	}
	else
	{
		return ClampLight(lightlevel + GetPlaneLight(ceiling));
	}
}


FSectorPortal *sector_t::ValidatePortal(int which)
{
	FSectorPortal *port = GetPortal(which);
	if (port->mType == PORTS_SKYVIEWPOINT && port->mSkybox == nullptr) return nullptr;				// A skybox without a viewpoint is just a regular sky.
	if (PortalBlocksView(which)) return nullptr;													// disabled or obstructed linked portal.
	if ((port->mFlags & PORTSF_SKYFLATONLY) && GetTexture(which) != skyflatnum) return nullptr;		// Skybox without skyflat texture
	return port;
}


sector_t *sector_t::GetHeightSec() const 
{
	if (heightsec == NULL)
	{
		return NULL;
	}
	if (heightsec->MoreFlags & SECF_IGNOREHEIGHTSEC)
	{
		return NULL;
	}
	if (e && e->XFloor.ffloors.Size())
	{
		// If any of these fake floors render their planes, ignore heightsec.
		for (unsigned i = e->XFloor.ffloors.Size(); i-- > 0; )
		{
			if ((e->XFloor.ffloors[i]->flags & (FF_EXISTS | FF_RENDERPLANES)) == (FF_EXISTS | FF_RENDERPLANES))
			{
				return NULL;
			}
		}
	}
	return heightsec;
}


void sector_t::GetSpecial(secspecial_t *spec)
{
	spec->special = special;
	spec->damageamount = damageamount;
	spec->damagetype = damagetype;
	spec->damageinterval = damageinterval;
	spec->leakydamage = leakydamage;
	spec->Flags = Flags & SECF_SPECIALFLAGS;
}

void sector_t::SetSpecial(const secspecial_t *spec)
{
	special = spec->special;
	damageamount = spec->damageamount;
	damagetype = spec->damagetype;
	damageinterval = spec->damageinterval;
	leakydamage = spec->leakydamage;
	Flags = (Flags & ~SECF_SPECIALFLAGS) | (spec->Flags & SECF_SPECIALFLAGS);
}

void sector_t::TransferSpecial(sector_t *model)
{
	special = model->special;
	damageamount = model->damageamount;
	damagetype = model->damagetype;
	damageinterval = model->damageinterval;
	leakydamage = model->leakydamage;
	Flags = (Flags&~SECF_SPECIALFLAGS) | (model->Flags & SECF_SPECIALFLAGS);
}

int sector_t::GetTerrain(int pos) const
{
	return terrainnum[pos] >= 0 ? terrainnum[pos] : TerrainTypes[GetTexture(pos)];
}

void sector_t::CheckPortalPlane(int plane)
{
	if (GetPortalType(plane) == PORTS_LINKEDPORTAL)
	{
		double portalh = GetPortalPlaneZ(plane);
		double planeh = GetPlaneTexZ(plane);
		int obstructed = PLANEF_OBSTRUCTED * (plane == sector_t::floor ? planeh > portalh : planeh < portalh);
		planes[plane].Flags = (planes[plane].Flags  & ~PLANEF_OBSTRUCTED) | obstructed;
	}
}

//===========================================================================
//
// Finds the highest ceiling at the given position, all portals considered
//
//===========================================================================

double sector_t::HighestCeilingAt(const DVector2 &p, sector_t **resultsec)
{
	sector_t *check = this;
	double planeheight = -FLT_MAX;
	DVector2 pos = p;

	// Continue until we find a blocking portal or a portal below where we actually are.
	while (!check->PortalBlocksMovement(ceiling) && planeheight < check->GetPortalPlaneZ(ceiling))
	{
		pos += check->GetPortalDisplacement(ceiling);
		planeheight = check->GetPortalPlaneZ(ceiling);
		check = P_PointInSector(pos);
	}
	if (resultsec) *resultsec = check;
	return check->ceilingplane.ZatPoint(pos);
}

//===========================================================================
//
// Finds the lowest floor at the given position, all portals considered
//
//===========================================================================

double sector_t::LowestFloorAt(const DVector2 &p, sector_t **resultsec)
{
	sector_t *check = this;
	double planeheight = FLT_MAX;
	DVector2 pos = p;

	// Continue until we find a blocking portal or a portal above where we actually are.
	while (!check->PortalBlocksMovement(floor) && planeheight > check->GetPortalPlaneZ(floor))
	{
		pos += check->GetPortalDisplacement(floor);
		planeheight = check->GetPortalPlaneZ(ceiling);
		check = P_PointInSector(pos);
	}
	if (resultsec) *resultsec = check;
	return check->floorplane.ZatPoint(pos);
}


double sector_t::NextHighestCeilingAt(double x, double y, double bottomz, double topz, int flags, sector_t **resultsec, F3DFloor **resultffloor)
{
	sector_t *sec = this;
	double planeheight = -FLT_MAX;

	while (true)
	{
		// Looking through planes from bottom to top
		double realceil = sec->ceilingplane.ZatPoint(x, y);
		for (int i = sec->e->XFloor.ffloors.Size() - 1; i >= 0; --i)
		{
			F3DFloor *rover = sec->e->XFloor.ffloors[i];
			if (!(rover->flags & FF_SOLID) || !(rover->flags & FF_EXISTS)) continue;

			double ff_bottom = rover->bottom.plane->ZatPoint(x, y);
			double ff_top = rover->top.plane->ZatPoint(x, y);

			double delta1 = bottomz - (ff_bottom + ((ff_top - ff_bottom) / 2));
			double delta2 = topz - (ff_bottom + ((ff_top - ff_bottom) / 2));

			if (ff_bottom < realceil && fabs(delta1) > fabs(delta2))
			{ 
				if (resultsec) *resultsec = sec;
				if (resultffloor) *resultffloor = rover;
				return ff_bottom;
			}
		}
		if ((flags & FFCF_NOPORTALS) || sec->PortalBlocksMovement(ceiling) || planeheight >= sec->GetPortalPlaneZ(ceiling))
		{ // Use sector's ceiling
			if (resultffloor) *resultffloor = NULL;
			if (resultsec) *resultsec = sec;
			return realceil;
		}
		else
		{
			DVector2 pos = sec->GetPortalDisplacement(ceiling);
			x += pos.X;
			y += pos.Y;
			planeheight = sec->GetPortalPlaneZ(ceiling);
			sec = P_PointInSector(x, y);
		}
	}
}

DEFINE_ACTION_FUNCTION(_Sector, NextHighestCeilingAt)
{
	PARAM_SELF_STRUCT_PROLOGUE(sector_t);
	PARAM_FLOAT(x);
	PARAM_FLOAT(y);
	PARAM_FLOAT(bottomz);
	PARAM_FLOAT(topz);
	PARAM_INT_DEF(flags);
	sector_t *resultsec;
	F3DFloor *resultff;
	double resultheight = self->NextHighestCeilingAt(x, y, bottomz, topz, flags, &resultsec, &resultff);

	if (numret > 2)
	{
		ret[2].SetPointer(resultff, ATAG_GENERIC);
		numret = 3;
	}
	if (numret > 1)
	{
		ret[1].SetPointer(resultsec, ATAG_GENERIC);
	}
	if (numret > 0)
	{
		ret[0].SetFloat(resultheight);
	}
	return numret;
}

double sector_t::NextLowestFloorAt(double x, double y, double z, int flags, double steph, sector_t **resultsec, F3DFloor **resultffloor)
{
	sector_t *sec = this;
	double planeheight = FLT_MAX;
	while (true)
	{
		// Looking through planes from top to bottom
		unsigned numff = sec->e->XFloor.ffloors.Size();
		double realfloor = sec->floorplane.ZatPoint(x, y);
		for (unsigned i = 0; i < numff; ++i)
		{
			F3DFloor *ff = sec->e->XFloor.ffloors[i];


			// either with feet above the 3D floor or feet with less than 'stepheight' map units inside
			if ((ff->flags & (FF_EXISTS | FF_SOLID)) == (FF_EXISTS | FF_SOLID))
			{
				double ffz = ff->top.plane->ZatPoint(x, y);
				double ffb = ff->bottom.plane->ZatPoint(x, y);

				if (ffz > realfloor && (z >= ffz || (!(flags & FFCF_3DRESTRICT) && (ffb < z && ffz < z + steph))))
				{ // This floor is beneath our feet.
					if (resultsec) *resultsec = sec;
					if (resultffloor) *resultffloor = ff;
					return ffz;
				}
			}
		}
		if ((flags & FFCF_NOPORTALS) || sec->PortalBlocksMovement(sector_t::floor) || planeheight <= sec->GetPortalPlaneZ(floor))
		{ // Use sector's floor
			if (resultffloor) *resultffloor = NULL;
			if (resultsec) *resultsec = sec;
			return realfloor;
		}
		else
		{
			DVector2 pos = sec->GetPortalDisplacement(floor);
			x += pos.X;
			y += pos.Y;
			planeheight = sec->GetPortalPlaneZ(floor);
			sec = P_PointInSector(x, y);
		}
	}
}

DEFINE_ACTION_FUNCTION(_Sector, NextLowestFloorAt)
{
	PARAM_SELF_STRUCT_PROLOGUE(sector_t);
	PARAM_FLOAT(x);
	PARAM_FLOAT(y);
	PARAM_FLOAT(z);
	PARAM_INT_DEF(flags);
	PARAM_FLOAT_DEF(steph);
	sector_t *resultsec;
	F3DFloor *resultff;
	double resultheight = self->NextLowestFloorAt(x, y, z, flags, steph, &resultsec, &resultff);

	if (numret > 2)
	{
		ret[2].SetPointer(resultff, ATAG_GENERIC);
		numret = 3;
	}
	if (numret > 1)
	{
		ret[1].SetPointer(resultsec, ATAG_GENERIC);
	}
	if (numret > 0)
	{
		ret[0].SetFloat(resultheight);
	}
	return numret;
}


//===========================================================================
//
// 
//
//===========================================================================

 double sector_t::GetFriction(int plane, double *pMoveFac) const
{
	if (Flags & SECF_FRICTION) 
	{ 
		if (pMoveFac) *pMoveFac = movefactor;
		return friction; 
	}
	FTerrainDef *terrain = &Terrains[GetTerrain(plane)];
	if (terrain->Friction != 0)
	{
		if (pMoveFac) *pMoveFac = terrain->MoveFactor;
		return terrain->Friction;
	}
	else
	{
		if (pMoveFac) *pMoveFac = ORIG_FRICTION_FACTOR;
		return ORIG_FRICTION;
	}
}

 //===========================================================================
 //
 // 
 //
 //===========================================================================

 void sector_t::RemoveForceField()
 {
	 for (auto line : Lines)
	 {
		 if (line->backsector != NULL && line->special == ForceField)
		 {
			 line->flags &= ~(ML_BLOCKING | ML_BLOCKEVERYTHING);
			 line->special = 0;
			 line->sidedef[0]->SetTexture(side_t::mid, FNullTextureID());
			 line->sidedef[1]->SetTexture(side_t::mid, FNullTextureID());
		 }
	 }
 }

 DEFINE_ACTION_FUNCTION(_Sector, RemoveForceField)
 {
	 PARAM_SELF_STRUCT_PROLOGUE(sector_t);
	 self->RemoveForceField();
	 return 0;
 }


 DEFINE_ACTION_FUNCTION(_Sector, PointInSector)
 {
	 PARAM_PROLOGUE;
	 PARAM_FLOAT(x);
	 PARAM_FLOAT(y);
	 ACTION_RETURN_POINTER(P_PointInSector(x, y));
 }

//===========================================================================
//
// 
//
//===========================================================================

 FSerializer &Serialize(FSerializer &arc, const char *key, secspecial_t &spec, secspecial_t *def)
 {
	 if (arc.BeginObject(key))
	 {
		 arc("special", spec.special)
			 ("damageamount", spec.damageamount)
			 ("damagetype", spec.damagetype)
			 ("damageinterval", spec.damageinterval)
			 ("leakydamage", spec.leakydamage)
			 ("flags", spec.Flags)
			 .EndObject();
	 }
	return arc;
}


//===========================================================================
//
// 
//
//===========================================================================

bool secplane_t::CopyPlaneIfValid (secplane_t *dest, const secplane_t *opp) const
{
	bool copy = false;

	// If the planes do not have matching slopes, then always copy them
	// because clipping would require creating new sectors.
	if (Normal() != dest->Normal())
	{
		copy = true;
	}
	else if (opp->Normal() != -dest->Normal())
	{
		if (fD() < dest->fD())
		{
			copy = true;
		}
	}
	else if (fD() < dest->fD() && fD() > -opp->fD())
	{
		copy = true;
	}

	if (copy)
	{
		*dest = *this;
	}

	return copy;
}

//==========================================================================
//
// P_AlignFlat
//
//==========================================================================

bool P_AlignFlat (int linenum, int side, int fc)
{
	line_t *line = lines + linenum;
	sector_t *sec = side ? line->backsector : line->frontsector;

	if (!sec)
		return false;

	DAngle angle = line->Delta().Angle();
	DAngle norm = angle - 90;
	double dist = -(norm.Cos() * line->v1->fX() + norm.Sin() * line->v1->fY());

	if (side)
	{
		angle += 180.;
		dist = -dist;
	}

	sec->SetBase(fc, dist, -angle);
	return true;
}

//==========================================================================
//
// P_BuildPolyBSP
//
//==========================================================================
static FNodeBuilder::FLevel PolyNodeLevel;
static FNodeBuilder PolyNodeBuilder(PolyNodeLevel);

void subsector_t::BuildPolyBSP()
{
	assert((BSP == NULL || BSP->bDirty) && "BSP computed more than once");

	// Set up level information for the node builder.
	PolyNodeLevel.Sides = sides;
	PolyNodeLevel.NumSides = numsides;
	PolyNodeLevel.Lines = lines;
	PolyNodeLevel.NumLines = numlines;

	// Feed segs to the nodebuilder and build the nodes.
	PolyNodeBuilder.Clear();
	PolyNodeBuilder.AddSegs(firstline, numlines);
	for (FPolyNode *pn = polys; pn != NULL; pn = pn->pnext)
	{
		PolyNodeBuilder.AddPolySegs(&pn->segs[0], (int)pn->segs.Size());
	}
	PolyNodeBuilder.BuildMini(false);
	if (BSP == NULL)
	{
		BSP = new FMiniBSP;
	}
	PolyNodeBuilder.ExtractMini(BSP);
	for (unsigned int i = 0; i < BSP->Subsectors.Size(); ++i)
	{
		BSP->Subsectors[i].sector = sector;
	}
}

//==========================================================================
//
//
//
//==========================================================================

CUSTOM_CVAR(Int, r_fakecontrast, true, CVAR_ARCHIVE|CVAR_GLOBALCONFIG)
{
	if (self < 0) self = 1;
	else if (self > 2) self = 2;
}

//==========================================================================
//
//
//
//==========================================================================

int side_t::GetLightLevel (bool foggy, int baselight, bool is3dlight, int *pfakecontrast) const
{
	if (!is3dlight && (Flags & WALLF_ABSLIGHTING))
	{
		baselight = Light;
	}

	if (pfakecontrast != NULL)
	{
		*pfakecontrast = 0;
	}

	if (!foggy || level.flags3 & LEVEL3_FORCEFAKECONTRAST) // Don't do relative lighting in foggy sectors
	{
		if (!(Flags & WALLF_NOFAKECONTRAST) && r_fakecontrast != 0)
		{
			DVector2 delta = linedef->Delta();
			int rel;
			if (((level.flags2 & LEVEL2_SMOOTHLIGHTING) || (Flags & WALLF_SMOOTHLIGHTING) || r_fakecontrast == 2) &&
				delta.X != 0)
			{
				rel = xs_RoundToInt // OMG LEE KILLOUGH LIVES! :/
					(
						level.WallHorizLight
						+ fabs(atan(delta.Y / delta.X) / 1.57079)
						* (level.WallVertLight - level.WallHorizLight)
					);
			}
			else
			{
				rel = delta.X == 0 ? level.WallVertLight : 
					  delta.Y == 0 ? level.WallHorizLight : 0;
			}
			if (pfakecontrast != NULL)
			{
				*pfakecontrast = rel;
			}
			else
			{
				baselight += rel;
			}
		}
	}
	if (!is3dlight && !(Flags & WALLF_ABSLIGHTING) && (!foggy || (Flags & WALLF_LIGHT_FOG)))
	{
		baselight += this->Light;
	}
	return baselight;
}


DEFINE_FIELD_X(Sector, sector_t, floorplane)
DEFINE_FIELD_X(Sector, sector_t, ceilingplane)
DEFINE_FIELD_X(Sector, sector_t, ColorMap)
DEFINE_FIELD_X(Sector, sector_t, SoundTarget)
DEFINE_FIELD_X(Sector, sector_t, special)
DEFINE_FIELD_X(Sector, sector_t, lightlevel)
DEFINE_FIELD_X(Sector, sector_t, seqType)
DEFINE_FIELD_X(Sector, sector_t, sky)
DEFINE_FIELD_X(Sector, sector_t, SeqName)
DEFINE_FIELD_X(Sector, sector_t, centerspot)
DEFINE_FIELD_X(Sector, sector_t, validcount)
DEFINE_FIELD_X(Sector, sector_t, thinglist)
DEFINE_FIELD_X(Sector, sector_t, friction)
DEFINE_FIELD_X(Sector, sector_t, movefactor)
DEFINE_FIELD_X(Sector, sector_t, terrainnum)
DEFINE_FIELD_X(Sector, sector_t, floordata)
DEFINE_FIELD_X(Sector, sector_t, ceilingdata)
DEFINE_FIELD_X(Sector, sector_t, lightingdata)
DEFINE_FIELD_X(Sector, sector_t, interpolations)
DEFINE_FIELD_X(Sector, sector_t, soundtraversed)
DEFINE_FIELD_X(Sector, sector_t, stairlock)
DEFINE_FIELD_X(Sector, sector_t, prevsec)
DEFINE_FIELD_X(Sector, sector_t, nextsec)
DEFINE_FIELD_X(Sector, sector_t, Lines)
DEFINE_FIELD_X(Sector, sector_t, heightsec)
DEFINE_FIELD_X(Sector, sector_t, bottommap)
DEFINE_FIELD_X(Sector, sector_t, midmap)
DEFINE_FIELD_X(Sector, sector_t, topmap)
DEFINE_FIELD_X(Sector, sector_t, touching_thinglist)
DEFINE_FIELD_X(Sector, sector_t, sectorportal_thinglist)
DEFINE_FIELD_X(Sector, sector_t, gravity)
DEFINE_FIELD_X(Sector, sector_t, damagetype)
DEFINE_FIELD_X(Sector, sector_t, damageamount)
DEFINE_FIELD_X(Sector, sector_t, damageinterval)
DEFINE_FIELD_X(Sector, sector_t, leakydamage)
DEFINE_FIELD_X(Sector, sector_t, ZoneNumber)
DEFINE_FIELD_X(Sector, sector_t, MoreFlags)
DEFINE_FIELD_X(Sector, sector_t, Flags)
DEFINE_FIELD_X(Sector, sector_t, SecActTarget)
DEFINE_FIELD_X(Sector, sector_t, Portals)
DEFINE_FIELD_X(Sector, sector_t, PortalGroup)
DEFINE_FIELD_X(Sector, sector_t, sectornum)

DEFINE_FIELD_X(Line, line_t, v1)
DEFINE_FIELD_X(Line, line_t, v2)
DEFINE_FIELD_X(Line, line_t, delta)
DEFINE_FIELD_X(Line, line_t, flags)
DEFINE_FIELD_X(Line, line_t, activation)
DEFINE_FIELD_X(Line, line_t, special)
DEFINE_FIELD_X(Line, line_t, args)
DEFINE_FIELD_X(Line, line_t, alpha)
DEFINE_FIELD_X(Line, line_t, sidedef)
DEFINE_FIELD_X(Line, line_t, bbox)
DEFINE_FIELD_X(Line, line_t, frontsector)
DEFINE_FIELD_X(Line, line_t, backsector)
DEFINE_FIELD_X(Line, line_t, validcount)
DEFINE_FIELD_X(Line, line_t, locknumber)
DEFINE_FIELD_X(Line, line_t, portalindex)
