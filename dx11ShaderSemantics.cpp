#include "dx11ShaderSemantics.h"

namespace dx11ShaderSemantic
{
	const char* kSTANDARDSGLOBAL						= "STANDARDSGLOBAL";
	const char* kUndefined								= "Undefined";

	const char* kWorld									= "World";
	const char* kWorldTranspose							= "WorldTranspose";
	const char* kWorldInverse							= "WorldInverse";
	const char* kWorldInverseTranspose					= "WorldInverseTranspose";

	const char* kView									= "View";
	const char* kViewTranspose							= "ViewTranspose";
	const char* kViewInverse							= "ViewInverse";
	const char* kViewInverseTranspose					= "ViewInverseTranspose";

	const char* kProjection								= "Projection";
	const char* kProjectionTranspose					= "ProjectionTranspose";
	const char* kProjectionInverse						= "ProjectionInverse";
	const char* kProjectionInverseTranspose				= "ProjectionInverseTranspose";

	const char* kWorldView								= "WorldView";
	const char* kWorldViewTranspose						= "WorldViewTranspose";
	const char* kWorldViewInverse						= "WorldViewInverse";
	const char* kWorldViewInverseTranspose				= "WorldViewInverseTranspose";

	const char* kViewProjection							= "ViewProjection";
	const char* kViewProjectionTranspose				= "ViewProjectionTranspose";
	const char* kViewProjectionInverse					= "ViewProjectionInverse";
	const char* kViewProjectionInverseTranspose			= "ViewProjectionInverseTranspose";

	const char* kWorldViewProjection					= "WorldViewProjection";
	const char* kWorldViewProjectionTranspose			= "WorldViewProjectionTranspose";
	const char* kWorldViewProjectionInverse				= "WorldViewProjectionInverse";
	const char* kWorldViewProjectionInverseTranspose	= "WorldViewProjectionInverseTranspose";

	const char* kViewDirection							= "ViewDirection";
	const char* kViewPosition							= "ViewPosition";
	const char* kLocalViewer							= "LocalViewer";

	const char* kViewportPixelSize						= "ViewportPixelSize";
	const char* kBackgroundColor						= "BackgroundColor";

	const char* kFrame									= "Frame";
	const char* kFrameNumber							= "FrameNumber";
	const char* kAnimationTime							= "AnimationTime";
	const char* kTime									= "Time";

	const char* kColor									= "Color";
	const char* kLightColor								= "LightColor";
	const char* kAmbient								= "Ambient";
	const char* kLightAmbientColor						= "LightAmbientColor";
	const char* kSpecular								= "Specular";
	const char* kLightSpecularColor						= "LightSpecularColor";
	const char* kDiffuse								= "Diffuse";
	const char* kLightDiffuseColor						= "LightDiffuseColor";
	const char* kNormal									= "Normal";
	const char* kBump									= "Bump";
	const char* kEnvironment							= "Environment";

	const char* kPosition								= "Position";
	const char* kAreaPosition0							= "AreaPosition0";
	const char* kAreaPosition1							= "AreaPosition1";
	const char* kAreaPosition2							= "AreaPosition2";
	const char* kAreaPosition3							= "AreaPosition3";
	const char* kDirection								= "Direction";

	const char* kTexCoord								= "TexCoord";
	const char* kTangent								= "Tangent";
	const char* kBinormal								= "Binormal";

	const char* kShadowMap								= "ShadowMap";
	const char* kShadowColor							= "ShadowColor";
	const char* kShadowFlag								= "ShadowFlag";
	const char* kShadowMapBias							= "ShadowMapBias";
	const char* kShadowMapMatrix						= "ShadowMapMatrix";
	const char* kShadowMapXForm							= "ShadowMapXForm";
	const char* kStandardsGlobal						= "StandardsGlobal";

	// Used to determine the type of a light parameter
	const char* kLightEnable							= "LightEnable";
	const char* kLightIntensity							= "LightIntensity";
	const char* kLightFalloff							= "LightFalloff";
	const char* kFalloff								= "Falloff";
	const char* kHotspot								= "Hotspot";
	const char* kLightType								= "LightType";
	const char* kDecayRate								= "DecayRate";

	const char* kLightRange								= "LightRange";
	const char* kLightAttenuation0						= "LightAttenuation0";
	const char* kLightAttenuation1						= "LightAttenuation1";
	const char* kLightAttenuation2						= "LightAttenuation2";
	const char* kLightTheta								= "LightTheta";
	const char* kLightPhi								= "LightPhi";

	const char* kTranspDepthTexture						= "transpdepthtexture";
	const char* kOpaqueDepthTexture						= "opaquedepthtexture";

	//--------------------------
	// Maya custom semantics

	// Define a boolean parameter to flag for swatch rendering
	const char* kMayaSwatchRender						= "MayaSwatchRender";

	// Define a float parameter to use to control the bounding box extra scale
	const char* kBboxExtraScale							= "BoundingBoxExtraScale";

	// Define a float parameter to use to check for transparency flag
	// Used in collaboration with the kIsTransparent annotation
	const char* kOpacity								= "Opacity";

	// Define a boolean parameter for full screen gamma correction
	const char* kMayaGammaCorrection						= "MayaGammaCorrection";
}

namespace dx11ShaderAnnotation
{
	// Used to groups multiple parameters in a single
	// collapsible panel in the attribute editor.
	const char* kUIGroup								= "UIGroup";

	// Used for the short/long/nice name of a parameter
	// in the attribute editor.
	const char* kUIName									= "UIName";

	// Used to populate a dropdown menu when a
	// parameter is shown in the attribute editor.
	const char* kUIFieldNames							= "UIFieldNames";

	// Used to make sure all parameters appear in a predictable
	// sequence in the attribute editor.
	const char* kUIOrder								= "UIOrder";
	
	// These annotations control the visibility of a paramerer in the
	// attribute editor.
	const char* kSasUiVisible							= "SasUiVisible";
	const char* kUIType									= "UIType";
	const char* kUIWidget								= "UIWidget";

	// These annotations control the numeric range of a slider.
	const char* kSasUiMin								= "SasUiMin";
	const char* kUIMin									= "UIMin";
	const char* kuimin									= "uimin";
	const char* kSasUiMax								= "SasUiMax";
	const char* kUIMax									= "UIMax";
	const char* kuimax									= "uimax";
	const char* kSasUiSoftMin							= "SasUiSoftMin";
	const char* kUISoftMin								= "UISoftMin";
	const char* kuisoftmin								= "uisoftmin";
	const char* kSasUiSoftMax							= "SasUiSoftMax";
	const char* kUISoftMax								= "UISoftMax";
	const char* kuisoftmax								= "uisoftmax";

	// These annotations are used to get the default file to assign to
	// a texture resource.
	const char* kResourceName							= "ResourceName";
	const char* kSasResourceAddress						= "SasResourceAddress";

	// These annotations are used to determine the type of a
	// a texture resource.
	const char* kTextureType							= "TextureType";
	const char* kResourceType							= "ResourceType";

	// Used to convert from DX to Maya space
	const char* kSpace									= "Space";

	// Used to determine the semantic of a parameter
	const char* kSasBindAddress							= "SasBindAddress";
	const char* kSasUiControl							= "SasUiControl";

	// Used to determine the light type of a parameter
	const char* kObject									= "Object";

	//--------------------------
	// Maya custom annotations

	// Technique annotations

	// Define the required index buffer generator to use for the technique
	const char* kIndexBufferType						= "index_buffer_type";

	// Define the mipmap levels to load/generate for textures used by the technique
	const char* kTextureMipmaplevels					= "texture_mipmaplevels";

	// Define if the technique should follow the Maya transparent object rendering or is self-managed (multi-passes)
	const char* kOverridesDrawState						= "overridesDrawState";

	// Define how the technique handles transparency
	const char* kIsTransparent							= "isTransparent";
	// Describe a condition that will be converted to a MEL procedure
	const char* kTransparencyTest						= "transparencyTest";
	// Describe whether the technique supports advanced transparency.
	const char* kSupportsAdvancedTransparency			= "supportsAdvancedTransparency";

	// Texture annotations

	// Define the mipmap levels to load/generate for the texture
	const char* kMipmaplevels							= "mipmaplevels";

	// Allow the shader writer to force the variable name to become the attribute name, even if UIName annotation is used
	const char* kVariableNameAsAttributeName			= "VariableNameAsAttributeName";
}

namespace dx11ShaderSemanticValue
{
	// Custom semantic values used by the customPrimitiveGenerator plugin
	const char* kCustomPrimitiveTest					= "customPrimitiveTest";
	const char* kCustomPositionStream					= "customPositionStream";
	const char* kCustomNormalStream						= "customNormalStream";
}

namespace dx11ShaderAnnotationValue
{
	const char* kNone									= "None";

	// Supported values for kTextureType and kResourceType annotations.
	const char* k1D										= "1D";
	const char* k2D										= "2D";
	const char* k3D										= "3D";
	const char* kCube									= "Cube";

	// Supported values for the kSpace annotation
	const char* kObject									= "Object";
	const char* kWorld									= "World";
	const char* kView									= "View";
	const char* kCamera									= "Camera";

	// Supported values for the kSasBindAddress annotation
	const char* kSas_Skeleton_MeshToJointToWorld_0_		= "Sas.Skeleton.MeshToJointToWorld[0]";
	const char* kSas_Camera_WorldToView					= "Sas.Camera.WorldToView";
	const char* kSas_Camera_Projection					= "Sas.Camera.Projection";
	const char* kSas_Time_Now							= "Sas.Time.Now";
	const char* k_Position								= ".Position";
	const char* k_Direction								= ".Direction";
	const char* k_Directional							= ".Directional";

	// Supported value for the kSasUiControl annotation
	const char* kColorPicker							= "ColorPicker";

	// Used to try to determine an unknown semantic
	const char* kPosition								= "Position";
	const char* kDirection								= "Direction";
	const char* kColor									= "Color";
	const char* kColour									= "Colour";
	const char* kDiffuse								= "Diffuse";
	const char* kSpecular								= "Specular";
	const char* kAmbient								= "Ambient";

	// Supported values for the kObject annotation
	const char* kLight									= "Light";
	const char* kLamp									= "Lamp";
	const char* kPoint									= "Point";
	const char* kSpot									= "Spot";
	const char* kDirectional							= "Directional";
}


//-
// Copyright 2012 Autodesk, Inc.  All rights reserved.
//
// Use of this software is subject to the terms of the Autodesk license agreement
// provided at the time of installation or download, or which otherwise
// accompanies this software in either electronic or hard copy form.
//+
