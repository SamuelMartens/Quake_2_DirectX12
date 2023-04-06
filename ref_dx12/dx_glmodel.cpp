#include "dx_glmodel.h"

#include "dx_app.h"

model_t	*loadmodel;
int		modfilelen;

void Mod_LoadSpriteModel(model_t *mod, void *buffer, GPUJobContext& context);
void Mod_LoadBrushModel(model_t *mod, void *buffer, GPUJobContext& context);
void Mod_LoadAliasModel(model_t *mod, void *buffer, GPUJobContext& context);

byte	mod_novis[MAX_MAP_LEAFS/8];

#define	MAX_MOD_KNOWN	512
model_t	mod_known[MAX_MOD_KNOWN];
int		mod_numknown;

// the inline * models from the current map are kept seperate
model_t	mod_inline[MAX_MOD_KNOWN];

int		registration_sequence;

//#TODO make sure this globals are filled in needed time, or even better - remove them
model_t* currentmodel;
Resource* r_notexture;
model_t* r_worldmodel;

PointLight* staticpointlights = NULL;
int staticpointlightsnum = 0;

#define	MAX_LBM_HEIGHT		480

#define POINT_LIGHTS_MAX_ENTITY_LIGHTS		2048
#define POINT_LIGHTS_MAX_BSP_TREE_DEPTH		32
#define POINT_LIGHTS_MAX_CLUSTERS			8192

#define	BLOCK_WIDTH		128
#define	BLOCK_HEIGHT	128

void BuildPolygonFromSurface(msurface_t *fa)
{
	int			i, lindex, lnumverts;
	medge_t		*pedges, *r_pedge;
	int			vertpage;
	float		*vec;
	float		s, t;
	glpoly_t	*poly;
	vec3_t		total;

	// reconstruct the polygon
	pedges = currentmodel->edges;
	lnumverts = fa->numedges;
	vertpage = 0;

	VectorClear(total);
	//
	// draw texture
	//
	poly = (glpoly_t*)Hunk_Alloc(sizeof(glpoly_t) + (lnumverts - 4) * VERTEXSIZE * sizeof(float));
	poly->next = fa->polys;
	poly->flags = fa->flags;
	fa->polys = poly;
	poly->numverts = lnumverts;

	for (i = 0; i < lnumverts; i++)
	{
		lindex = currentmodel->surfedges[fa->firstedge + i];

		if (lindex > 0)
		{
			r_pedge = &pedges[lindex];
			vec = currentmodel->vertexes[r_pedge->v[0]].position;
		}
		else
		{
			r_pedge = &pedges[-lindex];
			vec = currentmodel->vertexes[r_pedge->v[1]].position;
		}
		s = DotProduct(vec, fa->texinfo->vecs[0]) + fa->texinfo->vecs[0][3];
		s /= fa->texinfo->image->desc.width;

		t = DotProduct(vec, fa->texinfo->vecs[1]) + fa->texinfo->vecs[1][3];
		t /= fa->texinfo->image->desc.height;

		VectorAdd(total, vec, total);
		VectorCopy(vec, poly->verts[i]);
		poly->verts[i][3] = s;
		poly->verts[i][4] = t;

		//
		// lightmap texture coordinates
		//
		s = DotProduct(vec, fa->texinfo->vecs[0]) + fa->texinfo->vecs[0][3];
		s -= fa->texturemins[0];
		s += fa->light_s * 16;
		s += 8;
		s /= BLOCK_WIDTH * 16; //fa->texinfo->texture->width;

		t = DotProduct(vec, fa->texinfo->vecs[1]) + fa->texinfo->vecs[1][3];
		t -= fa->texturemins[1];
		t += fa->light_t * 16;
		t += 8;
		t /= BLOCK_HEIGHT * 16; //fa->texinfo->texture->height;

		poly->verts[i][5] = s;
		poly->verts[i][6] = t;
	}

	poly->numverts = lnumverts;

}

const PointLight* Mod_StaticPointLights(int* numlights)
{
	DX_ASSERT(staticpointlights != NULL && "Asking for empty static point lights");
	DX_ASSERT(numlights != NULL && "Argument for Mod_StaticPointLights is NULL");

	*numlights = staticpointlightsnum;

	return staticpointlights;
}

void Mod_AllocStaticPointLights()
{
	DX_ASSERT(staticpointlights == NULL && "Static point lights is not cleaned up");

	staticpointlights = new PointLight[POINT_LIGHTS_MAX_ENTITY_LIGHTS];
	staticpointlightsnum = 0;
}

void Mod_FreeStaticPointLights()
{
	DX_ASSERT(staticpointlights != NULL && "Trying to free static point lights array, but it is empty");

	delete[] (staticpointlights);

	staticpointlights = nullptr;
	staticpointlightsnum = 0;
}

qboolean SphereIntersectsAnySolidLeaf(vec3_t origin, float radius)
{
	mnode_t* node_stack[POINT_LIGHTS_MAX_BSP_TREE_DEPTH];
	int stack_size;
	mnode_t* node;
	float d;
	cplane_t* plane;
	model_t* model;

	stack_size = 0;
	model = r_worldmodel;

	node_stack[stack_size++] = model->nodes;

	while (stack_size > 0)
	{
		node = node_stack[--stack_size];

		if (node->contents != -1)
		{
			if (node->contents == CONTENTS_SOLID || ((mleaf_t*)node)->cluster == -1)
				return qTrue;
			else
				continue;
		}

		plane = node->plane;
		d = DotProduct(origin, plane->normal) - plane->dist;

		if (d > -radius)
		{
			if (stack_size < POINT_LIGHTS_MAX_BSP_TREE_DEPTH)
				node_stack[stack_size++] = node->children[0];
		}

		if (d < +radius)
		{
			if (stack_size < POINT_LIGHTS_MAX_BSP_TREE_DEPTH)
				node_stack[stack_size++] = node->children[1];
		}
	}

	return qFalse;
}

void EnsurePointLightDoesNotIntersectWalls(PointLight *pointlight)
{
	const refimport_t& ri = Renderer::Inst().GetRefImport();

	mleaf_t *leaf;
	
	DX_ASSERT(pointlight != NULL);

	vec3_t origin;
	origin[0] = pointlight->origin.x;
	origin[1] = pointlight->origin.y;
	origin[2] = pointlight->origin.z;

	leaf = Mod_PointInLeaf(origin, r_worldmodel);
	
	if (!leaf || leaf->contents == CONTENTS_SOLID || leaf->cluster == -1)
	{
		// I always hit this, and it doesn't seems to be that critical. Comment for now,
		// it might bite me later tho
		//char msg[] = "EnsureEntityLightDoesNotIntersectWalls: Entity's origin is within a wall.\n";
		//
		//ri.Sys_Error(PRINT_DEVELOPER, msg);
		pointlight->objectPhysicalRadius = 0;
		return;
	}

	/* If the entity intersects a solid wall then reduce it's radius by half repeatedly until either
		it becomes free or it becomes too small. */

	while (SphereIntersectsAnySolidLeaf(origin, pointlight->objectPhysicalRadius) && pointlight->objectPhysicalRadius > 1.0f / 8.0f)
	{
		pointlight->objectPhysicalRadius /= 2.0f;
	}
}

void ParseEntityVector(XMFLOAT4* vec, const char* str)
{
	DX_ASSERT(vec != NULL);
	DX_ASSERT(str != NULL);

	sscanf(str, "%f %f %f", &vec->x, &vec->y, &vec->z);
}

void BuildClusterListForPointLight(PointLight* pointlight)
{
	mnode_t* node_stack[POINT_LIGHTS_MAX_BSP_TREE_DEPTH];
	int i, stack_size;
	mnode_t* node;
	float d, r;
	cplane_t* plane;
	model_t* model;
	mleaf_t* leaf;
	short num_clusters;
	qboolean already_listed;

	DX_ASSERT(pointlight != NULL);

	stack_size = 0;
	r = pointlight->objectPhysicalRadius;
	model = r_worldmodel;
	num_clusters = 0;

	node_stack[stack_size++] = model->nodes;

	while (stack_size > 0)
	{
		node = node_stack[--stack_size];

		if (node->contents != -1)
		{
			leaf = (mleaf_t*)node;

			if (leaf->cluster == -1 || leaf->cluster >= POINT_LIGHTS_MAX_CLUSTERS)
				continue;

			already_listed = qFalse;

			for (i = 0; i < num_clusters; ++i)
				if (pointlight->clusters[i] == leaf->cluster)
				{
					already_listed = qTrue;
					break;
				}

			if (!already_listed && num_clusters < POINT_LIGHTS_MAX_ENTITY_LIGHTS)
				pointlight->clusters[num_clusters++] = leaf->cluster;

			continue;
		}

		plane = node->plane;

		vec3_t origin;
		origin[0] = pointlight->origin.x;
		origin[1] = pointlight->origin.y;
		origin[2] = pointlight->origin.z;

		d = DotProduct(origin, plane->normal) - plane->dist;

		if (d > -r)
		{
			if (stack_size < POINT_LIGHTS_MAX_BSP_TREE_DEPTH)
				node_stack[stack_size++] = node->children[0];
		}

		if (d < +r)
		{
			if (stack_size < POINT_LIGHTS_MAX_BSP_TREE_DEPTH)
				node_stack[stack_size++] = node->children[1];
		}
	}
}

char* ParseEntityDictionary(char* data, PointLight* pointlights, int* numlights)
{
	const refimport_t& ri = Renderer::Inst().GetRefImport();

	char keyname[256];
	const char* com_token;
	char classname[256];
	char origin[256];
	char color[256];
	char light[256];
	char style[256];

	classname[0] = 0;
	origin[0] = 0;
	color[0] = 0;
	light[0] = 0;
	style[0] = 0;

	/* go through all the dictionary pairs */
	while (1)
	{
		/* parse key */
		com_token = COM_Parse(&data);

		if (com_token[0] == '}')
		{
			break;
		}

		if (!data)
		{
			char msg[] = "ParseEntityDictionary: EOF without closing brace\n";

			ri.Sys_Error(ERR_DROP, msg);
		}

		Q_strlcpy(keyname, com_token, sizeof(keyname));

		/* parse value */
		com_token = COM_Parse(&data);

		if (!data)
		{
			char msg[] = "ParseEntityDictionary: EOF without closing brace\n";
			ri.Sys_Error(ERR_DROP, msg);
		}

		if (com_token[0] == '}')
		{
			char msg[] = "ParseEntityDictionary: closing brace without data\n";
			ri.Sys_Error(ERR_DROP, msg);
		}

		if (!Q_stricmp(keyname, "classname"))
		{
			Q_strlcpy(classname, com_token, sizeof(classname));
		}
		else if (!Q_stricmp(keyname, "origin"))
		{
			Q_strlcpy(origin, com_token, sizeof(origin));
		}
		else if (!Q_stricmp(keyname, "_color"))
		{
			Q_strlcpy(color, com_token, sizeof(color));
		}
		else if (!Q_stricmp(keyname, "_light") || !Q_stricmp(keyname, "light"))
		{
			Q_strlcpy(light, com_token, sizeof(light));
		}
		else if (!Q_stricmp(keyname, "_style") || !Q_stricmp(keyname, "style"))
		{
			Q_strlcpy(style, com_token, sizeof(style));
		}
	}

	if (!Q_stricmp(classname, "light") && *numlights < POINT_LIGHTS_MAX_ENTITY_LIGHTS)
	{
		PointLight* pointlight = pointlights + *numlights;
		(*numlights)++;

		ParseEntityVector(&pointlight->origin, origin);

		// Clear point light cluster data
		pointlight->clusters.clear();
		// Resize, with appropriate new values
		pointlight->clusters.resize(POINT_LIGHTS_MAX_CLUSTERS, Const::INVALID_INDEX);

		
		if (color[0])
		{
			ParseEntityVector(&pointlight->color, color);
		}
		else
		{
			pointlight->color.x = pointlight->color.y = pointlight->color.z = 1.0;
		}

		pointlight->intensity = atof(light);

		/* The default radius is set to stay within the QUAKED bounding box specified for lights in g_misc.c */
		pointlight->objectPhysicalRadius = 8;

		/* Enforce the same defaults and restrictions that qrad3 enforces. */

		if (pointlight->intensity == 0)
			pointlight->intensity = 300;
		
		EnsurePointLightDoesNotIntersectWalls(pointlight);
		BuildClusterListForPointLight(pointlight);
	}

	return data;
}

/* A lot of the code used for extraction of static light data is taken from 
* https://github.com/eddbiddulph/yquake2/tree/pathtracing
*/
void ParseStaticEntityLights(char* entitystring, PointLight* pointlight, int* numlights)
{
	const refimport_t& ri = Renderer::Inst().GetRefImport();

	const char* com_token;

	if (!entitystring)
	{
		return;
	}

	/* parse ents */
	while (1)
	{
		/* parse the opening brace */
		com_token = COM_Parse(&entitystring);

		if (!entitystring)
		{
			break;
		}

		if (com_token[0] != '{')
		{
			char msg[] = "ParseStaticEntityLights: found %s when expecting {\n";

			ri.Sys_Error(ERR_DROP, msg, com_token);
			return;
		}
		
		if (*numlights >= POINT_LIGHTS_MAX_ENTITY_LIGHTS)
			break;

		entitystring = ParseEntityDictionary(entitystring, pointlight, numlights);
	}

}

/*
===============
Mod_PointInLeaf
===============
*/
mleaf_t *Mod_PointInLeaf (vec3_t p, model_t *model)
{
	const refimport_t& ri = Renderer::Inst().GetRefImport();

	mnode_t		*node;
	float		d;
	cplane_t	*plane;
	
	if (!model || !model->nodes)
	{
		char msg[] = "Mod_PointInLeaf: bad model";
		ri.Sys_Error (ERR_DROP, msg);
	}

	node = model->nodes;
	while (1)
	{
		if (node->contents != -1)
			return (mleaf_t *)node;
		plane = node->plane;
		d = DotProduct (p,plane->normal) - plane->dist;
		if (d > 0)
			node = node->children[0];
		else
			node = node->children[1];
	}
	
	return NULL;	// never reached
}


/*
===================
Mod_DecompressVis
===================
*/
byte *Mod_DecompressVis (byte *in, model_t *model)
{
	static byte	decompressed[MAX_MAP_LEAFS/8];
	int		c;
	byte	*out;
	int		row;

	row = (model->vis->numclusters+7)>>3;	
	out = decompressed;

	if (!in)
	{	// no vis info, so make all visible
		while (row)
		{
			*out++ = 0xff;
			row--;
		}
		return decompressed;		
	}

	do
	{
		if (*in)
		{
			*out++ = *in++;
			continue;
		}
	
		c = in[1];
		in += 2;
		while (c)
		{
			*out++ = 0;
			c--;
		}
	} while (out - decompressed < row);
	
	return decompressed;
}

/*
==============
Mod_ClusterPVS
==============
*/
byte *Mod_ClusterPVS (int cluster, model_t *model)
{
	if (cluster == -1 || !model->vis)
		return mod_novis;
	return Mod_DecompressVis ( (byte *)model->vis + model->vis->bitofs[cluster][DVIS_PVS],
		model);
}


//===============================================================================

/*
================
Mod_Modellist_f
================
*/
void Mod_Modellist_f (void)
{
	int		i;
	model_t	*mod;
	int		total;

	const refimport_t& ri = Renderer::Inst().GetRefImport();

	total = 0;

	char loadMdlMsg[] = "Loaded models:\n";
	ri.Con_Printf (PRINT_ALL, loadMdlMsg);
	for (i=0, mod=mod_known ; i < mod_numknown ; i++, mod++)
	{
		if (!mod->name[0])
			continue;

		char msg[] = "%8i : %s\n";
		ri.Con_Printf (PRINT_ALL, msg, mod->extradatasize, mod->name);
		total += mod->extradatasize;
	}

	char totalResidentMsg[] = "Total resident: %i\n";
	ri.Con_Printf (PRINT_ALL, totalResidentMsg, total);
}

/*
===============
Mod_Init
===============
*/
void Mod_Init (void)
{
	memset (mod_novis, 0xff, sizeof(mod_novis));
}



/*
==================
Mod_ForName

Loads in a model for the given name
==================
*/

model_t * Mod_ForName(char *name, qboolean crash, GPUJobContext& context)
{
	model_t	*mod;
	unsigned *buf;
	int		i;

	const refimport_t& ri = Renderer::Inst().GetRefImport();


	if (!name[0])
	{
		char msg[] = "Mod_ForName: NULL name";
		ri.Sys_Error(ERR_DROP, msg);
	}

	//
	// inline models are grabbed only from world model
	//
	if (name[0] == '*')
	{
		i = atoi(name + 1);
		if (i < 1 || !r_worldmodel || i >= r_worldmodel->numsubmodels)
		{
			char msg[] = "bad inline model number";
			ri.Sys_Error(ERR_DROP, msg);
		}
		return &mod_inline[i];
	}

	//
	// search the currently loaded models
	//
	for (i = 0, mod = mod_known; i < mod_numknown; i++, mod++)
	{
		if (!mod->name[0])
			continue;
		if (!strcmp(mod->name, name))
			return mod;
	}

	//
	// find a free model slot spot
	//
	for (i = 0, mod = mod_known; i < mod_numknown; i++, mod++)
	{
		if (!mod->name[0])
			break;	// free spot
	}
	if (i == mod_numknown)
	{
		if (mod_numknown == MAX_MOD_KNOWN)
		{
			char msg[] = "mod_numknown == MAX_MOD_KNOWN";
			ri.Sys_Error(ERR_DROP, msg);
		}
		mod_numknown++;
	}
	strcpy(mod->name, name);

	//
	// load the file
	//
	modfilelen = ri.FS_LoadFile(mod->name, (void**)&buf);

	if (!buf)
	{
		if (crash)
		{
			char msg[] = "Mod_NumForName: %s not found";
			ri.Sys_Error(ERR_DROP, msg, mod->name);
		}
		memset(mod->name, 0, sizeof(mod->name));
		return NULL;
	}

	loadmodel = mod;

	//
	// fill it in
	//


	// call the appropriate loader
	switch (LittleLong(*(unsigned *)buf))
	{
	case IDALIASHEADER:
		loadmodel->extradata = Hunk_Begin(0x200000);
		Mod_LoadAliasModel(mod, buf, context);
		break;

	case IDSPRITEHEADER:
		//#TODO implement proper Sprite loading
		//loadmodel->extradata = Hunk_Begin (0x10000);
		//Mod_LoadSpriteModel (mod, buf);
		break;

	case IDBSPHEADER:
		loadmodel->extradata = Hunk_Begin(0x1000000);
		Mod_LoadBrushModel(mod, buf, context);
		break;

	default:
	{
		char msg[] = "Mod_NumForName: unknown filed for %s";
		ri.Sys_Error(ERR_DROP, msg, mod->name);
		break;
	}
	}

	loadmodel->extradatasize = Hunk_End();

	ri.FS_FreeFile(buf);

	return mod;
}

/*
===============================================================================

					BRUSHMODEL LOADING

===============================================================================
*/

byte	*mod_base;


char* Mod_LoadEntityString(lump_t* l, int* numentitychars)
{
	*numentitychars = l->filelen;
	char* entitystring = (char*)Hunk_Alloc(l->filelen);

	memcpy(entitystring, mod_base + l->fileofs, l->filelen);

	return entitystring;
}


/*
=================
Mod_LoadLighting
=================
*/
void Mod_LoadLighting (lump_t *l)
{
	if (!l->filelen)
	{
		loadmodel->lightdata = NULL;
		return;
	}
	loadmodel->lightdata = (byte*)Hunk_Alloc ( l->filelen);	
	memcpy (loadmodel->lightdata, mod_base + l->fileofs, l->filelen);
}


/*
=================
Mod_LoadVisibility
=================
*/
void Mod_LoadVisibility (lump_t *l)
{
	int		i;

	if (!l->filelen)
	{
		loadmodel->vis = NULL;
		return;
	}
	loadmodel->vis = (dvis_t*)Hunk_Alloc ( l->filelen);	
	loadmodel->vissize = l->filelen;

	memcpy (loadmodel->vis, mod_base + l->fileofs, l->filelen);

	loadmodel->vis->numclusters = LittleLong (loadmodel->vis->numclusters);
	for (i=0 ; i<loadmodel->vis->numclusters ; i++)
	{
		loadmodel->vis->bitofs[i][0] = LittleLong (loadmodel->vis->bitofs[i][0]);
		loadmodel->vis->bitofs[i][1] = LittleLong (loadmodel->vis->bitofs[i][1]);
	}
}


/*
=================
Mod_LoadVertexes
=================
*/
void Mod_LoadVertexes (lump_t *l)
{
	dvertex_t	*in;
	mvertex_t	*out;
	int			i, count;

	const refimport_t& ri = Renderer::Inst().GetRefImport();

	// Add offset of this lump to model base.
	in = (dvertex_t *)(mod_base + l->fileofs);
	if (l->filelen % sizeof(*in))
	{
		char msg[] = "MOD_LoadBmodel: funny lump size in %s";
		ri.Sys_Error (ERR_DROP, msg,loadmodel->name);
	}
	// Get count
	count = l->filelen / sizeof(*in);
	out = (mvertex_t*)Hunk_Alloc ( count*sizeof(*out));	
	// Global variable is used for result
	loadmodel->vertexes = out;
	loadmodel->numvertexes = count;

	for ( i=0 ; i<count ; i++, in++, out++)
	{
		out->position[0] = LittleFloat (in->point[0]);
		out->position[1] = LittleFloat (in->point[1]);
		out->position[2] = LittleFloat (in->point[2]);
	}
}

/*
=================
RadiusFromBounds
=================
*/
float RadiusFromBounds (vec3_t mins, vec3_t maxs)
{
	int		i;
	vec3_t	corner;

	for (i=0 ; i<3 ; i++)
	{
		corner[i] = fabs(mins[i]) > fabs(maxs[i]) ? fabs(mins[i]) : fabs(maxs[i]);
	}

	return VectorLength (corner);
}


/*
=================
Mod_LoadSubmodels
=================
*/
void Mod_LoadSubmodels (lump_t *l)
{
	dmodel_t	*in;
	mmodel_t	*out;
	int			i, j, count;

	const refimport_t& ri = Renderer::Inst().GetRefImport();

	in = (dmodel_t*)(mod_base + l->fileofs);
	if (l->filelen % sizeof(*in))
	{
		char msg[] = "MOD_LoadBmodel: funny lump size in %s";
		ri.Sys_Error (ERR_DROP, msg,loadmodel->name);
	}
	count = l->filelen / sizeof(*in);
	out = (mmodel_t*)Hunk_Alloc ( count*sizeof(*out));	

	loadmodel->submodels = out;
	loadmodel->numsubmodels = count;

	for ( i=0 ; i<count ; i++, in++, out++)
	{
		for (j=0 ; j<3 ; j++)
		{	// spread the mins / maxs by a pixel
			out->mins[j] = LittleFloat (in->mins[j]) - 1;
			out->maxs[j] = LittleFloat (in->maxs[j]) + 1;
			out->origin[j] = LittleFloat (in->origin[j]);
		}
		out->radius = RadiusFromBounds (out->mins, out->maxs);
		out->headnode = LittleLong (in->headnode);
		out->firstface = LittleLong (in->firstface);
		out->numfaces = LittleLong (in->numfaces);
	}
}

/*
=================
Mod_LoadEdges
=================
*/
void Mod_LoadEdges (lump_t *l)
{
	dedge_t *in;
	medge_t *out;
	int 	i, count;

	const refimport_t& ri = Renderer::Inst().GetRefImport();

	in = (dedge_t *)(mod_base + l->fileofs);
	if (l->filelen % sizeof(*in)) 
	{
		char msg[] = "MOD_LoadBmodel: funny lump size in %s";
		ri.Sys_Error (ERR_DROP, msg, loadmodel->name);
	}
	count = l->filelen / sizeof(*in);
	out = (medge_t*)Hunk_Alloc ( (count + 1) * sizeof(*out));	

	loadmodel->edges = out;
	loadmodel->numedges = count;

	for ( i=0 ; i<count ; i++, in++, out++)
	{
		out->v[0] = (unsigned short)LittleShort(in->v[0]);
		out->v[1] = (unsigned short)LittleShort(in->v[1]);
	}
}

/*
=================
Mod_LoadTexinfo
=================
*/

void Mod_LoadTexinfo(lump_t *l, GPUJobContext& context)
{
	texinfo_t *in;
	mtexinfo_t *out, *step;
	int 	i, j, count;
	char	name[MAX_QPATH];
	int		next;

	const refimport_t& ri = Renderer::Inst().GetRefImport();

	in = (texinfo_t *)(mod_base + l->fileofs);
	if (l->filelen % sizeof(*in))
	{
		char msg[] = "MOD_LoadBmodel: funny lump size in %s";
		ri.Sys_Error(ERR_DROP, msg, loadmodel->name);
	}
	count = l->filelen / sizeof(*in);
	out = (mtexinfo_t *)Hunk_Alloc(count * sizeof(*out));

	loadmodel->texinfo = out;
	loadmodel->numtexinfo = count;

	for (i = 0; i < count; i++, in++, out++)
	{
		for (j = 0; j < 8; j++)
			out->vecs[0][j] = LittleFloat(in->vecs[0][j]);

		out->flags = LittleLong(in->flags);
		next = LittleLong(in->nexttexinfo);
		if (next > 0)
			out->next = loadmodel->texinfo + next;
		else
			out->next = NULL;

		char texNameFormat[] = "textures/%s.wal";
		Com_sprintf(name, sizeof(name), texNameFormat, in->texture);

		out->image = ResourceManager::Inst().FindOrCreateResource(name, context, true);

		out->image->desc.surfaceProperties = SurfacePropertes{};

		out->image->desc.surfaceProperties->irradiance = in->value;
		out->image->desc.surfaceProperties->flags = out->flags;

		if (!out->image)
		{
			char msg[] = "Couldn't load %s\n";
			ri.Con_Printf(PRINT_ALL, msg, name);
			out->image = r_notexture;
		}
	}

	// count animation frames
	for (i = 0; i < count; i++)
	{
		out = &loadmodel->texinfo[i];
		out->numframes = 1;
		for (step = out->next; step && step != out; step = step->next)
			out->numframes++;
	}
}

/*
================
CalcSurfaceExtents

Fills in s->texturemins[] and s->extents[]
================
*/
void CalcSurfaceExtents (msurface_t *s)
{
	float	mins[2], maxs[2], val;
	int		i,j, e;
	mvertex_t	*v;
	mtexinfo_t	*tex;
	int		bmins[2], bmaxs[2];

	mins[0] = mins[1] = 999999;
	maxs[0] = maxs[1] = -99999;

	tex = s->texinfo;
	
	for (i=0 ; i<s->numedges ; i++)
	{
		e = loadmodel->surfedges[s->firstedge+i];
		if (e >= 0)
			v = &loadmodel->vertexes[loadmodel->edges[e].v[0]];
		else
			v = &loadmodel->vertexes[loadmodel->edges[-e].v[1]];
		
		for (j=0 ; j<2 ; j++)
		{
			val = v->position[0] * tex->vecs[j][0] + 
				v->position[1] * tex->vecs[j][1] +
				v->position[2] * tex->vecs[j][2] +
				tex->vecs[j][3];
			if (val < mins[j])
				mins[j] = val;
			if (val > maxs[j])
				maxs[j] = val;
		}
	}

	for (i=0 ; i<2 ; i++)
	{	
		bmins[i] = floor(mins[i]/16);
		bmaxs[i] = ceil(maxs[i]/16);

		s->texturemins[i] = bmins[i] * 16;
		s->extents[i] = (bmaxs[i] - bmins[i]) * 16;

//		if ( !(tex->flags & TEX_SPECIAL) && s->extents[i] > 512 /* 256 */ )
//			ri.Sys_Error (ERR_DROP, "Bad surface extents");
	}
}


/*
=================
Mod_LoadFaces
=================
*/
void Mod_LoadFaces (lump_t *l)
{
	dface_t		*in;
	msurface_t 	*out;
	int			i, count, surfnum;
	int			planenum, side;
	int			ti;

	const refimport_t& ri = Renderer::Inst().GetRefImport();

	in = (dface_t *)(mod_base + l->fileofs);
	if (l->filelen % sizeof(*in)) 
	{
		char msg[] = "MOD_LoadBmodel: funny lump size in %s";
		ri.Sys_Error (ERR_DROP, msg,loadmodel->name);
	}
	count = l->filelen / sizeof(*in);
	out = (msurface_t *)Hunk_Alloc ( count*sizeof(*out));	

	loadmodel->surfaces = out;
	loadmodel->numsurfaces = count;

	currentmodel = loadmodel;
	// Create lightmap texture 
	// I don't do any light mapping right now, so just, leave it as it is
	//GL_BeginBuildingLightmaps (loadmodel);

	for ( surfnum=0 ; surfnum<count ; surfnum++, in++, out++)
	{
		out->firstedge = LittleLong(in->firstedge);
		out->numedges = LittleShort(in->numedges);		
		out->flags = 0;
		out->polys = NULL;

		planenum = LittleShort(in->planenum);
		side = LittleShort(in->side);
		if (side)
			out->flags |= SURF_PLANEBACK;			

		out->plane = loadmodel->planes + planenum;

		ti = LittleShort (in->texinfo);
		if (ti < 0 || ti >= loadmodel->numtexinfo) 
		{
			char msg[] = "MOD_LoadBmodel: bad texinfo number";
			ri.Sys_Error (ERR_DROP, msg);
		}
		out->texinfo = loadmodel->texinfo + ti;

		CalcSurfaceExtents (out);
				
	// lighting info

		for (i=0 ; i<MAXLIGHTMAPS ; i++)
			out->styles[i] = in->styles[i];
		i = LittleLong(in->lightofs);
		if (i == -1)
			out->samples = NULL;
		else
			out->samples = loadmodel->lightdata + i;
		
	// set the drawing flags
		
		if (out->texinfo->flags & SURF_WARP)
		{
			out->flags |= SURF_DRAWTURB;
			for (i=0 ; i<2 ; i++)
			{
				out->extents[i] = 16384;
				out->texturemins[i] = -8192;
			}
			// This needed for warp effect on water and sky, and I am not gonna do
			// this, cause I can use tessellation instead of software renderer
			//GL_SubdivideSurface (out);	// cut up polygon for warps
		}

		// create light maps and polygons
		if (!(out->texinfo->flags & (SURF_SKY | SURF_TRANS33 | SURF_TRANS66 | SURF_WARP))) 
		{
			// Allocate block in lightmap texture, combine it with dynamic light
			// and do the rest of stuff to actually fill up previously created
			// texture with data.
			// I don't do any light mapping right now, so just, leave it as it is
			//GL_CreateSurfaceLightmap(out);
		}

		if (!(out->texinfo->flags & SURF_WARP)) 
		{
			BuildPolygonFromSurface(out);
		}

	}
	// Upload lightmap on GPU
	// I don't do any light mapping right now, so just, leave it as it is
	//GL_EndBuildingLightmaps ();
}


/*
=================
Mod_SetParent
=================
*/
void Mod_SetParent (mnode_t *node, mnode_t *parent)
{
	node->parent = parent;
	if (node->contents != -1)
		return;
	Mod_SetParent (node->children[0], node);
	Mod_SetParent (node->children[1], node);
}

/*
=================
Mod_LoadNodes
=================
*/
void Mod_LoadNodes (lump_t *l)
{
	int			i, j, count, p;
	dnode_t		*in;
	mnode_t 	*out;

	const refimport_t& ri = Renderer::Inst().GetRefImport();

	in = ( dnode_t*)(mod_base + l->fileofs);
	if (l->filelen % sizeof(*in))
	{
		char msg[] = "MOD_LoadBmodel: funny lump size in %s";
		ri.Sys_Error (ERR_DROP, msg, loadmodel->name);
	}
	count = l->filelen / sizeof(*in);
	out = (mnode_t*)Hunk_Alloc ( count*sizeof(*out));	

	loadmodel->nodes = out;
	loadmodel->numnodes = count;

	for ( i=0 ; i<count ; i++, in++, out++)
	{
		for (j=0 ; j<3 ; j++)
		{
			out->minmaxs[j] = LittleShort (in->mins[j]);
			out->minmaxs[3+j] = LittleShort (in->maxs[j]);
		}
	
		p = LittleLong(in->planenum);
		out->plane = loadmodel->planes + p;

		out->firstsurface = LittleShort (in->firstface);
		out->numsurfaces = LittleShort (in->numfaces);
		out->contents = -1;	// differentiate from leafs

		for (j=0 ; j<2 ; j++)
		{
			p = LittleLong (in->children[j]);
			if (p >= 0)
				out->children[j] = loadmodel->nodes + p;
			else
				out->children[j] = (mnode_t *)(loadmodel->leafs + (-1 - p));
		}
	}
	
	Mod_SetParent (loadmodel->nodes, NULL);	// sets nodes and leafs
}

/*
=================
Mod_LoadLeafs
=================
*/
void Mod_LoadLeafs (lump_t *l)
{
	dleaf_t 	*in;
	mleaf_t 	*out;
	int			i, j, count, p;
//	glpoly_t	*poly;

	const refimport_t& ri = Renderer::Inst().GetRefImport();

	in = (dleaf_t *)(mod_base + l->fileofs);
	if (l->filelen % sizeof(*in))
	{
		char msg[] = "MOD_LoadBmodel: funny lump size in %s";
		ri.Sys_Error (ERR_DROP, msg, loadmodel->name);
	}
	count = l->filelen / sizeof(*in);
	out = (mleaf_t *)Hunk_Alloc ( count*sizeof(*out));	

	loadmodel->leafs = out;
	loadmodel->numleafs = count;

	for ( i=0 ; i<count ; i++, in++, out++)
	{
		for (j=0 ; j<3 ; j++)
		{
			out->minmaxs[j] = LittleShort (in->mins[j]);
			out->minmaxs[3+j] = LittleShort (in->maxs[j]);
		}

		p = LittleLong(in->contents);
		out->contents = p;

		out->cluster = LittleShort(in->cluster);
		out->area = LittleShort(in->area);

		out->firstmarksurface = loadmodel->marksurfaces +
			LittleShort(in->firstleafface);
		out->nummarksurfaces = LittleShort(in->numleaffaces);
		
		// gl underwater warp
#if 0
		if (out->contents & (CONTENTS_WATER|CONTENTS_SLIME|CONTENTS_LAVA|CONTENTS_THINWATER) )
		{
			for (j=0 ; j<out->nummarksurfaces ; j++)
			{
				out->firstmarksurface[j]->flags |= SURF_UNDERWATER;
				for (poly = out->firstmarksurface[j]->polys ; poly ; poly=poly->next)
					poly->flags |= SURF_UNDERWATER;
			}
		}
#endif
	}	
}

/*
=================
Mod_LoadMarksurfaces
=================
*/
void Mod_LoadMarksurfaces (lump_t *l)
{	
	int		i, j, count;
	short		*in;
	msurface_t **out;
	
	const refimport_t& ri = Renderer::Inst().GetRefImport();

	in = (short *)(mod_base + l->fileofs);
	if (l->filelen % sizeof(*in)) 
	{
		char msg[] = "MOD_LoadBmodel: funny lump size in %s";
		ri.Sys_Error (ERR_DROP, msg, loadmodel->name);
	}
	count = l->filelen / sizeof(*in);
	out = (msurface_t **)Hunk_Alloc ( count*sizeof(*out));	

	loadmodel->marksurfaces = out;
	loadmodel->nummarksurfaces = count;

	for ( i=0 ; i<count ; i++)
	{
		j = LittleShort(in[i]);
		if (j < 0 || j >= loadmodel->numsurfaces) 
		{
			char msg[] = "Mod_ParseMarksurfaces: bad surface number";
			ri.Sys_Error (ERR_DROP, msg);
		}
		out[i] = loadmodel->surfaces + j;
	}
}

/*
=================
Mod_LoadSurfedges
=================
*/
void Mod_LoadSurfedges (lump_t *l)
{	
	int		i, count;
	int		*in, *out;
	
	const refimport_t& ri = Renderer::Inst().GetRefImport();

	in = (int *)(mod_base + l->fileofs);
	if (l->filelen % sizeof(*in)) 
	{
		char msg[] = "MOD_LoadBmodel: funny lump size in %s";
		ri.Sys_Error (ERR_DROP, msg, loadmodel->name);
	}
	count = l->filelen / sizeof(*in);
	if (count < 1 || count >= MAX_MAP_SURFEDGES) 
	{
		char msg[] = "MOD_LoadBmodel: bad surfedges count in %s: %i";
		ri.Sys_Error (ERR_DROP, msg,
		loadmodel->name, count);
	}

	out = (int *)Hunk_Alloc ( count*sizeof(*out));	

	loadmodel->surfedges = out;
	loadmodel->numsurfedges = count;

	for ( i=0 ; i<count ; i++)
		out[i] = LittleLong (in[i]);
}


/*
=================
Mod_LoadPlanes
=================
*/
void Mod_LoadPlanes (lump_t *l)
{
	int			i, j;
	cplane_t	*out;
	dplane_t 	*in;
	int			count;
	int			bits;
	
	const refimport_t& ri = Renderer::Inst().GetRefImport();

	in = (dplane_t *)(mod_base + l->fileofs);
	if (l->filelen % sizeof(*in)) 
	{
		char msg[] = "MOD_LoadBmodel: funny lump size in %s";
		ri.Sys_Error (ERR_DROP, msg, loadmodel->name);
	}
	count = l->filelen / sizeof(*in);
	out = (cplane_t *)Hunk_Alloc ( count*2*sizeof(*out));	
	
	loadmodel->planes = out;
	loadmodel->numplanes = count;

	for ( i=0 ; i<count ; i++, in++, out++)
	{
		bits = 0;
		for (j=0 ; j<3 ; j++)
		{
			out->normal[j] = LittleFloat (in->normal[j]);
			if (out->normal[j] < 0)
				bits |= 1<<j;
		}

		out->dist = LittleFloat (in->dist);
		out->type = LittleLong (in->type);
		out->signbits = bits;
	}
}

/*
=================
Mod_LoadBrushModel
=================
*/

void Mod_LoadBrushModel(model_t *mod, void *buffer, GPUJobContext& context)
{
	int			i;
	dheader_t	*header;
	mmodel_t 	*bm;

	const refimport_t& ri = Renderer::Inst().GetRefImport();

	loadmodel->type = mod_brush;
	if (loadmodel != mod_known)
	{
		char msg[] = "Loaded a brush model after the world";
		ri.Sys_Error(ERR_DROP, msg);
	}

	header = (dheader_t *)buffer;

	i = LittleLong(header->version);
	if (i != BSPVERSION)
	{
		char msg[] = "Mod_LoadBrushModel: %s has wrong version number (%i should be %i)";
		ri.Sys_Error(ERR_DROP, msg, mod->name, i, BSPVERSION);
	}

	// swap all the lumps
	mod_base = (byte *)header;

	for (i = 0; i < sizeof(dheader_t) / 4; i++)
		((int *)header)[i] = LittleLong(((int *)header)[i]);

	// load into heap
		// Very similar loading functions. 
		// Usual algorithm is:
		// 1) Find offset from beginning of the file
		// 2) Find count
		// 3) Iterate and load one by one vertex or surface, whatever
	Mod_LoadVertexes(&header->lumps[LUMP_VERTEXES]);
	Mod_LoadEdges(&header->lumps[LUMP_EDGES]);
	Mod_LoadSurfedges(&header->lumps[LUMP_SURFEDGES]);
	Mod_LoadLighting(&header->lumps[LUMP_LIGHTING]);
	Mod_LoadPlanes(&header->lumps[LUMP_PLANES]);
	Mod_LoadTexinfo(&header->lumps[LUMP_TEXINFO], context);
	Mod_LoadFaces(&header->lumps[LUMP_FACES]);
	Mod_LoadMarksurfaces(&header->lumps[LUMP_LEAFFACES]);
	Mod_LoadVisibility(&header->lumps[LUMP_VISIBILITY]);
	Mod_LoadLeafs(&header->lumps[LUMP_LEAFS]);
	Mod_LoadNodes(&header->lumps[LUMP_NODES]);
	Mod_LoadSubmodels(&header->lumps[LUMP_MODELS]);
	mod->numframes = 2;		// regular and alternate animation

//
// set up the submodels
//
	for (i = 0; i < mod->numsubmodels; i++)
	{
		model_t	*starmod;

		bm = &mod->submodels[i];
		starmod = &mod_inline[i];

		*starmod = *loadmodel;

		starmod->firstmodelsurface = bm->firstface;
		starmod->nummodelsurfaces = bm->numfaces;
		starmod->firstnode = bm->headnode;
		if (starmod->firstnode >= loadmodel->numnodes)
		{
			char msg[] = "Inline model %i has bad firstnode";
			ri.Sys_Error(ERR_DROP, msg, i);
		}

		VectorCopy(bm->maxs, starmod->maxs);
		VectorCopy(bm->mins, starmod->mins);
		starmod->radius = bm->radius;

		if (i == 0)
			*loadmodel = *starmod;

		starmod->numleafs = bm->visleafs;
	}

	r_worldmodel = mod;

	int entitiystringlength = 0;
	char* entitystring = Mod_LoadEntityString(&header->lumps[LUMP_ENTITIES], &entitiystringlength);

	Mod_AllocStaticPointLights();
	ParseStaticEntityLights(entitystring, staticpointlights, &staticpointlightsnum);

	Hunk_Free(entitystring);
}

/*
==============================================================================

ALIAS MODELS

==============================================================================
*/

/*
=================
Mod_LoadAliasModel
=================
*/

void Mod_LoadAliasModel(model_t *mod, void *buffer, GPUJobContext& context)
{
	int					i, j;
	dmdl_t				*pinmodel, *pheader;
	dstvert_t			*pinst, *poutst;
	dtriangle_t			*pintri, *pouttri;
	daliasframe_t		*pinframe, *poutframe;
	int					*pincmd, *poutcmd;
	int					version;

	const refimport_t& ri = Renderer::Inst().GetRefImport();

	pinmodel = (dmdl_t *)buffer;

	version = LittleLong(pinmodel->version);
	if (version != ALIAS_VERSION)
	{
		char msg[] = "%s has wrong version number (%i should be %i)";
		ri.Sys_Error(ERR_DROP, msg,
			mod->name, version, ALIAS_VERSION);
	}

	pheader = (dmdl_t *)Hunk_Alloc(LittleLong(pinmodel->ofs_end));

	// byte swap the header fields and sanity check
	for (i = 0; i < sizeof(dmdl_t) / 4; i++)
		((int *)pheader)[i] = LittleLong(((int *)buffer)[i]);

	if (pheader->skinheight > MAX_LBM_HEIGHT)
	{
		char msg[] = "model %s has a skin taller than %d";
		ri.Sys_Error(ERR_DROP, msg, mod->name,
			MAX_LBM_HEIGHT);
	}

	if (pheader->num_xyz <= 0)
	{
		char msg[] = "model %s has no vertices";
		ri.Sys_Error(ERR_DROP, msg, mod->name);
	}

	if (pheader->num_xyz > MAX_VERTS)
	{
		char msg[] = "model %s has too many vertices";
		ri.Sys_Error(ERR_DROP, msg, mod->name);
	}

	if (pheader->num_st <= 0)
	{
		char msg[] = "model %s has no st vertices";
		ri.Sys_Error(ERR_DROP, msg, mod->name);
	}

	if (pheader->num_tris <= 0)
	{
		char msg[] = "model %s has no triangles";
		ri.Sys_Error(ERR_DROP, msg, mod->name);
	}

	if (pheader->num_frames <= 0)
	{
		char msg[] = "model %s has no frames";
		ri.Sys_Error(ERR_DROP, msg, mod->name);
	}

	//
	// load base s and t vertices (not used in gl version)
	//
	pinst = (dstvert_t *)((byte *)pinmodel + pheader->ofs_st);
	poutst = (dstvert_t *)((byte *)pheader + pheader->ofs_st);

	for (i = 0; i < pheader->num_st; i++)
	{
		poutst[i].s = LittleShort(pinst[i].s);
		poutst[i].t = LittleShort(pinst[i].t);
	}

	//
	// load triangle lists
	//
	pintri = (dtriangle_t *)((byte *)pinmodel + pheader->ofs_tris);
	pouttri = (dtriangle_t *)((byte *)pheader + pheader->ofs_tris);

	for (i = 0; i < pheader->num_tris; i++)
	{
		for (j = 0; j < 3; j++)
		{
			pouttri[i].index_xyz[j] = LittleShort(pintri[i].index_xyz[j]);
			pouttri[i].index_st[j] = LittleShort(pintri[i].index_st[j]);
		}
	}

	//
	// load the frames
	//
	for (i = 0; i < pheader->num_frames; i++)
	{
		pinframe = (daliasframe_t *)((byte *)pinmodel
			+ pheader->ofs_frames + i * pheader->framesize);
		poutframe = (daliasframe_t *)((byte *)pheader
			+ pheader->ofs_frames + i * pheader->framesize);

		memcpy(poutframe->name, pinframe->name, sizeof(poutframe->name));
		for (j = 0; j < 3; j++)
		{
			poutframe->scale[j] = LittleFloat(pinframe->scale[j]);
			poutframe->translate[j] = LittleFloat(pinframe->translate[j]);
		}
		// verts are all 8 bit, so no swapping needed
		memcpy(poutframe->verts, pinframe->verts,
			pheader->num_xyz * sizeof(dtrivertx_t));

	}

	mod->type = mod_alias;

	//
	// load the glcmds
	//
	pincmd = (int *)((byte *)pinmodel + pheader->ofs_glcmds);
	poutcmd = (int *)((byte *)pheader + pheader->ofs_glcmds);
	for (i = 0; i < pheader->num_glcmds; i++)
		poutcmd[i] = LittleLong(pincmd[i]);


	// register all skins
	memcpy((char *)pheader + pheader->ofs_skins, (char *)pinmodel + pheader->ofs_skins,
		pheader->num_skins*MAX_SKINNAME);
	for (i = 0; i < pheader->num_skins; i++)
	{
		mod->skins[i] = ResourceManager::Inst().FindOrCreateResource(
			(char *)pheader + pheader->ofs_skins + i * MAX_SKINNAME, 
			context,
			false);
	}

	mod->mins[0] = -32;
	mod->mins[1] = -32;
	mod->mins[2] = -32;
	mod->maxs[0] = 32;
	mod->maxs[1] = 32;
	mod->maxs[2] = 32;
}

/*
==============================================================================

SPRITE MODELS

==============================================================================
*/

/*
=================
Mod_LoadSpriteModel
=================
*/

void Mod_LoadSpriteModel(model_t *mod, void *buffer, GPUJobContext& context)
{
	dsprite_t	*sprin, *sprout;
	int			i;

	const refimport_t& ri = Renderer::Inst().GetRefImport();

	sprin = (dsprite_t *)buffer;
	sprout = (dsprite_t *)Hunk_Alloc(modfilelen);

	sprout->ident = LittleLong(sprin->ident);
	sprout->version = LittleLong(sprin->version);
	sprout->numframes = LittleLong(sprin->numframes);

	if (sprout->version != SPRITE_VERSION)
	{
		char msg[] = "%s has wrong version number (%i should be %i)";
		ri.Sys_Error(ERR_DROP, msg,
			mod->name, sprout->version, SPRITE_VERSION);
	}

	if (sprout->numframes > MAX_MD2SKINS)
	{
		char msg[] = "%s has too many frames (%i > %i)";
		ri.Sys_Error(ERR_DROP, msg,
			mod->name, sprout->numframes, MAX_MD2SKINS);
	}

	// byte swap everything
	for (i = 0; i < sprout->numframes; i++)
	{
		sprout->frames[i].width = LittleLong(sprin->frames[i].width);
		sprout->frames[i].height = LittleLong(sprin->frames[i].height);
		sprout->frames[i].origin_x = LittleLong(sprin->frames[i].origin_x);
		sprout->frames[i].origin_y = LittleLong(sprin->frames[i].origin_y);
		memcpy(sprout->frames[i].name, sprin->frames[i].name, MAX_SKINNAME);

		mod->skins[i] = ResourceManager::Inst().FindOrCreateResource(sprout->frames[i].name, context, false);
	}

	mod->type = mod_sprite;
}

//=============================================================================

/*
================
Mod_Free
================
*/
void Mod_Free (model_t *mod)
{
	Hunk_Free (mod->extradata);
	memset (mod, 0, sizeof(*mod));
}

bool Node_IsLeaf(const mnode_t* node)
{
	return node->contents != -1;
}

bool Surf_IsEmpty(const msurface_t* surf)
{
	return surf->polys == nullptr;
}

/*
================
Mod_FreeAll
================
*/
void Mod_FreeAll (void)
{
	int		i;

	for (i=0 ; i<mod_numknown ; i++)
	{
		if (mod_known[i].extradatasize)
			Mod_Free (&mod_known[i]);
	}
}
