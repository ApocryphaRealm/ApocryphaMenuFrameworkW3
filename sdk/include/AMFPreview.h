#pragma once

// ============================================================================================================
// Apocrypha Menu Framework (Witcher 3, 1.0.2+) - the 3D PREVIEW: a consumer hands the framework one model (vertices,
// indices, draws, block-compressed textures) and gets back a texture with it drawn, lit and turned the way it asks, to
// show with ImGui::Image. For Item Explorer's item card (the owner, 2026-10-06: "a simple window that pops up over AMF
// with a frame. Displays the item's name and its appearance with rotation on controller and keyboard and mouse").
//
// Plain C structs: they cross the DLL boundary as they are. Units are the game's (metres, Z up). The framework copies
// everything at AMF_PreviewCreate; the caller's memory may be freed when it returns.
// ============================================================================================================

#include <cstdint>

extern "C"
{
	// 32 bytes. Positions already dequantised; the normal and tangent as the game stores them: 10:10:10:2 unsigned
	// (x/1023*2-1), the tangent's top two bits its bitangent sign (set = +1).
	struct AMF_PreviewVertex
	{
		float         position[3];
		float         uv[2];
		std::uint32_t normal;
		std::uint32_t tangent;
		std::uint32_t reserved;
	};

	enum AMF_PreviewFormat : std::int32_t
	{
		AMF_PREVIEW_BC1_SRGB = 0,      // colour, no alpha
		AMF_PREVIEW_BC3_SRGB = 1,      // colour with alpha (hair, cards)
		AMF_PREVIEW_BC3_UNORM = 2,     // normal map: RGB the tangent-space normal, A gloss
		AMF_PREVIEW_RGBA8_SRGB = 3,
		AMF_PREVIEW_RGBA8_UNORM = 4,
		AMF_PREVIEW_BC1_UNORM = 5,
		AMF_PREVIEW_BC7_SRGB = 6,
		AMF_PREVIEW_BC7_UNORM = 7,
	};

	// One texture: its mips packed one after another, mip 0 first, each tightly (BC: max(1,(w+3)/4) blocks per row of
	// 8 or 16 bytes; RGBA8: w*4 bytes per row).
	struct AMF_PreviewTexture
	{
		std::int32_t  format;   // AMF_PreviewFormat
		std::int32_t  width;
		std::int32_t  height;
		std::int32_t  mips;
		const void*   data;
		std::uint32_t size;
	};

	enum AMF_PreviewDrawFlags : std::uint32_t
	{
		AMF_PREVIEW_TWO_SIDED = 1u << 0,    // no back-face culling
		AMF_PREVIEW_ALPHA_TEST = 1u << 1,   // discard where the colour texture's alpha is under 0.5 (hair, fur, cards)
		AMF_PREVIEW_DYE = 1u << 2,          // apply the two colour shifts to the colour texture's dye zones
	};

	// One draw: a range of the index buffer with its material.
	struct AMF_PreviewDraw
	{
		std::uint32_t firstIndex;
		std::uint32_t indexCount;
		std::int32_t  baseVertex;
		std::int32_t  diffuse;        // index into the textures, -1 = none (a flat grey)
		std::int32_t  normal;         // index into the textures, -1 = none (the vertex normal)
		std::uint32_t flags;          // AMF_PreviewDrawFlags
		float         specular[4];    // rgb tint and strength (0 = none)
		float         tint[4];        // multiplies the colour texture (1,1,1,1 = as is)
		// With AMF_PREVIEW_DYE (REDengine's colour shift; Item Explorer's model-reader.md section 4):
		//   colorShift1.xyz  the red zone's shift:  hue (degrees), saturation, luminance (-100..100)
		//   colorShift1.w    the dye mask's texture index (its green channel), -1 = none
		//   colorShift2.xyz  the blue zone's shift
		//   colorShift2.w    >= 0: KeepGray on, and the constant mask when there is no texture;
		//                    <  0: KeepGray off, constant mask = -1 - value
		float         colorShift1[4];
		float         colorShift2[4];
	};

	struct AMF_PreviewMesh
	{
		const AMF_PreviewVertex*  vertices;
		std::uint32_t             vertexCount;
		const std::uint32_t*      indices;
		std::uint32_t             indexCount;
		const AMF_PreviewDraw*    draws;
		std::uint32_t             drawCount;
		const AMF_PreviewTexture* textures;
		std::uint32_t             textureCount;
		float                     boundsMin[3];   // the model's box, for the camera's framing
		float                     boundsMax[3];
	};

	// How to look at it. The camera orbits the box's centre at a distance that fits the whole box, times zoom.
	struct AMF_PreviewView
	{
		float yaw;          // degrees, around Z (the game's up)
		float pitch;        // degrees, up from the horizontal
		float zoom;         // 1 = the whole model fits; under 1 closer
		float background[4];   // rgba clear colour (alpha 0 = transparent, the card shows through)
	};
}
