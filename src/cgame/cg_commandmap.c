/*
 * Wolfenstein: Enemy Territory GPL Source Code
 * Copyright (C) 1999-2010 id Software LLC, a ZeniMax Media company.
 *
 * ET: Legacy
 * Copyright (C) 2012-2024 ET:Legacy team <mail@etlegacy.com>
 *
 * This file is part of ET: Legacy - http://www.etlegacy.com
 *
 * ET: Legacy is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * ET: Legacy is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with ET: Legacy. If not, see <http://www.gnu.org/licenses/>.
 *
 * In addition, Wolfenstein: Enemy Territory GPL Source Code is also
 * subject to certain additional terms. You should have received a copy
 * of these additional terms immediately following the terms and conditions
 * of the GNU General Public License which accompanied the source code.
 * If not, please request a copy in writing from id Software at the address below.
 *
 * id Software LLC, c/o ZeniMax Media Inc., Suite 120, Rockville, Maryland 20850 USA.
 */
/**
 * @file cg_commandmap.c
 */

#include "cg_local.h"

static mapEntityData_t mapEntities[MAX_GENTITIES];
static int             mapEntityCount = 0;
static qboolean        expanded       = qfalse;

qboolean ccInitial = qtrue;

#define AUTOMAP_ZOOM                5.159f
#define COMMANDMAP_PLAYER_ICON_SIZE 6
#define AUTOMAP_PLAYER_ICON_SIZE    5
#define CONST_ICON_NORMAL_SIZE      32.f
#define CONST_ICON_EXPANDED_SIZE    48.f
#define CONST_ICON_LANDMINE_SIZE    12.f
#define FLAGSIZE_EXPANDED           48.f
#define FLAGSIZE_NORMAL             32.f
#define FLAG_LEFTFRAC               0.1953125f // 25/128
#define FLAG_TOPFRAC                0.7421875f // 95/128
#define SPAWN_SIZEUPTIME            1000.f
#define MARKSIZE                    32.f

// shoutcaster minimap values
#define AUTOMAP_PLAYER_ICON_SIZE_SC     20
#define CONST_ICON_NORMAL_SIZE_SC       17
#define CONST_ICON_EXPANDED_SIZE_SC     20
#define CONST_ICON_LANDMINE_SIZE_SC     8
#define FLAGSIZE_EXPANDED_SC            98
#define FLAGSIZE_NORMAL_SC              82
#define MARKSIZE_EXPANDED_SC            17
#define MARKSIZE_SC                     20

/**
 * @brief CG_IsShoutcaster
 * @return
 */
static inline qboolean CG_IsShoutcaster(void)
{
	return cgs.clientinfo[cg.clientNum].shoutcaster ? qtrue : qfalse;
}

/**
 * @brief CG_TransformToCommandMapCoord
 * @param[in] coord_x
 * @param[in] coord_y
 */
void CG_TransformToCommandMapCoord(float *coord_x, float *coord_y)
{
	*coord_x = CC_2D_X + ((*coord_x - cg.mapcoordsMins[0]) * cg.mapcoordsScale[0]) * CC_2D_W;
	*coord_y = CC_2D_Y + ((*coord_y - cg.mapcoordsMins[1]) * cg.mapcoordsScale[1]) * CC_2D_H;
}

/**
 * @brief CG_CurLayerForZ
 * @param[in] z
 * @return
 */
int CG_CurLayerForZ(int z)
{
	int curlayer = 0;

	while (curlayer < cgs.ccLayers && z > cgs.ccLayerCeils[curlayer])
	{
		curlayer++;
	}

	if (curlayer == cgs.ccLayers)
	{
		CG_Printf("^3Warning: no valid command map layer for z\n");
		curlayer = 0;
	}

	return curlayer;
}

/**
 * @brief CG_ScissorEntIsCulled
 * @param[in] mEnt
 * @param[in] scissor
 * @return
 */
static qboolean CG_ScissorEntIsCulled(mapEntityData_t *mEnt, mapScissor_t *scissor, vec2_t tolerance)
{
	if (!scissor->circular)
	{
		if (mEnt->automapTransformed[0] < scissor->tl[0]
		    || mEnt->automapTransformed[0] > scissor->br[0]
		    || mEnt->automapTransformed[1] < scissor->tl[1]
		    || mEnt->automapTransformed[1] > scissor->br[1])
		{
			return qtrue;
		}
	}
	else
	{
		float  distSquared;
		vec2_t distVec;

		distVec[0]  = mEnt->automapTransformed[0] - (scissor->tl[0] + (0.5f * (scissor->br[0] - scissor->tl[0])));
		distVec[1]  = mEnt->automapTransformed[1] - (scissor->tl[1] + (0.5f * (scissor->br[1] - scissor->tl[1])));
		distSquared = distVec[0] * distVec[0] + distVec[1] * distVec[1];

		if (distSquared > Square(0.5f * (scissor->br[0] - scissor->tl[0] + tolerance[0])))
		{
			return qtrue;
		}
	}

	return qfalse;
}

/**
 * @brief CG_ScissorPointIsCulled
 * @param[in] vec
 * @param[in] scissor
 * @return
 */
static qboolean CG_ScissorPointIsCulled(vec2_t vec, mapScissor_t *scissor, vec2_t tolerance)
{
	if (!scissor->circular)
	{
		if (vec[0] < scissor->tl[0]
		    || vec[0] > scissor->br[0]
		    || vec[1] < scissor->tl[1]
		    || vec[1] > scissor->br[1])
		{
			return qtrue;
		}
	}
	else
	{
		float  distSquared;
		vec2_t distVec;

		distVec[0]  = vec[0] - (scissor->tl[0] + (0.5f * (scissor->br[0] - scissor->tl[0])));
		distVec[1]  = vec[1] - (scissor->tl[1] + (0.5f * (scissor->br[1] - scissor->tl[1])));
		distSquared = distVec[0] * distVec[0] + distVec[1] * distVec[1];

		if (distSquared > Square(0.5f * (scissor->br[0] - scissor->tl[0] + tolerance[0])))
		{
			return qtrue;
		}
	}

	return qfalse;
}

/**
* @brief CG_GetVoiceChatForCommandMap
* @details Maps default voice chat shader to command map shader.
*          Command map requires different shaders to not draw icons outside of the map.
*
* @param[in,out] voiceChat
*/
static int CG_GetVoiceChatForCommandMap(int voiceChat)
{
	if (voiceChat == cgs.media.voiceChatShader)
	{
		return cgs.media.ccVoiceChatShader;
	}

	if (voiceChat == cgs.media.medicIcon)
	{
		return cgs.media.ccMedicIcon;
	}

	if (voiceChat == cgs.media.ammoIcon)
	{
		return cgs.media.ccAmmoIcon;
	}

	return voiceChat;
}

/**
* @brief CG_PlayerDistanceScaling
* @details Calculates smallest player icon scale based on distance between players
*
* @param[in] player
* @return
*/
static float CG_PlayerDistanceScaling(mapEntityData_t *player)
{
	mapEntityData_t *mEnt, *mEntScale;
	centity_t       *cent, *centScale;
	int             i;
	float           distance, scale, minScale = cg_dynamicIconsMaxScale.value;
	vec3_t          vec1, vec2;

	if (cg_dynamicIconsMinScale.value == cg_dynamicIconsMaxScale.value || cg_dynamicIconsDistance.integer <= 0)
	{
		return minScale;
	}

	mEnt = player;
	cent = &cg_entities[mEnt->data];

	// get up-to-date position if possible so scaling is smooth
	if (mEnt->data == cg.clientNum)
	{
		VectorCopy(cg.predictedPlayerEntity.lerpOrigin, vec1);
	}
	else if (cent->currentValid)
	{
		VectorCopy(cent->lerpOrigin, vec1);
	}
	else
	{
		VectorSet(vec1, mEnt->x, mEnt->y, mEnt->z);
	}

	vec1[2] = 0;

	for (i = 0; i < mapEntityCount; i++)
	{
		mEntScale = &mapEntities[i];

		if (mEntScale->type != ME_PLAYER &&
		    mEntScale->type != ME_PLAYER_DISGUISED &&
		    mEntScale->type != ME_PLAYER_OBJECTIVE &&
		    mEntScale->type != ME_PLAYER_REVIVE)
		{
			continue;
		}

		if (mEnt->data == mEntScale->data)
		{
			continue;
		}

		centScale = &cg_entities[mEntScale->data];

		// get up-to-date position if possible so scaling is smooth
		if (mEntScale->data == cg.clientNum)
		{
			VectorCopy(cg.predictedPlayerEntity.lerpOrigin, vec2);
		}
		else if (centScale->currentValid)
		{
			VectorCopy(centScale->lerpOrigin, vec2);
		}
		else
		{
			VectorSet(vec2, mEntScale->x, mEntScale->y, mEntScale->z);
		}

		vec2[2] = 0;

		// calculate distance and scale
		distance = VectorDistance(vec1, vec2);
		scale    = distance / cg_dynamicIconsDistance.integer;

		if (minScale > scale)
		{
			if (scale < cg_dynamicIconsMinScale.value)
			{
				// found the min scale value
				minScale = cg_dynamicIconsMinScale.value;
				break;
			}
			minScale = scale;
		}
	}

	return minScale;
}

/**
 * @brief Calculate the scaled (zoomed) yet unshifted coordinate for
 * each map entity within the automap
 */
void CG_TransformAutomapEntity(void)
{
	mapEntityData_t *mEnt;
	int             i;
	float           w = 100.f, h = 100.f;
	hudStucture_t   *hud = CG_GetActiveHUD();

	if (hud)
	{
		// subtract surrounding decoration of the compass
		w = hud->compass.location.w - (hud->compass.location.w * 0.25f);
		h = hud->compass.location.h - (hud->compass.location.h * 0.25f);
	}

	for (i = 0; i < mapEntityCount; i++)
	{
		mEnt = &mapEntities[i];

		// calculate the screen coordinate of this entity for the automap, consider the zoom value
		mEnt->automapTransformed[0] = (mEnt->x - cg.mapcoordsMins[0]) * cg.mapcoordsScale[0] * w * cg_automapZoom.value; // FIXME: check value out of range before?
		mEnt->automapTransformed[1] = (mEnt->y - cg.mapcoordsMins[1]) * cg.mapcoordsScale[1] * h * cg_automapZoom.value;
	}
}

/**
 * @brief CG_AdjustAutomapZoom
 * @param[in] zoomIn
 */
void CG_AdjustAutomapZoom(int zoomIn)
{
	float automapZoom = cg_automapZoom.value;

	if (zoomIn)
	{
		automapZoom *= 1.2f;
		if (automapZoom > 7.43f)  // approximately 1.2^11
		{
			automapZoom = 7.43f;
		}
	}
	else
	{
		automapZoom /= 1.2f;
		// zoom value of 1 corresponds to the most zoomed out view. The whole map is displayed
		// in the automap
		if (automapZoom < 1)
		{
			automapZoom = 1;
		}
	}

	trap_Cvar_Set("cg_automapZoom", va("%f", automapZoom));

	// recalculate the screen coordinates since the zoom changed
	CG_TransformAutomapEntity();
}

/**
 * @brief CG_ParseMapEntity
 * @param[in] mapEntityCount
 * @param[in,out] offset
 * @param[in] team
 */
void CG_ParseMapEntity(int *mapEntityCount, int *offset, team_t team)
{
	mapEntityData_t *mEnt = &mapEntities[(*mapEntityCount)];
	char            buffer[16];

	trap_Argv((*offset)++, buffer, 16);
	mEnt->type = Q_atoi(buffer);

	switch (mEnt->type)
	{
	// These are needed for command map icons to show up on the correct layer.
	case ME_CONSTRUCT:
	case ME_DESTRUCT:
	case ME_DESTRUCT_2:
	case ME_TANK:
	case ME_TANK_DEAD:
	case ME_COMMANDMAP_MARKER:
		trap_Argv((*offset)++, buffer, 16);
		mEnt->x = Q_atoi(buffer) * 128;

		trap_Argv((*offset)++, buffer, 16);
		mEnt->y = Q_atoi(buffer) * 128;

		if (cgs.ccLayers)
		{
			trap_Argv((*offset)++, buffer, 16);
			mEnt->z = Q_atoi(buffer) * 128;
		}
		break;
	default:
		trap_Argv((*offset)++, buffer, 16);
		mEnt->x = Q_atoi(buffer) * 128;

		trap_Argv((*offset)++, buffer, 16);
		mEnt->y = Q_atoi(buffer) * 128;

		if (cgs.ccLayers)
		{
			trap_Argv((*offset)++, buffer, 16);
			mEnt->z = Q_atoi(buffer) * 128;
		}

		trap_Argv((*offset)++, buffer, 16);
		mEnt->yaw = Q_atoi(buffer);
		break;
	}

	trap_Argv((*offset)++, buffer, 16);
	mEnt->data = Q_atoi(buffer);

	mEnt->transformed[0] = (mEnt->x - cg.mapcoordsMins[0]) * cg.mapcoordsScale[0] * CC_2D_W;
	mEnt->transformed[1] = (mEnt->y - cg.mapcoordsMins[1]) * cg.mapcoordsScale[1] * CC_2D_H;

	mEnt->team = team;

	(*mapEntityCount)++;
}

/**
 * @brief CG_ParseMapEntityInfo
 * @param[in] axis_number
 * @param[in] allied_number
 */
void CG_ParseMapEntityInfo(int axis_number, int allied_number)
{
	int i, offset = 3;

	mapEntityCount = 0;

	for (i = 0; i < axis_number; i++)
	{
		CG_ParseMapEntity(&mapEntityCount, &offset, TEAM_AXIS);
	}

	for (i = 0; i < allied_number; i++)
	{
		CG_ParseMapEntity(&mapEntityCount, &offset, TEAM_ALLIES);
	}

	CG_TransformAutomapEntity();
}

static qboolean gridInitDone = qfalse;
static vec2_t   gridStartCoord, gridStep;

/**
 * @brief CG_DrawGrid
 * @param[in] x
 * @param[in] y
 * @param[in] w
 * @param[in] h
 * @param[in] scissor
 */
static void CG_DrawGrid(float x, float y, float w, float h, mapScissor_t *scissor)
{
	vec2_t step;
	vec2_t dim_x, dim_y;
	vec4_t line;
	float  xscale, yscale;
	float  grid_x, grid_y;
	vec2_t dist;
	vec4_t gridColor;
	float  Max0Min0 = cg.mapcoordsMaxs[0] - cg.mapcoordsMins[0];
	float  Max1Min1 = cg.mapcoordsMaxs[1] - cg.mapcoordsMins[1];
	float  Min1Max1 = cg.mapcoordsMins[1] - cg.mapcoordsMaxs[1];
	float  tmp;

	dist[0] = Max0Min0;
	dist[1] = Max1Min1;

	if (!gridInitDone)
	{
		gridStep[0] = 1200.f;
		gridStep[1] = 1200.f;

		// ensure minimal grid density
		while (Max0Min0 / gridStep[0] < 7)
		{
			gridStep[0] -= 50.f;
		}
		while (Min1Max1 / gridStep[1] < 7)
		{
			gridStep[1] -= 50.f;
		}

		tmp               = Max0Min0 / gridStep[0];
		gridStartCoord[0] = .5f * (tmp - (int)(tmp)) * gridStep[0];
		tmp               = Min1Max1 / gridStep[1];
		gridStartCoord[1] = .5f * (tmp - (int)(tmp)) * gridStep[1];

		gridInitDone = qtrue;
	}

	if (scissor)
	{
		dim_x[0] = cg.mapcoordsMins[0];
		dim_x[1] = cg.mapcoordsMaxs[0];

		dim_y[0] = cg.mapcoordsMaxs[1];
		dim_y[1] = cg.mapcoordsMins[1];

		// transform
		xscale = (w * scissor->zoomFactor) / dist[0];
		yscale = (h * scissor->zoomFactor) / -dist[1];

		dim_x[0] = (dim_x[0] - cg.mapcoordsMins[0]) * xscale;
		dim_x[1] = (dim_x[1] - cg.mapcoordsMins[0]) * xscale;

		dim_y[0] = (dim_y[0] - cg.mapcoordsMaxs[1]) * yscale;
		dim_y[1] = (dim_y[1] - cg.mapcoordsMaxs[1]) * yscale;

		grid_x = ((gridStartCoord[0] / dist[0]) * w * scissor->zoomFactor) - scissor->tl[0];
		grid_y = ((-gridStartCoord[1] / dist[1]) * h * scissor->zoomFactor) - scissor->tl[1];

		step[0] = gridStep[0] * xscale;
		step[1] = gridStep[1] * yscale;

		// draw
		Vector4Set(gridColor, clrBrownLine[0], clrBrownLine[1], clrBrownLine[2], .4f);
		trap_R_SetColor(gridColor);
		for ( ; grid_x < dim_x[1]; grid_x += step[0])
		{
			if (grid_x < dim_x[0])
			{
				continue;
			}

			if (grid_x > w)
			{
				break;
			}

			if (scissor->circular)
			{
				// clip line against circle
				float xc, yc;

				line[0] = x + grid_x;
				tmp     = x + .5f * w;
				xc      = line[0] >= tmp ? line[0] - tmp : tmp - line[0];
				yc      = SQRTFAST(Square(.5f * w) - Square(xc));
				line[1] = y + (.5f * h) - yc;
				line[2] = 1.f;
				line[3] = 2 * yc;
			}
			else
			{
				Vector4Set(line, x + grid_x, y + dim_y[0], 1.f, h);
			}

			CG_AdjustFrom640(&line[0], &line[1], &line[2], &line[3]);

			trap_R_DrawStretchPic(line[0], line[1], line[2], line[3], 0, 0, 0, 1, cgs.media.whiteShader);
		}

		for ( ; grid_y < dim_y[1]; grid_y += step[1])
		{
			if (grid_y < dim_y[0])
			{
				continue;
			}

			if (grid_y > h)
			{
				break;
			}

			if (scissor->circular)
			{
				// clip line against circle
				float xc, yc;

				line[1] = y + grid_y;
				tmp     = y + .5f * h;
				yc      = line[1] >= tmp ? line[1] - tmp : tmp - line[1];
				xc      = SQRTFAST(Square(.5f * h) - Square(yc));
				line[0] = x + (.5f * w) - xc;
				line[2] = 2 * xc;
				line[3] = 1.f;
			}
			else
			{
				Vector4Set(line, x + dim_x[0], y + grid_y, w, 1);
			}
			CG_AdjustFrom640(&line[0], &line[1], &line[2], &line[3]);

			trap_R_DrawStretchPic(line[0], line[1], line[2], line[3], 0, 0, 0, 1, cgs.media.whiteShader);
		}
		trap_R_SetColor(NULL);
	}
	else
	{
		char        coord_char[3];
		signed char coord_int;
		float       text_width, text_height;
		vec2_t      textOrigin;

		dim_x[0] = cg.mapcoordsMins[0];
		dim_x[1] = cg.mapcoordsMaxs[0];

		dim_y[0] = cg.mapcoordsMaxs[1];
		dim_y[1] = cg.mapcoordsMins[1];

		// transform
		xscale = w / dist[0];
		yscale = h / -dist[1];

		dim_x[0] = (dim_x[0] - cg.mapcoordsMins[0]) * xscale;
		dim_x[1] = (dim_x[1] - cg.mapcoordsMins[0]) * xscale;

		dim_y[0] = (dim_y[0] - cg.mapcoordsMaxs[1]) * yscale;
		dim_y[1] = (dim_y[1] - cg.mapcoordsMaxs[1]) * yscale;

		grid_x = gridStartCoord[0] * xscale;
		grid_y = gridStartCoord[1] * yscale;

		step[0] = gridStep[0] * xscale;
		step[1] = gridStep[1] * yscale;

		// draw
		textOrigin[0] = grid_x;
		textOrigin[1] = grid_y;

		Vector4Set(gridColor, clrBrownLine[0], clrBrownLine[1], clrBrownLine[2], 1.f);

		coord_char[1] = '\0';
		for (coord_char[0] = ('A' - 1); grid_x < dim_x[1]; grid_x += step[0], coord_char[0]++)
		{
			if (coord_char[0] >= 'A')
			{
				text_width  = CG_Text_Width_Ext(coord_char, 0.2f, 0, &cgs.media.limboFont2);
				text_height = CG_Text_Height_Ext(coord_char, 0.2f, 0, &cgs.media.limboFont2);
				CG_Text_Paint_Ext((x + grid_x) - (.5f * step[0]) - (.5f * text_width), y + dim_y[0] + textOrigin[1] + 1.5f * text_height, 0.2f, 0.2f, colorBlack, coord_char, 0, 0, 0, &cgs.media.limboFont2);
			}
			trap_R_SetColor(gridColor);

			Vector4Set(line, x + grid_x, y + dim_y[0], 1, dim_x[1] - dim_x[0]);
			CG_AdjustFrom640(&line[0], &line[1], &line[2], &line[3]);
			trap_R_DrawStretchPic(line[0], line[1], line[2], line[3], 0, 0, 0, 1, cgs.media.whiteShader);
		}

		for (coord_int = -1; grid_y < dim_y[1]; grid_y += step[1], coord_int++)
		{
			if (coord_int >= 0)
			{
				Com_sprintf(coord_char, sizeof(coord_char), "%i", coord_int);
				text_width  = CG_Text_Width_Ext("0", 0.2f, 0, &cgs.media.limboFont2);
				text_height = CG_Text_Height_Ext(coord_char, 0.2f, 0, &cgs.media.limboFont2);
				CG_Text_Paint_Ext(x + dim_x[0] + textOrigin[0] + .5f * text_width, (y + grid_y) - (.5f * step[1]) + (.5f * text_height), 0.2f, 0.2f, colorBlack, coord_char, 0, 0, 0, &cgs.media.limboFont2);
			}
			trap_R_SetColor(gridColor);

			Vector4Set(line, x + dim_x[0], y + grid_y, dim_y[1] - dim_y[0], 1);
			CG_AdjustFrom640(&line[0], &line[1], &line[2], &line[3]);
			trap_R_DrawStretchPic(line[0], line[1], line[2], line[3], 0, 0, 0, 1, cgs.media.whiteShader);
		}
		trap_R_SetColor(NULL);
	}
}

/**
 * @brief CG_RotateMapEntCoordinatePoint
 * @param[in] x
 * @param[in] y
 * @param[in] w
 * @param[in] h
 * @param[in,out] point
 * @param[in] theta
 */
ID_INLINE static void CG_RotateMapEntCoordinatePoint(float x, float y, float w, float h, vec2_t *point, float theta)
{
	Rotate2DPointAroundPoint(point, &(const vec2_t) { x + w * 0.5, y + h * 0.5 }, theta);
}

/**
 * @brief CG_ComputeMapEntCoordinate
 * @param[in,out] mEnt
 * @param[in] scissor
 * @param[in] origin
 * @param[in] yaw
 * @param[in] w
 * @param[in] h
 */
static void CG_ComputeMapEntCoordinate(mapEntityData_t *mEnt, mapScissor_t *scissor, vec3_t *origin, float *yaw, float w, float h)
{
	vec2_t *pTransformed;
	float  widthFactor;
	float  heightFactor;

	if (!scissor)
	{
		pTransformed = &(mEnt->transformed);
		widthFactor  = w;
		heightFactor = h;
	}
	else
	{
		pTransformed = &(mEnt->automapTransformed);
		widthFactor  = w * scissor->zoomFactor;
		heightFactor = h * scissor->zoomFactor;
	}

	(*pTransformed)[0] = (((*origin)[0] - cg.mapcoordsMins[0]) * cg.mapcoordsScale[0]) * widthFactor;
	(*pTransformed)[1] = (((*origin)[1] - cg.mapcoordsMins[1]) * cg.mapcoordsScale[1]) * heightFactor;

	if (yaw)
	{
		mEnt->yaw = (int)(*yaw);
	}
}

/**
 * @brief CG_DrawMapEntity
 * @param[in] mEnt
 * @param[in] x
 * @param[in] y
 * @param[in] w
 * @param[in] h
 * @param[in] mEntFilter
 * @param[in] scissor
 * @param[in] interactive
 * @param[in] snap
 * @param[in] icon_size
 * @param[in] pointTowardNorth
 */
static void CG_DrawMapEntity(mapEntityData_t *mEnt, float x, float y, float w, float h, int mEntFilter, mapScissor_t *scissor,
                             qboolean interactive, snapshot_t *snap, int icon_size, qboolean pointTowardNorth)
{
	int              j = 1;
	qhandle_t        pic;
	clientInfo_t     *ci;
	bg_playerclass_t *classInfo;
	centity_t        *cent;
	const char       *name;
	vec4_t           c_clr = { 1.f, 1.f, 1.f, 1.f };
	vec2_t           icon_extends, icon_pos, string_pos = { 0 };
	int              customimage = 0;
	oidInfo_t        *oidInfo    = NULL;
	int              entNum;

	switch (mEnt->type)
	{
	case ME_PLAYER_OBJECTIVE:
	case ME_PLAYER_DISGUISED:
	case ME_PLAYER_REVIVE:
	case ME_PLAYER:
		ci = &cgs.clientinfo[mEnt->data];
		if (!ci->infoValid)
		{
			return;
		}

		if (ci->team == TEAM_AXIS)
		{
			if (mEntFilter & CC_FILTER_AXIS)
			{
				return;
			}
		}
		else if (ci->team == TEAM_ALLIES)
		{
			if (mEntFilter & CC_FILTER_ALLIES)
			{
				return;
			}
		}
		else
		{
			return;
		}

		cent = &cg_entities[mEnt->data];

		if (mEnt->type == ME_PLAYER_DISGUISED && !(cent->currentState.powerups & (1 << PW_OPS_DISGUISED)))
		{
			return;
		}

		classInfo = CG_PlayerClassForClientinfo(ci, cent);

		// For these, if available, ignore the coordinate data and grab the most up to date pvs data
		if (cent - cg_entities == cg.clientNum)
		{
			// use our own lerp'ed origin
			CG_ComputeMapEntCoordinate(mEnt, scissor, &cg.predictedPlayerEntity.lerpOrigin, &cg.predictedPlayerState.viewangles[YAW], w, h);
		}
		else if ((cgs.clientinfo[cg.clientNum].team == TEAM_SPECTATOR && snap->ps.clientNum != cg.clientNum && cent - cg_entities == snap->ps.clientNum))
		{
			// we are following someone, so use their info
			CG_ComputeMapEntCoordinate(mEnt, scissor, &snap->ps.origin, &snap->ps.viewangles[YAW], w, h);
		}
		else if (cent->currentValid)
		{
			// use more up-to-date info from pvs
			CG_ComputeMapEntCoordinate(mEnt, scissor, &cent->lerpOrigin, &cent->lerpAngles[YAW], w, h);
		}
		else
		{
			// only see revivables for own team, unless shoutcaster
			if (mEnt->type == ME_PLAYER_REVIVE && !CG_IsShoutcaster())
			{
				return;
			}

			// use the coordinates from clientinfo
			CG_ComputeMapEntCoordinate(mEnt, scissor, &ci->location, NULL, w, h);
		}

		if (cgs.ccLayers)
		{
			if (CG_CurLayerForZ(mEnt->z) != cgs.ccSelectedLayer)
			{
				return;
			}
		}

		if (scissor)
		{
			if (cg_dynamicIcons.integer)
			{
				icon_size *= CG_PlayerDistanceScaling(mEnt);
			}

			icon_pos[0] = mEnt->automapTransformed[0] - scissor->tl[0] + x;
			icon_pos[1] = mEnt->automapTransformed[1] - scissor->tl[1] + y;

			icon_extends[0] = 2 * icon_size * (scissor->zoomFactor / AUTOMAP_ZOOM);
			icon_extends[1] = 2 * icon_size * (scissor->zoomFactor / AUTOMAP_ZOOM);

			// now check to see if the entity is within our clip region
			if (CG_ScissorEntIsCulled(mEnt, scissor, icon_extends))
			{
				break;
			}
		}
		else
		{
			icon_pos[0]   = x + mEnt->transformed[0];
			icon_pos[1]   = y + mEnt->transformed[1];
			string_pos[0] = x + mEnt->transformed[0];
			string_pos[1] = y + mEnt->transformed[1];

			icon_extends[0] = 2 * icon_size;
			icon_extends[1] = 2 * icon_size;
		}

		if (!pointTowardNorth)
		{
			CG_RotateMapEntCoordinatePoint(x, y, w, h, &icon_pos, cg.predictedPlayerState.viewangles[YAW] - 90);
		}

		if (mEnt->type == ME_PLAYER_REVIVE && !(cent->currentState.powerups & (1 << PW_INVULNERABLE)))
		{
			float  msec;
			vec4_t reviveClr = { 1.f, 1.f, 1.f, 1.f };

			if (cgs.clientinfo[cg.snap->ps.clientNum].team == TEAM_AXIS || (CG_IsShoutcaster() && mEnt->team == TEAM_AXIS))
			{
				msec = (cg_redlimbotime.integer - (cg.time % cg_redlimbotime.integer)) / (float)cg_redlimbotime.integer;
			}
			else if (cgs.clientinfo[cg.snap->ps.clientNum].team == TEAM_ALLIES || (CG_IsShoutcaster() && mEnt->team == TEAM_ALLIES))
			{
				msec = (cg_bluelimbotime.integer - (cg.time % cg_bluelimbotime.integer)) / (float)cg_bluelimbotime.integer;
			}
			else
			{
				msec = 0;
			}

			reviveClr[3] = .5f + .5f * (float)((sin(sqrt((double)msec) * 25 * M_TAU_F) + 1) * 0.5);

			trap_R_SetColor(reviveClr);
			CG_DrawPic(icon_pos[0] - icon_extends[0] * 0.5f, icon_pos[1] - icon_extends[1] * 0.5f, icon_extends[0], icon_extends[1], cgs.media.ccMedicIcon);
		}
		else
		{
			if (cg.clientNum == mEnt->data)
			{
				if (ci->selected)
				{
					trap_R_SetColor(colorRed);
				}
				else
				{
					trap_R_SetColor(colorYellow);
				}

				CG_DrawPic(icon_pos[0] - icon_extends[0] * 0.5f, icon_pos[1] - icon_extends[1] * 0.5f, icon_extends[0], icon_extends[1], cgs.media.ccPlayerHighlight);
				trap_R_SetColor(NULL);

				if (cg.predictedPlayerEntity.voiceChatSpriteTime > cg.time)
				{
					CG_DrawPic(icon_pos[0] + icon_extends[0] * 0.5f, icon_pos[1], icon_extends[0] * 0.5f, icon_extends[1] * 0.5f, CG_GetVoiceChatForCommandMap(cg.predictedPlayerEntity.voiceChatSprite));
				}
			}
			else if (CG_IsOnSameFireteam(cg.clientNum, mEnt->data))
			{
				if (ci->selected)
				{
					trap_R_SetColor(colorRed);
				}
				else
				{
					trap_R_SetColor(colorGreen);
				}

				CG_DrawPic(icon_pos[0] - icon_extends[0] * 0.5f, icon_pos[1] - icon_extends[1] * 0.5f, icon_extends[0], icon_extends[1], cgs.media.ccPlayerHighlight);
				trap_R_SetColor(NULL);

				if (!scissor)
				{
					CG_Text_Paint_Ext(string_pos[0] - icon_extends[0] * 0.5f, string_pos[1] - icon_extends[1] * 0.5f, 0.2f, 0.2f, colorWhite, ci->name, 0, 0, ITEM_TEXTSTYLE_SHADOWED, &cgs.media.limboFont2);
				}

				if (cent->voiceChatSpriteTime > cg.time)
				{
					CG_DrawPic(icon_pos[0] + icon_extends[0] * 0.5f, icon_pos[1], icon_extends[0] * 0.5f, icon_extends[1] * 0.5f, CG_GetVoiceChatForCommandMap(cent->voiceChatSprite));
				}
			}
			else if (ci->team == snap->ps.persistant[PERS_TEAM])
			{
				if (ci->selected)
				{
					trap_R_SetColor(colorRed);
					CG_DrawPic(icon_pos[0] - icon_extends[0] * 0.5f, icon_pos[1] - icon_extends[1] * 0.5f, icon_extends[0], icon_extends[1], cgs.media.ccPlayerHighlight);
					trap_R_SetColor(NULL);
				}

				if (cent->voiceChatSpriteTime > cg.time)
				{
					CG_DrawPic(icon_pos[0] + icon_extends[0] * 0.5f, icon_pos[1], icon_extends[0] * 0.5f, icon_extends[1] * 0.5f, CG_GetVoiceChatForCommandMap(cent->voiceChatSprite));
				}
			}

			// hide ghost icon for following shoutcaster, highlight followed player.
			if (CG_IsShoutcaster() && (cg.snap->ps.pm_flags & PMF_FOLLOW) && cg.snap->ps.clientNum == mEnt->data)
			{
				// otherwise highlighting takes some time to disappear
				if (cg.snap->ps.stats[STAT_HEALTH] > 0)
				{
					trap_R_SetColor(colorYellow);
					CG_DrawPic(icon_pos[0] - icon_extends[0] * 0.5f, icon_pos[1] - icon_extends[1] * 0.5f, icon_extends[0], icon_extends[1], cgs.media.ccPlayerHighlight);
				}
				break;
			}

			c_clr[3] = 1.0f;

			trap_R_SetColor(c_clr);

			// FIXME: the map entity ME_PLAYER_DISGUISED is never defined here, so this is a bit hackish
			if (cent->currentState.powerups & (1 << PW_OPS_DISGUISED))
			{
				CG_DrawPic(icon_pos[0] - icon_extends[0] * 0.5f, icon_pos[1] - icon_extends[1] * 0.5f, icon_extends[0], icon_extends[1], classInfo->icon);
				if (ci->team == snap->ps.persistant[PERS_TEAM] || CG_IsShoutcaster())
				{
					CG_DrawPic(icon_pos[0] - icon_extends[0] * 0.5f, icon_pos[1] - icon_extends[1] * 0.5f, icon_extends[0], icon_extends[1], cgs.media.friendShader);
				}
			}
			else if (mEnt->type == ME_PLAYER_OBJECTIVE)
			{
				CG_DrawPic(icon_pos[0] - icon_extends[0] * 0.5f, icon_pos[1] - icon_extends[1] * 0.5f, icon_extends[0], icon_extends[1], cgs.media.objectiveShader);
			}
			else
			{
				CG_DrawPic(icon_pos[0] - icon_extends[0] * 0.5f, icon_pos[1] - icon_extends[1] * 0.5f, icon_extends[0], icon_extends[1], classInfo->icon);
			}

			if (!pointTowardNorth)
			{
				if (cent - cg_entities == cg.clientNum)
				{
					// point always upper direction on self
					CG_DrawRotatedPic(icon_pos[0] - icon_extends[0] * 0.5f - 1, icon_pos[1] - icon_extends[1] * 0.5f - 1, icon_extends[0] + 2, icon_extends[1] + 2, classInfo->arrow,
					                  (0.5f + 90.f / 360.f));
				}
				else
				{
					CG_DrawRotatedPic(icon_pos[0] - icon_extends[0] * 0.5f - 1, icon_pos[1] - icon_extends[1] * 0.5f - 1, icon_extends[0] + 2, icon_extends[1] + 2, classInfo->arrow,
					                  (0.5f - (mEnt->yaw - (cg.predictedPlayerState.viewangles[YAW] - 90) - 180.f) / 360.f));
				}
			}
			else
			{
				CG_DrawRotatedPic(icon_pos[0] - icon_extends[0] * 0.5f - 1, icon_pos[1] - icon_extends[1] * 0.5f - 1, icon_extends[0] + 2, icon_extends[1] + 2, classInfo->arrow,
				                  (0.5f - (mEnt->yaw - 180.f) / 360.f));
			}
		}
		break;
	case ME_CONSTRUCT:
	case ME_DESTRUCT:
	case ME_DESTRUCT_2:
	case ME_TANK:
	case ME_TANK_DEAD:
	case ME_COMMANDMAP_MARKER:
		cent = NULL;
		if (mEnt->type == ME_TANK || mEnt->type == ME_TANK_DEAD)
		{
			oidInfo = &cgs.oidInfo[mEnt->data];

			for (j = 0; j < cg.snap->numEntities; j++)
			{
				if (cg.snap->entities[j].eType == ET_OID_TRIGGER && cg.snap->entities[j].teamNum == mEnt->data)
				{
					cent = &cg_entities[cg.snap->entities[j].number];

					CG_ComputeMapEntCoordinate(mEnt, scissor, &cent->lerpOrigin, NULL, w, h);
					break;
				}
			}
		}
		else if (mEnt->type == ME_CONSTRUCT || mEnt->type == ME_DESTRUCT || mEnt->type == ME_DESTRUCT_2)
		{
			cent = &cg_entities[mEnt->data];
			if (!cent->currentValid)
			{
				return;
			}

			oidInfo = &cgs.oidInfo[cent->currentState.modelindex2];

			CG_ComputeMapEntCoordinate(mEnt, scissor, &cent->lerpOrigin, NULL, w, h);
		}
		else if (mEnt->type == ME_COMMANDMAP_MARKER)
		{
			oidInfo = &cgs.oidInfo[mEnt->data];

			CG_ComputeMapEntCoordinate(mEnt, scissor, &oidInfo->origin, NULL, w, h);
		}

		if (cgs.ccLayers)
		{
			if (CG_CurLayerForZ(mEnt->z) != cgs.ccSelectedLayer)
			{
				return;
			}
		}

		if (oidInfo)
		{
			customimage = mEnt->team == TEAM_AXIS ? oidInfo->customimageaxis : oidInfo->customimageallies;

			// we have an oidInfo - check for main objective and do special color in case of
			// note: to make this work map scripts have to be adjusted
			if (snap->ps.persistant[PERS_TEAM] != TEAM_SPECTATOR)
			{
				entNum = Q_atoi(CG_ConfigString(mEnt->team == TEAM_AXIS ? CS_MAIN_AXIS_OBJECTIVE : CS_MAIN_ALLIES_OBJECTIVE));

				if (entNum == oidInfo->entityNum)
				{
					trap_R_SetColor(colorYellow);
				}
			}
		}

		switch (mEnt->type)
		{
		case ME_CONSTRUCT:
			if (mEntFilter & CC_FILTER_CONSTRUCTIONS)
			{
				trap_R_SetColor(NULL);
				return;
			}
			pic = mEnt->team == TEAM_AXIS ? cgs.media.ccConstructIcon[0] : cgs.media.ccConstructIcon[1];
			break;
		case ME_TANK:
			if (mEntFilter & CC_FILTER_OBJECTIVES)
			{
				trap_R_SetColor(NULL);
				return;
			}
			pic = cgs.media.ccTankIcon; // FIXME: this is churchill - add jagdpanther?
			break;
		case ME_TANK_DEAD:
			if (mEntFilter & CC_FILTER_OBJECTIVES)
			{
				trap_R_SetColor(NULL);
				return;
			}
			pic = cgs.media.ccTankIcon; // FIXME: this is churchill - add jagdpanther?
			trap_R_SetColor(colorRed);
			break;
		case ME_COMMANDMAP_MARKER:
			pic = 0;
			break;
		case ME_DESTRUCT_2:
			pic = 0;
			break;
		case ME_DESTRUCT:
			if (mEntFilter & CC_FILTER_DESTRUCTIONS)
			{
				trap_R_SetColor(NULL);
				return;
			}
			pic = mEnt->team == TEAM_AXIS ? cgs.media.ccDestructIcon[cent->currentState.effect1Time][0] : cgs.media.ccDestructIcon[cent->currentState.effect1Time][1];
			break;
		default:
			break;
		}

		if (oidInfo)
		{
			if (oidInfo->spawnflags & (1 << 4))
			{
				if (mEntFilter & CC_FILTER_OBJECTIVES)
				{
					break;
				}
			}
			if (oidInfo->spawnflags & (1 << 5))
			{
				if (mEntFilter & CC_FILTER_HACABINETS)
				{
					break;
				}
			}
			if (oidInfo->spawnflags & (1 << 6))
			{
				if (mEnt->type == ME_DESTRUCT_2)
				{
					pic = mEnt->team == TEAM_AXIS ? cgs.media.ccCmdPost[0] : cgs.media.ccCmdPost[1];
				}
				if (mEntFilter & CC_FILTER_CMDPOST)
				{
					break;
				}
			}
		}

		if (customimage)
		{
			pic = customimage;
		}

		if (scissor)
		{
			icon_pos[0] = mEnt->automapTransformed[0] - scissor->tl[0] + x;
			icon_pos[1] = mEnt->automapTransformed[1] - scissor->tl[1] + y;
		}
		else
		{
			icon_pos[0] = x + mEnt->transformed[0];
			icon_pos[1] = y + mEnt->transformed[1];
		}

		if (interactive && !expanded && BG_RectContainsPoint(x + mEnt->transformed[0] - CONST_ICON_NORMAL_SIZE * 0.5f, y + mEnt->transformed[1] - CONST_ICON_NORMAL_SIZE * 0.5f, CONST_ICON_NORMAL_SIZE, CONST_ICON_NORMAL_SIZE, cgDC.cursorx, cgDC.cursory))
		{
			float width;

			if (scissor && CG_IsShoutcaster())
			{
				icon_extends[0] = CONST_ICON_EXPANDED_SIZE_SC;
				icon_extends[1] = CONST_ICON_EXPANDED_SIZE_SC;
			}
			else
			{
				icon_extends[0] = CONST_ICON_EXPANDED_SIZE;
				icon_extends[1] = CONST_ICON_EXPANDED_SIZE;
			}

			if (mEnt->type == ME_TANK_DEAD || mEnt->type == ME_TANK)
			{
				icon_extends[1] *= 0.5f;
			}

			if (scissor)
			{
				icon_extends[0] *= (scissor->zoomFactor / AUTOMAP_ZOOM);
				icon_extends[1] *= (scissor->zoomFactor / AUTOMAP_ZOOM);

				// now check to see if the entity is within our clip region
				if (CG_ScissorEntIsCulled(mEnt, scissor, icon_extends))
				{
					break;
				}
			}
			else
			{
				icon_extends[0] *= cgs.ccZoomFactor;
				icon_extends[1] *= cgs.ccZoomFactor;
			}

			if (!pointTowardNorth)
			{
				CG_RotateMapEntCoordinatePoint(x, y, w, h, &icon_pos, cg.predictedPlayerState.viewangles[YAW] - 90);
			}

			CG_DrawPic(icon_pos[0] - icon_extends[0] * 0.5f, icon_pos[1] - icon_extends[1] * 0.5f, icon_extends[0], icon_extends[1], pic);

			if (oidInfo)
			{
				name = oidInfo->name;
			}
			else
			{
				name = va("%i", j);
			}

			width = CG_Text_Width_Ext(name, 0.2f, 0, &cgs.media.limboFont2);
			CG_CommandMap_SetHighlightText(name, icon_pos[0] - (width * 0.5f), icon_pos[1] - 8);
		}
		else if (interactive && ((mEnt->yaw & 0xFF) & (1 << cgs.ccSelectedObjective)))
		{
			float scalesize;
			int   time = cg.time % 1400;

			if (time <= 700)
			{
				scalesize = 12 * (time) / 700.f;
			}
			else
			{
				scalesize = 12 * (1 - ((time - 700) / 700.f));
			}

			if (scissor && CG_IsShoutcaster())
			{
				icon_extends[0] = CONST_ICON_NORMAL_SIZE_SC + scalesize;
				icon_extends[1] = CONST_ICON_NORMAL_SIZE_SC + scalesize;
			}
			else
			{
				icon_extends[0] = CONST_ICON_NORMAL_SIZE + scalesize;
				icon_extends[1] = CONST_ICON_NORMAL_SIZE + scalesize;
			}

			if (mEnt->type == ME_TANK_DEAD || mEnt->type == ME_TANK)
			{
				icon_extends[1] *= 0.5f;
			}

			if (scissor)
			{
				icon_extends[0] *= (scissor->zoomFactor / AUTOMAP_ZOOM);
				icon_extends[1] *= (scissor->zoomFactor / AUTOMAP_ZOOM);

				// now check to see if the entity is within our clip region
				if (CG_ScissorEntIsCulled(mEnt, scissor, icon_extends))
				{
					break;
				}
			}
			else
			{
				icon_extends[0] *= cgs.ccZoomFactor;
				icon_extends[1] *= cgs.ccZoomFactor;
			}

			if (!pointTowardNorth)
			{
				CG_RotateMapEntCoordinatePoint(x, y, w, h, &icon_pos, cg.predictedPlayerState.viewangles[YAW] - 90);
			}

			CG_DrawPic(icon_pos[0] - icon_extends[0] * 0.5f, icon_pos[1] - icon_extends[1] * 0.5f, icon_extends[0], icon_extends[1], pic);
		}
		else
		{
			if (scissor && CG_IsShoutcaster())
			{
				icon_extends[0] = CONST_ICON_NORMAL_SIZE_SC;
				icon_extends[1] = CONST_ICON_NORMAL_SIZE_SC;
			}
			else
			{
				icon_extends[0] = CONST_ICON_NORMAL_SIZE;
				icon_extends[1] = CONST_ICON_NORMAL_SIZE;
			}

			if (mEnt->type == ME_TANK_DEAD || mEnt->type == ME_TANK)
			{
				icon_extends[1] *= 0.5f;
			}

			if (CG_IsShoutcaster())
			{
				icon_extends[0] *= cgs.ccZoomFactor;
				icon_extends[1] *= cgs.ccZoomFactor;
			}
			else if (scissor)
			{
				icon_extends[0] *= (scissor->zoomFactor / AUTOMAP_ZOOM);
				icon_extends[1] *= (scissor->zoomFactor / AUTOMAP_ZOOM);
			}

			// now check to see if the entity is within our clip region
			if (scissor && CG_ScissorEntIsCulled(mEnt, scissor, icon_extends))
			{
				break;
			}

			if (!pointTowardNorth)
			{
				CG_RotateMapEntCoordinatePoint(x, y, w, h, &icon_pos, cg.predictedPlayerState.viewangles[YAW] - 90);
			}

			CG_DrawPic(icon_pos[0] - icon_extends[0] * 0.5f, icon_pos[1] - icon_extends[1] * 0.5f, icon_extends[0], icon_extends[1], pic);
		}
		break;
	case ME_LANDMINE:
	{
		if (mEntFilter & CC_FILTER_LANDMINES)
		{
			return;
		}

		if (cgs.ccLayers)
		{
			if (CG_CurLayerForZ(mEnt->z) != cgs.ccSelectedLayer)
			{
				return;
			}
		}

		pic = mEnt->data == TEAM_AXIS ? cgs.media.commandCentreAxisMineShader : cgs.media.commandCentreAlliedMineShader;

		c_clr[3] = 1.0f;

		if (scissor)
		{
			icon_pos[0] = mEnt->automapTransformed[0] - scissor->tl[0] + x;
			icon_pos[1] = mEnt->automapTransformed[1] - scissor->tl[1] + y;
		}
		else
		{
			icon_pos[0] = x + mEnt->transformed[0];
			icon_pos[1] = y + mEnt->transformed[1];
		}

		if (scissor && CG_IsShoutcaster())
		{
			icon_extends[0] = CONST_ICON_LANDMINE_SIZE_SC;
			icon_extends[1] = CONST_ICON_LANDMINE_SIZE_SC;
		}
		else
		{
			icon_extends[0] = CONST_ICON_LANDMINE_SIZE;
			icon_extends[1] = CONST_ICON_LANDMINE_SIZE;
		}

		if (CG_IsShoutcaster())
		{
			icon_extends[0] *= cgs.ccZoomFactor;
			icon_extends[1] *= cgs.ccZoomFactor;
		}
		else if (scissor)
		{
			icon_extends[0] *= (scissor->zoomFactor / AUTOMAP_ZOOM);
			icon_extends[1] *= (scissor->zoomFactor / AUTOMAP_ZOOM);
		}

		// now check to see if the entity is within our clip region
		if (scissor && CG_ScissorEntIsCulled(mEnt, scissor, icon_extends))
		{
			break;
		}

		if (!pointTowardNorth)
		{
			CG_RotateMapEntCoordinatePoint(x, y, w, h, &icon_pos, cg.predictedPlayerState.viewangles[YAW] - 90);
		}

		trap_R_SetColor(c_clr);
		CG_DrawPic(icon_pos[0] - icon_extends[0] * 0.5f, icon_pos[1] - icon_extends[1] * 0.5f, icon_extends[0], icon_extends[1], pic);

		break;
	}
	default:
		break;
	}

	trap_R_SetColor(NULL);
}
/**
 * @brief CG_DisguiseMapCheck
 * @param[in] mEnt
 * @return
 */
qboolean CG_DisguiseMapCheck(mapEntityData_t *mEnt)
{
	if (mEnt->data < 0 || mEnt->data >= 64)
	{
		return qfalse;
	}

	if (!cgs.clientinfo[mEnt->data].infoValid)
	{
		return qfalse;
	}

	if (!(cg_entities[mEnt->data].currentState.powerups & (1 << PW_OPS_DISGUISED)))
	{
		return qfalse;
	}

	if (VectorDistance(cg.snap->ps.origin, cg_entities[mEnt->data].lerpOrigin) < 512)
	{
		return qfalse;
	}

	return qtrue;
}

/**
 * @brief CG_DrawMap
 * @param[in] x
 * @param[in] y
 * @param[in] w
 * @param[in] h
 * @param[in] mEntFilter
 * @param[in] scissor
 * @param[in] interactive
 * @param[in] alpha
 * @param[in] borderblend
 * @param[in] pointTowardNorth
 */
void CG_DrawMap(float x, float y, float w, float h, int mEntFilter, mapScissor_t *scissor, qboolean interactive, float alpha, qboolean borderblend, qboolean pointTowardNorth)
{
	int             i;
	snapshot_t      *snap;
	mapEntityData_t *mEnt;
	int             icon_size;
	int             exspawn;
	team_t          RealTeam = CG_LimboPanel_GetRealTeam();

	expanded = qfalse;

	if (cg.nextSnap && !cg.nextFrameTeleport && !cg.thisFrameTeleport)
	{
		snap = cg.nextSnap;
	}
	else
	{
		snap = cg.snap;
	}

	if (scissor)
	{
		if (cg_dynamicIcons.integer)
		{
			icon_size = cg_dynamicIconsSize.integer;
		}
		else
		{
			icon_size = CG_IsShoutcaster() ? AUTOMAP_PLAYER_ICON_SIZE_SC : AUTOMAP_PLAYER_ICON_SIZE;
		}

		if (scissor->br[0] >= scissor->tl[0])
		{
			float s0, s1, t0, t1;
			float sc_x = x, sc_y = y, sc_w = w, sc_h = h;

			if (scissor->circular)
			{
				CG_DrawPic(sc_x, sc_y, sc_w, sc_h, cgs.media.commandCentreAutomapMaskShader);
			}
			else
			{
				trap_R_SetColor((vec4_t) { 1.f, 1.f, 1.f, alpha });
				CG_DrawPic(sc_x, sc_y, sc_w, sc_h, cgs.media.blackmask);
				trap_R_SetColor(NULL);
			}

			s0 = (scissor->tl[0]) / (w * scissor->zoomFactor);
			s1 = (scissor->br[0]) / (w * scissor->zoomFactor);
			t0 = (scissor->tl[1]) / (h * scissor->zoomFactor);
			t1 = (scissor->br[1]) / (h * scissor->zoomFactor);

			CG_AdjustFrom640(&sc_x, &sc_y, &sc_w, &sc_h);

			if (pointTowardNorth)
			{
				trap_R_DrawStretchPic(sc_x, sc_y, sc_w, sc_h, s0, t0, s1, t1,
				                      cgs.media.commandCentreAutomapShader[cgs.ccLayers ? cgs.ccSelectedLayer : 0]);
			}
			else
			{
				trap_R_DrawRotatedPic(sc_x, sc_y, sc_w, sc_h, s0, t0, s1, t1,
				                      cgs.media.commandCentreAutomapShader[cgs.ccLayers ? cgs.ccSelectedLayer : 0],
				                      (cg.predictedPlayerState.viewangles[YAW] - 180.f - 45.f) / 360.f);
			}
		}
	}
	else
	{
		icon_size = COMMANDMAP_PLAYER_ICON_SIZE;

		trap_R_SetColor((vec4_t) { 1.f, 1.f, 1.f, alpha });
		CG_DrawPic(x, y, w, h, cgs.media.blackmask);

		CG_DrawPic(x, y, w, h, cgs.media.commandCentreMapShaderTrans[cgs.ccLayers ? cgs.ccSelectedLayer : 0]);

		trap_R_SetColor(NULL);

		// Draw the grid
		CG_DrawGrid(x, y, w, h, NULL);
	}

	if (borderblend)
	{
		trap_R_SetColor((vec4_t) { 0.f, 0.f, 0.f, 0.75f });
		CG_DrawPic(x, y, w, h, cgs.media.limboBlendThingy);
		trap_R_SetColor(NULL);
	}

	exspawn = CG_DrawSpawnPointInfo(x, y, w, h, qfalse, scissor, -1, pointTowardNorth);

	// entnfo data
	for (i = 0, mEnt = &mapEntities[0]; i < mapEntityCount; ++i, ++mEnt)
	{
		// spectators can see icons of both teams
		if ((interactive && mEnt->team != RealTeam) ||
		    (mEnt->team != snap->ps.persistant[PERS_TEAM] && snap->ps.persistant[PERS_TEAM] != TEAM_SPECTATOR))
		{
			if (mEnt->team != RealTeam && !CG_DisguiseMapCheck(mEnt))
			{
				continue;
			}

			if (mEnt->type != ME_PLAYER &&
			    mEnt->type != ME_PLAYER_DISGUISED &&
			    mEnt->type != ME_PLAYER_OBJECTIVE &&
			    mEnt->type != ME_PLAYER_REVIVE)
			{
				continue;
			}
		}

		CG_DrawMapEntity(mEnt, x, y, w, h, mEntFilter, scissor, interactive, snap, icon_size, pointTowardNorth);
	}

	// spawn point info
	CG_DrawSpawnPointInfo(x, y, w, h, qtrue, scissor, exspawn, pointTowardNorth);

	// mortar impact markers
	CG_DrawMortarMarker(x, y, w, h, qtrue, scissor, exspawn, pointTowardNorth);

	// entnfo players data
	for (i = 0, mEnt = &mapEntities[0]; i < mapEntityCount; ++i, ++mEnt)
	{
		if (mEnt->team != RealTeam && !CG_DisguiseMapCheck(mEnt) && !CG_IsShoutcaster())
		{
			continue;
		}

		if (mEnt->type != ME_PLAYER &&
		    mEnt->type != ME_PLAYER_DISGUISED &&
		    mEnt->type != ME_PLAYER_OBJECTIVE &&
		    mEnt->type != ME_PLAYER_REVIVE)
		{
			continue;
		}

		CG_DrawMapEntity(mEnt, x, y, w, h, mEntFilter, scissor, interactive, snap, icon_size, pointTowardNorth);
	}

	// draw spectator position and direction
	if (cgs.clientinfo[cg.clientNum].team == TEAM_SPECTATOR)
	{
		vec2_t icon_pos, icon_extends;

		icon_extends[0] = 2 * icon_size;
		icon_extends[1] = 2 * icon_size;

		if (scissor)
		{
			icon_extends[0] *= (scissor->zoomFactor / AUTOMAP_ZOOM);
			icon_extends[1] *= (scissor->zoomFactor / AUTOMAP_ZOOM);

			icon_pos[0] = ((cg.predictedPlayerEntity.lerpOrigin[0] - cg.mapcoordsMins[0]) * cg.mapcoordsScale[0]) * w * scissor->zoomFactor - scissor->tl[0] + x;
			icon_pos[1] = ((cg.predictedPlayerEntity.lerpOrigin[1] - cg.mapcoordsMins[1]) * cg.mapcoordsScale[1]) * h * scissor->zoomFactor - scissor->tl[1] + y;
		}
		else
		{
			icon_extends[0] *= cgs.ccZoomFactor;
			icon_extends[1] *= cgs.ccZoomFactor;

			icon_pos[0] = x + ((cg.predictedPlayerEntity.lerpOrigin[0] - cg.mapcoordsMins[0]) * cg.mapcoordsScale[0]) * w;
			icon_pos[1] = y + ((cg.predictedPlayerEntity.lerpOrigin[1] - cg.mapcoordsMins[1]) * cg.mapcoordsScale[1]) * h;
		}

		if (!pointTowardNorth)
		{
			CG_RotateMapEntCoordinatePoint(x, y, w, h, &icon_pos, cg.predictedPlayerState.viewangles[YAW] - 90);
		}

		if (snap->ps.persistant[PERS_TEAM] == TEAM_SPECTATOR)
		{
			// draw a arrow when free-spectating
			CG_DrawPic(icon_pos[0] - icon_extends[0] * 0.5f, icon_pos[1] - icon_extends[1] * 0.5f, icon_extends[0], icon_extends[1], cgs.media.cm_spec_icon);
			CG_DrawRotatedPic(icon_pos[0] - icon_extends[0] * 0.5f - 1, icon_pos[1] - icon_extends[1] * 0.5f - 1, icon_extends[0] + 2, icon_extends[1] + 2, cgs.media.cm_arrow_spec, (0.5f - (cg.predictedPlayerState.viewangles[YAW] - 180.f) / 360.f));
		}
		else
		{
			// show only current player position to spectators
			bg_playerclass_t *classInfo = CG_PlayerClassForClientinfo(&cgs.clientinfo[snap->ps.clientNum], &cg_entities[snap->ps.clientNum]);

			if (snap->ps.powerups[PW_OPS_DISGUISED])
			{
				CG_DrawPic(icon_pos[0] - icon_extends[0] * 0.5f, icon_pos[1] - icon_extends[1] * 0.5f, icon_extends[0], icon_extends[1], classInfo->icon);
				CG_DrawPic(icon_pos[0] - icon_extends[0] * 0.5f, icon_pos[1] - icon_extends[1] * 0.5f, icon_extends[0], icon_extends[1], cgs.media.friendShader);
			}
			else if (snap->ps.powerups[PW_REDFLAG] || snap->ps.powerups[PW_BLUEFLAG])
			{
				CG_DrawPic(icon_pos[0] - icon_extends[0] * 0.5f, icon_pos[1] - icon_extends[1] * 0.5f, icon_extends[0], icon_extends[1], cgs.media.objectiveShader);
			}
			else if (snap->ps.eFlags & EF_DEAD && !(snap->ps.powerups[PW_INVULNERABLE]))
			{
				vec4_t reviveClr = { 1.f, 1.f, 1.f, 1.f };

				if (snap->ps.persistant[PERS_TEAM] == TEAM_SPECTATOR || CG_IsShoutcaster())
				{
					float msec;

					if (cgs.clientinfo[cg.snap->ps.clientNum].team == TEAM_AXIS || (CG_IsShoutcaster() && mEnt->team == TEAM_AXIS))
					{
						msec = (cg_redlimbotime.integer - (cg.time % cg_redlimbotime.integer)) / (float)cg_redlimbotime.integer;
					}
					else if (cgs.clientinfo[cg.snap->ps.clientNum].team == TEAM_ALLIES || (CG_IsShoutcaster() && mEnt->team == TEAM_ALLIES))
					{
						msec = (cg_bluelimbotime.integer - (cg.time % cg_bluelimbotime.integer)) / (float)cg_bluelimbotime.integer;
					}
					else
					{
						msec = 0;
					}

					reviveClr[3] = .5f + .5f * (float)((sin(sqrt((double)msec) * 25 * M_TAU_F) + 1) * 0.5);
				}
				else
				{
					// following spectator can't guess reinforcement timer
					reviveClr[3] = .5f + fabs(sin(cg.time * 0.002)) * 0.5;
				}

				trap_R_SetColor(reviveClr);
				CG_DrawPic(icon_pos[0] - icon_extends[0] * 0.5f + 2, icon_pos[1] - icon_extends[1] * 0.5f + 2, icon_extends[0] - 2, icon_extends[1] - 2, cgs.media.ccMedicIcon);
				trap_R_SetColor(NULL);
				return;
			}
			else
			{
				CG_DrawPic(icon_pos[0] - icon_extends[0] * 0.5f, icon_pos[1] - icon_extends[1] * 0.5f, icon_extends[0], icon_extends[1], classInfo->icon);
			}

			if (pointTowardNorth)
			{
				CG_DrawRotatedPic(icon_pos[0] - icon_extends[0] * 0.5f - 1, icon_pos[1] - icon_extends[1] * 0.5f - 1, icon_extends[0] + 2, icon_extends[1] + 2,
				                  classInfo->arrow, (0.5f - (cg.predictedPlayerState.viewangles[YAW] - 180.f) / 360.f));
			}
			else
			{
				CG_DrawRotatedPic(icon_pos[0] - 1, icon_pos[1] - 1, icon_extends[0] + 2, icon_extends[1] + 2, classInfo->arrow,
				                  (0.5f + 90.f / 360.f));
			}
		}
	}
}

/**
 * @brief CG_DrawExpandedAutoMap
 */
void CG_DrawExpandedAutoMap(void)
{
	float b_x, b_y, b_w, b_h;
	float s1, t1, s2, t2;
	float x = Ccg_WideX(SCREEN_WIDTH) + 10.f;
	float y = 20.f;
	float w = CC_2D_W;
	float h = CC_2D_H;

	if (cgs.autoMapExpanded)
	{
		if (cg.time - cgs.autoMapExpandTime < cg_commandMapTime.value)
		{
			x -= ((cg.time - cgs.autoMapExpandTime) / cg_commandMapTime.value) * (w + 30.f);
		}
		else
		{
			x = Ccg_WideX(SCREEN_WIDTH) - w - 20.f;
		}
	}
	else
	{
		if (cg.time - cgs.autoMapExpandTime < cg_commandMapTime.value)
		{
			x = (Ccg_WideX(SCREEN_WIDTH) - w - 20.f) + ((cg.time - cgs.autoMapExpandTime) / cg_commandMapTime.value) * (w + 30.f);
		}
		else
		{
			return;
		}
	}

	CG_DrawMap(x, y, w, h, cgs.ccFilter, NULL, qfalse, .7f, qfalse, qtrue);

	// Draw the border

	// top left
	s1  = 0;
	t1  = 0;
	s2  = 1;
	t2  = 1;
	b_x = x - 8;
	b_y = y - 8;
	b_w = 8;
	b_h = 8;
	CG_AdjustFrom640(&b_x, &b_y, &b_w, &b_h);
	trap_R_DrawStretchPic(b_x, b_y, b_w, b_h, s1, t1, s2, t2, cgs.media.commandCentreAutomapCornerShader);

	// top
	s2  = w / 256.f;
	b_x = x;
	b_y = y - 8;
	b_w = w;
	b_h = 8;
	CG_AdjustFrom640(&b_x, &b_y, &b_w, &b_h);
	trap_R_DrawStretchPic(b_x, b_y, b_w, b_h, s1, t1, s2, t2, cgs.media.commandCentreAutomapBorderShader);

	// top right
	s1  = 1;
	t1  = 0;
	s2  = 0;
	t2  = 1;
	b_x = x + w;
	b_y = y - 8;
	b_w = 8;
	b_h = 8;
	CG_AdjustFrom640(&b_x, &b_y, &b_w, &b_h);
	trap_R_DrawStretchPic(b_x, b_y, b_w, b_h, s1, t1, s2, t2, cgs.media.commandCentreAutomapCornerShader);

	// right
	s1  = 1;
	t1  = h / 256.f;
	s2  = 0;
	t2  = 0;
	b_x = x + w;
	b_y = y;
	b_w = 8;
	b_h = h;
	CG_AdjustFrom640(&b_x, &b_y, &b_w, &b_h);
	trap_R_DrawStretchPic(b_x, b_y, b_w, b_h, s1, t1, s2, t2, cgs.media.commandCentreAutomapBorder2Shader);

	// bottom right
	s1  = 1;
	t1  = 1;
	s2  = 0;
	t2  = 0;
	b_x = x + w;
	b_y = y + h;
	b_w = 8;
	b_h = 8;
	CG_AdjustFrom640(&b_x, &b_y, &b_w, &b_h);
	trap_R_DrawStretchPic(b_x, b_y, b_w, b_h, s1, t1, s2, t2, cgs.media.commandCentreAutomapCornerShader);

	// bottom
	s1  = w / 256.f;
	b_x = x;
	b_y = y + h;
	b_w = w;
	b_h = 8;
	CG_AdjustFrom640(&b_x, &b_y, &b_w, &b_h);
	trap_R_DrawStretchPic(b_x, b_y, b_w, b_h, s1, t1, s2, t2, cgs.media.commandCentreAutomapBorderShader);

	// bottom left
	s1  = 0;
	t1  = 1;
	s2  = 1;
	t2  = 0;
	b_x = x - 8;
	b_y = y + h;
	b_w = 8;
	b_h = 8;
	CG_AdjustFrom640(&b_x, &b_y, &b_w, &b_h);
	trap_R_DrawStretchPic(b_x, b_y, b_w, b_h, s1, t1, s2, t2, cgs.media.commandCentreAutomapCornerShader);

	// left
	s1  = 0;
	t1  = 0;
	s2  = 1;
	t2  = h / 256.f;
	b_x = x - 8;
	b_y = y;
	b_w = 8;
	b_h = h;
	CG_AdjustFrom640(&b_x, &b_y, &b_w, &b_h);
	trap_R_DrawStretchPic(b_x, b_y, b_w, b_h, s1, t1, s2, t2, cgs.media.commandCentreAutomapBorder2Shader);
}

/**
 * @brief CG_DrawAutoMap
 * @param[in] x
 * @param[in] y
 * @param[in] w
 * @param[in] h
 */
void CG_DrawAutoMap(float basex, float basey, float basew, float baseh, int style)
{
	int          i;
	float        x;
	float        y;
	float        w;
	float        h;
	float        angle;
	float        diff;
	mapScissor_t mapScissor;
	vec2_t       automapTransformed;
	snapshot_t   *snap;

	if (cg.nextSnap && !cg.nextFrameTeleport && !cg.thisFrameTeleport)
	{
		snap = cg.nextSnap;
	}
	else
	{
		snap = cg.snap;
	}

	Com_Memset(&mapScissor, 0, sizeof(mapScissor));

	if (cgs.ccLayers)
	{
		cgs.ccSelectedLayer = CG_CurLayerForZ((int)cg.predictedPlayerEntity.lerpOrigin[2]);
	}

#ifdef FEATURE_EDV
	if (cgs.demoCamera.renderingFreeCam == qtrue || cgs.demoCamera.renderingWeaponCam == qtrue)
	{
		return;
	}
#endif

	diff = basew * 0.25f;
	x    = basex + (diff / 2);
	y    = basey + (diff / 2);
	w    = basew - diff;
	h    = baseh - diff;

	mapScissor.circular   = !(style & COMPASS_SQUARE);
	mapScissor.zoomFactor = cg_automapZoom.value;

	mapScissor.tl[0] = mapScissor.tl[1] = 0;
	mapScissor.br[0] = mapScissor.br[1] = -1;

	automapTransformed[0] = ((cg.predictedPlayerEntity.lerpOrigin[0] - cg.mapcoordsMins[0]) * cg.mapcoordsScale[0]) * w * mapScissor.zoomFactor;
	automapTransformed[1] = ((cg.predictedPlayerEntity.lerpOrigin[1] - cg.mapcoordsMins[1]) * cg.mapcoordsScale[1]) * h * mapScissor.zoomFactor;

	// update clip region (for next drawing). clip region has a size kAutomap_width x kAutomap_height
	// and it is after zooming is accounted for.

	// first try to center the clip region around the player. then make sure the region
	// stays within the world map.
	mapScissor.tl[0] = automapTransformed[0] - w / 2;
	if (mapScissor.tl[0] < 0)
	{
		mapScissor.tl[0] = 0;
	}
	mapScissor.br[0] = mapScissor.tl[0] + w;
	if (mapScissor.br[0] > (w * mapScissor.zoomFactor))
	{
		mapScissor.br[0] = w * mapScissor.zoomFactor;
		mapScissor.tl[0] = mapScissor.br[0] - w;
	}

	mapScissor.tl[1] = automapTransformed[1] - h / 2;
	if (mapScissor.tl[1] < 0)
	{
		mapScissor.tl[1] = 0;
	}
	mapScissor.br[1] = mapScissor.tl[1] + h;
	if (mapScissor.br[1] > (h * mapScissor.zoomFactor))
	{
		mapScissor.br[1] = h * mapScissor.zoomFactor;
		mapScissor.tl[1] = mapScissor.br[1] - h;
	}

	CG_DrawMap(x, y, w, h, cgs.ccFilter, &mapScissor, qfalse, 1.f, qfalse, style & COMPASS_POINT_TOWARD_NORTH);

	if (mapScissor.circular)
	{
		if (style & COMPASS_DECOR)
		{
			CG_DrawPic(x - w * 0.125, y - h * 0.125, w * 1.25, h * 1.25, cgs.media.compassShader);
		}
		else if (style & COMPASS_DIRECTION)
		{
			CG_DrawPic(x - w * 0.125, y - h * 0.125, w * 1.25, h * 1.25, cgs.media.compassDirectionShader);
		}

		if (style & COMPASS_CARDINAL_POINTS)
		{
			trap_R_SetColor(colorLtGrey);
			if (style & COMPASS_POINT_TOWARD_NORTH)
			{
				CG_DrawPic(x - w * 0.125, y - h * 0.125, w * 1.25, h * 1.25, style & COMPASS_DECOR ? cgs.media.compass2Shader : cgs.media.compassCircleTickShader);
			}
			else
			{
				static float lastangle  = 0;
				static float anglespeed = 0;

				angle       = (cg.predictedPlayerState.viewangles[YAW] + 180.f) / 360.f - (0.125f);
				diff        = AngleSubtract(angle * 360, lastangle * 360) / 360.f;
				anglespeed /= 1.08f;
				anglespeed += diff * 0.01f;
				if (Q_fabs(anglespeed) < 0.00001f)
				{
					anglespeed = 0;
				}
				lastangle += anglespeed;

				if (style & COMPASS_DECOR)
				{
					CG_DrawRotatedPic(x - w * 0.1275, y - h * 0.1275, w * 1.255, h * 1.255, cgs.media.compass2Shader, lastangle);
				}
				else
				{
					CG_DrawRotatedPic(x - w * 0.125, y - h * 0.125, w * 1.25, h * 1.25, cgs.media.compassCircleTickShader, lastangle);
				}
			}
			trap_R_SetColor(NULL);
		}
	}
	else if ((style & COMPASS_CARDINAL_POINTS))   // square map
	{
		float        centerX   = x + (w * .5f);
		float        centerY   = y + (h * .5f);
		float        textScale = (w / 100) * 0.18f;
		float        textHeight;
		float        offsetX            = (w / 100) * 3.f;
		float        offsetY            = (h / 100) * 3.f;
		fontHelper_t *font              = &cgs.media.limboFont2;
		const char   *cardinalPoints[8] = { "N", "NE", "E", "SE", "S", "SW", "W", "NW" };
		int          index              = 0;

		CG_DrawRect_FixedBorder(x - 0.75f, y - 0.75f, w + 1.5f, h + 1.5f, 2, colorLtGrey);

		if (!(style & COMPASS_POINT_TOWARD_NORTH))
		{
			float angle = -(cg.predictedPlayerState.viewangles[YAW] - 180) - 90;

			if (angle < 0)
			{
				angle += 360;
			}
			else if (angle > 360)
			{
				angle -= 360;
			}

			for (index = 0; index < 8; ++index)
			{
				if (angle < 22.5f + (index * 45))
				{
					break;
				}
			}

			if (index >= 8)
			{
				index -= 8;
			}
		}

		CG_Text_Paint_Centred_Ext(centerX, y - offsetY, textScale, textScale, colorLtGrey, cardinalPoints[index], 0, 0, ITEM_TEXTSTYLE_SHADOWED, font);

		index += 2;
		if (index >= 8)
		{
			index -= 8;
		}

		textHeight = (float)CG_Text_Height_Ext(cardinalPoints[index], textScale, 0, font);
		CG_Text_Paint_Ext(x + w + offsetX, centerY + (textHeight * .5f), textScale, textScale, colorLtGrey, cardinalPoints[index], 0, 0, ITEM_TEXTSTYLE_SHADOWED, font);

		index += 2;
		if (index >= 8)
		{
			index -= 8;
		}

		textHeight = (float)CG_Text_Height_Ext(cardinalPoints[index], textScale, 0, font);
		CG_Text_Paint_Centred_Ext(centerX, y + h + textHeight + offsetY, textScale, textScale, colorLtGrey, cardinalPoints[index], 0, 0, ITEM_TEXTSTYLE_SHADOWED, font);

		index += 2;
		if (index >= 8)
		{
			index -= 8;
		}

		textHeight = (float)CG_Text_Height_Ext(cardinalPoints[index], textScale, 0, font);
		CG_Text_Paint_RightAligned_Ext(x - offsetX, centerY + (textHeight * .5f), textScale, textScale, colorLtGrey, cardinalPoints[index], 0, 0, ITEM_TEXTSTYLE_SHADOWED, font);
	}

	for (i = 0; i < snap->numEntities; ++i)
	{
		centity_t *cent = &cg_entities[snap->entities[i].number];
		qhandle_t icon;

		// skip self
		if (cent->currentState.eType == ET_PLAYER && cent->currentState.clientNum == cg.clientNum)
		{
			continue;
		}

		icon = CG_GetCompassIcon(&snap->entities[i], qfalse, qtrue, CG_GetActiveHUD()->compass.style & COMPASS_PRIMARY_OBJECTIVES,
		                         CG_GetActiveHUD()->compass.style & COMPASS_SECONDARY_OBJECTIVES, CG_GetActiveHUD()->compass.style & COMPASS_ITEM,
		                         qtrue, NULL);

		if (icon)
		{
			CG_DrawCompassIcon(basex, basey, basew, baseh, cg.predictedPlayerState.origin, cent->lerpOrigin, icon, 1.f, 14, &mapScissor);

			// draw overlapping shader for disguised covops
			if (icon == cgs.media.friendShader)
			{
				CG_DrawCompassIcon(basex, basey, basew, baseh, cg.predictedPlayerState.origin, cent->lerpOrigin, cgs.media.buddyShader, 1.f, 14, &mapScissor);
			}
		}
	}
}

/**
 * @brief CG_DrawSpawnPointInfoFlag
 * @param[in] i
 * @param[in] size
 * @param[in] point
 */
static void CG_DrawSpawnPointInfoFlag(int i, float size, vec2_t point)
{
	// render flag shadow if spawn point is the one that is currently resolved
	if (i == cgs.ccResolvedSpawnPoint + 1)
	{
		float offsetSize = size * 1.3;

		trap_R_SetColor((vec4_t) { 0.5, 0.8, 0.1, 0.85 });
		CG_DrawPic(point[0] - FLAG_LEFTFRAC * offsetSize, point[1] - FLAG_TOPFRAC * offsetSize, offsetSize, offsetSize, cgs.media.commandCentreSpawnShadow);
		trap_R_SetColor(NULL);
	}

	CG_DrawPic(point[0] - FLAG_LEFTFRAC * size, point[1] - FLAG_TOPFRAC * size, size, size, cgs.media.commandCentreSpawnShader[cg.spawnTeams[i] == TEAM_AXIS ? 0 : 1]);
}

/**
 * @brief CG_DrawSpawnPointInfo
 * @param[in] px
 * @param[in] py
 * @param[in] pw
 * @param[in] ph
 * @param[in] draw
 * @param[in] scissor
 * @param[in] expand
 * @param[in] pointTowardNorth
 * @return
 */
int CG_DrawSpawnPointInfo(float px, float py, float pw, float ph, qboolean draw, mapScissor_t *scissor, int expand, qboolean pointTowardNorth)
{
	team_t team = CG_LimboPanel_GetRealTeam();
	char   buffer[64];
	vec2_t icon_extends;
	vec2_t point;
	float  changetime;
	int    i, width, e = -1;

	if (cgs.ccFilter & CC_FILTER_SPAWNS)
	{
		return -1;
	}

	for (i = 1; i < cg.spawnCount; i++)
	{
		changetime = 0;

		if (cg.spawnTeams_changeTime[i])
		{
			changetime = (cg.time - cg.spawnTeams_changeTime[i]);
			if (changetime > SPAWN_SIZEUPTIME || changetime < 0)
			{
				changetime = cg.spawnTeams_changeTime[i] = 0;
			}
		}

		if (!(cg.spawnTeams[i] & 0xF) ||
		    (cgs.clientinfo[cg.clientNum].team != TEAM_SPECTATOR && cg.spawnTeams[i] != team) ||
		    ((cg.spawnTeams[i] & 256) && changetime == 0.f))
		{
			continue;
		}

		if (cgs.ccLayers)
		{
			if (CG_CurLayerForZ((int)cg.spawnCoords[i][2]) != cgs.ccSelectedLayer)
			{
				break;
			}
		}

		if (scissor)
		{
			point[0] = ((cg.spawnCoordsUntransformed[i][0] - cg.mapcoordsMins[0]) * cg.mapcoordsScale[0]) * pw * scissor->zoomFactor;
			point[1] = ((cg.spawnCoordsUntransformed[i][1] - cg.mapcoordsMins[1]) * cg.mapcoordsScale[1]) * ph * scissor->zoomFactor;
		}
		else
		{
			point[0] = px + (((cg.spawnCoordsUntransformed[i][0] - cg.mapcoordsMins[0]) * cg.mapcoordsScale[0]) * pw);
			point[1] = py + (((cg.spawnCoordsUntransformed[i][1] - cg.mapcoordsMins[1]) * cg.mapcoordsScale[1]) * ph);
		}

		if (scissor && CG_IsShoutcaster())
		{
			icon_extends[0] = FLAGSIZE_NORMAL_SC;
			icon_extends[1] = FLAGSIZE_NORMAL_SC;
		}
		else
		{
			icon_extends[0] = FLAGSIZE_NORMAL;
			icon_extends[1] = FLAGSIZE_NORMAL;
		}

		if (scissor)
		{
			if (!CG_IsShoutcaster())
			{
				icon_extends[0] *= (scissor->zoomFactor / AUTOMAP_ZOOM);
				icon_extends[1] *= (scissor->zoomFactor / AUTOMAP_ZOOM);
			}
		}
		else
		{
			icon_extends[0] *= cgs.ccZoomFactor;
			icon_extends[1] *= cgs.ccZoomFactor;
		}

		if (scissor && CG_ScissorPointIsCulled(point, scissor, icon_extends))
		{
			continue;
		}

		if (scissor)
		{
			point[0] += px - scissor->tl[0];
			point[1] += py - scissor->tl[1];
		}

		if (!scissor || (scissor && !CG_IsShoutcaster()))
		{
			point[0] -= (icon_extends[0] * (39 / 128.f));
			point[1] += (icon_extends[1] * (31 / 128.f));
		}

		if (!pointTowardNorth)
		{
			CG_RotateMapEntCoordinatePoint(px, py, pw, ph, &point, cg.predictedPlayerState.viewangles[YAW] - 90);
		}

		if (changetime != 0.f)
		{
			if (draw)
			{
				float size;

				if (cg.spawnTeams[i] == team)
				{
					size = 20 * (changetime / SPAWN_SIZEUPTIME);
				}
				else
				{
					size = 20 * (1 - (changetime / SPAWN_SIZEUPTIME));
				}

				if (scissor)
				{
					size *= (scissor->zoomFactor / AUTOMAP_ZOOM);
				}
				else
				{
					size *= cgs.ccZoomFactor;
				}

				CG_DrawSpawnPointInfoFlag(i, size, point);
			}
		}
		else if ((draw && i == expand) || (!expanded && BG_RectContainsPoint(point[0] - FLAGSIZE_NORMAL * 0.5f, point[1] - FLAGSIZE_NORMAL * 0.5f, FLAGSIZE_NORMAL, FLAGSIZE_NORMAL, cgDC.cursorx, cgDC.cursory)))
		{
			if (draw)
			{
				float size = FLAGSIZE_EXPANDED;
				if (scissor && CG_IsShoutcaster())
				{
					size = FLAGSIZE_EXPANDED_SC;
				}

				if (scissor)
				{
					size *= (scissor->zoomFactor / AUTOMAP_ZOOM);
				}
				else
				{
					size *= cgs.ccZoomFactor;
				}

				CG_DrawSpawnPointInfoFlag(i, size, point);
			}
			else
			{
				if (!scissor)
				{
					float w;

					Com_sprintf(buffer, sizeof(buffer), "%s (%i)", cg.spawnPoints[i], cg.spawnPlayerCounts[i]);
					w = CG_Text_Width_Ext(buffer, 0.2f, 0, &cgs.media.limboFont2);
					CG_CommandMap_SetHighlightText(buffer, point[0] - (w * 0.5f), point[1] - 8);
				}

				e = i;
			}
		}
		else
		{
			if (draw)
			{
				float size = FLAGSIZE_NORMAL;
				if (scissor && CG_IsShoutcaster())
				{
					size = FLAGSIZE_NORMAL_SC;
				}

				if (scissor)
				{
					size *= (scissor->zoomFactor / AUTOMAP_ZOOM);
				}
				else
				{
					size *= cgs.ccZoomFactor;
				}

				CG_DrawSpawnPointInfoFlag(i, size, point);

				Com_sprintf(buffer, sizeof(buffer), "(%i)", cg.spawnPlayerCounts[i]);

				if (scissor && CG_IsShoutcaster())
				{
					// recalculate for player spawn count clipping
					point[0] = ((cg.spawnCoordsUntransformed[i][0] - cg.mapcoordsMins[0]) * cg.mapcoordsScale[0]) * pw * scissor->zoomFactor;
					point[1] = ((cg.spawnCoordsUntransformed[i][1] - cg.mapcoordsMins[1]) * cg.mapcoordsScale[1]) * ph * scissor->zoomFactor;

					width = CG_Text_Width_Ext(buffer, 0.15f, 0, &cgs.media.limboFont2);

					point[0] += 1.5f + scissor->zoomFactor + width;
					point[1] -= 5;

					if (CG_ScissorPointIsCulled(point, scissor, icon_extends))
					{
						continue;
					}

					point[0] += px - scissor->tl[0] - width;
					point[1] += py - scissor->tl[1] + 4;

					CG_Text_Paint_Ext(point[0], point[1], 0.15f, 0.15f, colorWhite, buffer, 0, 0, ITEM_TEXTSTYLE_SHADOWED, &cgs.media.limboFont2);
				}
				else if (!scissor)
				{
					CG_Text_Paint_Ext(point[0] + FLAGSIZE_NORMAL * 0.25f, point[1], 0.2f, 0.2f, colorWhite, buffer, 0, 0, ITEM_TEXTSTYLE_SHADOWED, &cgs.media.limboFont2);
				}
			}
		}
	}

	return e;
}

/**
 * @brief CG_DrawMortarMarker
 * @param[in] px
 * @param[in] py
 * @param[in] pw
 * @param[in] ph
 * @param[in] draw
 * @param[in] scissor
 * @param[in] expand
 * @param[in] pointTowardNorth
 */
void CG_DrawMortarMarker(float px, float py, float pw, float ph, qboolean draw, mapScissor_t *scissor, int expand, qboolean pointTowardNorth)
{
	vec2_t point;
	vec2_t icon_extends;
	int    i, fadeTime;

	if (CHECKBITWISE(GetWeaponTableData(cg.lastFiredWeapon)->type, WEAPON_TYPE_MORTAR | WEAPON_TYPE_SET) && cg.mortarImpactTime >= 0)
	{
		vec4_t color = { 1.f, 1.f, 1.f, 1.f };

		if (!(CHECKBITWISE(GetWeaponTableData(cg.snap->ps.weapon)->type, WEAPON_TYPE_MORTAR | WEAPON_TYPE_SET)))
		{
			cg.mortarImpactTime = 0;
		}
		else
		{
			if (scissor)
			{
				point[0] = ((cg.mortarImpactPos[0] - cg.mapcoordsMins[0]) * cg.mapcoordsScale[0]) * pw * scissor->zoomFactor;
				point[1] = ((cg.mortarImpactPos[1] - cg.mapcoordsMins[1]) * cg.mapcoordsScale[1]) * ph * scissor->zoomFactor;
			}
			else
			{
				point[0] = px + (((cg.mortarImpactPos[0] - cg.mapcoordsMins[0]) * cg.mapcoordsScale[0]) * pw);
				point[1] = py + (((cg.mortarImpactPos[1] - cg.mapcoordsMins[1]) * cg.mapcoordsScale[1]) * ph);
			}

			if (scissor && CG_IsShoutcaster())
			{
				icon_extends[0] = MARKSIZE_SC;
				icon_extends[1] = MARKSIZE_SC;
			}
			else
			{
				icon_extends[0] = MARKSIZE;
				icon_extends[1] = MARKSIZE;
			}

			if (scissor)
			{
				icon_extends[0] *= (scissor->zoomFactor / AUTOMAP_ZOOM);
				icon_extends[1] *= (scissor->zoomFactor / AUTOMAP_ZOOM);
			}
			else
			{
				icon_extends[0] *= cgs.ccZoomFactor;
				icon_extends[1] *= cgs.ccZoomFactor;
			}

			// don't return if the marker is culled, just don't draw it
			if (!(scissor && CG_ScissorPointIsCulled(point, scissor, icon_extends)))
			{
				if (scissor)
				{
					point[0] += px - scissor->tl[0];
					point[1] += py - scissor->tl[1];
				}

				if (cg.mortarImpactOutOfMap)
				{
					if (!scissor)
					{
						// near the edge of the map, fit into it
						if (point[0] + 8.f > (px + pw))
						{
							point[0] -= 8.f;
						}
						else if (point[0] - 8.f < px)
						{
							point[0] += 8.f;
						}

						if (point[1] + 8.f > (py + ph))
						{
							point[1] -= 8.f;
						}
						else if (point[1] - 8.f < py)
						{
							point[1] += 8.f;
						}
					}
					color[3] = .5f;
				}

				if (!pointTowardNorth)
				{
					CG_RotateMapEntCoordinatePoint(px, py, pw, ph, &point, cg.predictedPlayerState.viewangles[YAW] - 90);
				}

				trap_R_SetColor(color);
				CG_DrawRotatedPic(point[0] - 8.f, point[1] - 8.f, 16, 16, cgs.media.ccMortarHit, .5f - (cg.mortarFireAngles[YAW] /*- 180.f */ + 45.f) / 360.f);
				trap_R_SetColor(NULL);
			}
		}
	}

	// display arty target to all teammates
	for (i = 0; i < cgs.maxclients; i++)
	{
		vec4_t color = { 1.f, 1.f, 1.f, 1.f };

		fadeTime = cg.time - (cg.artilleryRequestTime[i] + 25000);

		if (fadeTime < 5000 && cg.artilleryRequestTime[i] > 0)
		{
			if (fadeTime > 0)
			{
				color[3] = 1.f - (fadeTime / 5000.f);
			}

			if (scissor)
			{
				point[0] = ((cg.artilleryRequestPos[i][0] - cg.mapcoordsMins[0]) * cg.mapcoordsScale[0]) * pw * scissor->zoomFactor;
				point[1] = ((cg.artilleryRequestPos[i][1] - cg.mapcoordsMins[1]) * cg.mapcoordsScale[1]) * ph * scissor->zoomFactor;
			}
			else
			{
				point[0] = px + (((cg.artilleryRequestPos[i][0] - cg.mapcoordsMins[0]) * cg.mapcoordsScale[0]) * pw);
				point[1] = py + (((cg.artilleryRequestPos[i][1] - cg.mapcoordsMins[1]) * cg.mapcoordsScale[1]) * ph);
			}

			if (scissor && CG_IsShoutcaster())
			{
				icon_extends[0] = MARKSIZE_SC;
				icon_extends[1] = MARKSIZE_SC;
			}
			else
			{
				icon_extends[0] = MARKSIZE;
				icon_extends[1] = MARKSIZE;
			}

			if (scissor)
			{
				icon_extends[0] *= (scissor->zoomFactor / AUTOMAP_ZOOM);
				icon_extends[1] *= (scissor->zoomFactor / AUTOMAP_ZOOM);
			}
			else
			{
				icon_extends[0] *= cgs.ccZoomFactor;
				icon_extends[1] *= cgs.ccZoomFactor;
			}

			// don't return if the marker is culled, just skip it (so we draw the rest, if any)
			if (scissor && CG_ScissorPointIsCulled(point, scissor, icon_extends))
			{
				continue;
			}

			if (scissor)
			{
				point[0] += px - scissor->tl[0];
				point[1] += py - scissor->tl[1];
			}

			if (!pointTowardNorth)
			{
				CG_RotateMapEntCoordinatePoint(px, py, pw, ph, &point, cg.predictedPlayerState.viewangles[YAW] - 90);
			}

			trap_R_SetColor(color);
			CG_DrawPic(point[0] - 8.f, point[1] - 8.f, 16, 16, cgs.media.ccMortarTarget);
			trap_R_SetColor(NULL);
		}
	}
}

/**
 * @brief CG_CommandCentreSpawnPointClick
 * @return
 */
qboolean CG_CommandCentreSpawnPointClick(void)
{
	int    i;
	vec2_t point;

	if (cgs.ccFilter & CC_FILTER_SPAWNS)
	{
		return qfalse;
	}

	for (i = 1; i < cg.spawnCount; i++)
	{
		if ((cgs.clientinfo[cg.clientNum].team != TEAM_SPECTATOR) && cg.spawnTeams[i] && cg.spawnTeams[i] != CG_LimboPanel_GetRealTeam())
		{
			continue;
		}

		if (cg.spawnTeams[i] & 256)
		{
			continue;
		}

		if (cgs.ccLayers)
		{
			if (CG_CurLayerForZ((int)cg.spawnCoords[i][2]) != cgs.ccSelectedLayer)
			{
				continue;
			}
		}

		point[0] = cg.spawnCoords[i][0];
		point[1] = cg.spawnCoords[i][1];

		if (BG_RectContainsPoint((point[0] - FLAGSIZE_NORMAL * 0.5f) + cgs.wideXoffset, point[1] - FLAGSIZE_NORMAL * 0.5f, FLAGSIZE_NORMAL, FLAGSIZE_NORMAL, cgDC.cursorx, cgDC.cursory))
		{
			trap_SendConsoleCommand(va("setspawnpt %i\n", i));
			cgs.ccSelectedSpawnPoint = i;
			cgs.ccRequestedObjective = -1;
			return qtrue;
		}
	}

	return qfalse;
}

char      cg_highlightText[256];
rectDef_t cg_highlightTextRect;

/**
 * @brief CG_CommandMap_SetHighlightText
 * @param[in] text
 * @param[in] x
 * @param[in] y
 */
void CG_CommandMap_SetHighlightText(const char *text, float x, float y)
{
	Q_strncpyz(cg_highlightText, text, sizeof(cg_highlightText));
	cg_highlightTextRect.x = x;
	cg_highlightTextRect.y = y;
	expanded               = qtrue;
}

/**
 * @brief CG_CommandMap_DrawHighlightText
 */
void CG_CommandMap_DrawHighlightText(void)
{
	CG_Text_Paint_Ext(cg_highlightTextRect.x, cg_highlightTextRect.y, 0.25f, 0.25f, colorWhite, cg_highlightText, 0, 0, ITEM_TEXTSTYLE_SHADOWED, &cgs.media.limboFont2);
	*cg_highlightText = '\0';
}

/**
* @brief CG_DrawCompassIcon
* @param[in] x
* @param[in] y
* @param[in] w
* @param[in] h
* @param[in] origin
* @param[in] dest
* @param[in] shader
*/
void CG_DrawCompassIcon(float x, float y, float w, float h, vec3_t origin, vec3_t dest, qhandle_t shader, float dstScale, float baseSize, mapScissor_t *scissor)
{
	float  iconx, icony, iconWidth, iconHeight, radius;
	float  angle, len, diff;
	vec3_t v1, angles;

	VectorCopy(dest, v1);
	VectorSubtract(origin, v1, v1);
	len = VectorLength(v1);
	VectorNormalize(v1);
	vectoangles(v1, angles);

	if (v1[0] == 0.f && v1[1] == 0.f && v1[2] == 0.f)
	{
		return;
	}

	angles[YAW] = AngleSubtract(cg.predictedPlayerState.viewangles[YAW], angles[YAW]);
	angle       = ((angles[YAW] + 180.f) / 360.f - (0.50f / 2.f)) * M_TAU_F;

	len        = 1 - MIN(1.f, len / 2000.f * dstScale);
	iconWidth  = baseSize * len + 8;
	iconHeight = baseSize * len + 8;

	if (scissor->circular)
	{
		w /= 2;
		h /= 2;

		iconx = x + w;
		icony = y + h;

		radius = (float)sqrt((w * w) + (h * h)) / 3.f * 2.f * 0.9f;

		iconx = iconx + ((float)cos(angle) * radius);
		icony = icony + ((float)sin(angle) * radius);

		iconx = iconx - (iconWidth - 4) / 2;
		icony = icony - (iconHeight - 4) / 2;
	}
	else
	{
		diff = w * 0.25f;
		x    = x + (diff / 2);
		y    = y + (diff / 2);
		w    = w - diff;
		h    = h - diff;

		iconx = x + (w / 2);
		icony = y + (h / 2);

		radius = (float)sqrt((w * w) + (h * h)) / 2.0f;

		iconx = iconx + ((float)cos(angle) * radius);
		icony = icony + ((float)sin(angle) * radius);

		iconWidth  *= ((scissor->zoomFactor + 2.5f) / AUTOMAP_ZOOM);
		iconHeight *= ((scissor->zoomFactor + 2.5f) / AUTOMAP_ZOOM);

		iconx -= iconWidth / 2;
		icony -= iconHeight / 2;

		// keep the icon from going outside map boundaries
		iconx = Com_Clamp(x, x + w - iconWidth, iconx);
		icony = Com_Clamp(y, y + h - iconHeight, icony);
	}

	CG_DrawPic(iconx, icony, iconWidth, iconHeight, shader);
}
