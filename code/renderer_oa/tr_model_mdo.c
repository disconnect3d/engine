/*
===========================================================================
Copyright (C) 2011 Thilo Schulz <thilo@tjps.eu>
Copyright (C) 2011 Matthias Bentrup <matthias.bentrup@googlemail.com>

This file is part of Quake III Arena source code.

Quake III Arena source code is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.

Quake III Arena source code is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Quake III Arena source code; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
===========================================================================
*/

#include "tr_local.h"

#define	LL(x) x=LittleLong(x)

typedef struct{
	float matrix[3][4];
}boneMatrix_t;

/*
=================
LOADING CODE
=================
*/
/*
=================
R_LoadMDO
=================
*/
qboolean R_LoadMDO( model_t *mod, void *buffer, int filesize, const char *mod_name ) {
	int					i, j, k, lodindex;
	mdoHeader_t			*pinmodel, *mdo;
    mdoFrame_t			*frame;
	mdoLOD_t			*lod;
	mdoSurface_t		*surf;
	mdoTriangle_t		*tri;
	mdoVertex_t			*v;
	mdoBoneInfo_t		*bone;
	int					*boneRef;
	int					version;
	int					size;
	shader_t			*sh;
	int					frameSize;

	pinmodel = (mdoHeader_t *)buffer;

	if( Q_strncmp(pinmodel->ident, MDO_IDENT, sizeof(pinmodel->ident) ) ){ // failed
		ri.Printf( PRINT_WARNING, "R_LoadMDO: %s has wrong ident (%s should be %s)\n",
				 mod_name, pinmodel->ident, MDO_IDENT);
	}

	version = LittleLong (pinmodel->version);
	if (version != MDO_VERSION) {
		ri.Printf( PRINT_WARNING, "R_LoadMDO: %s has wrong version (%i should be %i)\n",
				 mod_name, version, MDO_VERSION);
		return qfalse;
	}

	mod->type = MOD_MDO;

	size = LittleLong(pinmodel->ofsEnd);
	if(size > filesize)
	{
		ri.Printf(PRINT_WARNING, "R_LoadMDO: Header of %s is broken. Wrong filesize declared!\n", mod_name);
		return qfalse;
	}

	mod->dataSize += size;
	mod->modelData = mdo = ri.Hunk_Alloc( size, h_low );

	Com_Memcpy( mdo, buffer, LittleLong(pinmodel->ofsEnd) );

    LL(mdo->version);
    LL(mdo->numFrames);
    LL(mdo->numBones);
    LL(mdo->numLODs);
    LL(mdo->ofsFrames);
    LL(mdo->ofsLODs);
    LL(mdo->ofsEnd);

	if ( mdo->numFrames < 1 ) {
		ri.Printf( PRINT_WARNING, "R_LoadMDO: %s has no frames\n", mod_name );
		return qfalse;
	}

	if( mdo->numLODs < 1){
		ri.Printf( PRINT_WARNING, "R_LoadMDO: %s has no LODs\n", mod_name );
		return qfalse;
	}

	mod->numLods = mdo->numLODs; // ... don't forget this or LODs won't work
    
	// swap all the frames
	frameSize = (int)( &((mdoFrame_t *)0)->bones[ mdo->numBones ] );
    for ( i = 0 ; i < mdo->numFrames ; i++, frame++) {
	    frame = (mdoFrame_t *) ( (byte *)mdo + mdo->ofsFrames + i * frameSize );
    	frame->radius = LittleFloat( frame->radius );
        for ( j = 0 ; j < 3 ; j++ ) {
            frame->bounds[0][j] = LittleFloat( frame->bounds[0][j] );
            frame->bounds[1][j] = LittleFloat( frame->bounds[1][j] );
	    	frame->localOrigin[j] = LittleFloat( frame->localOrigin[j] );
        }

		for ( j = 0 ; j < mdo->numBones * sizeof( mdoBoneComp_t ) / sizeof( short ); j++ ) {
			( (short *)frame->bones )[j] = LittleShort( ( (short *)frame->bones )[j] );
		}
	}

	// swap all the bone infos
	bone = (mdoBoneInfo_t *) ( (byte *)mdo + mdo->ofsBones );
	for ( i = 0; i < mdo->numBones; i++, bone++ ){
		// lowercase the bone name so name compares are faster
		Q_strlwr( bone->name );
		bone->flags = LittleLong(bone->flags);
		bone->parent = LittleLong(bone->parent);
	}

	// swap all the LOD's
	lod = (mdoLOD_t *) ( (byte *)mdo + mdo->ofsLODs );
	for ( lodindex = 0 ; lodindex < mdo->numLODs ; lodindex++ ) {
		LL(lod->numSurfaces);
		LL(lod->ofsSurfaces);
		LL(lod->ofsEnd);
		// swap all the surfaces
		surf = (mdoSurface_t *) ( (byte *)lod + lod->ofsSurfaces );
		for ( i = 0 ; i < lod->numSurfaces ; i++) {
			//LL(surf->ident);
			LL(surf->ofsHeader); // don't swap header offset
			LL(surf->numTriangles);
			LL(surf->ofsTriangles);
			LL(surf->numVerts);
			LL(surf->ofsVerts);
			LL(surf->numBoneRefs);
			LL(surf->ofsBoneRefs);
			LL(surf->ofsEnd);


			if ( surf->numVerts > SHADER_MAX_VERTEXES ) {
				ri.Error (ERR_DROP, "R_LoadMDO: %s has more than %i verts on a surface (%i)",
					mod_name, SHADER_MAX_VERTEXES, surf->numVerts );
			}
			if ( surf->numTriangles*3 > SHADER_MAX_INDEXES ) {
				ri.Error (ERR_DROP, "R_LoadMDO: %s has more than %i triangles on a surface (%i)",
					mod_name, SHADER_MAX_INDEXES / 3, surf->numTriangles );
			}

			// change to surface identifier
			surf->ident = SF_MDO;

			// lowercase the surface name so skin compares are faster
			Q_strlwr( surf->name );
		
			// register the shaders
			sh = R_FindShader(surf->shader, LIGHTMAP_NONE, qtrue);
			if ( sh->defaultShader ) {
				surf->shaderIndex = 0;
			} else {
				surf->shaderIndex = sh->index;
			}

			// swap all the vertexes and texcoords
			v = (mdoVertex_t *) ( (byte *)surf + surf->ofsVerts);
			for ( j = 0 ; j < surf->numVerts ; j++ ) {
				v->xyz[0] = LittleFloat( v->xyz[0] );
				v->xyz[1] = LittleFloat( v->xyz[1] );
				v->xyz[2] = LittleFloat( v->xyz[2] );

				v->texCoords[0] = LittleFloat( v->texCoords[0] );
				v->texCoords[1] = LittleFloat( v->texCoords[1] );

				v->normal = LittleShort( v->normal );
				// num bone weights + weights[] are bytes
				v = (mdoVertex_t *)&v->weights[v->numWeights];
			}

			// swap all the triangles
			tri = (mdoTriangle_t *) ( (byte *)surf + surf->ofsTriangles );
			for ( j = 0 ; j < surf->numTriangles ; j++, tri++ ) {
				LL(tri->indexes[0]);
				LL(tri->indexes[1]);
				LL(tri->indexes[2]);
			}

			// swap all the bone references
			boneRef = ( int * )( ( byte *)surf + surf->ofsBoneRefs );
			for ( j = 0; j < surf->numBoneRefs; j++, boneRef++ ){
				*boneRef = LittleLong( *boneRef );
			}

			// find the next surface
			surf = (mdoSurface_t *)( (byte *)surf + surf->ofsEnd );
		}

		// find the next LOD
		lod = (mdoLOD_t *)( (byte *)lod + lod->ofsEnd );
	}

	return qtrue;
}

/*
=================
DRAWING CODE
=================
*/

/*
=============
R_CullMDO
=============
*/

static int R_CullMDO( mdoHeader_t *header, trRefEntity_t *ent ) {
	vec3_t		bounds[2];
	mdoFrame_t	*oldFrame, *newFrame;
	int			i, frameSize;

	frameSize = (size_t)( &((mdoFrame_t *)0)->bones[ header->numBones ] );
	
	// compute frame pointers
	newFrame = ( mdoFrame_t * ) ( ( byte * ) header + header->ofsFrames + frameSize * ent->e.frame);
	oldFrame = ( mdoFrame_t * ) ( ( byte * ) header + header->ofsFrames + frameSize * ent->e.oldframe);

	// cull bounding sphere ONLY if this is not an upscaled entity
	if ( !ent->e.nonNormalizedAxes )
	{
		if ( ent->e.frame == ent->e.oldframe )
		{
			switch ( R_CullLocalPointAndRadius( newFrame->localOrigin, newFrame->radius ) )
			{
				
				case CULL_OUT:
					tr.pc.c_sphere_cull_md3_out++;
					return CULL_OUT;

				case CULL_IN:
					tr.pc.c_sphere_cull_md3_in++;
					return CULL_IN;

				case CULL_CLIP:
					tr.pc.c_sphere_cull_md3_clip++;
					break;
			}
		}
		else
		{
			int sphereCull, sphereCullB;

			sphereCull  = R_CullLocalPointAndRadius( newFrame->localOrigin, newFrame->radius );
			if ( newFrame == oldFrame ) {
				sphereCullB = sphereCull;
			} else {
				sphereCullB = R_CullLocalPointAndRadius( oldFrame->localOrigin, oldFrame->radius );
			}

			if ( sphereCull == sphereCullB )
			{
				if ( sphereCull == CULL_OUT )
				{
					tr.pc.c_sphere_cull_md3_out++;
					return CULL_OUT;
				}
				else if ( sphereCull == CULL_IN )
				{
					tr.pc.c_sphere_cull_md3_in++;
					return CULL_IN;
				}
				else
				{
					tr.pc.c_sphere_cull_md3_clip++;
				}
			}
		}
	}
	
	// calculate a bounding box in the current coordinate system
	for (i = 0 ; i < 3 ; i++) {
		bounds[0][i] = oldFrame->bounds[0][i] < newFrame->bounds[0][i] ? oldFrame->bounds[0][i] : newFrame->bounds[0][i];
		bounds[1][i] = oldFrame->bounds[1][i] > newFrame->bounds[1][i] ? oldFrame->bounds[1][i] : newFrame->bounds[1][i];
	}

	switch ( R_CullLocalBox( bounds ) )
	{
		case CULL_IN:
			tr.pc.c_box_cull_md3_in++;
			return CULL_IN;
		case CULL_CLIP:
			tr.pc.c_box_cull_md3_clip++;
			return CULL_CLIP;
		case CULL_OUT:
		default:
			tr.pc.c_box_cull_md3_out++;
			return CULL_OUT;
	}
}

/*
=================
R_ComputeMDOFogNum

=================
*/
int R_ComputeMDOFogNum( mdoHeader_t *header, trRefEntity_t *ent ) {
	int				i, j;
	fog_t			*fog;
	mdoFrame_t		*frame;
	vec3_t			localOrigin;
	int frameSize;

	if ( tr.refdef.rdflags & RDF_NOWORLDMODEL ) {
		return 0;
	}
	
	frameSize = (size_t)( &((mdoFrame_t *)0)->bones[ header->numBones ] );

	// FIXME: non-normalized axis issues
	frame = ( mdoFrame_t * ) ( ( byte * ) header + header->ofsFrames + frameSize * ent->e.frame);
	VectorAdd( ent->e.origin, frame->localOrigin, localOrigin );
	for ( i = 1 ; i < tr.world->numfogs ; i++ ) {
		fog = &tr.world->fogs[i];
		for ( j = 0 ; j < 3 ; j++ ) {
			if ( localOrigin[j] - frame->radius >= fog->bounds[1][j] ) {
				break;
			}
			if ( localOrigin[j] + frame->radius <= fog->bounds[0][j] ) {
				break;
			}
		}
		if ( j == 3 ) {
			return i;
		}
	}

	return 0;
}

/*
==============
R_AddMDOSurfaces
==============
*/
void R_AddMDOSurfaces( trRefEntity_t *ent ) {
	mdoHeader_t		*header;
	mdoSurface_t	*surface;
	mdoLOD_t		*lod;
	shader_t		*shader;
	skin_t		   *skin;
	int				i, j, k;
	int				lodnum = 0;
	int				fogNum = 0;
	int				cull;
	qboolean	personalModel;

	header = (mdoHeader_t *) tr.currentModel->modelData;
	
	personalModel = (ent->e.renderfx & RF_THIRD_PERSON) && !tr.viewParms.isPortal;

	if ( ent->e.renderfx & RF_WRAP_FRAMES )
	{
		ent->e.frame %= header->numFrames;
		ent->e.oldframe %= header->numFrames;
	}	
	
	//
	// Validate the frames so there is no chance of a crash.
	// This will write directly into the entity structure, so
	// when the surfaces are rendered, they don't need to be
	// range checked again.
	//
	if ((ent->e.frame >= header->numFrames) 
		|| (ent->e.frame < 0)
		|| (ent->e.oldframe >= header->numFrames)
		|| (ent->e.oldframe < 0) )
	{
		ri.Printf( PRINT_DEVELOPER, "R_AddMDOSurfaces: no such frame %d to %d for '%s'\n",
			   ent->e.oldframe, ent->e.frame, tr.currentModel->name );
		ent->e.frame = 0;
		ent->e.oldframe = 0;
	}

	//
	// cull the entire model if merged bounding box of both frames
	// is outside the view frustum.
	//
	cull = R_CullMDO (header, ent);
	if ( cull == CULL_OUT ) {
		return;
	}	

	// figure out the current LOD of the model we're rendering, and set the lod pointer respectively.
	lodnum = R_ComputeLOD(ent);
	// check whether this model has as that many LODs at all. If not, try the closest thing we got.
	if(header->numLODs <= 0)
		return;
	if(header->numLODs <= lodnum)
		lodnum = header->numLODs - 1;

	lod = (mdoLOD_t *)( (byte *)header + header->ofsLODs);
	for(i = 0; i < lodnum; i++)
	{
		lod = (mdoLOD_t *) ((byte *) lod + lod->ofsEnd);
	}
	
	// set up lighting
	if ( !personalModel || r_shadows->integer > 1 )
	{
		R_SetupEntityLighting( &tr.refdef, ent );
	}

	// fogNum
	fogNum = R_ComputeMDOFogNum( header, ent );

	surface = (mdoSurface_t *)( (byte *)lod + lod->ofsSurfaces );

	for ( i = 0 ; i < lod->numSurfaces ; i++ )
	{

		if(ent->e.customShader)
			shader = R_GetShaderByHandle(ent->e.customShader);
		else if(ent->e.customSkin > 0 && ent->e.customSkin < tr.numSkins)
		{
			skin = R_GetSkinByHandle(ent->e.customSkin);
			shader = tr.defaultShader;
			
			for(j = 0; j < skin->numSurfaces; j++)
			{
				if (!strcmp(skin->surfaces[j]->name, surface->name))
				{
					shader = skin->surfaces[j]->shader;
					break;
				}
			}
		}
		else if(surface->shaderIndex > 0)
			shader = R_GetShaderByHandle( surface->shaderIndex );
		else
			shader = tr.defaultShader;

		// we will add shadows even if the main object isn't visible in the view

		// stencil shadows can't do personal models unless I polyhedron clip
		if ( !personalModel
		    && r_shadows->integer == 2
			&& fogNum == 0
			&& !(ent->e.renderfx & ( RF_NOSHADOW | RF_DEPTHHACK ) )
			&& shader->sort == SS_OPAQUE )
		{
			R_AddDrawSurf( (void *)surface, tr.shadowShader, 0, qfalse );
		}

		// projection shadows work fine with personal models
		if ( r_shadows->integer == 3
			&& fogNum == 0
			&& (ent->e.renderfx & RF_SHADOW_PLANE )
			&& shader->sort == SS_OPAQUE )
		{
			R_AddDrawSurf( (void *)surface, tr.projectionShadowShader, 0, qfalse );
		}

		if (!personalModel)
			R_AddDrawSurf( (void *)surface, shader, fogNum, qfalse );

		surface = (mdoSurface_t *)( (byte *)surface + surface->ofsEnd );
	}
}

/*
=================
ANIMATION
=================
*/

void CompBoneToMatrix( const vec4_t quat, vec3_t trans, float matrix[3][4] ) {
	float xx = 2.0f * quat[0] * quat[0];
	float yy = 2.0f * quat[1] * quat[1];
	float zz = 2.0f * quat[2] * quat[2];
	float xy = 2.0f * quat[0] * quat[1];
	float xz = 2.0f * quat[0] * quat[2];
	float yz = 2.0f * quat[1] * quat[2];
	float wx = 2.0f * quat[3] * quat[0];
	float wy = 2.0f * quat[3] * quat[1];
	float wz = 2.0f * quat[3] * quat[2];

	matrix[0][0] = ( 1.0f - ( yy + zz ) );
	matrix[0][1] = ( xy - wz );
	matrix[0][2] = ( xz + wy );
	matrix[0][3] = trans[0];

	matrix[1][0] = ( xy + wz);
	matrix[1][1] = ( 1.0f - ( xx + zz ) );
	matrix[1][2] = ( yz - wx);
	matrix[1][3] = trans[1];

	matrix[2][0] = ( xz - wy );
	matrix[2][1] = ( yz + wx );
	matrix[2][2] = ( 1.0f - ( xx + yy ) );
	matrix[2][3] = trans[2];
}

void R_UncompressBone( int boneIndex, mdoFrame_t *frame, float matrix[3][4] )
{
	mdoBoneComp_t	*bone = &frame->bones[boneIndex];
	vec4_t	quat;
	vec3_t	trans;


	quat[0] = bone->rotQuat[0] * MDO_QUAT_SCALE;
	quat[1] = bone->rotQuat[1] * MDO_QUAT_SCALE;
	quat[2] = bone->rotQuat[2] * MDO_QUAT_SCALE;
	quat[3] = bone->rotQuat[3] * MDO_QUAT_SCALE;

	trans[0] = bone->translation[0] * MDO_TRANSLATION_SCALE;
	trans[1] = bone->translation[1] * MDO_TRANSLATION_SCALE;
	trans[2] = bone->translation[2] * MDO_TRANSLATION_SCALE;

	CompBoneToMatrix( quat, trans, matrix );
}

void Matrix3x4Lerp( float mat1[3][4], float mat2[3][4], float blerp, float out[3][4], qboolean slerp )
{
	float	flerp = 1.0f - blerp;

	out[0][0] = flerp * mat1[0][0] + blerp * mat2[0][0];
	out[0][1] = flerp * mat1[0][1] + blerp * mat2[0][1];
	out[0][2] = flerp * mat1[0][2] + blerp * mat2[0][2];
	out[0][3] = flerp * mat1[0][3] + blerp * mat2[0][3];

	out[1][0] = flerp * mat1[1][0] + blerp * mat2[1][0];
	out[1][1] = flerp * mat1[1][1] + blerp * mat2[1][1];
	out[1][2] = flerp * mat1[1][2] + blerp * mat2[1][2];
	out[1][3] = flerp * mat1[1][3] + blerp * mat2[1][3];

	out[2][0] = flerp * mat1[2][0] + blerp * mat2[2][0];
	out[2][1] = flerp * mat1[2][1] + blerp * mat2[2][1];
	out[2][2] = flerp * mat1[2][2] + blerp * mat2[2][2];
	out[2][3] = flerp * mat1[2][3] + blerp * mat2[2][3];

	if( slerp ){ // unsquash the rotations
		VectorNormalize( out[0] );
		VectorNormalize( out[1] );
		VectorNormalize( out[2] );
	}
}

/*
================
R_GetMDOTag
================
*/
md3Tag_t *R_GetMDOTag( mdoHeader_t *mod, int framenum, const char *tagName, md3Tag_t * dest) 
{
	int				i, j, k;
	int				frameSize;
	mdoFrame_t		*frame;
	mdoBoneInfo_t	*bone;

	if ( framenum >= mod->numFrames ) 
	{
		// it is possible to have a bad frame while changing models, so don't error
		framenum = mod->numFrames - 1;
	}

	bone = (mdoBoneInfo_t *)((byte *)mod + mod->ofsBones); 
	for ( i = 0; i < mod->numBones; i++, bone++ )
	{
		if( !(bone->flags & BFLAG_TAG) ){
			continue;
		}

		if ( !strcmp( bone->name, tagName ) )
		{
			vec4_t	quat;
			vec3_t	trans;
			float	matrix[3][4];

			Q_strncpyz(dest->name, bone->name, sizeof(dest->name));

			frameSize = (size_t)( &((mdoFrame_t *)0)->bones[ mod->numBones ] );
			frame = (mdoFrame_t *)((byte *)mod + mod->ofsFrames + framenum * frameSize );

			R_UncompressBone( i, frame, matrix);

			for (j = 0; j < 3; j++)
			{
				for (k = 0; k < 3; k++){
					dest->axis[j][k] = matrix[k][j];
				}
				dest->origin[j] = matrix[j][3];
			}

			return dest;
		}
	}

	return NULL;
}

/*
================
RB_SurfaceMDO
================
*/
#define FTABLESIZE_DIV_256	4	// optimizations, may want to move this right next to the functables
#define FTABLESIZE_DIV_4	256 // and use it for other functions that use them
void RB_SurfaceMDO( mdoSurface_t *surface )
{
	int				i, j, k;
	int				frameSize;
	float			frontlerp, backlerp;
	int				*triangles;
	int				indexes;
	int				baseIndex, baseVertex;
	int				numVerts;
	int				*boneRefList;
	int				*boneRef;
	mdoVertex_t		*v;
	mdoHeader_t		*header;
	mdoFrame_t		*frame;
	mdoFrame_t		*oldFrame;
	boneMatrix_t	bones[MDO_MAX_BONES], *bonePtr, *bone;	
	mdoBoneInfo_t	*boneInfo;
	refEntity_t		*ent;

	ent = &backEnd.currentEntity->e;

	// don't lerp if lerping off, or this is the only frame, or the last frame...
	//
	if (ent->oldframe == ent->frame) 
	{
		backlerp	= 0;	// if backlerp is 0, lerping is off and frontlerp is never used
		frontlerp	= 1;
	} 
	else  
	{
		backlerp	= ent->backlerp;
		frontlerp	= 1.0f - backlerp;
	}

	header = (mdoHeader_t *)((byte *)surface - surface->ofsHeader);


	//
	// Set up all triangles.
	//
	RB_CHECKOVERFLOW( surface->numVerts, surface->numTriangles * 3 );
	triangles	= (int *) ((byte *)surface + surface->ofsTriangles);
	indexes		= surface->numTriangles * 3;
	baseIndex	= tess.numIndexes;
	baseVertex	= tess.numVertexes;

	for (j = 0 ; j < indexes ; j++) 
	{
		tess.indexes[baseIndex + j] = baseVertex + triangles[j];
	}
	tess.numIndexes += indexes;

	//
	// Setup and lerp all the needed bones
	//
	frameSize = (size_t)( &((mdoFrame_t *)0)->bones[ header->numBones ] );

	frame = (mdoFrame_t *)((byte *)header + header->ofsFrames + ent->frame * frameSize );
	oldFrame = (mdoFrame_t *)((byte *)header + header->ofsFrames + ent->oldframe * frameSize );

	// boneRefs
	boneRefList = ( int * )( (byte *)surface + surface->ofsBoneRefs );
	boneRef = boneRefList;

	// boneInfos
	boneInfo = (mdoBoneInfo_t *)((byte *)header + header->ofsBones);

	bonePtr = bones;
	for( i = 0 ; i < surface->numBoneRefs ; i++, boneRef++ )
	{

		if( !backlerp ){ // no need for cache, animation is simple
			R_UncompressBone( *boneRef, frame, bonePtr[*boneRef].matrix );
		}else{
			float mat1[3][4], mat2[3][4];
			R_UncompressBone( *boneRef, frame, mat1 );
			R_UncompressBone( *boneRef, oldFrame, mat2 );
			Matrix3x4Lerp(mat1, mat2, backlerp, bonePtr[*boneRef].matrix, qtrue);
		}
	}

	//
	// deform the vertexes by the lerped bones
	//
	numVerts = surface->numVerts;
	v = (mdoVertex_t *) ((byte *)surface + surface->ofsVerts);
	for ( j = 0; j < numVerts; j++ ) 
	{
		vec3_t	vertex, normal;
		vec3_t	unCompNormal;
		unsigned int lat, lng;

		VectorClear( vertex );
		VectorClear( normal );

		// Uncompress the normal
		lat = ( v->normal >> 8 ) & 0xff;
		lng = ( v->normal & 0xff );
		lat *= (FTABLESIZE_DIV_256);
		lng *= (FTABLESIZE_DIV_256);

		unCompNormal[0] = tr.sinTable[(lat+(FTABLESIZE_DIV_4))&FUNCTABLE_MASK] * tr.sinTable[lng];
		unCompNormal[1] = tr.sinTable[lat] * tr.sinTable[lng];
		unCompNormal[2] = tr.sinTable[(lng+(FTABLESIZE_DIV_4))&FUNCTABLE_MASK];

		if( !v->numWeights || !header->numBones ){ // static model, or un weighted vertex
			vertex[0] = v->xyz[0];
			vertex[1] = v->xyz[1];
			vertex[2] = v->xyz[2];

			normal[0] = unCompNormal[0];
			normal[1] = unCompNormal[1];
			normal[2] = unCompNormal[2];

			VectorNormalizeFast(normal); // ensure a normalized normal
		}else{
			mdoWeight_t *w;
			w = v->weights;
			for( k = 0; k < v->numWeights; k++, w++ ){
				float	fWeight = w->boneWeight * MDO_BYTE_WEIGHT_SCALE;
				int		boneIndex = w->boneIndex;

				bone = bonePtr + boneIndex;
				// Transform the vert and normal
				vertex[0] += fWeight * ( DotProduct( bone->matrix[0], v->xyz ) + bone->matrix[0][3] );
				vertex[1] += fWeight * ( DotProduct( bone->matrix[1], v->xyz ) + bone->matrix[1][3] );
				vertex[2] += fWeight * ( DotProduct( bone->matrix[2], v->xyz ) + bone->matrix[2][3] );
			
				normal[0] += fWeight * DotProduct( bone->matrix[0], unCompNormal );
				normal[1] += fWeight * DotProduct( bone->matrix[1], unCompNormal );
				normal[2] += fWeight * DotProduct( bone->matrix[2], unCompNormal );
			}
			VectorNormalizeFast(normal); // ensure a normalized normal
		}

		tess.xyz[baseVertex + j][0] = vertex[0];
		tess.xyz[baseVertex + j][1] = vertex[1];
		tess.xyz[baseVertex + j][2] = vertex[2];

		tess.normal[baseVertex + j][0] = normal[0];
		tess.normal[baseVertex + j][1] = normal[1];
		tess.normal[baseVertex + j][2] = normal[2];

		tess.texCoords[baseVertex + j][0][0] = v->texCoords[0];
		tess.texCoords[baseVertex + j][0][1] = v->texCoords[1];

		v = (mdoVertex_t *)&v->weights[v->numWeights];
	}

	tess.numVertexes += surface->numVerts;
}

