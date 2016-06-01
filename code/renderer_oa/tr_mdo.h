/*
===========================================================================
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

/*
==============================================================================

.MDO Open Model format - supports simple skeletal animation and static meshes
v2 - removed frame name, and converted boneframe to compressed using quats
==============================================================================
*/

#define MDO_IDENT			"OPENMDL2"
#define MDO_VERSION			2
#define	MDO_MAX_BONES		128
#define MDO_MAX_WEIGHTS		4

// compression scales
#define	MDO_TRANSLATION_SCALE		(1.0 / 64) 
#define	MDO_QUAT_SCALE				(1.0 / 768)
#define MDO_BYTE_WEIGHT_SCALE		(1.0 / 255)

// bone flags
#define BFLAG_TAG	1

typedef struct {
	unsigned char boneIndex;
	unsigned char boneWeight;
}mdoWeight_t;

typedef struct {
	float	xyz[3];
	float	texCoords[2];
	unsigned short	normal;		// compressed normal
	unsigned char numWeights;
	mdoWeight_t	weights[1];
} mdoVertex_t;

typedef struct {
	int			indexes[3];
} mdoTriangle_t;

typedef struct {
	int	ident;				// for renderer use

	char name[MAX_QPATH];	// polyset name
	char shader[MAX_QPATH];
	int	shaderIndex;	// for in-game use

	int	ofsHeader;	// subtract this to get header

	int	numVerts;
	int	numTriangles;
	int	numBoneRefs; 
	int	ofsVerts;
	int	ofsTriangles;
	int	ofsBoneRefs;

	int	ofsEnd;		// next surface follows
} mdoSurface_t;

typedef struct {
	short	rotQuat[4]; // always normalized
	short	translation[3];
} mdoBoneComp_t;

typedef struct {
	char	name[32];
	int		parent;
	int		flags;
} mdoBoneInfo_t;

typedef struct {
	float		bounds[2][3];		// bounds of all surfaces of all LOD's for this frame
	float		localOrigin[3];		// midpoint of bounds, used for sphere cull
	float		radius;				// dist from localOrigin to corner
	mdoBoneComp_t	bones[1];		// [numBones], bone mats
} mdoFrame_t;

typedef struct {
	int	numSurfaces;
	int	ofsSurfaces;		// first surface, others follow
	int	ofsEnd;				// next lod follows
} mdoLOD_t;

typedef struct {
	char ident[8];
	int	version;

	// frames and bones are shared by all levels of detail
	int	numFrames;
	int	numBones;
	int	numLODs;

	int	ofsFrames;
	int	ofsBones; // boneinfos
	int	ofsLODs;

	int	ofsEnd;
} mdoHeader_t;