
#include "quakedef.h"

static int max_meshs;
static int max_batch;
static int max_verts; // always max_meshs * 3
#define TRANSDEPTHRES 4096

//#define FLOATCOLORS

//static cvar_t gl_mesh_maxtriangles = {0, "gl_mesh_maxtriangles", "21760"};
static cvar_t gl_mesh_maxtriangles = {0, "gl_mesh_maxtriangles", "8192"};
static cvar_t gl_mesh_batchtriangles = {0, "gl_mesh_batchtriangles", "1024"};
static cvar_t gl_mesh_merge = {0, "gl_mesh_merge", "1"};
#if FLOATCOLORS
static cvar_t gl_mesh_floatcolors = {0, "gl_mesh_floatcolors", "1"};
#endif
static cvar_t gl_mesh_dupetransverts = {0, "gl_mesh_dupetransverts", "0"};
static cvar_t gl_mesh_sorttransbymesh = {0, "gl_mesh_sorttransbymesh", "1"};

typedef struct buf_mesh_s
{
	//struct buf_mesh_s *next;
	int depthmask;
	int depthtest;
	int blendfunc1, blendfunc2;
	int textures[MAX_TEXTUREUNITS];
	float texturergbscale[MAX_TEXTUREUNITS];
	int firsttriangle;
	int triangles;
	int firstvert;
	int lastvert;
	struct buf_mesh_s *chain;
	struct buf_transtri_s *transchain;
	//struct buf_transtri_s **transchainpointer;
}
buf_mesh_t;

typedef struct buf_transtri_s
{
	struct buf_transtri_s *next;
	struct buf_transtri_s *meshsortchain;
	buf_mesh_t *mesh;
	int index[3];
}
buf_transtri_t;

typedef struct buf_tri_s
{
	int index[3];
}
buf_tri_t;

typedef struct
{
	float v[4];
}
buf_vertex_t;

#if FLOATCOLORS
typedef struct
{
	float c[4];
}
buf_fcolor_t;
#endif

typedef struct
{
	byte c[4];
}
buf_bcolor_t;

typedef struct
{
	float t[2];
}
buf_texcoord_t;

static float meshfarclip;
static int currentmesh, currenttriangle, currentvertex, backendunits, backendactive, meshmerge, transranout;
#if FLOATCOLORS
static int floatcolors;
#endif
static buf_mesh_t *buf_mesh;
static buf_tri_t *buf_tri;
static buf_vertex_t *buf_vertex;
#if FLOATCOLORS
static buf_fcolor_t *buf_fcolor;
#endif
static buf_bcolor_t *buf_bcolor;
static buf_texcoord_t *buf_texcoord[MAX_TEXTUREUNITS];

static int currenttransmesh, currenttransvertex, currenttranstriangle;
static buf_mesh_t *buf_transmesh;
static buf_transtri_t *buf_transtri;
static buf_transtri_t **buf_transtri_list;
static buf_vertex_t *buf_transvertex;
#if FLOATCOLORS
static buf_fcolor_t *buf_transfcolor;
#endif
static buf_bcolor_t *buf_transbcolor;
static buf_texcoord_t *buf_transtexcoord[MAX_TEXTUREUNITS];

static mempool_t *gl_backend_mempool;
static int resizingbuffers = false;

static void gl_backend_start(void)
{
	int i;

	max_verts = max_meshs * 3;

	if (!gl_backend_mempool)
		gl_backend_mempool = Mem_AllocPool("GL_Backend");

#define BACKENDALLOC(var, count, sizeofstruct)\
	{\
		var = Mem_Alloc(gl_backend_mempool, count * sizeof(sizeofstruct));\
		if (var == NULL)\
			Sys_Error("gl_backend_start: unable to allocate memory\n");\
		memset(var, 0, count * sizeof(sizeofstruct));\
	}

	BACKENDALLOC(buf_mesh, max_meshs, buf_mesh_t)
	BACKENDALLOC(buf_tri, max_meshs, buf_tri_t)
	BACKENDALLOC(buf_vertex, max_verts, buf_vertex_t)
#if FLOATCOLORS
	BACKENDALLOC(buf_fcolor, max_verts, buf_fcolor_t)
#endif
	BACKENDALLOC(buf_bcolor, max_verts, buf_bcolor_t)

	BACKENDALLOC(buf_transmesh, max_meshs, buf_mesh_t)
	BACKENDALLOC(buf_transtri, max_meshs, buf_transtri_t)
	BACKENDALLOC(buf_transtri_list, TRANSDEPTHRES, buf_transtri_t *)
	BACKENDALLOC(buf_transvertex, max_verts, buf_vertex_t)
#if FLOATCOLORS
	BACKENDALLOC(buf_transfcolor, max_verts, buf_fcolor_t)
#endif
	BACKENDALLOC(buf_transbcolor, max_verts, buf_bcolor_t)

	for (i = 0;i < MAX_TEXTUREUNITS;i++)
	{
		// only allocate as many texcoord arrays as we need
		if (i < gl_textureunits)
		{
			BACKENDALLOC(buf_texcoord[i], max_verts, buf_texcoord_t)
			BACKENDALLOC(buf_transtexcoord[i], max_verts, buf_texcoord_t)
		}
		else
		{
			buf_texcoord[i] = NULL;
			buf_transtexcoord[i] = NULL;
		}
	}
	backendunits = min(MAX_TEXTUREUNITS, gl_textureunits);
	backendactive = true;
}

static void gl_backend_shutdown(void)
{
	//int i;
	/*
#define BACKENDFREE(var)\
	if (var)\
	{\
		Mem_Free(var);\
		var = NULL;\
	}
	*/
	/*
#define BACKENDFREE(var) var = NULL;

	BACKENDFREE(buf_mesh)
	BACKENDFREE(buf_tri)
	BACKENDFREE(buf_vertex)
#if FLOATCOLORS
	BACKENDFREE(buf_fcolor)
#endif
	BACKENDFREE(buf_bcolor)

	BACKENDFREE(buf_transmesh)
	BACKENDFREE(buf_transtri)
	BACKENDFREE(buf_transtri_list)
	BACKENDFREE(buf_transvertex)
#if FLOATCOLORS
	BACKENDFREE(buf_transfcolor)
#endif
	BACKENDFREE(buf_transbcolor)

	for (i = 0;i < MAX_TEXTUREUNITS;i++)
	{
		BACKENDFREE(buf_texcoord[i])
		BACKENDFREE(buf_transtexcoord[i])
	}
	*/

	if (resizingbuffers)
		Mem_EmptyPool(gl_backend_mempool);
	else
		Mem_FreePool(&gl_backend_mempool);

	backendunits = 0;
	backendactive = false;
}

static void gl_backend_bufferchanges(int init)
{
	// 21760 is (65536 / 3) rounded off to a multiple of 128
	if (gl_mesh_maxtriangles.integer < 256)
		Cvar_SetValue("gl_mesh_maxtriangles", 256);
	if (gl_mesh_maxtriangles.integer > 21760)
		Cvar_SetValue("gl_mesh_maxtriangles", 21760);

	if (gl_mesh_batchtriangles.integer < 0)
		Cvar_SetValue("gl_mesh_batchtriangles", 0);
	if (gl_mesh_batchtriangles.integer > gl_mesh_maxtriangles.integer)
		Cvar_SetValue("gl_mesh_batchtriangles", gl_mesh_maxtriangles.integer);

	max_batch = gl_mesh_batchtriangles.integer;

	if (max_meshs != gl_mesh_maxtriangles.integer)
	{
		max_meshs = gl_mesh_maxtriangles.integer;

		if (!init)
		{
			resizingbuffers = true;
			gl_backend_shutdown();
			gl_backend_start();
			resizingbuffers = false;
		}
	}
}

float r_farclip, r_newfarclip;

static void gl_backend_newmap(void)
{
	r_farclip = r_newfarclip = 2048.0f;
}

int polyindexarray[768];

void gl_backend_init(void)
{
	int i;
	Cvar_RegisterVariable(&gl_mesh_maxtriangles);
	Cvar_RegisterVariable(&gl_mesh_batchtriangles);
	Cvar_RegisterVariable(&gl_mesh_merge);
#if FLOATCOLORS
	Cvar_RegisterVariable(&gl_mesh_floatcolors);
#endif
	Cvar_RegisterVariable(&gl_mesh_dupetransverts);
	Cvar_RegisterVariable(&gl_mesh_sorttransbymesh);
	R_RegisterModule("GL_Backend", gl_backend_start, gl_backend_shutdown, gl_backend_newmap);
	gl_backend_bufferchanges(true);
	for (i = 0;i < 256;i++)
	{
		polyindexarray[i*3+0] = 0;
		polyindexarray[i*3+1] = i + 1;
		polyindexarray[i*3+2] = i + 2;
	}
}

static float viewdist;

int c_meshs, c_meshtris, c_transmeshs, c_transtris;

// called at beginning of frame
void R_Mesh_Clear(void)
{
	if (!backendactive)
		Sys_Error("R_Mesh_Clear: called when backend is not active\n");

	gl_backend_bufferchanges(false);

	currentmesh = 0;
	currenttriangle = 0;
	currentvertex = 0;
	currenttransmesh = 0;
	currenttranstriangle = 0;
	currenttransvertex = 0;
	meshfarclip = 0;
	meshmerge = gl_mesh_merge.integer;
#if FLOATCOLORS
	floatcolors = gl_mesh_floatcolors.integer;
#endif
	transranout = false;
	viewdist = DotProduct(r_origin, vpn);

	c_meshs = 0;
	c_meshtris = 0;
	c_transmeshs = 0;
	c_transtris = 0;
}

#ifdef DEBUGGL
void GL_PrintError(int errornumber, char *filename, int linenumber)
{
	switch(errornumber)
	{
	case GL_INVALID_ENUM:
		Con_Printf("GL_INVALID_ENUM at %s:%i\n", filename, linenumber);
		break;
	case GL_INVALID_VALUE:
		Con_Printf("GL_INVALID_VALUE at %s:%i\n", filename, linenumber);
		break;
	case GL_INVALID_OPERATION:
		Con_Printf("GL_INVALID_OPERATION at %s:%i\n", filename, linenumber);
		break;
	case GL_STACK_OVERFLOW:
		Con_Printf("GL_STACK_OVERFLOW at %s:%i\n", filename, linenumber);
		break;
	case GL_STACK_UNDERFLOW:
		Con_Printf("GL_STACK_UNDERFLOW at %s:%i\n", filename, linenumber);
		break;
	case GL_OUT_OF_MEMORY:
		Con_Printf("GL_OUT_OF_MEMORY at %s:%i\n", filename, linenumber);
		break;
#ifdef GL_TABLE_TOO_LARGE
    case GL_TABLE_TOO_LARGE:
		Con_Printf("GL_TABLE_TOO_LARGE at %s:%i\n", filename, linenumber);
		break;
#endif
	default:
		Con_Printf("GL UNKNOWN (%i) at %s:%i\n", errornumber, filename, linenumber);
		break;
	}
}

int errornumber = 0;
#endif

// renders mesh buffers, called to flush buffers when full
void R_Mesh_Render(void)
{
	int i, k, blendfunc1, blendfunc2, blend, depthmask, depthtest, unit = 0, clientunit = 0, firsttriangle, triangles, firstvert, lastvert, texture[MAX_TEXTUREUNITS];
	float farclip, texturergbscale[MAX_TEXTUREUNITS];
	buf_mesh_t *mesh;
	if (!backendactive)
		Sys_Error("R_Mesh_Render: called when backend is not active\n");
	if (!currentmesh)
		return;

CHECKGLERROR

	// push out farclip based on vertices
	for (i = 0;i < currentvertex;i++)
	{
		farclip = DotProduct(buf_vertex[i].v, vpn);
		if (meshfarclip < farclip)
			meshfarclip = farclip;
	}

	farclip = meshfarclip + 256.0f - viewdist; // + 256 just to be safe

	// push out farclip for next frame
	if (farclip > r_newfarclip)
		r_newfarclip = ceil((farclip + 255) / 256) * 256 + 256;

	for (i = 0;i < backendunits;i++)
		texturergbscale[i] = 1;

	glEnable(GL_CULL_FACE);
CHECKGLERROR
	glCullFace(GL_FRONT);
CHECKGLERROR
	depthtest = true;
	glEnable(GL_DEPTH_TEST);
CHECKGLERROR
	blendfunc1 = GL_ONE;
	blendfunc2 = GL_ZERO;
	glBlendFunc(blendfunc1, blendfunc2);
CHECKGLERROR
	blend = 0;
	glDisable(GL_BLEND);
CHECKGLERROR
	depthmask = true;
	glDepthMask((GLboolean) depthmask);
CHECKGLERROR

	glVertexPointer(3, GL_FLOAT, sizeof(buf_vertex_t), buf_vertex);
CHECKGLERROR
	glEnableClientState(GL_VERTEX_ARRAY);
CHECKGLERROR
#if FLOATCOLORS
	if (floatcolors)
	{
		glColorPointer(4, GL_FLOAT, sizeof(buf_fcolor_t), buf_fcolor);
CHECKGLERROR
	}
	else
	{
#endif
		glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(buf_bcolor_t), buf_bcolor);
CHECKGLERROR
#if FLOATCOLORS
	}
#endif
	glEnableClientState(GL_COLOR_ARRAY);
CHECKGLERROR

	if (backendunits > 1)
	{
		for (i = 0;i < backendunits;i++)
		{
			qglActiveTexture(GL_TEXTURE0_ARB + (unit = i));
CHECKGLERROR
			glBindTexture(GL_TEXTURE_2D, (texture[i] = 0));
CHECKGLERROR
			glDisable(GL_TEXTURE_2D);
CHECKGLERROR
			if (gl_combine.integer)
			{
				glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE_ARB);
CHECKGLERROR
				glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_RGB_ARB, GL_MODULATE);
CHECKGLERROR
				glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE0_RGB_ARB, GL_TEXTURE);
CHECKGLERROR
				glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE1_RGB_ARB, GL_PREVIOUS_ARB);
CHECKGLERROR
				glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE2_RGB_ARB, GL_CONSTANT_ARB);
CHECKGLERROR
				glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND0_RGB_ARB, GL_SRC_COLOR);
CHECKGLERROR
				glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND1_RGB_ARB, GL_SRC_COLOR);
CHECKGLERROR
				glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND2_RGB_ARB, GL_SRC_ALPHA);
CHECKGLERROR
				glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_ALPHA_ARB, GL_MODULATE);
CHECKGLERROR
				glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE0_ALPHA_ARB, GL_TEXTURE);
CHECKGLERROR
				glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE1_ALPHA_ARB, GL_PREVIOUS_ARB);
CHECKGLERROR
				glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE2_ALPHA_ARB, GL_CONSTANT_ARB);
CHECKGLERROR
				glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND0_ALPHA_ARB, GL_SRC_ALPHA);
CHECKGLERROR
				glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND1_ALPHA_ARB, GL_SRC_ALPHA);
CHECKGLERROR
				glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND2_ALPHA_ARB, GL_SRC_ALPHA);
CHECKGLERROR
				glTexEnvf(GL_TEXTURE_ENV, GL_RGB_SCALE_ARB, 1.0f);
CHECKGLERROR
				glTexEnvf(GL_TEXTURE_ENV, GL_ALPHA_SCALE, 1.0f);
CHECKGLERROR
			}
			else
			{
				glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
CHECKGLERROR
			}

			qglClientActiveTexture(GL_TEXTURE0_ARB + (clientunit = i));
CHECKGLERROR
			glTexCoordPointer(2, GL_FLOAT, sizeof(buf_texcoord_t), buf_texcoord[i]);
CHECKGLERROR
			glEnableClientState(GL_TEXTURE_COORD_ARRAY);
CHECKGLERROR
		}
	}
	else
	{
		glBindTexture(GL_TEXTURE_2D, (texture[0] = 0));
CHECKGLERROR
		glDisable(GL_TEXTURE_2D);
CHECKGLERROR
		glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
CHECKGLERROR

		glTexCoordPointer(2, GL_FLOAT, sizeof(buf_texcoord_t), buf_texcoord[0]);
CHECKGLERROR
		glEnableClientState(GL_TEXTURE_COORD_ARRAY);
CHECKGLERROR
	}

	// lock as early as possible
	GL_LockArray(0, currentvertex);
CHECKGLERROR

	for (k = 0;k < currentmesh;)
	{
		mesh = &buf_mesh[k];

		if (backendunits > 1)
		{
//			int topunit = 0;
			for (i = 0;i < backendunits;i++)
			{
				if (texture[i] != mesh->textures[i])
				{
					if (unit != i)
					{
						qglActiveTexture(GL_TEXTURE0_ARB + (unit = i));
CHECKGLERROR
					}
					if (texture[i] == 0)
					{
						glEnable(GL_TEXTURE_2D);
CHECKGLERROR
						// have to disable texcoord array on disabled texture
						// units due to NVIDIA driver bug with
						// compiled_vertex_array
						if (clientunit != i)
						{
							qglClientActiveTexture(GL_TEXTURE0_ARB + (clientunit = i));
CHECKGLERROR
						}
						glEnableClientState(GL_TEXTURE_COORD_ARRAY);
CHECKGLERROR
					}
					glBindTexture(GL_TEXTURE_2D, (texture[i] = mesh->textures[i]));
CHECKGLERROR
					if (texture[i] == 0)
					{
						glDisable(GL_TEXTURE_2D);
CHECKGLERROR
						// have to disable texcoord array on disabled texture
						// units due to NVIDIA driver bug with
						// compiled_vertex_array
						if (clientunit != i)
						{
							qglClientActiveTexture(GL_TEXTURE0_ARB + (clientunit = i));
CHECKGLERROR
						}
						glDisableClientState(GL_TEXTURE_COORD_ARRAY);
CHECKGLERROR
					}
				}
				if (texturergbscale[i] != mesh->texturergbscale[i])
				{
					if (unit != i)
					{
						qglActiveTexture(GL_TEXTURE0_ARB + (unit = i));
CHECKGLERROR
					}
					glTexEnvf(GL_TEXTURE_ENV, GL_RGB_SCALE_ARB, (texturergbscale[i] = mesh->texturergbscale[i]));
CHECKGLERROR
				}
//				if (texture[i])
//					topunit = i;
			}
//			if (unit != topunit)
//			{
//				qglActiveTexture(GL_TEXTURE0_ARB + (unit = topunit));
//CHECKGLERROR
//			}
		}
		else
		{
			if (texture[0] != mesh->textures[0])
			{
				if (texture[0] == 0)
				{
					glEnable(GL_TEXTURE_2D);
CHECKGLERROR
					glEnableClientState(GL_TEXTURE_COORD_ARRAY);
CHECKGLERROR
				}
				glBindTexture(GL_TEXTURE_2D, (texture[0] = mesh->textures[0]));
CHECKGLERROR
				if (texture[0] == 0)
				{
					glDisable(GL_TEXTURE_2D);
CHECKGLERROR
					glDisableClientState(GL_TEXTURE_COORD_ARRAY);
CHECKGLERROR
				}
			}
		}
		if (blendfunc1 != mesh->blendfunc1 || blendfunc2 != mesh->blendfunc2)
		{
			blendfunc1 = mesh->blendfunc1;
			blendfunc2 = mesh->blendfunc2;
			glBlendFunc(blendfunc1, blendfunc2);
CHECKGLERROR
			if (blendfunc2 == GL_ZERO)
			{
				if (blendfunc1 == GL_ONE)
				{
					if (blend)
					{
						blend = 0;
						glDisable(GL_BLEND);
CHECKGLERROR
					}
				}
				else
				{
					if (!blend)
					{
						blend = 1;
						glEnable(GL_BLEND);
CHECKGLERROR
					}
				}
			}
			else
			{
				if (!blend)
				{
					blend = 1;
					glEnable(GL_BLEND);
CHECKGLERROR
				}
			}
		}
		if (depthtest != mesh->depthtest)
		{
			depthtest = mesh->depthtest;
			if (depthtest)
				glEnable(GL_DEPTH_TEST);
			else
				glDisable(GL_DEPTH_TEST);
		}
		if (depthmask != mesh->depthmask)
		{
			depthmask = mesh->depthmask;
			glDepthMask((GLboolean) depthmask);
CHECKGLERROR
		}

		firsttriangle = mesh->firsttriangle;
		triangles = mesh->triangles;
		firstvert = mesh->firstvert;
		lastvert = mesh->lastvert;
		mesh = &buf_mesh[++k];

		if (meshmerge)
		{
			#if MAX_TEXTUREUNITS != 4
			#error update this code
			#endif
			while (k < currentmesh
				&& mesh->blendfunc1 == blendfunc1
				&& mesh->blendfunc2 == blendfunc2
				&& mesh->depthtest == depthtest
				&& mesh->depthmask == depthmask
				&& mesh->textures[0] == texture[0]
				&& mesh->textures[1] == texture[1]
				&& mesh->textures[2] == texture[2]
				&& mesh->textures[3] == texture[3]
				&& mesh->texturergbscale[0] == texturergbscale[0]
				&& mesh->texturergbscale[1] == texturergbscale[1]
				&& mesh->texturergbscale[2] == texturergbscale[2]
				&& mesh->texturergbscale[3] == texturergbscale[3])
			{
				triangles += mesh->triangles;
				if (firstvert > mesh->firstvert)
					firstvert = mesh->firstvert;
				if (lastvert < mesh->lastvert)
					lastvert = mesh->lastvert;
				mesh = &buf_mesh[++k];
			}
		}

#ifdef WIN32
		// FIXME: dynamic link to GL so we can get DrawRangeElements on WIN32
		glDrawElements(GL_TRIANGLES, triangles * 3, GL_UNSIGNED_INT, (unsigned int *)&buf_tri[firsttriangle]);
#else
		glDrawRangeElements(GL_TRIANGLES, firstvert, lastvert + 1, triangles * 3, GL_UNSIGNED_INT, (unsigned int *)&buf_tri[firsttriangle]);
#endif
CHECKGLERROR
	}

	currentmesh = 0;
	currenttriangle = 0;
	currentvertex = 0;

	GL_UnlockArray();
CHECKGLERROR

	if (backendunits > 1)
	{
		for (i = backendunits - 1;i >= 0;i--)
		{
			qglActiveTexture(GL_TEXTURE0_ARB + (unit = i));
CHECKGLERROR
			glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
CHECKGLERROR
			if (gl_combine.integer)
			{
				glTexEnvf(GL_TEXTURE_ENV, GL_RGB_SCALE_ARB, 1.0f);
CHECKGLERROR
			}
			if (i > 0)
			{
				glDisable(GL_TEXTURE_2D);
CHECKGLERROR
			}
			else
			{
				glEnable(GL_TEXTURE_2D);
CHECKGLERROR
			}
			glBindTexture(GL_TEXTURE_2D, 0);
CHECKGLERROR

			qglClientActiveTexture(GL_TEXTURE0_ARB + (clientunit = i));
CHECKGLERROR
			glDisableClientState(GL_TEXTURE_COORD_ARRAY);
CHECKGLERROR
		}
	}
	else
	{
		glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
CHECKGLERROR
		glEnable(GL_TEXTURE_2D);
CHECKGLERROR
		glDisableClientState(GL_TEXTURE_COORD_ARRAY);
CHECKGLERROR
	}
	glDisableClientState(GL_COLOR_ARRAY);
CHECKGLERROR
	glDisableClientState(GL_VERTEX_ARRAY);
CHECKGLERROR

	glDisable(GL_BLEND);
CHECKGLERROR
	glEnable(GL_DEPTH_TEST);
CHECKGLERROR
	glDepthMask(true);
CHECKGLERROR
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
CHECKGLERROR
}

void R_Mesh_AddTransparent(void)
{
	if (gl_mesh_sorttransbymesh.integer)
	{
		int i, j, k;
		float viewdistcompare, centerscaler, dist1, dist2, dist3, center, maxdist;
		buf_vertex_t *vert1, *vert2, *vert3;
		buf_transtri_t *tri;
		buf_mesh_t *mesh, *transmesh;

		// process and add transparent mesh triangles
		if (!currenttranstriangle)
			return;

		// map farclip to 0-4095 list range
		centerscaler = (TRANSDEPTHRES / r_farclip) * (1.0f / 3.0f);
		viewdistcompare = viewdist + 4.0f;

		memset(buf_transtri_list, 0, TRANSDEPTHRES * sizeof(buf_transtri_t *));

		k = 0;
		for (j = 0;j < currenttranstriangle;j++)
		{
			tri = &buf_transtri[j];

			vert1 = &buf_transvertex[tri->index[0]];
			vert2 = &buf_transvertex[tri->index[1]];
			vert3 = &buf_transvertex[tri->index[2]];

			dist1 = DotProduct(vert1->v, vpn);
			dist2 = DotProduct(vert2->v, vpn);
			dist3 = DotProduct(vert3->v, vpn);

			maxdist = max(dist1, max(dist2, dist3));
			if (maxdist < viewdistcompare)
				continue;

			center = (dist1 + dist2 + dist3) * centerscaler - viewdist;
	#if SLOWMATH
			i = (int) center;
			i = bound(0, i, (TRANSDEPTHRES - 1));
	#else
			if (center < 0.0f)
				center = 0.0f;
			center += 8388608.0f;
			i = *((long *)&center) & 0x7FFFFF;
			i = min(i, (TRANSDEPTHRES - 1));
	#endif
			tri->next = buf_transtri_list[i];
			buf_transtri_list[i] = tri;
			k++;
		}

		#ifndef TRANSBATCH
		if (currentmesh + k > max_meshs || currenttriangle + k > max_batch || currentvertex + currenttransvertex > max_verts)
			R_Mesh_Render();

		// note: can't batch these because they can be rendered in any order
		// there can never be more transparent triangles than fit in main buffers
		memcpy(&buf_vertex[currentvertex], &buf_transvertex[0], currenttransvertex * sizeof(buf_vertex_t));
#if FLOATCOLORS
		if (floatcolors)
			memcpy(&buf_fcolor[currentvertex], &buf_transfcolor[0], currenttransvertex * sizeof(buf_fcolor_t));
		else
#endif
			memcpy(&buf_bcolor[currentvertex], &buf_transbcolor[0], currenttransvertex * sizeof(buf_bcolor_t));
		for (i = 0;i < backendunits;i++)
			memcpy(&buf_texcoord[i][currentvertex], &buf_transtexcoord[i][0], currenttransvertex * sizeof(buf_texcoord_t));
		#endif

		for (i = 0;i < currenttransmesh;i++)
		{
			buf_transmesh[i].transchain = NULL;
			//buf_transmesh[i].transchainpointer = &buf_transmesh[i].transchain;
			buf_transmesh[i].triangles = 0;
		}
		transmesh = NULL;
		for (j = 0;j < TRANSDEPTHRES;j++)
		{
			if ((tri = buf_transtri_list[j]))
			{
				for (;tri;tri = tri->next)
				{
					if (!tri->mesh->transchain)
					{
						tri->mesh->chain = transmesh;
						transmesh = tri->mesh;
					}
					tri->meshsortchain = tri->mesh->transchain;
					tri->mesh->transchain = tri;
					/*
					*tri->mesh->transchainpointer = tri;
					tri->meshsortchain = NULL;
					tri->mesh->transchainpointer = &tri->meshsortchain;
					*/
					tri->mesh->triangles++;
				}
			}
		}

		#if TRANSBATCH
		for (;transmesh;transmesh = transmesh->chain)
		{
			int meshvertexadjust;
			int numverts = transmesh->lastvert - transmesh->firstvert + 1;
			if (currentmesh >= max_meshs || currenttriangle + transmesh->triangles > max_batch || currentvertex + numverts > max_verts)
				R_Mesh_Render();

			memcpy(&buf_vertex[currentvertex], &buf_transvertex[transmesh->firstvert], numverts * sizeof(buf_vertex_t));
#if FLOATCOLORS
			if (floatcolors)
				memcpy(&buf_fcolor[currentvertex], &buf_transfcolor[transmesh->firstvert], numverts * sizeof(buf_fcolor_t));
			else
#endif
				memcpy(&buf_bcolor[currentvertex], &buf_transbcolor[transmesh->firstvert], numverts * sizeof(buf_bcolor_t));
			for (i = 0;i < backendunits && transmesh->textures[i];i++)
				memcpy(&buf_texcoord[i][currentvertex], &buf_transtexcoord[i][transmesh->firstvert], numverts * sizeof(buf_texcoord_t));

			mesh = &buf_mesh[currentmesh++];
			*mesh = *transmesh; // copy mesh properties
			mesh->firstvert = currentvertex;
			mesh->lastvert = currentvertex + numverts - 1;
			currentvertex += numverts;
			meshvertexadjust = mesh->firstvert - transmesh->firstvert;
			mesh->firsttriangle = currenttriangle;
			for (tri = transmesh->transchain;tri;tri = tri->meshsortchain)
			{
				buf_tri[currenttriangle].index[0] = tri->index[0] + meshvertexadjust;
				buf_tri[currenttriangle].index[1] = tri->index[1] + meshvertexadjust;
				buf_tri[currenttriangle].index[2] = tri->index[2] + meshvertexadjust;
				/*
				if (tri->mesh != transmesh)
					Sys_Error("!?!");
				if ((unsigned int) buf_tri[currenttriangle].index[0] < (unsigned int) mesh->firstvert
				 || (unsigned int) buf_tri[currenttriangle].index[0] > (unsigned int) mesh->lastvert
				 || (unsigned int) buf_tri[currenttriangle].index[1] < (unsigned int) mesh->firstvert
				 || (unsigned int) buf_tri[currenttriangle].index[1] > (unsigned int) mesh->lastvert
				 || (unsigned int) buf_tri[currenttriangle].index[2] < (unsigned int) mesh->firstvert
				 || (unsigned int) buf_tri[currenttriangle].index[2] > (unsigned int) mesh->lastvert)
					Sys_Error("!?");
				*/
				currenttriangle++;
			}
			/*
			if (mesh->triangles != currenttriangle - mesh->firsttriangle)
				Sys_Error("!");
			*/
		}
		#else
		for (;transmesh;transmesh = transmesh->chain)
		{
			mesh = &buf_mesh[currentmesh++];
			*mesh = *transmesh; // copy mesh properties
			mesh->firstvert += currentvertex;
			mesh->lastvert += currentvertex;
			mesh->firsttriangle = currenttriangle;
			for (tri = transmesh->transchain;tri;tri = tri->meshsortchain)
			{
				buf_tri[currenttriangle].index[0] = tri->index[0] + currentvertex;
				buf_tri[currenttriangle].index[1] = tri->index[1] + currentvertex;
				buf_tri[currenttriangle].index[2] = tri->index[2] + currentvertex;
				/*
				if (tri->mesh != transmesh)
					Sys_Error("!?!");
				if ((unsigned int) buf_tri[currenttriangle].index[0] < (unsigned int) mesh->firstvert
				 || (unsigned int) buf_tri[currenttriangle].index[0] > (unsigned int) mesh->lastvert
				 || (unsigned int) buf_tri[currenttriangle].index[1] < (unsigned int) mesh->firstvert
				 || (unsigned int) buf_tri[currenttriangle].index[1] > (unsigned int) mesh->lastvert
				 || (unsigned int) buf_tri[currenttriangle].index[2] < (unsigned int) mesh->firstvert
				 || (unsigned int) buf_tri[currenttriangle].index[2] > (unsigned int) mesh->lastvert)
					Sys_Error("!?");
				*/
				currenttriangle++;
			}
			/*
			if (mesh->triangles != currenttriangle - mesh->firsttriangle)
				Sys_Error("!");
			*/
		}
		currentvertex += currenttransvertex;
		#endif

		currenttransmesh = 0;
		currenttranstriangle = 0;
		currenttransvertex = 0;
	}
	else if (gl_mesh_dupetransverts.integer)
	{
		int i, j, k, index;
		float viewdistcompare, centerscaler, dist1, dist2, dist3, center, maxdist;
		buf_vertex_t *vert1, *vert2, *vert3;
		buf_transtri_t *tri;
		buf_mesh_t *mesh;

		// process and add transparent mesh triangles
		if (!currenttranstriangle)
			return;

		// map farclip to 0-4095 list range
		centerscaler = (TRANSDEPTHRES / r_farclip) * (1.0f / 3.0f);
		viewdistcompare = viewdist + 4.0f;

		memset(buf_transtri_list, 0, TRANSDEPTHRES * sizeof(buf_transtri_t *));

		// process in reverse because transtri_list adding code is in reverse as well
		k = 0;
		for (j = currenttranstriangle - 1;j >= 0;j--)
		{
			tri = &buf_transtri[j];

			vert1 = &buf_transvertex[tri->index[0]];
			vert2 = &buf_transvertex[tri->index[1]];
			vert3 = &buf_transvertex[tri->index[2]];

			dist1 = DotProduct(vert1->v, vpn);
			dist2 = DotProduct(vert2->v, vpn);
			dist3 = DotProduct(vert3->v, vpn);

			maxdist = max(dist1, max(dist2, dist3));
			if (maxdist < viewdistcompare)
				continue;

			center = (dist1 + dist2 + dist3) * centerscaler - viewdist;
	#if SLOWMATH
			i = (int) center;
			i = bound(0, i, (TRANSDEPTHRES - 1));
	#else
			if (center < 0.0f)
				center = 0.0f;
			center += 8388608.0f;
			i = *((long *)&center) & 0x7FFFFF;
			i = min(i, (TRANSDEPTHRES - 1));
	#endif
			tri->next = buf_transtri_list[i];
			buf_transtri_list[i] = tri;
			k++;
		}

		if (currentmesh + k > max_meshs || currenttriangle + k > max_batch || currentvertex + k * 3 > max_verts)
			R_Mesh_Render();

		currenttransmesh = 0;
		currenttranstriangle = 0;
		currenttransvertex = 0;

		// note: can't batch these because they can be rendered in any order
		// there can never be more transparent triangles than fit in main buffers
		for (j = TRANSDEPTHRES - 1;j >= 0;j--)
		{
			if ((tri = buf_transtri_list[j]))
			{
				while(tri)
				{
					mesh = &buf_mesh[currentmesh++];
					*mesh = *tri->mesh; // copy mesh properties
					mesh->firstvert = currentvertex;
					mesh->lastvert = currentvertex + 2;
					mesh->firsttriangle = currenttriangle;
					mesh->triangles = 1;
					for (k = 0;k < 3;k++)
					{
						index = tri->index[k];
						buf_tri[currenttriangle].index[k] = currentvertex;
						memcpy(buf_vertex[currentvertex].v, buf_transvertex[index].v, sizeof(buf_vertex_t));
#if FLOATCOLORS
						if (floatcolors)
							memcpy(buf_fcolor[currentvertex].c, buf_transfcolor[index].c, sizeof(buf_fcolor_t));
						else
#endif
							memcpy(buf_bcolor[currentvertex].c, buf_transbcolor[index].c, sizeof(buf_bcolor_t));
						for (i = 0;i < backendunits && tri->mesh->textures[i];i++)
							memcpy(buf_texcoord[i][currentvertex].t, buf_transtexcoord[i][index].t, sizeof(buf_texcoord_t));
						currentvertex++;
					}
					currenttriangle++;
					tri = tri->next;
				}
			}
		}
	}
	else
	{
		int i, j, k;
		float viewdistcompare, centerscaler, dist1, dist2, dist3, center, maxdist;
		buf_vertex_t *vert1, *vert2, *vert3;
		buf_transtri_t *tri;
		buf_mesh_t *mesh;

		// process and add transparent mesh triangles
		if (!currenttranstriangle)
			return;

		// map farclip to 0-4095 list range
		centerscaler = (TRANSDEPTHRES / r_farclip) * (1.0f / 3.0f);
		viewdistcompare = viewdist + 4.0f;

		memset(buf_transtri_list, 0, TRANSDEPTHRES * sizeof(buf_transtri_t *));

		// process in reverse because transtri_list adding code is in reverse as well
		k = 0;
		for (j = currenttranstriangle - 1;j >= 0;j--)
		{
			tri = &buf_transtri[j];

			vert1 = &buf_transvertex[tri->index[0]];
			vert2 = &buf_transvertex[tri->index[1]];
			vert3 = &buf_transvertex[tri->index[2]];

			dist1 = DotProduct(vert1->v, vpn);
			dist2 = DotProduct(vert2->v, vpn);
			dist3 = DotProduct(vert3->v, vpn);

			maxdist = max(dist1, max(dist2, dist3));
			if (maxdist < viewdistcompare)
				continue;

			center = (dist1 + dist2 + dist3) * centerscaler - viewdist;
	#if SLOWMATH
			i = (int) center;
			i = bound(0, i, (TRANSDEPTHRES - 1));
	#else
			if (center < 0.0f)
				center = 0.0f;
			center += 8388608.0f;
			i = *((long *)&center) & 0x7FFFFF;
			i = min(i, (TRANSDEPTHRES - 1));
	#endif
			tri->next = buf_transtri_list[i];
			buf_transtri_list[i] = tri;
			k++;
		}

		if (currentmesh + k > max_meshs || currenttriangle + k > max_batch || currentvertex + currenttransvertex > max_verts)
			R_Mesh_Render();

		// note: can't batch these because they can be rendered in any order
		// there can never be more transparent triangles than fit in main buffers
		memcpy(&buf_vertex[currentvertex], &buf_transvertex[0], currenttransvertex * sizeof(buf_vertex_t));
#if FLOATCOLORS
		if (floatcolors)
			memcpy(&buf_fcolor[currentvertex], &buf_transfcolor[0], currenttransvertex * sizeof(buf_fcolor_t));
		else
#endif
			memcpy(&buf_bcolor[currentvertex], &buf_transbcolor[0], currenttransvertex * sizeof(buf_bcolor_t));
		for (i = 0;i < backendunits;i++)
			memcpy(&buf_texcoord[i][currentvertex], &buf_transtexcoord[i][0], currenttransvertex * sizeof(buf_texcoord_t));

		for (j = TRANSDEPTHRES - 1;j >= 0;j--)
		{
			if ((tri = buf_transtri_list[j]))
			{
				while(tri)
				{
					mesh = &buf_mesh[currentmesh++];
					*mesh = *tri->mesh; // copy mesh properties
					buf_tri[currenttriangle].index[0] = tri->index[0] + currentvertex;
					buf_tri[currenttriangle].index[1] = tri->index[1] + currentvertex;
					buf_tri[currenttriangle].index[2] = tri->index[2] + currentvertex;
					mesh->firstvert = min(buf_tri[currenttriangle].index[0], min(buf_tri[currenttriangle].index[1], buf_tri[currenttriangle].index[2]));
					mesh->lastvert = max(buf_tri[currenttriangle].index[0], max(buf_tri[currenttriangle].index[1], buf_tri[currenttriangle].index[2]));
					mesh->firsttriangle = currenttriangle++;
					mesh->triangles = 1;
					tri = tri->next;
				}
			}
		}
		currentvertex += currenttransvertex;
		currenttransmesh = 0;
		currenttranstriangle = 0;
		currenttransvertex = 0;
	}
}

void R_Mesh_Draw(const rmeshinfo_t *m)
{
	// these are static because gcc runs out of virtual registers otherwise
	static int i, j, *index, overbright;
	static float c, *in, scaler;
#if FLOATCOLORS
	static float cr, cg, cb, ca;
#endif
	static buf_mesh_t *mesh;
	static buf_vertex_t *vert;
#if FLOATCOLORS
	static buf_fcolor_t *fcolor;
#endif
	static buf_bcolor_t *bcolor;
	static buf_texcoord_t *texcoord[MAX_TEXTUREUNITS];
	static buf_transtri_t *tri;
	static buf_bcolor_t flatbcolor;

	if (m->index == NULL
	 || !m->numtriangles
	 || m->vertex == NULL
	 || !m->numverts)
		return;
	// ignore meaningless alpha meshs
	if (!m->depthwrite && m->blendfunc1 == GL_SRC_ALPHA && (m->blendfunc2 == GL_ONE || m->blendfunc2 == GL_ONE_MINUS_SRC_ALPHA))
	{
		if (m->color)
		{
			for (i = 0, in = m->color + 3;i < m->numverts;i++, (int)in += m->colorstep)
				if (*in >= 0.01f)
					break;
			if (i == m->numverts)
				return;
		}
		else if (m->ca < 0.01f)
			return;
	}

	if (!backendactive)
		Sys_Error("R_Mesh_Draw: called when backend is not active\n");

	scaler = 1;
	if (m->blendfunc2 == GL_SRC_COLOR)
	{
		if (m->blendfunc1 == GL_DST_COLOR) // 2x modulate with framebuffer
			scaler *= 0.5f;
	}
	else
	{
		if (m->tex[0])
		{
			overbright = gl_combine.integer;
			if (overbright)
				scaler *= 0.25f;
		}
		if (lighthalf)
			scaler *= 0.5f;
	}

	if (m->transparent)
	{
		if (currenttransmesh >= max_meshs || (currenttranstriangle + m->numtriangles) > max_meshs || (currenttransvertex + m->numverts) > max_verts)
		{
			if (!transranout)
			{
				Con_Printf("R_Mesh_Draw: ran out of room for transparent meshs\n");
				transranout = true;
			}
			return;
		}

		c_transmeshs++;
		c_transtris += m->numtriangles;
		vert = &buf_transvertex[currenttransvertex];
#if FLOATCOLORS
		fcolor = &buf_transfcolor[currenttransvertex];
#endif
		bcolor = &buf_transbcolor[currenttransvertex];
		for (i = 0;i < backendunits;i++)
			texcoord[i] = &buf_transtexcoord[i][currenttransvertex];

		// transmesh is only for storage of transparent meshs until they
		// are inserted into the main mesh array
		mesh = &buf_transmesh[currenttransmesh++];

		// transparent meshs are broken up into individual triangles which can
		// be sorted by depth
		index = m->index;
		for (i = 0;i < m->numtriangles;i++)
		{
			tri = &buf_transtri[currenttranstriangle++];
			tri->mesh = mesh;
			tri->index[0] = *index++ + currenttransvertex;
			tri->index[1] = *index++ + currenttransvertex;
			tri->index[2] = *index++ + currenttransvertex;
		}

		mesh->firstvert = currenttransvertex;
		mesh->lastvert = currenttransvertex + m->numverts - 1;
		currenttransvertex += m->numverts;
	}
	else
	{
		if (m->numtriangles > max_meshs || m->numverts > max_verts)
		{
			Con_Printf("R_Mesh_Draw: mesh too big for buffers\n");
			return;
		}

		if (currentmesh >= max_meshs || (currenttriangle + m->numtriangles) > max_batch || (currentvertex + m->numverts) > max_verts)
			R_Mesh_Render();

		c_meshs++;
		c_meshtris += m->numtriangles;
		vert = &buf_vertex[currentvertex];
#if FLOATCOLORS
		fcolor = &buf_fcolor[currentvertex];
#endif
		bcolor = &buf_bcolor[currentvertex];
		for (i = 0;i < backendunits;i++)
			texcoord[i] = &buf_texcoord[i][currentvertex];

		mesh = &buf_mesh[currentmesh++];
		// opaque meshs are rendered directly
		index = (int *)&buf_tri[currenttriangle];
		mesh->firsttriangle = currenttriangle;
		currenttriangle += m->numtriangles;
		for (i = 0;i < m->numtriangles * 3;i++)
			index[i] = m->index[i] + currentvertex;

		mesh->firstvert = currentvertex;
		mesh->lastvert = currentvertex + m->numverts - 1;
		currentvertex += m->numverts;
	}

	// code shared for transparent and opaque meshs
	mesh->blendfunc1 = m->blendfunc1;
	mesh->blendfunc2 = m->blendfunc2;
	mesh->depthmask = (m->blendfunc2 == GL_ZERO || m->depthwrite);
	mesh->depthtest = !m->depthdisable;
	mesh->triangles = m->numtriangles;
	j = -1;
	for (i = 0;i < backendunits;i++)
	{
		if ((mesh->textures[i] = m->tex[i]))
			j = i;
		mesh->texturergbscale[i] = m->texrgbscale[i];
		if (mesh->texturergbscale[i] != 1 && mesh->texturergbscale[i] != 2 && mesh->texturergbscale[i] != 4)
			mesh->texturergbscale[i] = 1;
	}
	if (overbright && j >= 0)
		mesh->texturergbscale[j] = 4;

	if (m->vertexstep != sizeof(buf_vertex_t))
	{
		for (i = 0, in = m->vertex;i < m->numverts;i++, (int)in += m->vertexstep)
		{
			vert[i].v[0] = in[0];
			vert[i].v[1] = in[1];
			vert[i].v[2] = in[2];
		}
	}
	else
		memcpy(vert, m->vertex, m->numverts * sizeof(buf_vertex_t));

#if FLOATCOLORS
	if (floatcolors)
	{
		if (m->color)
		{
			for (i = 0, in = m->color;i < m->numverts;i++, (int)in += m->colorstep)
			{
				fcolor[i].c[0] = in[0] * scaler;
				fcolor[i].c[1] = in[1] * scaler;
				fcolor[i].c[2] = in[2] * scaler;
				fcolor[i].c[3] = in[3];
			}
		}
		else
		{
			cr = m->cr * scaler;
			cg = m->cg * scaler;
			cb = m->cb * scaler;
			ca = m->ca;
			for (i = 0;i < m->numverts;i++)
			{
				fcolor[i].c[0] = cr;
				fcolor[i].c[1] = cg;
				fcolor[i].c[2] = cb;
				fcolor[i].c[3] = ca;
			}
		}
	}
	else
	{
#endif
		if (m->color)
		{
			for (i = 0, in = m->color;i < m->numverts;i++, (int)in += m->colorstep)
			{
				// shift float to have 8bit fraction at base of number,
				// then read as integer and kill float bits...
				c = in[0] * scaler + 32768.0f;j = (*((long *)&c) & 0x7FFFFF);if (j > 255) j = 255;bcolor[i].c[0] = (byte) j;
				c = in[1] * scaler + 32768.0f;j = (*((long *)&c) & 0x7FFFFF);if (j > 255) j = 255;bcolor[i].c[1] = (byte) j;
				c = in[2] * scaler + 32768.0f;j = (*((long *)&c) & 0x7FFFFF);if (j > 255) j = 255;bcolor[i].c[2] = (byte) j;
				c = in[3]          + 32768.0f;j = (*((long *)&c) & 0x7FFFFF);if (j > 255) j = 255;bcolor[i].c[3] = (byte) j;
			}
		}
		else
		{
			c = m->cr * scaler + 32768.0f;j = (*((long *)&c) & 0x7FFFFF);if (j > 255) j = 255;flatbcolor.c[0] = (byte) j;
			c = m->cg * scaler + 32768.0f;j = (*((long *)&c) & 0x7FFFFF);if (j > 255) j = 255;flatbcolor.c[1] = (byte) j;
			c = m->cb * scaler + 32768.0f;j = (*((long *)&c) & 0x7FFFFF);if (j > 255) j = 255;flatbcolor.c[2] = (byte) j;
			c = m->ca          + 32768.0f;j = (*((long *)&c) & 0x7FFFFF);if (j > 255) j = 255;flatbcolor.c[3] = (byte) j;
			for (i = 0;i < m->numverts;i++)
				bcolor[i] = flatbcolor;
		}
#if FLOATCOLORS
	}
#endif

	for (j = 0;j < MAX_TEXTUREUNITS && m->tex[j];j++)
	{
		if (j >= backendunits)
			Sys_Error("R_Mesh_Draw: texture %i supplied when there are only %i texture units\n", j + 1, backendunits);
		if (m->texcoordstep[j] != sizeof(buf_texcoord_t))
		{
			for (i = 0, in = m->texcoords[j];i < m->numverts;i++, (int)in += m->texcoordstep[j])
			{
				texcoord[j][i].t[0] = in[0];
				texcoord[j][i].t[1] = in[1];
			}
		}
		else
			memcpy(&texcoord[j][0].t[0], m->texcoords[j], m->numverts * sizeof(buf_texcoord_t));
	}
	#if 0
	for (;j < backendunits;j++)
		memset(&texcoord[j][0].t[0], 0, m->numverts * sizeof(buf_texcoord_t));
	#endif
}

void R_Mesh_DrawPolygon(rmeshinfo_t *m, int numverts)
{
	m->index = polyindexarray;
	m->numverts = numverts;
	m->numtriangles = numverts - 2;
	if (m->numtriangles < 1)
	{
		Con_Printf("R_Mesh_DrawPolygon: invalid vertex count\n");
		return;
	}
	if (m->numtriangles >= 256)
	{
		Con_Printf("R_Mesh_DrawPolygon: only up to 256 triangles (258 verts) supported\n");
		return;
	}
	R_Mesh_Draw(m);
}

// LordHavoc: this thing is evil, but necessary because decals account for so much overhead
void R_Mesh_DrawDecal(const rmeshinfo_t *m)
{
	// these are static because gcc runs out of virtual registers otherwise
	static int i, j, *index, overbright;
	static float c, scaler;
#if FLOATCOLORS
	static float cr, cg, cb, ca;
#endif
	static buf_mesh_t *mesh;
	static buf_vertex_t *vert;
#if FLOATCOLORS
	static buf_fcolor_t *fcolor;
#endif
	static buf_bcolor_t *bcolor;
	static buf_texcoord_t *texcoord;
	static buf_transtri_t *tri;
	static buf_bcolor_t flatbcolor;

	if (!backendactive)
		Sys_Error("R_Mesh_Draw: called when backend is not active\n");

	scaler = 1;
	if (m->tex[0])
	{
		overbright = gl_combine.integer;
		if (overbright)
			scaler *= 0.25f;
	}
	if (lighthalf)
		scaler *= 0.5f;

	if (m->transparent)
	{
		if (currenttransmesh >= max_meshs || (currenttranstriangle + 2) > max_meshs || (currenttransvertex + 4) > max_verts)
		{
			if (!transranout)
			{
				Con_Printf("R_Mesh_Draw: ran out of room for transparent meshs\n");
				transranout = true;
			}
			return;
		}

		c_transmeshs++;
		c_transtris += 2;
		vert = &buf_transvertex[currenttransvertex];
#if FLOATCOLORS
		fcolor = &buf_transfcolor[currenttransvertex];
#endif
		bcolor = &buf_transbcolor[currenttransvertex];
		texcoord = &buf_transtexcoord[0][currenttransvertex];

		// transmesh is only for storage of transparent meshs until they
		// are inserted into the main mesh array
		mesh = &buf_transmesh[currenttransmesh++];
		mesh->blendfunc1 = m->blendfunc1;
		mesh->blendfunc2 = m->blendfunc2;
		mesh->depthmask = false;
		mesh->depthtest = true;
		mesh->triangles = 2;
		mesh->textures[0] = m->tex[0];
		mesh->texturergbscale[0] = overbright ? 4 : 1;
		for (i = 1;i < backendunits;i++)
		{
			mesh->textures[i] = 0;
			mesh->texturergbscale[i] = 1;
		}
		mesh->chain = NULL;

		// transparent meshs are broken up into individual triangles which can
		// be sorted by depth
		index = m->index;
		tri = &buf_transtri[currenttranstriangle++];
		tri->mesh = mesh;
		tri->index[0] = 0 + currenttransvertex;
		tri->index[1] = 1 + currenttransvertex;
		tri->index[2] = 2 + currenttransvertex;
		tri = &buf_transtri[currenttranstriangle++];
		tri->mesh = mesh;
		tri->index[0] = 0 + currenttransvertex;
		tri->index[1] = 2 + currenttransvertex;
		tri->index[2] = 3 + currenttransvertex;

		mesh->firstvert = currenttransvertex;
		mesh->lastvert = currenttransvertex + 3;
		currenttransvertex += 4;
	}
	else
	{
		if (2 > max_meshs || 4 > max_verts)
		{
			Con_Printf("R_Mesh_Draw: mesh too big for buffers\n");
			return;
		}

		if (currentmesh >= max_meshs || (currenttriangle + 2) > max_batch || (currentvertex + 4) > max_verts)
			R_Mesh_Render();

		c_meshs++;
		c_meshtris += 2;
		vert = &buf_vertex[currentvertex];
#if FLOATCOLORS
		fcolor = &buf_fcolor[currentvertex];
#endif
		bcolor = &buf_bcolor[currentvertex];
		texcoord = &buf_texcoord[0][currentvertex];

		mesh = &buf_mesh[currentmesh++];
		mesh->blendfunc1 = m->blendfunc1;
		mesh->blendfunc2 = m->blendfunc2;
		mesh->depthmask = false;
		mesh->depthtest = !m->depthdisable;
		mesh->firsttriangle = currenttriangle;
		mesh->triangles = 2;
		mesh->textures[0] = m->tex[0];
		mesh->texturergbscale[0] = overbright ? 4 : 1;
		for (i = 1;i < backendunits;i++)
		{
			mesh->textures[i] = 0;
			mesh->texturergbscale[i] = 1;
		}

		// opaque meshs are rendered directly
		index = (int *)&buf_tri[currenttriangle];
		index[0] = 0 + currentvertex;
		index[1] = 1 + currentvertex;
		index[2] = 2 + currentvertex;
		index[3] = 0 + currentvertex;
		index[4] = 2 + currentvertex;
		index[5] = 3 + currentvertex;
		mesh->firstvert = currentvertex;
		mesh->lastvert = currentvertex + 3;
		currenttriangle += 2;
		currentvertex += 4;
	}

	// buf_vertex_t must match the size of the decal vertex array (or vice versa)
	memcpy(vert, m->vertex, 4 * sizeof(buf_vertex_t));

#if FLOATCOLORS
	if (floatcolors)
	{
		cr = m->cr * scaler;
		cg = m->cg * scaler;
		cb = m->cb * scaler;
		ca = m->ca;
		fcolor[0].c[0] = cr;
		fcolor[0].c[1] = cg;
		fcolor[0].c[2] = cb;
		fcolor[0].c[3] = ca;
		fcolor[1].c[0] = cr;
		fcolor[1].c[1] = cg;
		fcolor[1].c[2] = cb;
		fcolor[1].c[3] = ca;
		fcolor[2].c[0] = cr;
		fcolor[2].c[1] = cg;
		fcolor[2].c[2] = cb;
		fcolor[2].c[3] = ca;
		fcolor[3].c[0] = cr;
		fcolor[3].c[1] = cg;
		fcolor[3].c[2] = cb;
		fcolor[3].c[3] = ca;
	}
	else
	{
#endif
		c = m->cr * scaler + 32768.0f;j = (*((long *)&c) & 0x7FFFFF);if (j > 255) j = 255;flatbcolor.c[0] = (byte) j;
		c = m->cg * scaler + 32768.0f;j = (*((long *)&c) & 0x7FFFFF);if (j > 255) j = 255;flatbcolor.c[1] = (byte) j;
		c = m->cb * scaler + 32768.0f;j = (*((long *)&c) & 0x7FFFFF);if (j > 255) j = 255;flatbcolor.c[2] = (byte) j;
		c = m->ca          + 32768.0f;j = (*((long *)&c) & 0x7FFFFF);if (j > 255) j = 255;flatbcolor.c[3] = (byte) j;
		bcolor[0] = flatbcolor;
		bcolor[1] = flatbcolor;
		bcolor[2] = flatbcolor;
		bcolor[3] = flatbcolor;
#if FLOATCOLORS
	}
#endif

	// buf_texcoord_t must be the same size as the decal texcoord array (or vice versa)
	memcpy(&texcoord[0].t[0], m->texcoords[0], 4 * sizeof(buf_texcoord_t));
}
